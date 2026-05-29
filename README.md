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
- 24 IOCTL handlers for UMD communication
- SDMA copy engine
- TDR reset recovery
- EDID parsing + hotplug detection
- 40 CU unlock (from duggasco/bc250-40cu-unlock)
- Power/thermal management
- PM4 Command Queue with fence event signaling

### User-Mode Driver (UMD) — `amdbc250umd64.dll`
- D3D9 DDI with 45+ functions
- DrawPrimitive with PM4 packets
- DMA command buffer allocation via KMD IOCTL
- Display flip via KMD IOCTL
- GetCaps for adapter capabilities

### Vulkan ICD — `amdbc250vulkan.dll`
- Vulkan 1.4 API (5 core functions)
- ACO shader compiler (SPIR-V → GFX10 ISA)
- QueueSubmit with KMD IOCTL + fence event
- QueuePresentKHR with display flip
- Buffer/Image creation via KMD IOCTL

### Shader Compiler — `bc250_shader.c`
- SPIR-V → GFX10 ISA compilation
- Vertex/Fragment/Compute/Geometry support
- GFX10 ISA opcode definitions

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

## Architecture

```
App → D3D9 Runtime → UMD D3D9 → KMD IOCTL → GPU
App → Vulkan ICD → ACO Compiler → KMD IOCTL → GPU
```

## Known Limitations

- D3D9 UMD: 45+ DDI functions, but rendering pipeline incomplete
- Vulkan ICD: 5 core functions, need more extensions
- Display: VGA fallback + 1080p (needs tuning)

## License

Source code for educational purposes. Use at your own risk.
ACO compiler: MIT license (Mesa project).
