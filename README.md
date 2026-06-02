# AMD BC-250 Windows Driver Project

## Who We Are

AMD BC-250 Windows 11 driver project by Keshas & Kumpis (AI). Goal: fully working GPU driver for AMD BC-250 (PS5 motherboard "Ariel Root Complex") on Windows 11.

**Everyone welcome!** GPU drivers, WDDM, Vulkan experience — or just want to help.

---

## Hardware

- **SoC:** AMD BC-250 (PS5 Oberon variant) — 24 CU RDNA2, 16GB shared GDDR6
- **GPU ID:** 0x9FFF9700 (PS5 custom)
- **Memory:** CPU and GPU share GDDR6 (UMA) — VRAM at 0xC0000000
- **GPU BAR5:** 0xFE800000 (read-only registers — writes cause hard freeze)
- **NBIO:** IO port config writes blocked by PS5 NBIO

### What We Discovered
- GPU register writes via BAR5 cause **machine check freeze** — NBIO hardware firewall
- `DxgkInitialize()` not exported by dxgkrnl.sys on Windows 11 26100 — WDDM miniport impossible
- IO port PCI config writes blocked — Host Bridge Memory Space cannot be enabled
- SMU/SMN access blocked from Windows
- **IOCTL channel works** — our custom KMD device `\\.\AMDBC250DreamV43` communicates with GPU

---

## Current Status (v4.3)

### Working
- ✅ **IOCTL channel** — all 3 core tests PASS:
  - `GET_CAPS` → Version, CUs, GPU/Mem clocks
  - `GET_VRAM_INFO` → Total/Visible/Used VRAM
  - `GET_RESOURCE_BARS` → BAR addresses, MMIO state
- ✅ **WDDM coexistence** — BasicDisplay + our KMD on same DriverObject
- ✅ **g_PciDevExt** allocated in DriverEntry, accessible from all IOCTL handlers
- ✅ Build + sign pipeline — `build.bat` produces signed `atikmdag.sys`
- ✅ No dxgkrnl.sys import — clean WDM driver, no Code 39
- ✅ PSP v11 firmware loading (C2PMSG mailbox, bootloader handshake)
- ✅ Vulkan ICD — 13/13 tests pass with official Vulkan loader
- ✅ D3D9 UMD — 45+ DDI functions, 5/5 adapter tests pass
- ✅ IB packet + EOP fence, GFX10 ring buffer, HDP Flush
- ✅ SDMA copy/fill engine, TDR reset, 40 CU unlock

### Known Limitations
- GPU MMIO not mapped (HardwareInitialized=0) — needs FORCE_ENABLE_MMIO
- DxgkDdiEscape unreachable via D3DKMTEscape — BasicDisplay handles it
- SMU/SMN blocked from Windows
- BAR5 writes cause hard freeze
- VCN locked by Sony firmware

---

## How to Build

### Prerequisites
- Visual Studio 2022 (Community/Professional) — auto-detected
- Windows WDK 10.0.26100.0
- Test signing: `bcdedit /set testsigning on` (Admin)

### Build
```cmd
build.bat
```

### Install
1. `build.bat` → `output\atikmdag.sys` + `output\amdbc250umd64.dll`
2. Device Manager → AMD Radeon BC-250 → Update Driver → Browse → `output\`
3. Reboot

### Test
```cmd
output\test-wddm.exe              # WDDM + IOCTL tests (S1-S20)
output\test-gpu-ioctls.exe        # Direct IOCTL tests
output\test-bar5-readonly.exe     # GPU BAR5 register reads
output\test-vulkan-icd.exe        # Vulkan ICD tests
```

---

## Architecture

```
┌─────────────────────────────────────────────────┐
│              User Applications                    │
├─────────────────────────────────────────────────┤
│         test-wddm.exe / test-gpu-ioctls.exe      │
│         DeviceIoControl → \\.\AMDBC250DreamV43   │
├─────────────────────────────────────────────────┤
│         atikmdag.sys (KMD — WDM)                  │
│         ├── IRP_MJ_DEVICE_CONTROL handler         │
│         ├── GET_CAPS (0x80000800)                 │
│         ├── GET_VRAM_INFO (0x80000804)            │
│         ├── GET_RESOURCE_BARS (0x80000BB8)        │
│         ├── ALLOC_VIDMEM (0x80000840) — MDL      │
│         ├── SUBMIT_COMMANDS (0x80000880)          │
│         ├── READ_MMIO / WRITE_MMIO               │
│         ├── FORCE_ENABLE_MMIO (0x80000BBC)       │
│         ├── PSP v11 firmware loading              │
│         └── PM4 ring buffer → GPU                 │
├─────────────────────────────────────────────────┤
│              AMD BC-250 GPU (RDNA2)               │
│              24 CU, 16GB GDDR6, PSP v11          │
└─────────────────────────────────────────────────┘
```

---

## File Structure

```
├── src/kmd/                        # Kernel-Mode Driver
│   ├── amdbc250_dream_kmd.c        # DriverEntry, IOCTL dispatch, InitData
│   ├── amdbc250_dream_hw_init.c    # GPU init, ring buffers, display
│   ├── amdbc250_dream_power.c      # Power/thermal management
│   ├── amdbc250_dream_vm.c         # GPUVM, GART, page tables
│   └── amdbc250_psp_v11.c          # PSP firmware loading
├── src/umd/                        # User-Mode Driver
│   └── amdbc250_umd_v46.c          # D3D9 DDI (45+ functions)
├── src/vulkan/                     # Vulkan ICD
│   ├── bc250_vulkan_icd.c          # 80+ Vulkan functions
│   ├── bc250_aco_wrapper.c         # ACO shader compiler stub
│   └── bc250_shader.c              # SPIR-V → GFX10 ISA
├── inc/                            # Shared headers
│   ├── amdbc250_dream_kmd.h        # KMD structures, register offsets
│   ├── amdbc250_dream_hw.h         # Hardware register definitions
│   ├── amdbc250_psp_v11.h          # PSP context and API
│   └── amdbc250_ioctl.h            # IOCTL codes + structures
├── test-tools/                     # Test source + compile scripts
│   ├── test-wddm.c                 # Main WDDM+IOCTL test
│   ├── test-bar5-readonly.c        # GPU register read test
│   ├── compile-wddm.bat            # Compile test-wddm
│   └── compile-*.bat               # Other test compile scripts
├── output/                         # Build output (signed drivers)
│   ├── atikmdag.sys                # KMD
│   ├── amdbc250umd64.dll           # UMD
│   ├── amdbc250vulkan.dll          # Vulkan ICD
│   └── amdbc250_dream.inf          # Driver INF
├── tools/                          # Install/uninstall scripts
├── build.bat                       # Build + sign
└── .gitignore                      # Excludes build artifacts
```

---

## Roadmap

### Next — GPU Init via IOCTL
1. **FORCE_ENABLE_MMIO** — enable PCI Memory Space + Bus Master via IOCTL
2. **Read GPU registers** — BAR5 read-only scan via IOCTL
3. **Map MMIO** — if BAR2 becomes accessible after FORCE_ENABLE_MMIO
4. **Alloc VRAM** — MDL-based allocation for GPU buffers

### Short Term
5. Real triangle rendering — vertex buffer + PM4 draw
6. ACO shader compilation — DXBC/SPIR-V → GFX10 ISA
7. D3D9 via IOCTL path

### Long Term
8. OpenGL ICD — Mesa radeonsi port
9. GPU compute — SDMA compute queue
10. Ray tracing — RT core support

---

## License

Source code for educational purposes. Use at your own risk.
ACO compiler: MIT license (Mesa project).
