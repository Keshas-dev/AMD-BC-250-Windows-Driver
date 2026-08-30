# AMD BC-250 Windows Driver Project

## Who We Are

AMD BC-250 Windows driver project by Keshas. Goal: fully working GPU driver for AMD BC-250 (Cyan Skillfish) on Windows.

**Everyone welcome!** GPU drivers, WDDM, Vulkan experience — or just want to help.

## Hardware

- **SoC:** AMD BC-250 (Cyan Skillfish) — RDNA2, 16GB GDDR6 shared memory
  - **40 CU die, 24 active** (harvest mask — `CC_GC_SHADER_ARRAY_CONFIG` = 0xFFF80000 stock; 40 CU unlock via Linux/EFI targets 0xFFE00000). BC-250 is a full PS5 Oberon die (8× Zen2 + RDNA2 + 40 CUs), NOT a fused mining ASIC — features are SOS/SMU-masked, not fused.
- **GPU ID:** 0x9FFF9700, **BIOS:** P4.00G
- **Memory:** CPU and GPU share GDDR6 (UMA). Linux reports **VRAM 256MB at 0xF400000000**; our driver defaults to 16GB total / 4GB visible. Split is configurable via memcfg (256MB–12GB, stored in CMOS).
- **GPU BAR5:** 0xFE800000 (512KB MMIO register space)
- **PSP BAR0:** 0xFD600000 (256KB)
- **GC_BASE:** 0x1260 (BC-250 uses shifted register offsets vs Navi10)
- **SMU version:** 88.6.0 (driver_if=8); Linux runs 88.7.1
- **PSP IP:** v11.0.8 (CYAN_SKILLFISH2 variant)

### F:\AMD Hybrid Package (2026-08-27)
- **Source:** `F:\AMD` — AMD Adrenalin 23.9.1 + Radeon ID Community (Amernime Zone) hybrid WDDM package
- **Fixes applied 2026-08-30:** MultiParse INF `16299→26200` (Win11 25H2/b26200) + `DEV_13FE` BC-250 entry, `u0395510.cat` regenerated via Inf2Cat, `KMD_EnableDisplayableSupport=1` (Mode 1, `GPUDisplayOne`) — see [docs/AMD_Hybrid.md](docs/AMD_Hybrid.md)

---

## Current Status (2026-08-30)

### Working
- ✅ **PSP KM GPCOM ring WORKS on hardware** — ring created at correct MP0 base `0x58000`, commands executed by the PSP through the ring, fences reached. **This reopens PSP firmware loading on Windows.**
- ✅ **PSP_RING_INIT / PSP_RING_SUBMIT kernel IOCTLs** (`0x80000C18`/`0x80000C1C`) — `GET_FW_ATTESTATION` returned SUCCESS (status 0)
- ✅ **PSP_RING_LOAD_IP_FW kernel IOCTL** (`0x80000C20`) — driver reads the firmware file itself, stages it GPU-visible, submits `GFX_CMD_ID_LOAD_IP_FW` (0x06) via the ring
- ✅ **PSP_RING_SETUP_TMR kernel IOCTL** (`0x80000C24`) — TMR set up in VRAM (MC 0xF40F800000 / BAR0 physical) via `GFX_CMD_ID_SETUP_TMR` (0x05); VERIFIED SUCCESS on hardware (2026-08-19), aper_base 0xC0000000 confirmed
- ✅ **DIRECT C2PMSG firmware loading WORKS** (2026-08-21 retest) — corrected MP0 base to `0x58000`; `psp-tos-test.exe` (Ta.bin as TOS) and `psp-fw-load.exe` (all 8 IP firmware) both PASS
- ✅ **KMDOD display driver** (2560x1440, Status OK, CM_ERR=0)
- ✅ **GPU driver loads** — WDM IOCTL mode on Win11 26100
- ✅ **BAR5 MMIO mapping** — `DreamV3WriteRegister`/`ReadRegister` via `WRITE_REGISTER_ULONG`
- ✅ **Build + sign pipeline** — `build.bat` with prebuild validation
- ✅ **SMU mailbox via SMN** (BAR5+0x38/0x3C) — freq, VID, features all work
- ✅ **SMU frequency control** — 1500MHz base, governor sequence proven safe
- ✅ **Register read/write** — GRBM_STATUS, SCRATCH, GPU_ID, CC_ARRAY, etc.
- ✅ **CC_ARRAY partially writable** — bits 24-28 toggle (0x1F000000), cosmetic only
- ✅ **CPU core unlock** via SMU Q3 msg 0x98 (6→8 cores, requires reboot)
- ✅ **Software PM4 executor** — IT_WRITE_DATA, IT_NOP, IT_SET_CONFIG_REG confirmed
- ✅ **14/15 IOCTL tests pass** — GetCaps, GetVramInfo, GetTempInfo, AllocVidMem, etc.
- ✅ **Win11 25H2 INF + CAT fixed** — `u0395510_VanGogh/FireFlight_Stock*.inf` now `26200` + `DEV_13FE`, CAT regenerated
- ✅ **Display support Mode 1** — `KMD_EnableDisplayableSupport=1` (`DisplayableSupport=_2_`) via F:\AMD `GPUDisplayOne` (requires reboot)

