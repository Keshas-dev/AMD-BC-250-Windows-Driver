> **Due to lack of free time, this project is postponed indefinitely.**

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

---

## Achievements (What We Accomplished)

### Working Infrastructure
- ✅ **GPU driver loads**, all 30+ IOCTL handlers operational
- ✅ **BAR5 MMIO mapping** — `DreamV3WriteRegister`/`ReadRegister` via `WRITE_REGISTER_ULONG`
- ✅ **Build + sign pipeline** — `build.bat` with prebuild validation (IOCTL uniqueness, leak patterns)
- ✅ **PSP proxy driver bridge** — GPU driver supplies BAR5 access to PSP driver on Win11 26100
- ✅ **NBIO_MAP init** — safe init path (flag=1), avoids GPU alive test hang

### Firmware & Engine Discovery
- ✅ **CP firmware loads successfully** via MMIO IC_BASE DMA
- ✅ **MEC firmware execution verified** — first bytes ucode corrupt → SCRATCH changes (engine really runs)
- ✅ **MEC2 firmware** has real ARM64 code (MEC1 is NOP only) — both behave identically
- ✅ **IC_BASE write order** — LO→HI→CNTL=0 (not 0x100), 500ms UCODE_ADDR polling

### Hardware Register Map (BC-250 specific)
- ✅ **GC_BASE=0x1260** — all GC registers offset shifted
- ✅ **GRBM_STATUS** (0x3260), GRBM_GFX_INDEX (0x34D0) — working
- ✅ **SCRATCH** (0x32D4) — writable, high nibble [31:28] HW-masked
- ✅ **KIQ_BASE_LO** (0xE060) writable; **KIQ_BASE_HI** read-only; **KIQ_SIZE** (0xE068) read-only=0
- ✅ **KIQ_WPTR** — only 9 bits (mask 0x1FF)
- ✅ **CP_HQD_*** (0xDAC0-0xDBFF) — all NBIO blocked (writes silently dropped)
- ✅ **GCVM** — PT_BASE0 (0x0B408) writable; PT_BASE (0x0B608) HW-locked
- ✅ **TLB invalidation** works (0x6C0C/0x6C10 protocol)
- ✅ **GPU_ID** = 0x9FFF9700, **GRBM_STATUS** = 0x00000000 (idle)

### Software PM4 Executor (Confirmed Working)
- ✅ **DreamV3SwPm4Process** — CPU translates PM4 packets to direct MMIO writes
- ✅ IT_WRITE_DATA (SCRATCH changes), IT_NOP, IT_EVENT_WRITE_EOP, PM4_TYPE_0 — **HW-confirmed**
- ✅ IT_SET_CONFIG_REG, IT_SET_CONTEXT_REG, IT_SET_SH_REG, IT_INDIRECT_BUFFER (with depth guard)
- ✅ **PATH 3 fallback** in SEND_PM4 — when PSP KIQ and GfxRing are both unavailable
- ✅ Recursion depth guard (max 32) — prevents stack overflow

## Latest Bug Fixes & Findings (2026-07-08)

