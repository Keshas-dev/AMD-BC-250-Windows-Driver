# AMD BC-250 Dream Drivers v4.3

Windows GPU driver for AMD BC-250 (Cyan Skillfish / RDNA2 / GFX1013).

## Hardware

| Spec | Value |
|------|-------|
| Architecture | RDNA 2 (GFX1013) |
| Compute Units | 24-40 (40 CU unlock supported) |
| Memory | 16GB GDDR6 (shared UMA, BIOS configurable) |
| Ray Tracing | Early gen RT cores |
| Display Engine | DCN 2.1 (4 pipes) |
| TDP | 220W |
| PCI ID | `VEN_1002&DEV_13FE` + 6 variants |

## Components

### Kernel-Mode Driver (KMD) — `atikmdag.sys`
- WDDM 2.x DDI callbacks
- GFX10 command processor initialization
- 22 IOCTL handlers for UMD communication
- SDMA copy engine
- TDR reset recovery
- EDID parsing + hotplug detection
- 40 CU unlock (from duggasco/bc250-40cu-unlock)
- Power/thermal management

### User-Mode Driver (UMD) — `amdbc250umd64.dll`
- D3D9 DDI with PM4 DrawPrimitive
- D3D12 DDI stubs
- DMA command buffer allocation
- Display flip via KMD IOCTL

### Vulkan ICD — `amdbc250vulkan.dll`
- Vulkan 1.4 API (stubs)
- ACO shader compiler wrapper (Mesa)
- QueueSubmit → KMD IOCTL

### Build System
- Auto-detect Visual Studio 2022 + WDK
- Auto catalog generation + signing
- One-command build: `build.bat`

## Quick Start

### Build
```cmd
build.bat
```

### Install
```powershell
bcdedit /set testsigning on    # Run as Admin
# Reboot
# Device Manager > Update Driver > Browse to output/
```

### 40 CU Unlock
```powershell
reg add "HKLM\SYSTEM\CurrentControlSet\Services\AMDBC250DreamV43" /v Enable40CU /t REG_DWORD /d 1
# Reboot for 40 CUs (1.61x performance boost)
```

## Project Structure

```
AMD-BC-250-Windows-Driver/
├── src/
│   ├── kmd/                    Kernel-Mode Driver (4 .c files)
│   ├── umd/                    User-Mode Driver (D3D9 + D3D12)
│   └── vulkan/                 Vulkan ICD + ACO wrapper
├── inc/                        Headers (KMD, HW, IOCTL, D3D)
├── inf/                        Driver installation
├── output/                     Build output (signed)
├── test-tools/                 Test applications
├── docs/                       Documentation
├── mesa-aco/                   Mesa ACO compiler (submodule)
├── build.bat                   Build + sign script
└── README.md
```

## Architecture

```
App → Vulkan/D3D9 → UMD → KMD IOCTL → GPU
                  ↓
            ACO Compiler (SPIR-V → GFX10 ISA)
```

## Known Limitations

- Vulkan ICD: stub implementations (need full API)
- ACO: placeholder (need Mesa integration)
- D3D9: DrawPrimitive works, but no shader compilation
- Display: VGA fallback + 1080p (needs tuning)

## Linux Comparison

| Feature | Linux (amdgpu) | Windows (Dream) |
|---------|---------------|-----------------|
| Vulkan | RADV ✅ | ICD stub ⚠️ |
| OpenGL | radeonsi ✅ | Not implemented ❌ |
| D3D12 | DXVK ✅ | Stub ⚠️ |
| D3D9 | Gallium Nine ✅ | D3D9 DDI ⚠️ |
| Compute | Broken HW ❌ | Disabled (quirk) |

## License

Source code for educational purposes. Use at your own risk.
ACO compiler: MIT license (Mesa project).
