# AMD BC-250 Dream Drivers - Documentation

## Overview

"Dream Drivers" because everyone dreams of working Windows drivers for BC-250.

Linux has full support via AMDGPU, but Windows has zero official GPU drivers. This project changes that.

## Hardware Specification

| Component | Specification |
|-----------|--------------|
| Codename | Cyan Skillfish |
| Architecture | RDNA 1.5 (GFX1013) |
| Compute Units | 24 (1536 shaders) |
| Memory | 16GB GDDR6 (shared CPU/GPU) |
| Ray Tracing | Dedicated cores (early gen) |
| Display | DCN 2.1 (4 pipes) |
| TDP | 220W |
| CPU | 6x Zen 2 cores @ ~3.5GHz |
| PCI Device ID | `0x13FE` |

## Architecture

### Kernel-Mode Driver (KMD)
- WDDM 2.x/3.x display miniport driver
- GFX10 command processor with PM4 packets
- DCN 2.1 display engine (4 display pipes)
- SDMA copy engine for GPU buffer operations
- 4-level GPU page tables (GFX10 VM)
- 21 IOCTL handlers for UMD communication
- TDR recovery with 6-step reset sequence

### User-Mode Driver (UMD)
- D3D12 DDI (Device Driver Interface) implementation
- Communicates with KMD via IOCTL
- Resource/Heap management through KMD
- Command list and fence management

### Build System
- Auto-detect Visual Studio 2022 + WDK
- Auto catalog generation and signing
- Self-signed test certificate

## Documentation Files

| File | Description |
|------|-------------|
| [PILNAS-APRASAS.md](PILNAS-APRASAS.md) | Full technical specification (Lithuanian) |
| [BUILD-STATUS.md](BUILD-STATUS.md) | Build environment notes |
| [D3D12-UMD-RESEARCH.md](D3D12-UMD-RESEARCH.md) | D3D12 UMD research notes |

## References

- Linux AMDGPU driver: `drivers/gpu/drm/amd/amdgpu/`
- Mesa RADV: `src/amd/vulkan/`
- GFX10 register definitions: community reverse-engineering
- BC-250 community docs: https://elektricm.github.io/amd-bc250-docs/
