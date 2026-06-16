# PSP Kernel Driver — KMD Integration

This page documents how the AMD BC-250 GPU driver (`atikmdag.sys`) talks to the
co-installed PSP kernel driver (`PspDriver.sys`).  
Binaries sit on separate repos but their IOCTL codes and register maps must stay in
sync.

---

## Two PSP Touchpoints

| Path | Where it is used | Why |
|------|------------------|-----|
| `src/kmd/amdbc250_psp.c` | GPU driver direct PSP MMIO + KIQ submit | Bypass PSP proxy and hit BAR0 directly |
| `src/kmd/amdbc250_psp_v11.c` | Legacy PSP init from Linux side | BAR5 mapping, MP0 discovery, SOS detection |

The `PspDriver.sys` IOCTLs below are the **only** contract between the GPU driver
and the PSP helper.  If an IOCTL code or struct layout changes, recompile both
drivers from the same source tree.

---

## Kernel-Only PSP IOCTLs (GPU driver → PSP driver)

| IOCTL code | Number | Purpose |
|-----------|--------|---------|
| `PSP_IOCTL_READ_REG` | `0x800` | Read a PSP register via BAR0 |
| `PSP_IOCTL_WRITE_REG` | `0x801` | Write a PSP register via BAR0 |
| `PSP_IOCTL_INIT_HW` | `0x803` | Map PSP BAR0 + GPU BAR5 into system VA |
| `PSP_IOCTL_BOOT_SEQ` | `0x810` | Send SYSDRV + SOS commands |
| `PSP_IOCTL_GET_GPU_INFO` | `0x815` | Snapshot of PSP ring state + health |
| `PSP_IOCTL_LOAD_TOC` | `0x820` | Load table-of-contents blob |
| `PSP_IOCTL_REG_PROG` | `0x816` | Program NBIO/MP0/MP1 registers |
| `PSP_IOCTL_KIQ_SUBMIT` | `0x818` | Submit a ring of PM4 dwords |
| `PSP_IOCTL_KIQ_LOAD_FW` | `0x822` | Load ME / PFP / MEC / RLC firmware by type |

---

## Register Map (PSP BAR0 vs GPU BAR5)

| Physical address | BAR | Owner | Notes |
|-----------------|-----|-------|-------|
| `0xFD600000` | PSP BAR0 | PSP | 256 KB — MP0, MP1, C2PMSG, RLC, KIQ registers |
| `0xFE800000` | GPU BAR5, segment `GC_BASE = 0x1260` | GPU | 512 KB — GC, CP, MMHUB, DF, NBIO |

> The GPU driver must keep these two windows separate.  A PSP init session maps both;
> later GPU driver code calls `amdbc250_psp_*` helpers that use `Bar0Base` for PSP
> registers and `MmioBase` for GPU GRBM/GC/memory registers.

---

## Boot sequence (proven working on 2026-06-15)

```
1. PSP_IOCTL_INIT_HW (Flags = 1)
   → Bar0Base = MmMapIoSpace(0xFD600000, 256KB)
   → MmioBase = MmMapIoSpace(0xFE800000, 512KB)

2. PSP_IOCTL_LOAD_EMBEDDED_FW
   → PA >> 20 = 0x448D returned
   → SOS firmware staged in TMR

3. PSP_IOCTL_BOOT_SEQUENCE
   → C2PMSG_65 = SYSDRV (type 0x4), SENT
   → C2PMSG_65 = SOS    (type 0x8), SENT
   → C2PMSG_81 = 0xF0000010  (SOS alive)
   → GRBM_STATUS = 0x00000000  (NBIO unlocked)
```

After step 3, the GPU BAR5 MMIO is live: `GPU_ID = 0x9FFF9700`, CP scratch reads
`0x4D585042`, MMHUB/DF/GC config readable and writable at **BC-250 corrected
offsets** (`GC_BASE = 0x1260`).

---

## PSP Mailbox Protocol (C2PMSG)

All PSP status checks go through the PSP driver because the host CPU has no direct
read access to C2PMSG after secure boot.

