# PSP GPCOM Ring — Working Kernel Implementation (2026-08-18)

## TL;DR

The **PSP KM GPCOM ring WORKS on BC-250**. A kernel-mode ring was created
through the GPU driver (`atikmdag.sys`) and a real command
(`GFX_CMD_ID_GET_FW_ATTESTATION`, 0x0F) was **executed by the PSP and returned
SUCCESS (status 0)**. All previous "ring permanently unavailable / no TOS"
conclusions were based on the **wrong MP0 base** (0x103D0 / 0x105D0). The real
base is **0x58000** in BAR5.

This reopens PSP firmware loading (LOAD_IP_FW) as a viable path on Windows —
the #1 blocker documented in AGENTS.md.

## The breakthrough: correct MP0 base

| Item | Wrong (prior tests) | Correct (verified) |
|------|---------------------|--------------------|
| MP0 C2PMSG block base | 0x103D0 / 0x105D0 | **0x58000** |
| C2PMSG_64 (cmd/TOS/RESP) | 0x105D0 → read 0 | **0x58200** → 0x80020000 |
| C2PMSG_81 (SOS status) | 0x10614 → 0xF0000010 | **0x58244** → 0x002B9309 |
| TOS-ready bit31 on C2PMSG_64 | never set | **SET (0x80020000)** |
| Ring create result | never ACKed | **ACKed, ring live** |

Why: ip_discovery reports MP0 base **0x16000 in DWORD units**; the BAR5 byte
offset is 0x16000 × 4 = **0x58000**. Earlier code treated 0x16000 (or unrelated
probe addresses) as byte offsets, so every mailbox write hit a dead address.

## Proof (psp-ring-submit-test.exe, against installed atikmdag.sys)

```
PSP_RING_INIT (0x80000C18):
  Result=1  RingPa=0x7E512000  RingSize=0x1000
  C2pmsg64=0x80020000  C2pmsg81=0x002B9309
PSP_RING_SUBMIT (0x80000C1C):
  GET_FW_ATTESTATION  (0x0F): FenceStatus=1 RespStatus=0x00000000  <- SUCCESS
  GET_FW_ATTESTATION2 (0x10): FenceStatus=1 RespStatus=0x00000100  <- PSP_ERR_UNKNOWN_COMMAND
  FB_FW_RESERV_ADDR   (0x50): FenceStatus=1 RespStatus=0x00000100  <- PSP_ERR_UNKNOWN_COMMAND
C2PMSG_67 (WPTR) advanced: 0x00 -> 0x10 -> 0x20 -> 0x30  (0x10 = 16 dwords = one 64B frame)
```

- Fence reached on every submit — PSP consumed each frame from the ring.
- WPTR advancing 0x10/frame is exactly `rb_frame_size_dw` for a 64B
  `psp_gfx_rb_frame`. Ring consumption confirmed.
- `0x00000100` = `PSP_ERR_UNKNOWN_COMMAND` (psp_gfx_if.h line 494) — BC-250 SOS
  does not implement GET_FW_ATTESTATION2 / FB_FW_RESERV_ADDR. Protocol itself
  succeeded; not a driver bug.

## Kernel implementation (files changed)

### `src/kmd/amdbc250_dream_kmd.c`
Two new IOCTL cases in `DreamV3DeviceControl`:

**`case 0x80000C18` — PSP_RING_INIT** (Linux `psp_v11_0_8_ring_create`):
1. Wait TOS-ready: `C2PMSG_64 bit31` (500 ms poll).
2. `MmAllocateContiguousMemory(0x1000)` ring buffer, zero it.
3. Write ring PA → `C2PMSG_69/70`, size → `C2PMSG_71`.
4. Write `C2PMSG_64 = PSP_RING_TYPE_KM(2) << 16` = 0x00020000.
5. `KeStallExecutionProcessor(20000)` (Linux mdelay(20)).
6. Wait RESP bit31 (500 ms poll).
7. On ACK: `DevExt->PspRingCreated = TRUE`, `PspRingWptr = 0`, `PspFenceValue = 0`.
   Output 24B: `{Result, RingPa U64, RingSize, C2pmsg64, C2pmsg81}`.

**`case 0x80000C1C` — PSP_RING_SUBMIT** (Linux `psp_ring_cmd_submit`):
1. Input 8+512B: `{CmdId U32, CmdDataSize U32, CmdData[0..511]}`; clamp
   CmdDataSize to 512 and to remaining input length.
2. Requires `PspRingCreated` (else STATUS_DEVICE_NOT_READY). Serialized by
   `DevExt->DeviceMutex`.
