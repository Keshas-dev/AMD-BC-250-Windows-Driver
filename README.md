# AMD BC-250 Windows Driver Project

## Who We Are

AMD BC-250 Windows driver project by Keshas. Goal: fully working GPU driver for AMD BC-250 (Cyan Skillfish) on Windows.

**Everyone welcome!** GPU drivers, WDDM, Vulkan experience — or just want to help.

## Hardware

- **SoC:** AMD BC-250 (Cyan Skillfish) — 24 CU RDNA2, 16GB GDDR6 shared memory
- **GPU ID:** 0x9FFF9700, **BIOS:** P4.00G
- **Memory:** CPU and GPU share GDDR6 (UMA) — VRAM at 0xC0000000
- **GPU BAR5:** 0xFE800000 (512KB MMIO register space)
- **PSP BAR0:** 0xFD600000 (256KB)
- **GC_BASE:** 0x1260 (BC-250 uses shifted register offsets vs Navi10)
- **SMU version:** 88.6.0 (driver_if=8)
- **PSP IP:** v11.0.8 (CYAN_SKILLFISH2 variant)

---

## Current Status (2026-08-13)

### Working
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

### Not Working / Blocked
- ❌ **PSP firmware loading via GPU driver** — C2PMSG offsets wrong for BC-250 PSP v11.0.8
- ❌ **SDMA self-test** — ring not initialized (RB_BASE_LO=0x00555555, no copy engine)
- ❌ **3D graphics** — WGP compute permanently disabled (SPI_PG=0, hardware-fused)
- ❌ **WGP unlock on Windows** — SPI_PG_ENABLE_STATIC_WGP_MASK SOS-locked
- ❌ **KIQ_SIZE=0** — hardware read-only, cannot create compute rings

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

### psp-fw-load.exe ❌ (0/8 firmware loaded)
```
[1/8] CE (cyan_skillfish2_ce.bin)...  FAIL (err=1)
[2/8] PFP (cyan_skillfish2_pfp.bin)... FAIL (err=1)
[3/8] ME (cyan_skillfish2_me.bin)...  FAIL (err=1)
[4/8] MEC (cyan_skillfish2_mec.bin)... FAIL (err=1)
[5/8] MEC2 (cyan_skillfish2_mec2.bin)... FAIL (err=1)
[6/8] RLC (cyan_skillfish2_rlc.bin)... FAIL (err=1)
[7/8] SDMA0 (navi12_sdma.bin)... FAIL (err=1)
[8/8] SDMA1 (navi12_sdma1.bin)... FAIL (err=1)
```

**Root cause:** PSP firmware loading via GPU driver uses wrong C2PMSG offsets for BC-250 PSP v11.0.8. Linux uses ring mechanism (C2PMSG_64/67/69/70/71), not direct C2PMSG_35/36/37 writes.

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
- ✅ **CC_ARRAY_CONFIG** (0x9C1C) — partially writable (bits 24-28: 0x1F000000)
- ✅ **SPI_PG_ENABLE_STATIC_WGP_MASK** (0x5C3C) — READ-ONLY 0 (WGPs fused off)
- ✅ **RLC_PG_ALWAYS_ON_WGP_MASK** (0x3D64) — READ-ONLY 0xFFFFFFFF
- ✅ **DISPATCH_INITIATOR** (0x80E0) — W1C trigger, VALID consumed but no execution
- ✅ **COMPUTE_PGM_LO** (0x8110) — WRITABLE, persists across boots (shadow)
- ✅ **CP_MQD_BASE_ADDR** (0x9104) — WRITABLE
- ✅ **CP_HQD_ACTIVE** (0x910C) — WRITABLE, ACKs (reads 1)
- ✅ **SDMA0 registers** at GC offsets 0xE000-0xE01C (ring BASE/CNTL/WPTR/RPTR)