| C2PMSG index | mm offset | BAR0 + 0x__ | Read | Write |
|-------------|-----------|-------------|------|-------|
| `C2PMSG_35` | `0x0088` | `0xFD6088` | `C2PMSG_35 → cmd` | `cmd → C2PMSG_35` |
| `C2PMSG_36` | `0x008C` | `0xFD608C` | result | write `PA >> 20` |
| `C2PMSG_64` | `0x0100` | `0xFD6100` | TOS_READY / TOS_RESP | cmd bits |
| `C2PMSG_65` | `0x0104` | `0xFD6104` | boot state | send SYSDRV (0x4) / SOS (0x8) |
| `C2PMSG_81` | `0x0144` | `0xFD6144` | `0xF0000010` when SOS alive | — |

> Readable layout is what `test-psp-driver.exe -m` returns when given `0x65..0x75`.
> Post-boot stable values: `C2PMSG_81 = 0xF0000010`, `C2PMSG_65 = 0x…0000C000` (busy/idle).

---

## KIQ Path — Submitting PM4 Firmware Loads

### Ring memory

`PspKiqInit` allocates a **contiguous**, physically-contiguous system-RAM ring and
saves `KiQRingPA`.  The hardware KIQ registers (`KIQ_BASE_LO/HI`, `KIQ_CNTL`,
`KIQ_WPTR/RPTR`) are not written by the host — the PSP tells the GPU head register
values via the boot sequence.  The driver just increments `wptr` and wraps the
software ring.

```c
PspKiqInit(pDevExt);
// ring is at pDevExt->KiQRingPA, size = KIQ_RING_SIZE_DWORDS * sizeof(ULONG)

PspKiqSubmit(pDevExt, pm4_dwords, count);
// posts PM4 to ring and issues PSP_IOCTL_KIQ_SUBMIT so the PSP forwards it
```

### Firmware blobs on disk

| File | FW type | Expected PSP KIQ type enum |
|------|---------|---------------------------|
| `cyan_skillfish2_me.bin` | Micro Engine (RLC / CP helpers) | `0x01` |
| `cyan_skillfish2_pfp.bin` | Pre-Fetch Parser | `0x02` |
| `cyan_skillfish2_mec.bin` | MEC firmware | `0x03` |
| `cyan_skillfish2_rlc.bin` | Run-Light-Context firmware | `0x04` |

Test tool invokes them through `IOCTL_PSP_KIQ_LOAD_FW`:

```
test-psp-driver.exe -Q 1 ..\output\cyan_skillfish2_me.bin
test-psp-driver.exe -Q 2 ..\output\cyan_skillfish2_pfp.bin
```

Napomena: MEC (`0x03`) and RLC (`0x04`) may not be needed for bare-minimum CP
scratch tests — ME + PFP are enough.  Load them in order: ME first, then PFP.

---

## CP Scratch Register Verification

After ME/PFP loads, the CP block should be more than a register stub:

| Offset (BAR5) | What to read | Before | After ME+PFP |
|--------------|-------------|--------|---------------|
| `0x3260` | `GRBM_STATUS` | `0x00000000` | still 0 → no hang |
| `0x32D4` | `CP_SCRATCH_REG0` | `0x4D585042` | expect stable or changing value |
| KIQ `wptr` | last known write pointer | `3` (test run) | monitor for increase |

If `CP_SCRATCH_REG0` changes when the PSP ring submits a NOOP PM4, CP is executing.
If it stays static, the ME/PFP firmware either failed silently or KIQ register
hand-off is still missing.

---

## Rules When Editing PSP Code

1. Any IOCTL number, struct layout, or enum must be mirrored in the **PSP driver
   repo** (`PspIoctl.h`) and in the **GPU driver** (`amdbc250_psp.h` or the local
   `#define` block in `amdbc250_psp.c`).  If you forget, the driver installs but
   the user-mode test returns `0x5 / STATUS_INVALID_PARAMETER`.
2. `Bar0Base` is PSP-only.  If you need a GPU register, use
   `READ_REG(WPtr, AMDBC250_REG_OFFSET, MmioBase)` — BAR5, not BAR0.
3. `MmioBase` was previously overwritten with BAR0; that made mailbox reads
   return `0xFFFFFFFF`.  Do not re-introduce that bug.
4. `PspKiqLoadFirmware` takes `ULONG FwType` (1 = ME, 2 = PFP, …) and a caller-
   supplied `PUCHAR FilePath`.
5. When PSP driver is recompiled but GPU driver isn’t, or vice versa, the first
   IOCTL after install will `STATUS_ACCESS_VIOLATION`.