### Not Working / Blocked
- ❌ **SDMA self-test** — ring not initialized (RB_BASE_LO=0x00555555, no copy engine)
- ❌ **3D graphics on Windows WDM** — WGP compute SOS-locked (SPI_PG=0); NOT hardware-fused, Linux amdgpu runs shaders
- ❌ **WGP unlock on Windows** — SPI_PG_ENABLE_STATIC_WGP_MASK SOS-locked
- ❌ **KIQ_SIZE=0** — hardware read-only, cannot create compute rings
- ❌ **Some PSP commands** — `GET_FW_ATTESTATION2` (0x10) and `FB_FW_RESERV_ADDR` (0x50) return `PSP_ERR_UNKNOWN_COMMAND` (0x100): BC-250 SOS does not implement them (protocol itself works)

---

## Latest Test Results

### bar5-smn-test.exe ✅
```
CreateFile OK
INIT_HW(0xFE800000/0x80000): ok=1 gle=0

=== GPU Basic ===
SCRATCH_REG0 (0x32D4): 0x4D585042
GRBM_STATUS  (0x3260): 0x00000000
GRBM_STATUS2 (0x326C): 0x00000000
GRBM_SOFT_RST(0x3278): 0x00000000
GPU_ID       (0x0000): 0x9FFF9700

=== SMU Queries ===
FW_FLAGS:           0x00000001
PUB_CTRL:           0x00000000
TestMessage:         0x00000001
GetSmuVersion:       0x00580600 (88.6.0)
GetDriverIfVersion:  0x00000008 (8)
Features:            0xDD602C7D (GFXCLK=ON GFXOFF=ON)
GfxFreq:             1500 MHz
GfxClk:              1500 MHz
GfxVid:              0x00000063
ActiveWgp:           0
CorePstate:          0x00000006
DfPstate:            0x00000003
```

### test-gpu-ioctls.exe ✅ (14/15 pass)
```
[PASS] GetCaps — Version 4.3.0, CUs: 24, SPs: 1536, RT: 24
[PASS] GetVramInfo — Total: 16384 MB, Visible: 4096 MB
[PASS] GetTempInfo — Edge: 0 C, Junction: 12 C
[PASS] GetDisplayInfo — Max: 7680x4320, Pipes: 4
[PASS] SetDisplayMode(1920x1080@60)
[PASS] AllocVidMem(1MB) — GPU VA: 0x7E512000
[PASS] FreeVidMem
[FAIL] SdmaFill(stub) — SDMA ring not initialized
[PASS] SignalFence(42)
[PASS] WaitFence(42)
[PASS] TdrReset
[PASS] ReadEdid(child0)
[PASS] GetChildRelations — Children: 4, Connected: 0x3
[PASS] ShaderCompile(stub)
[PASS] GetPowerTelemetry
```

### sdma-selftest.exe ❌
```
Init OK
RB_BASE_LO = 0x00555555  // Not a valid physical address
RB_CNTL    = 0x00000000  // Ring disabled
RB_WPTR    = 0x00000000  // No work submitted

SDMA Self-Test IOCTL: OK (br=4)
Result: 0xC00000A3 — SDMA copy returned STATUS_INVALID_PARAMETER
```

### psp-fw-load.exe ✅ (8/8 firmware loaded via DIRECT path, 2026-08-21 retest)
```
[1/8] CE (cyan_skillfish2_ce.bin)...
  Result=1 C2Pmsg35=0xFFFFFFFF C2Pmsg81=0x002C16FE
[2/8] PFP (cyan_skillfish2_pfp.bin)...
  Result=1 C2Pmsg35=0xFFFFFFFF C2Pmsg81=0x002C16FE
[3/8] ME (cyan_skillfish2_me.bin)...
  Result=1 C2Pmsg35=0xFFFFFFFF C2Pmsg81=0x002C16FE
[4/8] MEC (cyan_skillfish2_mec.bin)...
  Result=1 C2Pmsg35=0xFFFFFFFF C2Pmsg81=0x002C16FE
[5/8] MEC2 (cyan_skillfish2_mec2.bin)...
  Result=1 C2Pmsg35=0xFFFFFFFF C2Pmsg81=0x002C16FE
[6/8] RLC (cyan_skillfish2_rlc.bin)...
  Result=1 C2Pmsg35=0xFFFFFFFF C2Pmsg81=0x002C16FE
[7/8] SDMA0 (navi12_sdma.bin)...
  Result=1 C2Pmsg35=0xFFFFFFFF C2Pmsg81=0x002C16FE
[8/8] SDMA1 (navi12_sdma1.bin)...
  Result=1 C2Pmsg35=0xFFFFFFFF C2Pmsg81=0x002C16FE
```

**Note:** The earlier "0/8 FAIL" verdict (2026-08-18) was from the **wrong MP0 base (0x103D0/0x10614)**. The live C2PMSG block is at **BAR5 0x58000** (ip_discovery MP0 base 0x16000 × 4). After correcting the offsets, the DIRECT C2PMSG_35/36/37/81 path works on hardware.

### psp-ring-submit-test.exe ✅ (2026-08-18, ring protocol VERIFIED)
```
PSP_RING_INIT: Result=1 RingPa=0x7E512000 RingSize=0x1000 C2pmsg64=0x80020000
PSP_RING_SUBMIT: fence REACHED (every command)
GFX_CMD_ID_GET_FW_ATTESTATION (0x0F): RespStatus=0x00000000 SUCCESS
GET_FW_ATTESTATION2 (0x10) / FB_FW_RESERV_ADDR (0x50): 0x00000100 UNKNOWN_COMMAND
```

---

## Community Research (elektricM/amd-bc250-docs)

### Mesa/RADV Requirements
- **Mesa 25.1.0+ required** for BC-250 (GFX1013) support
- **Mesa 25.3.6+ recommended** (confirmed working Fedora 43, March 2026)
- **RADV is the only working GPU driver** — AMDVLK and proprietary don't support BC-250
- **RADV_DEBUG=nohiz** recommended for artifact fixes
- **RADV_DEBUG=nocompute** deprecated on Mesa 25.1+ (compute queue auto-disabled)