### ✓ PSP Driver Signing Fixed!
- **Root cause**: `build.bat` searched only `x64\` for Inf2Cat, but WDK 10.0.26100.0 has it in `x86\` directory
- **Fallback**: Without Inf2Cat, build fell back to `makecat.exe` which generates an incomplete catalog (1565 bytes vs Inf2Cat's 4439 bytes)
- **Result**: Windows rejected the PSP driver with "not digitally signed" for System class drivers
- **Fix**: Added `x86\` path search for Inf2Cat, fixed build order (sign .sys → Inf2Cat → sign .cat), fixed OS param (`11_X64` → `10_X64`)

### ✓ SMU v88.6.0 Fully Functional via SMN (NBIO 0x38/0x3C)
- **SMN access via NBIO** BAR5+0x38/0x3C works — Linux `WREG32_PCIE`/`RREG32_PCIE` path
- **SMU firmware running**: FW_FLAGS=0x00000001, PUB_CTRL=0, version 88.6.0, driver_if=8
- **Enabled features**: 0xDD602C7D (GFXCLK DPM, GFXOFF, CG, PG — all standard)
- **C2PMSG mailbox** via SMN (0x03B10A08/0x03B10A48/0x03B10A68), NOT BAR5 direct (reads 0)

### ✓ Governor Sequence Works (Frequency Control)
- Full SafePoint sequence proven safe: Q3 test → max temp → unforce → perf profile → force VID → force freq
- **1500→1166 MHz frequency change** confirmed working without DPM tables
- GPU never crashes when following the exact governor sequence

### ✓ All 6 Test Executables Pass
| Test | Result | Purpose |
|------|--------|---------|
| `psp-status-test` | ✅ | PSP driver, BAR5 mapping, SOS status |
| `bar5-smn-test` | ✅ | SMU mailbox via SMN, freq, VID, features |
| `smu-monitor` | ✅ | Live telemetry: freq, temp, fans, power |
| `governor-sequence` | ✅ | SMU freq/voltage control via governor |
| `gfxoff-kill-v2` | ✅ | GFXOFF+CG+PG disable, compute trigger |
| `dcn-init-test` | ✅ | DCN display engine probe, OTG counter |

### ✓ Compute Confirmed Fused Off
- DISPATCH_INITIATOR(0x80E0) accepts VALID command (consumed=YES)
- GRBM_STATUS=0, Scratch unchanged, QueryActiveWgp=0 — no shader execution
- SPI_PG_ENABLE_STATIC_WGP_MASK(0x5C3C) = READ-ONLY 0 — WGPs hardware-fused
- Consistent with Mesa MR 33116, ROCm issue #6313, RADV `RADV_DEBUG=nocompute`

### Previous Bug Fixes (2026-07-03)
- ✅ PSP GPU PM4 Submit IOCTL: METHOD_BUFFERED buffer sharing bug fixed
- ✅ 6 critical/high PSP driver bugs fixed (spinlock in ZwCreateFile, OOB size check, etc.)
- ✅ GFX Ring investigation finalized: MEC/CP engines do not process

---

## Mistakes & Lessons Learned

### Critical Mistakes
1. **IC_BASE_CNTL=0x100 before setting base address** — if bit 8 auto-started DMA from address 0, firmware never loads. Should be: LO → HI → CNTL=0
2. **PM4 TYPE3 header encoding** — `(3<<30) | ((count-1)<<16) | (op<<8)` but all tests used `0xC0370003` which is IT_NOP count=56, not IT_WRITE_DATA. Bug never manifested because KIQ never processed the ring.
3. **CP_HQD writability claim** — previously claimed 0xDAC0+ are writable; actually NBIO-blocked. Misled by aliased/stale readback values.
4. **RLC firmware loading** — attempted to load RLC firmware via 0x3A00 registers which are in FREEZE ZONE (0x3400-0x8100) — BC-250 BIOS/SMU loads RLC.
5. **KIQ_SIZE patch expectation** — believed MEC firmware could be patched to fix KIQ_SIZE=0; but 0xE068 address not found in firmware binary — check is hardware-level.
6. **PSP driver signing — Inf2Cat in x86\ not x64\** — build.bat only searched x64\ for Inf2Cat, but WDK 10.0.26100.0 has it in x86\. Fallback to makecat generated incomplete catalog — PSP driver rejected as "not digitally signed".

### Wasted Effort
1. **GRBM_GFX_CNTL (0x2022)** — attempted to unlock HQD access; doesn't work on BC-250.
2. **comprehensive-pm4-test** — leaves KIQ active + ME_CNTL unhalted → system hang
3. **PSP driver KIQ path** — PSP KIQ ring has the same problem (WPTR goes through, RPTR=0)
4. **RLC init** — DreamV3InitRlc is no-op; registers in freeze zone

### Technical Lessons
- **NBIO blocking** — 0xC000+ range is NBIO-protected; GC_BASE-shifted aliases can bypass but not for all registers
- **WRITE_REGISTER_ULONG vs volatile*** — on Win11 26100 volatile pointer writes can be silently dropped; WRITE_REGISTER_ULONG always works
- **Firmware execution** proven by corrupting first 8 bytes of ucode and observing SCRATCH changes
- **KIQ_WPTR** only 9 bits (not 32) — HW limitation
- **SDMA init** can cause BSOD if registers are wrong; needs sanity check
- **Code review before build** — saves hours of debugging (17 bugs found, 10 fixed)

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

**Conclusion:** This BC-250 variant is factory-locked for GPU command execution. All hardware ring paths are locked. **3D graphics with this specific hardware is not achievable.**

---

## What We Built Anyway (Software Workaround)

Since HW rings are inaccessible, we built a **Software PM4 executor** that:
- Accepts standard PM4 packets (IT_WRITE_DATA, IT_SET_CONFIG_REG, IT_INDIRECT_BUFFER, etc.)
- Translates them to direct MMIO writes (CPU work, not GPU)
- Allows testing any PM4 packet without hardware ring risk

This allows **GPU register control** and hardware understanding, but **does not replace GPU shader execution**.

---

## Code Review: 17 Bugs Found — 10 Fixed, 7 Remaining

### Fixed Bugs (10)
| # | File | Bug | Fix Date |
|---|------|-----|----------|
| 1 | kmd.c | Integer overflow in READ/WRITE_REG bounds check | 2026-06-26 |
| 2 | fw_load.c | BAR5_U32 volatile ptr silently dropped on Win11 26100 | 2026-06-26 |
| 3 | kmd.c | LOAD_CP_FW halts ALL engines when only MEC needs loading | 2026-06-26 |
| 4 | fw_load.c | Integer overflow in firmware header validation | 2026-06-26 |
| 5 | kmd.c | Integer overflow in JT bounds check | 2026-06-26 |
| 6 | kmd.c | SEND_PM4 ring wrap 32-bit overflow | 2026-06-26 |
| 7 | PspKiq.c | body_size=1 → 4 (mismatch with PspCore.c) | 2026-06-26 |
| 8 | PspKiq.c | Default ring size 0x2000 exceeds 9-bit WPTR max (512 DWORDS) | 2026-06-26 |
| 9 | PspKiq.c, PspCore.c | PspGpuProxyWriteRegister return values ignored | 2026-06-26 |
| 10 | PspCore.c | Race condition on g_GpuDriverHandle init | 2026-06-26 |

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

## Next Steps / Future Directions

1. **Complete BC-250 register map for other researchers** — all offsets, writability, blockers
2. **Fix remaining 7 bugs** from code review — alignment, naming, stale definitions
3. **Extend SW PM4 executor** — batch WRITE_DATA, VRAM BAR2 ring support, more opcodes
4. **Investigate alternative 40 CU unlock** via CC_GC_SHADER_ARRAY_CONFIG (0x3264) + SPI_PG_MASK (0x34FC)
5. **SDMA engine** — if 0xE000 registers are alive, DMA is possible without CP/KIQ
6. **Open-source documentation** — share all discoveries with community
7. **Port to newer AMD GPU** — where hardware is not locked (RDNA3+)

---

## Architecture

This is a **WDM control/IOCTL driver**, not a real WDDM miniport. `DxgkInitialize` is not exported on Windows 11 26100.

```
┌─────────────────────────────────────────────────┐
│              User Applications                    │
├─────────────────────────────────────────────────┤
│    gpu-kiq-test.exe / safe-test.exe / etc.       │
│    → DeviceIoControl → \\.\AMDBC250DreamV43      │
├─────────────────────────────────────────────────┤
│         atikmdag.sys (KMD — WDM)                  │
│         ├── DriverEntry                           │
│         ├── IRP_MJ_DEVICE_CONTROL (30+ handlers) │
│         ├── INIT_HARDWARE (MMIO map, Flags=1)    │
│         ├── READ_REG / WRITE_REG (BAR5 MMIO)    │
│         │   └── DreamV3WriteRegister/ReadRegister │
│         ├── GPU_KIQ_TEST — PM4 ring test          │
│         ├── PSP proxy (amdbc250_psp.c)            │
│         └── SMU v11.8 mailbox                    │
├─────────────────────────────────────────────────┤
│              AMD BC-250 GPU                        │
│              24 CU RDNA2, 16GB GDDR6              │
│              GC_BASE=0x1260                       │
└─────────────────────────────────────────────────┘
```

---

## How to Build

### Prerequisites
- Visual Studio 2022 (Professional) — auto-detected on D: or E: drive
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
output\smu-monitor.exe           # Live SMU telemetry (CSV logging)
output\smu-dcn-monitor.exe       # Combined SMU + DCN status (freq, voltage, temp, pipe)
output\governor-sequence.exe     # SMU frequency control test
output\gfxoff-kill-v2.exe        # GFXOFF+CG+PG disable + compute trigger
output\dcn-init-test.exe         # DCN display engine probe
output\psp-status-test.exe       # PSP driver status + BAR5 mapping
output\gcvm-pt-test.exe          # GCVM page table setup + KIQ test
output\smu-scan-all.exe          # SMU register space scanner
test-tools\sw-pm4-test.exe       # Software PM4 executor test (confirmed working)
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
│   ├── gpu-kiq-test.c              # PM4 ring execution test
│   ├── iommu-gcvm-check.c          # IOMMU/GCVM register scan
│   ├── kiq-diag.c                  # KIQ register diagnostic
│   └── kiq-unhalt.c                # ME_CNTL unhalt test
├── output/                         # Build output (signed drivers)
├── docs/                           # Technical documentation
├── build.bat                       # Build + sign driver
├── prebuild-check.ps1               # Pre-build validation
├── reinstall-both-drivers.bat       # Reinstall GPU + PSP drivers
└── .gitignore
```

---

## Key Documentation

| File | Description |
|------|-------------|
| [AGENTS.md](AGENTS.md) | Agent memory — hardware facts, current blockers |
| [docs/BC250-LINUX-IP-MAP.md](docs/BC250-LINUX-IP-MAP.md) | Linux-verified IP base addresses |
| [docs/REGISTER-MAP-BC250.md](docs/REGISTER-MAP-BC250.md) | Complete BC-250 register map |
| [docs/RING-INIT-STATUS.md](docs/RING-INIT-STATUS.md) | Ring init blockers and KIQ path |
| [docs/PSP-PROXY-BYPASS.md](docs/PSP-PROXY-BYPASS.md) | PSP proxy architecture |
| [docs/GCVM-ANALYSIS.md](docs/GCVM-ANALYSIS.md) | GCVM page table investigation |

---

## Related Projects

- **GPU Driver**: https://github.com/Keshas-dev/AMD-BC-250-Windows-Driver
- **PSP Driver**: https://github.com/Keshas-dev/AMD-BC-250-PSP-Windows-Driver

## License

Source code for educational purposes. Use at your own risk.
ACO compiler: MIT license (Mesa project).