### Hardware Locked / NBIO Blocked
- ❌ **CP_HQD_*** (0xDAC0-0xDBFF) — all NBIO blocked (writes silently dropped)
- ❌ **KIQ_SIZE** (0xE068) — read-only=0, cannot create compute rings
- ❌ **GFX_RING0_BASE_LO** (0xDA60) — read-only, BIOS sets ring base
- ❌ **SPI_PG_ENABLE_STATIC_WGP_MASK** (0x5C3C) — SOS-locked, reads 0
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

## PSP Firmware Loading Status

### Current Implementation (BROKEN)
- Driver uses direct C2PMSG_35/36/37/81 writes (offsets 0x1056C/0x10570/0x10574/0x10614)
- **These offsets are WRONG for BC-250 PSP v11.0.8**
- Linux `psp_v11_0_8.c` uses **ring mechanism** (C2PMSG_64/67/69/70/71), not direct writes
- Result: all 8 firmware types fail (CE, PFP, ME, MEC, MEC2, RLC, SDMA0, SDMA1)

### What Works
- **PSP driver (PspDriver.sys)** — separate driver, has its own firmware loading path
- **SMU mailbox via SMN** — works perfectly (TestMessage, GetSmuVersion, ForceGfxFreq, etc.)
- **PSP SOS alive detection** — C2PMSG_81 bit31 = 1 (0xF0000010)

### What Doesn't Work
- **Direct PSP mailbox firmware loading** via GPU driver — wrong C2PMSG offsets
- **PSP ring creation** — C2PMSG_64 bit31 never sets (no TOS support)
- **PSP PROG_REG** — not supported by BC-250 SOS

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
1. Fix PSP firmware loading (or use PSP driver)
2. Load working SDMA firmware (`navi12_sdma.bin` v0x2c works on Linux)
3. Initialize SDMA ring with valid physical address
4. Test SDMA copy/fill operations

---

## Community Findings (GabriWar/bc250-rocm-working)

### WGP Registers Pre-Unlocked from VBIOS
- **SPI_PG_ENABLE_STATIC_WGP_MASK** = 0x1F at boot (after VBIOS POST)
- **RLC_PG_ALWAYS_ON_WGP_MASK** = 0x1F at boot
- **CC_GC_SHADER_ARRAY_CONFIG** = 0xFFE00000 (harvest mask from fuse)
- **Our driver reads 0 for SPI_PG** — likely wrong GRBM_GFX_INDEX broadcast

### SDMA Firmware Broken on BC-250
- `cyan_skillfish2_sdma.bin` (v0x34) **never drives user queues**
- `navi12_sdma.bin` (v0x2c) **works correctly** on BC-250 silicon (+20% H2D bandwidth)
- No signature checking on BC-250 — other chips' firmware loads without rejection

### GRBM_GFX_INDEX Broadcast Correction
- **Correct broadcast for gfx10:** 0x15000000 (INST_BCAST_WR bit24 | SH_BCAST_WR bit26 | SE_BCAST_WR bit28)
- **Our hw.h documents 0xE0000000** — matches older soc15 layout, NOT gfx10
- Per-bank selects: SH=1<<8, SE=1<<16 (matches Linux `gfx_v9_0_select_se_sh`)

---

## Fundamental Blockers (Why Not 3D Ready)

| Blocker | Root Cause | Can We Fix? |
|---------|-----------|-------------|
| **KIQ_SIZE=0 read-only** | Hardware-level; address 0xE068 not in firmware binary | ❌ No |
| **CP_HQD NBIO-blocked** | NBIO firewall blocks 0xDAC0-0xDBFF | ❌ No |
| **GCVM PT_BASE HW-locked** | Always reads 0; cannot configure page tables | ❌ No |
| **GFX_RING0_BASE_LO read-only** | BIOS sets ring base; writes ignored | ❌ No |
| **KIQ_WPTR 9-bit limit** | Max ring 2048 bytes; HW limitation | ❌ No |
| **SOS firmware no ring protocol** (PSP side) | C2PMSG_64 bit 31 never sets; TOS doesn't support GPCOM | ❌ No |
| **SPI_PG SOS-locked** | Host BAR5 writes blocked by PSP Secure OS | ❌ No |
| **SDMA firmware broken** | Stock firmware never drives user queues | ⚠️ Maybe (navi12 firmware works on Linux) |