### VRAM Configuration
- **Stock BIOS:** 8GB RAM + 8GB VRAM split
- **Configurable via memcfg utility:** 256MB to 12GB VRAM (stored in CMOS)
- **Dynamic VRAM:** all splits are dynamic; 512MB minimum is most flexible
- **ttm.pages_limit** kernel param extends dynamic VRAM beyond default half-RAM limit

### Known Linux Issues
- **No VA-API** — VCN firmware blocked by Sony
- **ROCm experimental** — gfx1013 support incomplete, rocBLAS missing kernels
- **Visual artifacts** — fixed in Mesa 25.1.5+ with RADV_DEBUG=nohiz
- **Limited VRAM visibility in Vulkan** — ~10GB of 12GB split visible

---

## Hardware Register Map (BC-250 specific)

### Confirmed Working
- ✅ **GC_BASE=0x1260** — all GC registers offset shifted
- ✅ **GRBM_STATUS** (0x3260), GRBM_GFX_INDEX (0x34D0) — working
- ✅ **SCRATCH** (0x32D4) — writable, high nibble [31:28] HW-masked
- ✅ **KIQ_BASE_LO** (0xE060) writable; **KIQ_BASE_HI** read-only; **KIQ_SIZE** (0xE068) read-only=0
- ✅ **KIQ_WPTR** — only 9 bits (mask 0x1FF)
- ✅ **GCVM** — PT_BASE0 (0x0B408) writable; PT_BASE (0x0B608) HW-locked
- ✅ **TLB invalidation** works (0x6C0C/0x6C10 protocol)
- ✅ **GPU_ID** = 0x9FFF9700, **GRBM_STATUS** = 0x00000000 (idle)
- ✅ **CC_ARRAY_CONFIG** (0x9C1C) — partially writable (bits 24-28: 0x1F000000), persists across boots
- ✅ **DISPATCH_INITIATOR** (0x80E0) — W1C trigger, VALID consumed but no execution
- ✅ **COMPUTE_PGM_LO** (0x8110) — WRITABLE, persists across boots (shadow)
- ✅ **CP_MQD_BASE_ADDR** (0x9104) — WRITABLE
- ✅ **CP_HQD_ACTIVE** (0x910C) — WRITABLE, ACKs (reads 1)
- ✅ **SDMA0 registers** at GC offsets 0xE000-0xE01C (ring BASE/CNTL/WPTR/RPTR)

### Hardware Locked / NBIO Blocked
- ❌ **CP_HQD_*** (0xDAC0-0xDBFF) — all NBIO blocked (writes silently dropped)
- ❌ **KIQ_SIZE** (0xE068) — read-only=0, cannot create compute rings
- ❌ **GFX_RING0_BASE_LO** (0xDA60) — read-only, BIOS sets ring base
- ❌ **SPI_PG_ENABLE_STATIC_WGP_MASK** (0x5C3C) — SOS-locked, reads 0 (per-bank GRBM selects confirmed correct; Linux can write it, host BAR5 cannot)
- ❌ **RLC_PG_ALWAYS_ON_WGP_MASK** (0x3D64) — read-only 0xFFFFFFFF
- ❌ **GRBM_GFX_CNTL** (0x2022) — doesn't exist on BC-250

---

## Software PM4 Executor (Confirmed Working)

- ✅ **DreamV3SwPm4Process** — CPU translates PM4 packets to direct MMIO writes
- ✅ IT_WRITE_DATA (SCRATCH changes), IT_NOP, IT_EVENT_WRITE_EOP, PM4_TYPE_0 — **HW-confirmed**
- ✅ IT_SET_CONFIG_REG, IT_SET_CONTEXT_REG, IT_SET_SH_REG, IT_INDIRECT_BUFFER (with depth guard)
- ✅ **PATH 3 fallback** in SEND_PM4 — when PSP KIQ and GfxRing are both unavailable
- ✅ Recursion depth guard (max 32) — prevents stack overflow

---

## SMU Mailbox Protocol (Working)

### SMU v11.8 PPSMC Message IDs
| Msg | Name | Notes |
|-----|------|-------|
| 0x01 | TestMessage | Returns value+1 |
| 0x02 | GetSmuVersion | Returns 0x00580600 (88.6.0) |
| 0x03 | GetDriverIfVersion | Returns 8 |
| 0x0F | QueryGfxclk | Returns MHz |
| 0x37 | GetGfxFrequency | Returns MHz directly |
| 0x38 | GetGfxVid | Returns VID |
| 0x39 | ForceGfxFreq | SAFE with voltage+profile set first |
| 0x3B | ForceGfxVid | VID value |
| 0x3D | GetEnabledSmuFeatures | Returns bitmask |
| 0x1E | QueryActiveWgp | Always returns 0 (WGPs off) |

### Governor Sequence (PROVEN SAFE)
1. Q3(0x8C, 80) — Set GPU max temp to 80°C
2. Q0(0x3A, 0) — Unforce any previous frequency
3. Q0(0x3C, 0) — Unforce any previous voltage
4. Look up safe point: (freq_mhz, mv, profile)
5. Q3(0x1E, profile) — Set perf profile (1=low, 3=high)
6. Q0(0x3B, mv_to_vid(mv)) — Force voltage
7. Q0(0x39, freq_mhz) — Force frequency (SAFE when voltage+profile set)

