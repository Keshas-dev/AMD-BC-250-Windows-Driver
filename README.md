# AMD BC-250 Dream Drivers v4.3

Windows GPU driver for AMD BC-250 (Cyan Skillfish / RDNA2 / GFX1013).

## Hardware

| Spec | Value |
|------|-------|
| Architecture | RDNA 2 (GFX1013) |
| Compute Units | 24 (1536 shaders) |
| Memory | 16GB GDDR6 |
| Ray Tracing | Early gen RT cores |
| Display Engine | DCN 2.1 |
| PCI ID | `VEN_1002&DEV_13FE` |

## Project Structure

```
AMD-BC-250-Windows-Driver-main/
├── src/                        Source code
│   ├── kmd/                    Kernel-Mode Driver
│   │   ├── amdbc250_dream_v3_kmd.c       Main DDI callbacks
│   │   ├── amdbc250_dream_v3_hw_init.c   Hardware init (GFX10, DCN 2.1)
│   │   ├── amdbc250_dream_v3_power.c     Power/thermal management
│   │   ├── amdbc250_dream_v3_vm.c        GPU virtual memory
│   │   ├── makefile
│   │   └── SOURCES
│   └── umd/                    User-Mode Driver (D3D12 stub)
│       └── amdbc250_umd_v46.c
├── inc/                        Header files
│   ├── amdbc250_dream_v3_kmd.h    KMD structures & prototypes
│   ├── amdbc250_dream_v3_hw.h     Hardware register definitions
│   └── amdbc250_d3d12.h           D3D12 DDI interfaces
├── inf/                        Driver installation
│   └── amdbc250_dream_v3.inf
├── output/                     Build output
│   ├── atikmdag.sys            Kernel driver (signed)
│   ├── amdbc250umd64.dll       User driver (signed)
│   └── amdbc250_dream_v3.inf
├── tools/                      Utility scripts
│   ├── uninstall-bc250.ps1
│   └── Uninstall-Old-Drivers.bat
├── test-tools/                 GPU testing
│   ├── test-gpu-simple.c
│   ├── test-gpu-simple.exe
│   └── run-gpu-test.bat
├── docs/                       Documentation
│   ├── README.md               Overview (EN)
│   ├── PILNAS-APRASAS.md       Technical spec (LT)
│   ├── BUILD-STATUS.md         Build environment notes
│   └── D3D12-UMD-RESEARCH.md   D3D12 UMD research
├── build.bat                   Build script (VS 2022 + WDK)
├── .gitignore
└── README.md
```

## Quick Start

### Requirements
- Windows 10/11 64-bit
- Visual Studio 2022 (Community or higher)
- Windows Driver Kit (WDK) 10.0.26100.0
- Test signing enabled

### Build
```cmd
build.bat
```
Or open **Developer Command Prompt for VS 2022** and run manually.

### Install
```powershell
bcdedit /set testsigning on
```
Reboot, then: **Device Manager > Update Driver > Browse** to `output/`

## Architecture

Based on Linux `amdgpu` driver:
- `gfx_v10_0.c` - GFX10 command processor
- `nv.c` - Navi family init
- `dcn20/` - DCN 2.1 display engine
- `amdgpu_vm.c` - GPU virtual memory

### KMD Modules

| File | Purpose |
|------|---------|
| `amdbc250_dream_v3_kmd.c` | WDDM DDI callbacks, ring buffers, interrupt handling |
| `amdbc250_dream_v3_hw_init.c` | GPU init, golden registers, HDP flush, display |
| `amdbc250_dream_v3_power.c` | SMU stubs, thermal monitoring, fan control |
| `amdbc250_dream_v3_vm.c` | GART, GPU page tables (4-level), TLB invalidation |

### Key Features
- 64-bit fences (GFX10 requirement)
- HDP coherency flush before ring reads
- Thermal throttle with hysteresis (85C on, 80C off)
- Dynamic clock scaling
- GPU virtual memory with 4-level page tables

## Known Limitations

- **Compute queue**: Disabled (hardware quirk)
- **VCN firmware**: Blocked by Sony
- **VRAM**: ~10GB visible limit (hardware quirk)
- **UMD**: D3D12 stub only, no full rendering pipeline
- **No OpenGL/Vulkan**: Needs Mesa RADV or AMDVLK

## Linux Comparison

| Feature | Linux (amdgpu) | Windows (Dream) |
|---------|---------------|-----------------|
| Vulkan | RADV | AMDVLK required |
| OpenGL | radeonsi | Not implemented |
| D3D12 | DXVK | Basic stub |
| Compute | Broken HW | Disabled (quirk) |
| Video | Blocked | Blocked |

## License

Source code for educational purposes. Use at your own risk.