3. Lazy-allocate fence + cmd buffers (both `PSP_CMD_BUF_SIZE` 0x1000).
4. Build `psp_gfx_cmd_resp` in cmd buffer:
   `+0 buf_size=0x400`, `+4 buf_version=1`, `+8 cmd_id`, union cmd copied at **+28**,
   resp at +864.
5. Write 64B `psp_gfx_rb_frame` at `ring + wptr*4`:
   `+0 cmd_buf_addr_lo`, `+4 hi`, `+8 cmd_buf_size=0x400`,
   `+12 fence_addr_lo`, `+16 fence_addr_hi`, `+20 fence_value=index`.
6. `wptr = (wptr + 16) % 1024`; kick `C2PMSG_67` = wptr.
7. Poll fence (`*(ULONG*)PspFenceVa == index`) with `DreamV3HdpFlush`, 500 ms.
8. Read resp at cmdBuf+864: `status(+0), session(+4), fw_addr_lo(+8),
   fw_addr_hi(+12), tmr_size(+16)`. Output 24B:
   `{Result, FenceStatus, RespStatus, RespFwAddrLo, RespFwAddrHi, RespTmrSize}`.

File-scope defines (supersede the 0x103D0-based `DIRECT_C2PMSG_*` in amdbc250_psp.c):
```
PSP_MP0_BASE    0x58000
PSP_C2PMSG_64   (PSP_MP0_BASE + 0x200)   // 0x58200  cmd/TOS-ready/RESP
PSP_C2PMSG_67   (PSP_MP0_BASE + 0x20C)   // 0x5820C  ring WPTR
PSP_C2PMSG_69   (PSP_MP0_BASE + 0x214)   // 0x58214  ring addr low32
PSP_C2PMSG_70   (PSP_MP0_BASE + 0x218)   // 0x58218  ring addr high32
PSP_C2PMSG_71   (PSP_MP0_BASE + 0x21C)   // 0x5821C  ring size
PSP_C2PMSG_81   (PSP_MP0_BASE + 0x244)   // 0x58244  SOS status
PSP_RING_SIZE   0x1000
PSP_RING_TYPE_KM 2
PSP_CMD_BUF_SIZE 0x1000
```

### `inc/amdbc250_dream_kmd.h`
New `DREAM_V3_DEVICE_EXTENSION` fields:
`PspRingCreated, PspRingPa, PspRingVa, PspRingSize, PspCmdPa, PspCmdVa,
PspFencePa, PspFenceVa, PspFenceValue, PspRingWptr`.

### `inc/amdbc250_ioctl.h`
```
IOCTL_AMDBC250_PSP_RING_INIT   CTL_CODE_AMDBC250(0x96, ...)  // 0x80000C18
IOCTL_AMDBC250_PSP_RING_SUBMIT CTL_CODE_AMDBC250(0x97, ...)  // 0x80000C1C
```

### `src/kmd/amdbc250_dream_kmd.c` — DreamV3WdmUnload
Frees `PspRingVa`, `PspCmdVa`, `PspFenceVa` via `MmFreeContiguousMemory` and
clears `PspRingCreated`.

## Ring protocol references (Linux)

- `psp_v11_0_8_ring_create` — the exact 7-step sequence implemented in INIT.
- `psp_ring_cmd_submit` / `psp_cmd_submit_buf` (amdgpu_psp.c) — frame/fence flow.
- `psp_gfx_if.h` — structs `psp_gfx_cmd_resp`, `psp_gfx_rb_frame`,
  `psp_gfx_resp`, `psp_gfx_cmd_load_ip_fw`, and `GFX_CMD_ID_*` / `GFX_FW_TYPE_*`
  enums; `PSP_ERR_UNKNOWN_COMMAND = 0x00000100`.

## Next: LOAD_IP_FW via ring

- `GFX_CMD_ID_LOAD_IP_FW = 0x06`, union = `psp_gfx_cmd_load_ip_fw`
  `{fw_phy_addr_lo, fw_phy_addr_hi, fw_size, fw_type}`.
- Firmware must sit at a GPU-visible address. `ALLOC_VIDMEM` returns a kernel
  VA (not user-mappable), so the load IOCTL should read the firmware file itself
  from `bc-250\` (e.g. `C:\Windows\System32\drivers\bc-250\`) and stage the
  bytes into the cmd/fence-style contiguous buffer before submitting.
- fw_type values for BC-250: `GFX_FW_TYPE_CP_ME=1`, `CP_PFP=2`, `CP_CE=3`,
  `CP_MEC=4`, `RLC_G=8`, `SDMA0=9`, `SDMA1=10`, `SMU=18`.
- After LOAD_IP_FW: `SETUP_TMR (0x05)`, `LOAD_TOC (0x20)`, `AUTOLOAD_RLC (0x21)`.