### Safe Frequency/Voltage Points
| Freq (MHz) | Voltage (mV) | Profile |
|-----------|-------------|---------|
| 500 | 700 | 1 (low) |
| 800 | 750 | 1 |
| 1000 | 800 | 1 |
| 1175 | 850 | 3 (high) |
| 1400 | 900 | 3 |
| 1600 | 950 | 3 |
| 1800 | 1000 | 3 |
| 2000 | 1050 | 3 |

### SMU Mailbox Addresses
- **Q0:** cmd=0x03B10A08, rsp=0x03B10A68, arg=0x03B10A48
- **Q3:** cmd=0x03B10A20, rsp=0x03B10A80, arg=0x03B10A88

---

## PSP Firmware Loading Status (UPDATED 2026-08-18)

### ⭐ PSP KM GPCOM RING WORKS — verified on hardware
The earlier conclusion "ring permanently unavailable / no TOS" was **WRONG** — it was
based on the wrong MP0 base. The real C2PMSG block lives at **BAR5 byte base
0x58000** (= ip_discovery MP0 base `0x16000` in DWORD units × 4), not 0x103D0/0x105D0.

| Register | Offset | Value (verified) |
|----------|--------|------------------|
| C2PMSG_64 (cmd/TOS/RESP) | 0x58200 | **0x80020000** (bit31 RESP ACK + KM ring type 0x0002) |
| C2PMSG_67 (ring WPTR) | 0x5820C | advances 0x10 per frame |
| C2PMSG_69/70/71 (ring addr/size) | 0x58214/0x58218/0x5821C | programmed on ring create |
| C2PMSG_81 (SOS status) | 0x58244 | 0x002B9309 |

Proof (`psp-ring-submit-test.exe` against installed atikmdag.sys):
- **PSP_RING_INIT** (0x80000C18): `Result=1 RingPa=0x7E512000 RingSize=0x1000`
- **PSP_RING_SUBMIT** (0x80000C1C): fence reached for **every** command
- **`GET_FW_ATTESTATION` (0x0F): `RespStatus=0x00000000` = SUCCESS** — the PSP executed a real command through the ring
- `GET_FW_ATTESTATION2` (0x10) / `FB_FW_RESERV_ADDR` (0x50): `0x00000100` = `PSP_ERR_UNKNOWN_COMMAND` — protocol works, this SOS just doesn't implement them

### Kernel implementation
- `src/kmd/amdbc250_dream_kmd.c` — cases `0x80000C18` (PSP_RING_INIT) and `0x80000C1C`
  (PSP_RING_SUBMIT), plus `0x80000C20` (PSP_RING_LOAD_IP_FW). File-scope defines
  `PSP_MP0_BASE 0x58000`, C2PMSG_64/67/69/70/71/81, `PSP_RING_SIZE 0x1000`,
  `PSP_RING_TYPE_KM 2`, `PSP_CMD_BUF_SIZE 0x1000`. Supersedes the wrong
  `DIRECT_C2PMSG_*` (base 0x103D0) in `amdbc250_psp.c`.
- **INIT**: waits TOS-ready (C2PMSG_64 bit31) → allocates 0x1000 ring →
  writes C2PMSG_69/70/71 → `C2PMSG_64 = KM(2)<<16` → waits RESP bit31 →
  `PspRingCreated`.
- **SUBMIT**: builds `psp_gfx_cmd_resp` (cmd_id + 16B union at +28, resp at +864),
  writes 64B `psp_gfx_rb_frame`, advances WPTR, kicks C2PMSG_67, polls fence with
  HDP flush.
- **LOAD_IP_FW** (0x80000C20): reads the firmware file itself
  (`DreamV3LoadFirmwareFromFile`, path in input struct), stages it into a contiguous
  GPU-visible buffer, submits `GFX_CMD_ID_LOAD_IP_FW` (0x06) with
  `{fw_phy_addr_lo, fw_phy_addr_hi, fw_size, fw_type}`. File I/O runs before the
  fast mutex (IRQL safety); staging buffer freed only when the fence is reached.
- Cleanup of `PspRingVa`/`PspCmdVa`/`PspFenceVa` in `DreamV3WdmUnload`.

### Test tools
- `test-tools/psp-ring-submit-test.c` → `output/psp-ring-submit-test.exe`
  (ring init + submit proof; optional single cmd arg, e.g. `exe 0x50`)
- `test-tools/psp-ring-create-v2.c` — user-mode ring_create proof (base 0x58000)
- `test-tools/psp-ring-load-ip-fw-test.c` → `output/psp-ring-load-ip-fw-test.exe`
  (loads all fw types, or a single one: `exe 4` for MEC)

### Status of the DIRECT C2PMSG path (CORRECTED 2026-08-21)
- Driver has `IOCTL_AMDBC250_PSP_LOAD_IP_FW` (CTL_CODE 0x920) direct
  C2PMSG_35/36/37 path at **correct base 0x58000** — **now works** (verified 2026-08-21).
- `psp-tos-test.exe` (TOS load) and `psp-fw-load.exe` (8 IP firmware) both PASS.
- The **PSP driver (PspDriver.sys)** in the sibling repo is deprecated — firmware
  loading is now integrated into the GPU driver via BOTH the ring AND the corrected direct path.

---

## SDMA Status

### Current State
- **RB_BASE_LO** = 0x00555555 (not a valid physical address)
- **RB_CNTL** = 0x00000000 (ring disabled)
- **RB_WPTR** = 0x00000000 (no work submitted)
- **SDMA self-test** returns 0xC00000A3 (STATUS_INVALID_PARAMETER)

