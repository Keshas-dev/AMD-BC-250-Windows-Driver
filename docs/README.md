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

## Documentation Files

| File | Description |
|------|-------------|
| [PILNAS-APRASAS.md](PILNAS-APRASAS.md) | Full technical specification (Lithuanian) |
| [BUILD-STATUS.md](BUILD-STATUS.md) | Build environment and compilation notes |
| [D3D12-UMD-RESEARCH.md](D3D12-UMD-RESEARCH.md) | D3D12 User-Mode Driver research notes |

## References

- Linux AMDGPU driver: `drivers/gpu/drm/amd/amdgpu/`
- Mesa RADV: `src/amd/vulkan/`
- GFX10 register definitions: community reverse-engineering
- BC-250 community docs: https://elektricm.github.io/amd-bc250-docs/