**Conclusion:** This BC-250 variant is factory-locked for GPU command execution. All hardware ring paths are locked. **3D graphics with this specific hardware is not achievable on Windows.**

---

## What We Built Anyway (Software Workaround)

Since HW rings are inaccessible, we built a **Software PM4 executor** that:
- Accepts standard PM4 packets (IT_WRITE_DATA, IT_SET_CONFIG_REG, IT_INDIRECT_BUFFER, etc.)
- Translates them to direct MMIO writes (CPU work, not GPU)
- Allows testing any PM4 packet without hardware ring risk

This allows **GPU register control** and hardware understanding, but **does not replace GPU shader execution**.

---

## Code Review: Bugs Found & Fixed

### Fixed Bugs (17 total)
| # | File | Bug | Fix Date |
|---|------|-----|----------|
| 1 | kmd.c | Integer overflow in READ/WRITE_REG bounds check | 2026-06-26 |
| 2 | fw_load.c | BAR5_U32 volatile ptr silently dropped on Win11 26100 | 2026-06-26 |
| 3 | kmd.c | LOAD_CP_FW halts ALL engines when only MEC needs loading | 2026-06-26 |
| 4 | fw_load.c | Integer overflow in firmware header validation | 2026-06-26 |
| 5 | kmd.c | Integer overflow in JT bounds check | 2026-06-26 |
| 6 | kmd.c | SEND_PM4 ring wrap 32-bit overflow | 2026-06-26 |
| 7 | PspKiq.c | body_size=1 → 4 (mismatch with PspCore.c) | 2026-06-26 |
| 8 | PspKiq.c | Default ring size 0x2000 exceeds 9-bit WPTR max | 2026-06-26 |
| 9 | PspKiq.c, PspCore.c | PspGpuProxyWriteRegister return values ignored | 2026-06-26 |
| 10 | PspCore.c | Race condition on g_GpuDriverHandle init | 2026-06-26 |
| 11 | hw.h:378 | CP_HQD_PQ_WPTR_POLL_CNTL = 0x9148 (dup of PQ_CONTROL) | 2026-08-07 |
| 12 | test-tools/bar5-smn-test.c | Used header IOCTL codes (METHOD_BUFFERED) instead of driver's METHOD_NEITHER | 2026-08-13 |
| 13 | test-tools/psp-fw-load.c | IOCTL code 0x80002480 instead of 0x80002483 | 2026-08-13 |
| 14 | test-tools/psp-fw-load.c | SDMA firmware names wrong (cyan_skillfish2_sdma.bin → navi12_sdma.bin) | 2026-08-13 |

### Remaining Bugs (7)
| # | File | Description | Priority |
|---|------|-------------|----------|
| 1 | hw.h:360, PspKiq.c:57 | RLC_CP_SCHEDULERS at 0xECA1 not 4-byte aligned | High |
| 2 | ioctl.h:420, PspCore.c:17 | IOCTL name collision (CTL_CODE vs raw) | Medium |
| 3 | kmd.c:5419 | REG_DUMP reads GRBM_GFX_INDEX at wrong offset 0x33C4 | Low |
| 4 | hw.h:453-454, vm.c:755-762 | GCVM invalidate regs mismatch | Low |
| 5 | kmd.c:3570,3601,5412 | GPU_ID read from 3 different offsets | Low |
| 6 | kmd.c:5469 | REG_DUMP reads SDMA0_CNTL at 0x10040 (hw.h says 0xE018) | Low |
| 7 | kmd.h:428-430 | GCVM page table pages never freed on unload | Low |

---

## Mistakes & Lessons Learned

