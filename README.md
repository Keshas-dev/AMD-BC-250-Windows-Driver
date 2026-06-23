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
  - `GCVM_PT_SETUP` IOCTL: allocates 3-level (depth=2) page table
  - Fills PDEs/PTEs for KIQ ring identity mapping
  - Triggers TLB invalidation via 0x6C0C/0x6C10 protocol
  - Returns 0xCAFEBABE on success
- ✅ **PSP KIQ_BASE programmed** via GPU proxy on Windows 11 26100
- ✅ Pre-build validation script (`prebuild-check.ps1`): IOCTL uniqueness, memory leak patterns, BSOD patterns

### Current Blocker — CP Ring Processing
GPU CP still not reading from KIQ ring (RPTR=0) after PT_SETUP:
1. **CP may need queue activation** — HQD_ACTIVE write (0xDAC0) may be NBIO-blocked
2. **ME_CNTL halt bits** — CP firmware might still be halted
3. **Known crash**: rapid KIQ submits after PT_SETUP causes PSP driver 0x50 (page fault)

### Critical Discoveries

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
- System aperture enabled via write: possible? Read-only bits block it

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
output\gcvm-pt-test.exe       # GCVM page table setup + KIQ test (0xCAFEBABE on success)
output\gpu-kiq-test.exe       # PM4 ring execution test
output\safe-test.exe          # Safe read-only register test
output\iommu-gcvm-check.exe   # IOMMU + GCVM register scan
output\kiq-diag.exe           # Full KIQ diagnostic
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
