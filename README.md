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
- ✅ Build + sign pipeline (`build.bat`)
- ✅ Vulkan ICD, D3D9 UMD stubs

### Current Blocker — PM4 Execution
GPU CP cannot process PM4 commands from ring buffer because:
1. **GCVM page table translation fails** — GPU can't access system RAM via GCVM
2. **BIOS configures GCVM** — 12 compute queues active at boot, ring at 0x7E522000
3. **PT_BASE is hardware-locked** at old offset 0x0B608; writable at Linux offset 0x6C8C
4. **Direct BAR5 MMIO writes silently dropped on Win11 26100** — must use `DreamV3WriteRegister`

### Critical Discoveries

**Register Offset Map (BC-250 ≠ Navi10):**
| Register | OLD Offset | Linux Offset | Writable At |
|----------|-----------|-------------|-------------|
| GCVM_CONTEXT0_CNTL | 0x0B460 | 0x6AE0 | **OLD 0x0B460** |
| GCVM_CONTEXT0_PT_BASE | 0x0B608 (locked) | 0x6C8C | **Linux 0x6C8C** |
| GCVM_L2_CNTL | 0x0B360 | 0x69E0 | **OLD 0x0B360** |

**Win11 26100 MMIO Issue:**
- `*(volatile PULONG)(mmio + off) = val` → silently dropped
- `DreamV3WriteRegister` (= `WRITE_REGISTER_ULONG`) → works
- `DeviceIoControl(WRITE_REG)` → works

**BIOS GCVM Configuration (verified):**
- KIQ ring PA = 0x7E522000, WPTR=8 already kicked
- PML4 = 0x7E511000
- HQD_ACTIVE = 0x0000FFF0 (12 compute queues active)
- ME_CNTL = 0xFFFBD9FB (halted)

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
- Test signing: `bcdedit /set testsigning on` (Admin)
- **IOMMU must be DISABLED in BIOS** (zeros all GCVM registers)

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