### Why SDMA Doesn't Work
1. **SDMA firmware is broken** — `cyan_skillfish2_sdma.bin` (v0x34) never drives user queues
2. **Ring not initialized** — `DreamV3HwInitialize` with safe flags skips ring init
3. **Full init crashes** — `Flags=0` causes TDR/0x1A BSOD (known issue)

### What's Needed
1. Load SDMA firmware via the PSP ring (now possible — `psp-ring-load-ip-fw-test.exe`, type 9/10)
2. Load working SDMA firmware (`navi12_sdma.bin` v0x2c works on Linux)
3. Initialize SDMA ring with valid physical address
4. Test SDMA copy/fill operations

---

## Community Findings (GabriWar/bc250-rocm-working)

> ⚠️ These are **Linux** observations. **Our Windows unit does NOT reproduce them**
> (SPI_PG reads 0 even with correct per-bank GRBM selects, RLC_PG reads 0xFFFFFFFF
> read-only) — see "Hardware Locked" above and AGENTS.md 2026-08-01.

### WGP Registers Pre-Unlocked from VBIOS (Linux claim)
- **SPI_PG_ENABLE_STATIC_WGP_MASK** = 0x1F at boot (after VBIOS POST)
- **RLC_PG_ALWAYS_ON_WGP_MASK** = 0x1F at boot
- **CC_GC_SHADER_ARRAY_CONFIG** = 0xFFE00000 (harvest mask from fuse)

### SDMA Firmware Broken on BC-250
- `cyan_skillfish2_sdma.bin` (v0x34) **never drives user queues**
- `navi12_sdma.bin` (v0x2c) **works correctly** on BC-250 silicon (+20% H2D bandwidth)
- No signature checking on BC-250 — other chips' firmware loads without rejection

### GRBM_GFX_INDEX Layout (RESOLVED 2026-08-01)
- **hw.h documents BOTH layouts** (inc/amdbc250_dream_hw.h:410-455):
  - A) Linux gfx9/gfx10: INST bits 7:0, SH 15:8, SE 23:16, broadcast = **0x15000000** — the macro `AMDBC250_GRBM_GFX_INDEX_BROADCAST_VAL` uses this (correct for per-bank WGP/SE/SH selects)
  - B) older soc15: MEID/PIPEID/QUEUEID, broadcast = 0xE0000000 — KIQ select uses plain ME=1 (0x00010000), verified that KIQ regs only respond to layout B
- Empirically: per-bank A-layout selects read SPI_PG=0 on ALL banks → the SPI_PG=0 result is SOS-locking, NOT a wrong bank select.

---

## Fundamental Blockers (Why Not 3D Ready)

| Blocker | Root Cause | Can We Fix? |
|---------|-----------|-------------|
| **KIQ_SIZE=0 read-only** | Hardware-level; address 0xE068 not in firmware binary | ❌ No |
| **CP_HQD NBIO-blocked** | NBIO firewall blocks 0xDAC0-0xDBFF | ❌ No |
| **GCVM PT_BASE HW-locked** | Always reads 0; cannot configure page tables | ❌ No |
| **GFX_RING0_BASE_LO read-only** | BIOS sets ring base; writes ignored | ❌ No |
| **KIQ_WPTR 9-bit limit** | Max ring 2048 bytes; HW limitation | ❌ No |
| **SPI_PG SOS-locked** | Host BAR5 writes blocked by PSP Secure OS | ⚠️ Only via EFI/Linux (Linux amdgpu writes it; EFI Shell scripts in `third-party/EFI_Boot/`, but NBIO is locked at EFI boot on this unit) |
| **SDMA firmware broken** | Stock firmware never drives user queues | ⚠️ Maybe (navi12 firmware works on Linux) |