### Critical Mistakes
1. **IC_BASE_CNTL=0x100 before setting base address** — if bit 8 auto-started DMA from address 0, firmware never loads. Should be: LO → HI → CNTL=0
2. **PM4 TYPE3 header encoding** — `(3<<30) | ((count-1)<<16) | (op<<8)` but all tests used `0xC0370003` which is IT_NOP count=56, not IT_WRITE_DATA. Bug never manifested because KIQ never processed the ring.
3. **CP_HQD writability claim** — previously claimed 0xDAC0+ are writable; actually NBIO-blocked. Misled by aliased/stale readback values.
4. **RLC firmware loading** — attempted to load RLC firmware via 0x3A00 registers which are in FREEZE ZONE (0x3400-0x8100) — BC-250 BIOS/SMU loads RLC.
5. **KIQ_SIZE patch expectation** — believed MEC firmware could be patched to fix KIQ_SIZE=0; but 0xE068 address not found in firmware binary — check is hardware-level.
6. **PSP driver signing — Inf2Cat in x86\ not x64\** — build.bat only searched x64\ for Inf2Cat, but WDK 10.0.26100.0 has it in x86\. Fallback to makecat generated incomplete catalog — PSP driver rejected as "not digitally signed".
7. **PSP firmware loading via GPU driver** — used direct C2PMSG_35/36/37 writes with wrong offsets (0x1056C/0x10570/0x10574). BC-250 PSP v11.0.8 uses ring mechanism (C2PMSG_64/67/69/70/71), not direct writes.

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
- **Code review before build** — saves hours of debugging (17 bugs found, 14 fixed)
- **PSP ring protocol** — BC-250 SOS doesn't support TOS ring creation (C2PMSG_64 bit31 never sets)
- **SMU mailbox via SMN** — use NBIO BAR5+0x38/0x3C, NOT BAR5 direct (MP1 not mapped into BAR5 on BC-250)

---

## How to Build

### Prerequisites
- Visual Studio 2022 (Community/Professional) — auto-detected on F: or E: drive
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
output\bar5-smn-test.exe         # SMU mailbox via SMN (freq, VID, features)
output\test-gpu-ioctls.exe       # 15 IOCTL tests (14/15 pass)
output\sdma-selftest.exe         # SDMA self-test (currently fails — ring not init)
output\psp-fw-load.exe           # PSP firmware load (currently fails — wrong offsets)
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
│   ├── amdbc250_dream_kmd.c        # DriverEntry, IOCTL dispatch
│   ├── amdbc250_dream_hw_init.c    # GPU init, ring buffers, PSP
│   ├── amdbc250_dream_power.c      # Power/thermal management
│   ├── amdbc250_dream_vm.c         # GPUVM, GART, page tables
│   ├── amdbc250_psp.c              # PSP proxy driver interface
│   └── firmware_data.h             # Embedded PSP firmware
├── src/umd/                        # User-Mode Driver
│   └── amdbc250_umd_v46.c          # D3D9 DDI (45+ functions)
├── inc/                            # Shared headers
│   ├── amdbc250_dream_hw.h         # Hardware register definitions
│   └── amdbc250_ioctl.h            # IOCTL codes + structures
├── test-tools/                     # Diagnostic tools
│   ├── bar5-smn-test.c             # SMU mailbox via SMN
│   ├── test-gpu-ioctls.c           # 15 IOCTL tests
│   ├── sdma-selftest.c             # SDMA self-test
│   ├── psp-fw-load.c               # PSP firmware loading
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
| [docs/BC250-LINUX-IP-MAP.md](docs/BC250-LINUX-IP-MAP.md) | Linux-verified IP base addresses |
| [docs/REGISTER-MAP-BC250.md](docs/REGISTER-MAP-BC250.md) | Complete BC-250 register map |
| [docs/RING-INIT-STATUS.md](docs/RING-INIT-STATUS.md) | Ring init blockers and KIQ path |
| [docs/PSP-PROXY-BYPASS.md](docs/PSP-PROXY-BYPASS.md) | PSP proxy architecture |
| [docs/GCVM-ANALYSIS.md](docs/GCVM-ANALYSIS.md) | GCVM page table investigation |
| [third-party/EFI_Boot/](third-party/EFI_Boot/) | EFI Shell WGP unlock scripts (pre-boot) |
| [third-party/linuxinfo/](third-party/linuxinfo/) | Linux firmware blobs and dmesg logs |

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
