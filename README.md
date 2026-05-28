# AMD BC-250 Dream Drivers v4.3

Windows GPU driver for AMD BC-250 (Cyan Skillfish / RDNA2 / GFX1013).

## Hardware

| Spec | Value |
|------|-------|
| Architecture | RDNA 2 (GFX1013) |
| Compute Units | 24 (1536 shaders) |
| Memory | 16GB GDDR6 (shared UMA, BIOS configurable) |
| Ray Tracing | Early gen RT cores |
| Display Engine | DCN 2.1 (4 pipes) |
| TDP | 220W |
| PCI ID | `VEN_1002&DEV_13FE` |

## Features

### Kernel-Mode Driver (KMD)
- WDDM 2.x/3.x DDI callbacks
- GFX10 command processor initialization
- 64-bit fences for GPU synchronization
- HDP coherency flush (Linux quirk)
- DCN 2.1 display engine (VGA fallback + 1080p)
- SDMA copy engine (GPU buffer copy/fill)
- 4-level GPU page tables (GFX10 VM)
- Thermal monitoring with hysteresis (85°C on, 80°C off)
- Dynamic clock scaling
- TDR (Timeout Detection and Recovery) reset
- EDID parsing for monitor detection
- Display hotplug detection
- **21 IOCTL handlers** for UMD communication

### User-Mode Driver (UMD)
- D3D12 DDI adapter/device lifecycle
- Heap/Resource allocation via KMD IOCTL
- GPU virtual address management
- Command list, command queue, fence management
- Root signature, pipeline state, descriptor heap
- Shader, blend, depth-stencil, rasterizer states
- Display flip and mode setting

### Build System
- Auto-detect Visual Studio 2022 (E: or C: drive)
- Auto-detect WDK 10.0.26100.0
- Auto catalog generation (inf2cat)
- Self-signed test certificate
- Automatic driver signing

## Project Structure

```
AMD-BC-250-Windows-Driver/
├── src/
│   ├── kmd/                        Kernel-Mode Driver
│   │   ├── amdbc250_dream_v3_kmd.c       DDI callbacks + IOCTL dispatch
│   │   ├── amdbc250_dream_v3_hw_init.c   GPU init, display, SDMA
│   │   ├── amdbc250_dream_v3_power.c     Power/thermal management
│   │   ├── amdbc250_dream_v3_vm.c        GPU virtual memory
│   │   ├── makefile / SOURCES
│   │   └── README.md
│   └── umd/                        User-Mode Driver
│       ├── amdbc250_umd_v46.c           D3D12 UMD with KMD IOCTL
│       └── README.md
├── inc/                            Header files
│   ├── amdbc250_dream_v3_kmd.h        KMD structures
│   ├── amdbc250_dream_v3_hw.h         HW register definitions
│   ├── amdbc250_ioctl.h               IOCTL interface (UMD ↔ KMD)
│   └── amdbc250_d3d12.h               D3D12 DDI
├── inf/                            Driver installation
│   └── amdbc250_dream_v3.inf
├── output/                         Build output (signed)
│   ├── atikmdag.sys                    Kernel driver
│   ├── amdbc250umd64.dll               User driver
│   ├── amdbc250_dream_v3.cat           Catalog
│   └── amdbc250_dream_v3.inf
├── test-tools/                     Testing
│   ├── test-gpu-ioctls.c               IOCTL communication test (15 tests)
│   ├── test-gpu-simple.c               D3D12 device test
│   └── run-gpu-test.bat
├── tools/                          Utility scripts
├── docs/                           Documentation
├── build.bat                       Build + sign script
├── .gitignore
└── README.md
```

## Quick Start

### Requirements
- Windows 10/11 64-bit
- Visual Studio 2022 (Community or higher)
- Windows Driver Kit (WDK) 10.0.26100.0

### Build
```cmd
build.bat
```
Script auto-detects VS2022 + WDK, compiles, generates catalog, signs.

### Install
```powershell
bcdedit /set testsigning on    # Run as Admin
# Reboot
# Device Manager > Update Driver > Browse to output/
```

## IOCTL Interface

UMD communicates with KMD via `\\.\AMDBC250DreamV43`:

| IOCTL | Name | Description |
|-------|------|-------------|
| `0x80000800` | GetCaps | GPU capabilities |
| `0x80000804` | GetVramInfo | VRAM size and usage |
| `0x80000808` | GetTempInfo | Temperature sensors |
| `0x80000840` | AllocVidMem | Allocate GPU memory |
| `0x80000844` | FreeVidMem | Free GPU memory |
| `0x80000848` | MapVidMem | Map for CPU access |
| `0x80000880` | SubmitCommands | Submit GPU commands |
| `0x80000884` | WaitFence | Wait for GPU completion |
| `0x80000888` | SignalFence | Signal fence |
| `0x800008C0` | SetDisplayMode | Set resolution/refresh |
| `0x800008C4` | FlipDisplay | Page flip |
| `0x800008C8` | GetDisplayInfo | Display capabilities |
| `0x80000900` | SetPowerState | D0-D3 power transitions |
| `0x80000904` | GetPowerTelemetry | Power/thermal data |
| `0x80000930` | AllocDmaBuffer | DMA command buffer |
| `0x80000934` | FreeDmaBuffer | Free DMA buffer |
| `0x80000940` | SdmaCopy | GPU buffer copy |
| `0x80000944` | SdmaFill | GPU memory fill |
| `0x80000950` | TdrReset | GPU reset recovery |
| `0x80000960` | ReadEdid | Monitor EDID data |
| `0x80000964` | GetChildRelations | Monitor enumeration |
| `0x80000970` | ShaderCompile | Shader compilation |

## Architecture

Based on Linux `amdgpu` driver:
- `gfx_v10_0.c` - GFX10 command processor
- `nv.c` - Navi family init
- `dcn20/` - DCN 2.1 display engine
- `amdgpu_vm.c` - GPU virtual memory

## Known Limitations

- **Memory**: 16GB shared UMA, VRAM split must be configured in BIOS (512MB-15.5GB)
- **Compute queue**: Disabled (hardware quirk)
- **VCN firmware**: Blocked by Sony
- **UMD**: D3D12 DDI stubs, no full rendering pipeline
- **No OpenGL/Vulkan**: Needs Mesa RADV or AMDVLK
- **Display**: VGA fallback works, 1080p needs tuning

## License

Source code for educational purposes. Use at your own risk.