**Conclusion:** This BC-250 is **not factory-locked** — it is a full PS5 Oberon die
(8× Zen2 + RDNA2 + 40 CUs) and **Linux amdgpu runs shaders on it** (compute rings,
CachyOS dmesg). The GFX/compute/SDMA **hardware ring paths are SOS-locked from host
BAR5 writes**, so **3D graphics is not achievable on Windows WDM as-is** — it would
require EFI pre-boot WGP unlock, Linux kernel context, or a real WDDM miniport with
full PSP authentication. Note: the **PSP GPCOM ring works** on Windows (see "PSP
Firmware Loading Status" above) — not every ring path is locked.

---

## What We Built Anyway (Software Workaround)

Since HW rings are inaccessible, we built a **Software PM4 executor** that:
- Accepts standard PM4 packets (IT_WRITE_DATA, IT_SET_CONFIG_REG, IT_INDIRECT_BUFFER, etc.)
- Translates them to direct MMIO writes (CPU work, not GPU)
- Allows testing any PM4 packet without hardware ring risk

This allows **GPU register control** and hardware understanding, but **does not replace GPU shader execution**.

---

## Code Review: Bugs Found & Fixed

### Fixed Bugs (17 driver + 3 test-tool — from 2026-08-13 SDMA/IOCTL audit)
| # | Area | Bug | Status |
|---|------|-----|--------|
| 1 | BAR5_PROXY | Integer overflow in `offset + 4` (wraps when offset >= 0xFFFFFFFC) | FIXED |
| 2 | RLC | RLC_CP_SCHEDULERS at 0xECA1 not 4-byte aligned | FIXED (uses 0xECA8, writable) |
| 3 | GCVM | Page table pages double-freed on error | FIXED (`newlyAllocated[]` tracking) |
| 4 | IOCTL | Name collision between GPU and PSP driver | FIXED (PSP uses raw 0x900) |
| 5 | REG_DUMP | Reads GRBM_GFX_INDEX at wrong offset 0x33C4 | FIXED (reads 0x34D0) |
| 6 | GCVM | Invalidate regs mismatch (hw.h 0x0B51C vs working 0x6C0C) | FIXED (both use 0x6C0C/0x6C10) |
| 7 | REG_DUMP | GPU_ID read from 3 different offsets | FIXED (all use 0x0000) |
| 8 | KIQ | KIQ_NOP_TEST / KIQ_BIOS_RING_SUBMIT lack `__try/__except` | FIXED (SEH added) |
| 9 | PSP | PspGpuProxyWriteRegister return value ignored | FIXED |
| 10 | SEND_PM4 | Ring wrap check 32-bit overflow | FIXED (cast to ULONG64) |
| 11 | GCVM_PT | No MmioSize bounds check | FIXED |
| 12 | PSP proxy | Race on init (no lock) | FIXED (spinlock + guard) |
| 13 | LOAD_CP_FW | Halts ALL engines for MEC-only load | FIXED (targets fwType) |
| 14 | GCVM | Page table pages never freed on unload | FIXED (freed in DreamV3WdmUnload) |
| 15 | REG_DUMP | Reads SDMA0_CNTL at 0x10040 (wrong) | FIXED (uses hw.h 0xE018) |
| 16 | hw.h | MEC_ME1_HALT bit may be inverted | DOCUMENTED |
| 17 | hw.h | CpRb1BaseProbe field mislabels GFX ring0 regs | DOCUMENTED |

### Test tool fixes
| # | Tool | Bug | Status |
|---|------|-----|--------|
| 18 | bar5-smn-test.c | Used header IOCTL codes (METHOD_BUFFERED) instead of driver's METHOD_NEITHER raw values | FIXED 2026-08-13 |
| 19 | psp-fw-load.c | IOCTL code 0x80002480 instead of 0x80002483 | FIXED 2026-08-13 |
| 20 | psp-fw-load.c | SDMA firmware names wrong (cyan_skillfish2 → navi12) | FIXED 2026-08-13 |

---

## Mistakes & Lessons Learned

### Critical Mistakes
1. **IC_BASE_CNTL=0x100 before setting base address** — if bit 8 auto-started DMA from address 0, firmware never loads. Should be: LO → HI → CNTL=0
2. **PM4 TYPE3 header encoding** — `(3<<30) | ((count-1)<<16) | (op<<8)` but all tests used `0xC0370003` which is IT_NOP count=56, not IT_WRITE_DATA. Bug never manifested because KIQ never processed the ring.
3. **CP_HQD writability claim** — previously claimed 0xDAC0+ are writable; actually NBIO-blocked. Misled by aliased/stale readback values.
4. **RLC firmware loading** — attempted to load RLC firmware via 0x3A00 registers which are in FREEZE ZONE (0x3400-0x8100) — BC-250 BIOS/SMU loads RLC.
5. **KIQ_SIZE patch expectation** — believed MEC firmware could be patched to fix KIQ_SIZE=0; but 0xE068 address not found in firmware binary — check is hardware-level.
6. **PSP driver signing — Inf2Cat in x86\ not x64\** — build.bat only searched x64\ for Inf2Cat, but WDK 10.0.26100.0 has it in x86\. Fallback to makecat generated incomplete catalog — PSP driver rejected as "not digitally signed".
7. **PSP firmware loading via GPU driver** — first attempt used direct C2PMSG_35/36/37 writes with wrong offsets (0x1056C/0x10570/0x10574, base 0x103E0). **The real fix was the GPCOM ring at base 0x58000** (C2PMSG_64/67/69/70/71), which now works on hardware.

### Wasted Effort
1. **GRBM_GFX_CNTL (0x2022)** — attempted to unlock HQD access; doesn't work on BC-250.
2. **comprehensive-pm4-test** — leaves KIQ active + ME_CNTL unhalted → system hang
3. **PSP driver KIQ path** — PSP KIQ ring has the same problem (WPTR goes through, RPTR=0)
4. **RLC init** — DreamV3InitRlc is no-op; registers in freeze zone
5. **WGP unlock on Windows** — SPI_PG SOS-locked, cannot be written from host

### Technical Lessons
- **NBIO blocking** — 0xC000+ range is NBIO-protected; GC_BASE-shifted aliases can bypass but not for all registers
- **WRITE_REGISTER_ULONG vs volatile*** — on Win11 26100 volatile pointer writes can be silently dropped; WRITE_REGISTER_ULONG always works
- **Firmware execution** proven by corrupting first 8 bytes of ucode and observing SCRATCH changes
- **KIQ_WPTR** only 9 bits (not 32) — HW limitation
- **SDMA init** can cause BSOD if registers are wrong; needs sanity check
- **Code review before build** — saves hours of debugging (17 bugs found, 15 fixed + 2 documented)
- **PSP ring protocol** — earlier claim "SOS doesn't support TOS ring creation (C2PMSG_64 bit31 never sets)" was based on the **wrong MP0 base (0x103D0/0x105D0)**. Real base is `0x58000`; the ring now works (verified 2026-08-18). Always verify the register base before concluding hardware is dead.
- **SMU mailbox via SMN** — use NBIO BAR5+0x38/0x3C, NOT BAR5 direct (MP1 not mapped into BAR5 on BC-250)

---

## How to Build

### Prerequisites
- Visual Studio 2022 (Community/Professional/BuildTools) — auto-detected on C:, D:, or E: drive
- Windows WDK 10.0.26100.0
- Test signing: `bcdedit /set testsigning on` (Admin), Secure Boot OFF

### Build
```cmd
build.bat
```

### Install
1. `build.bat` → `output\atikmdag.sys`
2. Device Manager → AMD Radeon BC-250 → **Uninstall device** (check "Delete driver")
3. **Reboot**
4. Device Manager → Update Driver → Browse → `output\`
5. **Reboot**

### Test
```cmd
output\bar5-smn-test.exe              # SMU mailbox via SMN (freq, VID, features)
output\psp-ring-submit-test.exe       # PSP GPCOM ring init + submit (VERIFIED working)
output\psp-ring-load-ip-fw-test.exe   # Load IP firmware through the ring (e.g. "exe 4" = MEC)
output\psp-fw-load.exe                # DIRECT C2PMSG firmware load (8/8 PASS, base 0x58000)
output\psp-tos-test.exe               # TOS load (Ta.bin) via direct C2PMSG (PASS)
output\test-gpu-ioctls.exe            # 15 IOCTL tests (14/15 pass)
output\sdma-selftest.exe              # SDMA self-test (currently fails — ring not init)
```

---

## Registry Settings

All values live under `HKLM\SYSTEM\CurrentControlSet\Services\atikmdag` (DWORD).
Defaults are chosen **fail-closed** (dangerous behavior OFF unless explicitly enabled).

| Value | Default | Purpose |
|-------|---------|---------|
| `DriverEntryRan` | (auto) | 1 after DriverEntry. Confirms the new binary loaded. |
| `Step_HwInit` | (auto) | Highest hardware-init step reached (survives reboot). |
| `HwInitMaxStep` | 0 (all) | Cap hardware init at step N (0 = run all). Used to binary-search init crashes. |
| `HwInitFirmware` | 1 | 0 = skip CP firmware load (isolate 0x1A BSOD). |
| `HwUnhaltCp` | 0 | 0 = keep CP halted after firmware load (rogue host DMA → 0x1A). |
| `HwInitGfxRing` | 0 | 0 = skip GFX ring init (BASE regs host read-only / SOS-locked). |
| `HwInitSdmaRing` | 0 | 0 = skip SDMA ring init (suspected 0x1A source). |
| `HwInitGart` | 0 | 0 = skip GART (MC_VM_AGP_* are SOS-owned; writing → 0x1A). |
| `HwInitVm` | 0 | 0 = skip GPUVM system-aperture init (SOS-owned MC class). |
| `HwInitMemCtrl` | 0 | 0 = skip GB_ADDR_CONFIG (0x61D8, freeze zone) + MC_VM_FB_LOCATION writes. |
| `DisplayWritesEnabled` | 0 | 0 = DISABLE live DCN HUBPREQ/OTG writes (they target a real 2560x1440 scanout and black-screen the GPU). Set 1 only after a full DCN init exists. |

### Notes
- **`HwInitMemCtrl=0` is critical.** GB_ADDR_CONFIG now resolves to `0x61D8`
  (in the 0x3400–0x8100 FREEZE ZONE) and `MC_VM_FB_LOCATION` (0x9520/0x9524)
  is in the SOS-owned MC block — both were suspected 0x1A crash sources in
  full-init (Flags=0). The NBIO_MAP path (`AMDBC250_INIT_FLAG_NBIO_MAP`) never
  runs them.
- **`DisplayWritesEnabled=0` is critical.** After the DCN base correction to
  `0xD300`, the DDI/IOCTL HUBPREQ writes (0xEB28+) hit a LIVE framebuffer.
  Enabling them without a real DCN pipeline black-screens and hangs the GPU.
- Full-init (`Flags=0` via `full-init-test.exe`) is **not safe** until
  `HwInitMemCtrl` is verified. Use `Flags=AMDBC250_INIT_FLAG_NBIO_MAP` for all
  register/SMU work.

### Example
```cmd
reg add "HKLM\SYSTEM\CurrentControlSet\Services\atikmdag" /v HwInitMaxStep /t REG_DWORD /d 0 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Services\atikmdag" /v HwInitMemCtrl /t REG_DWORD /d 0 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Services\atikmdag" /v DisplayWritesEnabled /t REG_DWORD /d 0 /f
```

---

## File Structure

```
├── src/kmd/                        # Kernel-Mode Driver
│   ├── amdbc250_dream_kmd.c        # DriverEntry, IOCTL dispatch, PSP GPCOM ring (INIT/SUBMIT/LOAD_IP_FW)
│   ├── amdbc250_dream_kmd_driverentry.c  # DriverEntry wrapper
│   ├── amdbc250_dream_kmd_ddi_stubs.c    # WDDM DDI stubs
│   ├── amdbc250_dream_hw_init.c    # GPU init, ring buffers, PSP
│   ├── amdbc250_dream_fw_load.c    # CP firmware via IC_BASE DMA
│   ├── amdbc250_dream_psp_fw_load.c      # PSP/SMU firmware paths
│   ├── amdbc250_dream_power.c      # Power/thermal management
│   ├── amdbc250_dream_vm.c         # GPUVM, GART, page tables
│   ├── amdbc250_dream_rlc.c        # RLC init
│   ├── amdbc250_dream_vbios.c      # VBIOS handling
│   ├── amdbc250_dream_hdp.c        # HDP flush
│   ├── amdbc250_psp.c              # PSP proxy driver interface
│   └── firmware_data.h             # Embedded PSP firmware
├── src/umd/                        # User-Mode Driver
│   └── amdbc250_umd_v46.c          # D3D9 DDI (45+ functions)
├── inc/                            # Shared headers
│   ├── amdbc250_dream_hw.h         # Hardware register definitions
│   ├── amdbc250_hw_extra.h         # Extra register definitions
│   └── amdbc250_ioctl.h            # IOCTL codes + structures
├── test-tools/                     # Diagnostic tools
│   ├── bar5-smn-test.c             # SMU mailbox via SMN
│   ├── test-gpu-ioctls.c           # 15 IOCTL tests
│   ├── sdma-selftest.c             # SDMA self-test
│   ├── psp-fw-load.c               # OBSOLETE direct-C2PMSG firmware path
│   ├── psp-ring-submit-test.c      # PSP GPCOM ring init + submit (VERIFIED)
│   ├── psp-ring-create-v2.c        # user-mode ring_create proof (base 0x58000)
│   ├── psp-ring-load-ip-fw-test.c  # Load IP firmware via the ring
│   ├── smn-gc-alias-scan.c         # GC SMN alias scan + per-bank GRBM
│   ├── smn-core-unlock-test.c      # CPU core unlock via SMU Q3 0x98
│   └── ...                         # Many more diagnostic tools
├── output/                         # Build output (signed drivers)
├── docs/                           # Technical documentation
├── firmware/                       # Firmware binaries
│   ├── cyan_skillfish2_*.bin       # CP firmware (ME, PFP, CE, MEC, RLC)
│   ├── navi12_sdma.bin             # SDMA0 firmware (v0x2c, works on Linux)
│   └── navi12_sdma1.bin            # SDMA1 firmware (v0x2c, works on Linux)
├── build.bat                       # Build + sign driver
├── prebuild-check.ps1               # Pre-build validation
├── reinstall-gpu-driver.bat         # Reinstall GPU driver
└── .gitignore
```

---

## Key Documentation

| File | Description |
|------|-------------|
| [AGENTS.md](AGENTS.md) | Agent memory — hardware facts, current blockers, test results |
| [docs/AMD_Hybrid.md](docs/AMD_Hybrid.md) | F:\AMD hybrid driver analysis (WGP/CUMode registry, INF, CAT) |
| [docs/BC250-LINUX-IP-MAP.md](docs/BC250-LINUX-IP-MAP.md) | Linux-verified IP base addresses |
| [docs/REGISTER-MAP-BC250.md](docs/REGISTER-MAP-BC250.md) | Complete BC-250 register map |
| [docs/RING-INIT-STATUS.md](docs/RING-INIT-STATUS.md) | Ring init blockers and KIQ path |
| [docs/PSP-PROXY-BYPASS.md](docs/PSP-PROXY-BYPASS.md) | PSP proxy architecture |
| [docs/PSP-GPCOM-RING-WORKING.md](docs/PSP-GPCOM-RING-WORKING.md) | **PSP GPCOM ring — verified working, offsets + IOCTLs** |
| [third-party/EFI_Boot/](third-party/EFI_Boot/) | EFI Shell WGP unlock scripts (pre-boot) |
| [third-party/linuxinfo/](third-party/linuxinfo/) | Linux firmware blobs and dmesg logs |

---

## Next Steps

1. **DIRECT C2PMSG path verified** — `psp-fw-load.exe` (8/8 IP firmware) and `psp-tos-test.exe` (Ta.bin TOS) both PASS on hardware. The old "bootloader gone" verdict was WRONG BASE artifact (0x10614).
2. **PSP commands after LOAD_IP_FW** — `SETUP_TMR (0x05)` VERIFIED working (VRAM MC 0xF40F800000 + aper_base 0xCF800000). Next: `LOAD_TOC (0x20)`, `AUTOLOAD_RLC (0x21)` via the ring.
3. **SDMA via ring** — load `navi12_sdma.bin` (v0x2c, type 9/10) through
   `psp-ring-load-ip-fw-test.exe`, then retry SDMA ring init/copy.
4. **KMDOD display driver** — expand modes, EDID, power management.
5. **WGP unlock** — EFI Shell pre-boot scripts (`third-party/EFI_Boot/`) remain the
   only Windows-side path to write SPI_PG; NBIO is locked at EFI boot on this unit.
6. **wddm-ps5 real WDDM miniport** (displib.lib path) — production candidate, not
   yet display-verified.

---

## External Resources

### Community Documentation
- **elektricM/amd-bc250-docs** — Community-driven BC-250 documentation
  - Mesa 25.1+ required, RADV only
  - VRAM split configurable via memcfg utility (256MB-12GB)
  - Known issues: artifacts, no VA-API, ROCm experimental
- **GabriWar/bc250-rocm-working** — SDMA firmware analysis, WGP unlock research
- **duggasco/bc250-40cu-unlock** — 40 CU unlock via Linux kernel patch
- **rw-r-r-0644/bc250-core-unlock** — CPU core unlock via SMU Q3 msg 0x98

### Linux Driver Requirements
```bash
# Minimum Mesa version
glxinfo | grep "OpenGL version"  # Must be 25.1+

# Vulkan driver
vulkaninfo | grep "driverName"   # Should show: radv

# Environment variables
export RADV_DEBUG=nohiz          # Fix artifacts
export AMD_VULKAN_ICD=RADV       # Force RADV driver
```

---

## Related Projects

- **GPU Driver**: https://github.com/Keshas-dev/AMD-BC-250-Windows-Driver
- **PSP Driver**: https://github.com/Keshas-dev/AMD-BC-250-PSP-Windows-Driver
- **Community Docs**: https://github.com/elektricM/amd-bc250-docs
- **Linux Kernel**: https://github.com/torvalds/linux (drivers/gpu/drm/amd/amdgpu)

## License

Source code for educational purposes. Use at your own risk.
ACO compiler: MIT license (Mesa project).
