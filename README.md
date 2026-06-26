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

## Current Status

### What Works
- ✅ GPU driver loads, IOCTLs work (30+ handlers)
- ✅ BAR5 MMIO via `DreamV3WriteRegister` (`WRITE_REGISTER_ULONG`)
- ✅ PSP proxy driver — firmware loading, NBIO unlock, KIQ submit
- ✅ CP firmware loads via MMIO IC_BASE DMA (bypasses GCVM)
- ✅ Build + sign pipeline (`build.bat`) with prebuild validation
- ✅ Vulkan ICD, D3D9 UMD stubs
- ✅ **GCVM page table setup** — first successful PT_BASE0 (0x0B408) write on BC-250!
- ✅ **MEC firmware execution verified** — corrupting ucode changes SCRATCH register
- ✅ **Software PM4 executor** (`DreamV3SwPm4Process`) — translates PM4 IT_WRITE_DATA, IT_NOP, IT_EVENT_WRITE_EOP, PM4_TYPE_0 to direct MMIO writes when hardware KIQ is unavailable
- ✅ **PATH 3 fallback** in SEND_PM4 IOCTL — when PSP KIQ and GfxRing are unavailable, processes PM4 in software

### Current Blocker — KIQ_SIZE=0 Hardware Block
GPU CP cannot process KIQ ring because **KIQ_SIZE (0xE068) is factory read-only = 0**:
1. **KIQ_SIZE=0 read-only** — hardware thinks ring has 0 bytes, CP refuses to process
2. **ALL CP_HQD_* registers (0xDAC0-0xDBFF) NBIO-blocked** — ACTIVE, BASE, CONTROL, VMID all read-only
3. **KIQ_WPTR only 9 bits** (mask 0x1FF) — max ring 512 dwords (2048 bytes)
4. **KIQ_SIZE not patchable** — address 0xE068 not found in MEC firmware binary; check is hardware-level
5. **Workaround**: Software PM4 executor (`DreamV3SwPm4Process`) bypasses hardware ring entirely

### Critical Discoveries

**Software PM4 Executor (2026-06-26):**
- **DreamV3SwPm4Process** translates PM4 packets to direct register writes — NO hardware ring processing required
- Verified working: IT_WRITE_DATA (SCRATCH), IT_NOP, PM4_TYPE_0, fence writes via SEND_PM4 IOCTL
- Registered as PATH 3 fallback in SEND_PM4 handler (when PSP KIQ and GfxRing are unavailable)
- Test tool: `test-tools/sw-pm4-test.c`

**PM4 Header Format Correction:**
- ALL existing test tools used **wrong PM4 TYPE3 header** (`0xC0370003` instead of correct `0xC0023700`)
- Correct format: `PM4_TYPE3_HDR(op, count) = (3<<30) | ((count-1)<<16) | (op<<8)`
- Header `0xC0370003` encodes opcode=0, count=56 (wrong)
- Header `0xC0023700` encodes opcode=0x37=IT_WRITE_DATA, count=4 (correct)
- Bug never surfaced because KIQ never processed packets anyway

**MEC Firmware Execution Verified:**
- Corrupting first 8 bytes of MEC ucode at offset 0x41830 → SCRATCH changed from written value to 0x00000000, proving MEC engine executed and modified memory
- MEC2 firmware (cyan_skillfish2_mec2.bin) has real ARM64 ucode but KIQ behavior identical to MEC1

**Corrected HQD Register Offsets:**
- HQD_ACTIVE=0xDAC0, VMID=0xDAC4, PQ_BASE_LO=0xDAD8, PQ_BASE_HI=0xDADC
- PQ_RPTR=0xDAE0, PQ_CONTROL=0xDAFC, PQ_WPTR_LO=0xDB90
- ALL confirmed NBIO-blocked (writes silently dropped)

**IC_BASE Register Fixes:**
- Fixed offsets: MEC IC at 0x17390-0x17398 (was 0x7C10-0x7C18)
- Fixed IC_BASE_CNTL value: write 0 (not 0x100), write LO/HI before CNTL
- Added UCODE_ADDR polling (500ms timeout) before unhalt to confirm DMA completion

**Correct GCVM Page Table Register:**
- **PT_BASE0 (0x0B408)** = the real page table base register — writable, used by GPU MMU
- **PT_BASE (0x0B608/0x6C8C)** = HW-locked to 0 — NOT used for translation

**TLB Invalidation Protocol (verified working):**
1. Write 1 to 0x6C10 (clear ACK)
2. Write 1 to 0x6C0C (request invalidation)
3. Poll 0x6C10 bit 0 until 1 (ACK received)

**Page Table Format (RDNA2/GFX10):**
- PDE: VALID|SYSTEM = `0x03`
- PTE: VALID|SYSTEM|READABLE|WRITABLE = `0x63`
- CONTEXT0_CNTL (0x0B460): bit 0=ENABLE, bits 2:1=depth (10b=3-level)

**Win11 26100 MMIO Issue:**
- `*(volatile PULONG)(mmio + off) = val` → silently dropped
- `DreamV3WriteRegister` (= `WRITE_REGISTER_ULONG`) → works
- `DeviceIoControl(WRITE_REG)` → works
- PSP driver cannot map GPU BAR5 directly (different PCI device) → falls back to GPU proxy IOCTLs

**PSP KIQ Proxy Path Fix:**
On Windows 11 26100, PSP's `MmMapIoSpace` for GPU BAR5 fails. The PSP falls back to `PspGpuProxyInit` which opens `g_GpuDriverHandle` and routes all BAR5 access via IOCTLs to the GPU driver. Two guard checks in PspKiq.c were fixed to allow this proxy path:
- `PspKiqInit` line 163: `!devExt->MmioBase` → added `g_GpuDriverHandle == NULL`
- `PspKiqProgramHwRegisters` line 64: `!g_Bar5Mapping && !devExt->GpuMmioBase` → added `g_GpuDriverHandle == NULL`

**BIOS GCVM Configuration (varies per boot):**
- PT_BASE0 (0x0B408) = 0x017CCCC4_7D9AB14E (garbage/uninitialized, not valid page table)
- CONTEXT0_CNTL (0x0B460) = 0x0104A88D (ENABLE=1, depth=2=3-level)
- L2_CNTL (0x0B360) = 0x013C7798 (system aperture DISABLED, read-only)

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
output\gcvm-pt-test.exe         # GCVM page table setup + KIQ test
output\gpu-kiq-test.exe         # PM4 ring execution test (uses wrong header)
output\safe-test.exe            # Safe read-only register test
output\iommu-gcvm-check.exe     # IOMMU + GCVM register scan
output\kiq-diag.exe             # Full KIQ diagnostic
test-tools\sw-pm4-test.exe      # Software PM4 executor test (confirmed working)
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
