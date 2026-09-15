# RADV/Mesa Windows ICD Research

## Current Status (2026-09-15 — ICD STUB WORKS)

- Vulkan SDK 1.4.341.1 available at `F:\VulkanSDK\1.4.341.1` (note: `VULKAN_SDK` env was `C:\VulkanSDK` — wrong, fixed to `F:`)
- Loader (`vulkan-1.dll` 1.4.341.1) present, Microsoft loader in `C:\Windows\System32`
- **ICD stub `bc250_icd_stub.dll` (134 KB, 719 exports) BUILDS and LOADS** via `VK_ICD_FILENAMES`
- `vk-minimal-test.exe` now: `vkCreateInstance SUCCESS`, `vkEnumeratePhysicalDevices SUCCESS (GPUs=1) GPU[0]: AMD BC-250 (RADV Stub)`
- Previous error `loader_scanned_icd_add: Failed to open ".c250_icd_stub.dll" error 87` was JSON escape bug: `".\bc250..."` -> `\b` = backspace; fixed to `"C:/.../bc250_icd_stub.dll"` (forward slashes)
- Previous error `Unable to load vkDestroyInstance` was `vkGetInstanceProcAddr` returning NULL (generic stub); fixed to `GetProcAddress(g_hModule, pName)` dispatch
- Registry: `HKLM\SOFTWARE\Khronos\Vulkan\Drivers` has `amdbc250_icd.json` (0x0), `HKLM\SOFTWARE\Khronos\Vulkan\ExplicitLayers` incorrectly had ICD entry — should be removed, use `VK_ICD_FILENAMES` for testing
- `vulkaninfoSDK.exe --summary` still shows `LDP_DRIVER_8` warning (interface 0 exposing VkSurfaceKHR) but `vk-minimal-test` proves core ICD works; next is `vk_icdNegotiateLoaderICDInterfaceVersion` (v5) to fix surface policy

## What RADV Needs from Kernel Driver

RADV is a Vulkan UMD (user-mode driver). On Linux, it interfaces with `amdgpu` kernel driver via a well-defined ioctl interface. On Windows, it would need a similar interface.

### Core Kernel Services Required by Vulkan UMD

| Service | RADR/Mesa term | Our KMD status | Notes |
|---------|---------------|----------------|-------|
| **Buffer Object (BO) allocation** | `amdgpu_bo_alloc` | ❌ NOT IMPLEMENTED | VRAM/GART memory management needed for all Vulkan buffers |
| **BO mapping** | `amdgpu_bo_map` | ❌ | CPU-accessible mapping of VRAM |
| **Ring buffer creation** | `amdgpu_gfx_ring_init` | ❌ | GFX ring BASE registers are SOS-locked (read-only) |
| **Command submission** | `amdgpu_job_submit` | ❌ | Needs ring submission + scheduling |
| **Semaphore/fence** | `amdgpu_fence` | ❌ | GPU timelines for sync |
| **Display/swapchain** | `amdgpu_dm` / D3DKMT | ✅ KMDOD | Separate display driver works |
| **Device query** | `amdgpu_query_info` | ✅ Partial | GPU ID, SMU version via SMN |
| **Memory type query** | `amdgpu_vram_info` | ✅ Partial | VRAM size via IOCTLs |
| **Power management** | `amdgpu_pm` | ✅ FULL | SMU mailbox (freq, VID, features) |
| **UVD/VCN** | `amdgpu_vce` | ❌ | VCN dom6 unpowered |
| **GART/VM** | `amdgpu_gart` | ❌ | GART=0, VM init crashes (0x1A) |

### RADV's Kernel Interface (Linux amdgpu ioctl mapping)

On Linux, RADV uses these amdgpu ioctls:
- `DRM_AMDGPU_GEM_CREATE` — allocate BO
- `DRM_AMDGPU_GEM_MAP` — map BO to CPU
- `DRM_AMDGPU_GEM_PREALLOC` — preallocate Bo
- `DRM_AMDGPU_GEM_BUSY` — check BO busy status
- `DRM_AMDGPU_GEM_SET_DOMAIN` — change BO domain (VRAM/GTT)
- `DRM_AMDGPU_GEM_CPU_PRECAP` — CPU prefetch
- `DRM_AMDGPU_CS` — command submission (ring)
- `DRM_AMDGPU_FENCE_TO_USER` — fence mapping
- `DRM_AMDGPU_QUERY_INFO` — device info
- `DRM_AMDGPU_OVERALLOC` — VRAM over-allocation
- `DRM_AMDGPU_VM_UPDATE` — VM/page table update

## What Our KMD Currently Exposes via IOCTL

| Our IOCTL | Purpose | Usable for RADV? |
|-----------|---------|-------------------|
| `IOCTL_AMDBC250_READ_REG` / `WRITE_REG` | GPU register read/write | ✅ (low-level, but works) |
| `IOCTL_AMDBC250_BAR5_READ_PROXY` / `WRITE_PROXY` | Raw BAR5 access | ✅ (for MMIO registers) |
| `IOCTL_AMDBC250_SMU_CPU_MSG` (0x80000C2C) | SMU CPU mailbox | ✅ (power management) |
| `IOCTL_AMDBC250_PCI_SMN_ACCESS` | SMN access | ✅ (SMU mailbox) |
| `IOCTL_AMDBC250_PSP_RING_INIT/SUBMIT` | PSP ring | ✅ (firmware commands only) |
| `IOCTL_AMDBC250_INIT_HARDWARE` | Map BAR5 + init | ✅ (needed for everything) |
| `IOCTL_AMDBC250_CMOS_ACCESS` | CMOS/VRAM config | ✅ (VRAM split config) |
| **BO allocation** | — | ❌ **MISSING** |
| **Ring submission** | — | ❌ **MISSING** |
| **Fence/semaphore** | — | ❌ **MISSING** |
| **VM/GART** | — | ❌ **MISSING** |

## Gap Analysis

### CRITICAL MISSING: Memory Management
RADV requires buffer object (BO) allocation and management. Without VRAM/GART allocation, there are no Vulkan buffers, no command buffers, no textures — nothing works.

### CRITICAL MISSING: Command Submission
RADV submits GPU commands via ring buffers. Our GFX ring BASE is read-only (SOS-locked). KIQ_SIZE=0. No ring can be created.

### PARTIALLY AVAILABLE: Power Management
SMU mailbox (Q0/Q3) is fully working — frequency, voltage, features. This is a strong foundation for power management in Vulkan apps.

### AVAILABLE: Display
KMDOD driver works for display output. But Vulkan swapchain needs WDDM KMD or a real Vulkan-capable miniport.

## Implementation Plan (In Order of Difficulty)

### Phase 1: Minimal ICD Stub (1-2 weeks)
- Build a minimal Vulkan ICD DLL (`amdradv64.dll`) that:
  - Exports `vkGetInstanceProcAddr`, `vk_icdGetInstanceProcAddr`, `vk_icdNegotiateLoaderICDInterfaceVersion`
  - Implements `vkCreateInstance`, `vkEnumeratePhysicalDevices`, `vkCreateDevice`
  - Reports BC-250 as a physical device (GPU ID 0x9FFF9700)
  - Returns STUB results for all other calls (not functional, but no crashes)
- Goal: `vulkaninfo` shows BC-250 device without crashing

### Phase 2: Memory Management (2-4 weeks)
- Implement `amdgpu_bo_alloc` equivalent via IOCTL:
  - VRAM allocation via BAR5 + SMN (use SMU SRAM as staging)
  - System memory allocation via IOCTL
  - BO map/unmap via IOCTL
- Map to RADV's `VkDeviceMemory` allocation interface
- Goal: `VK_KHR_external_memory` basic operations work

### Phase 3: Command Submission (3-6 weeks)
- Implement ring submission via IOCTL:
  - Create ring buffer (via IOCTL)
  - Submit PM4 commands (we have `SEND_PM4` — software executor)
  - Fence/wait via IOCTL
- Map to RADV's `VkQueue` submission interface
- Goal: Basic draw calls submit to GPU (may not execute due to WGP lock)

### Phase 4: Display Integration (1-2 weeks)
- Connect Vulkan swapchain to KMDOD display
- Implement `VK_KHR_swapchain` via our display IOCTLs
- Goal: Vulkan app can present to screen

### Phase 5: Full Integration (ongoing)
- Complete RADV UMD port (200K+ lines of C)
- Optimize, fix bugs, add extensions
- Goal: Vulkan apps work on BC-250

## Registry: ICD Registration on Windows

A Vulkan ICD must be registered for the loader to find it. Two methods:

### Method 1: Registry (system-wide)
```
HKLM\SOFTWARE\Khronos\Vulkan\ICDs\{drivername}.json
```
JSON content:
```json
{
  "file_format_version": "1.0.0",
  "ICD": {
    "library_path": "C:\\path\\to\\amdradv64.dll",
    "api_version": "1.3.276"
  }
}
```

### Method 2: Environment Variable (per-user/process)
```
VK_ICD_FILENAMES=C:\path\to\amdradv64.json
```

### Method 3: vkconfig.exe
Use `F:\VulkanSDK\1.4.341.1\bin\vkconfig.exe` → ICDs tab → Add ICD

## Key Technical Challenges

1. **WGP/SPI_PG locked on Windows** — even with RADV, shaders can't execute without WGP power. This requires EFI/Linux unlock path first.

2. **Ring BASE registers SOS-locked** — no GPU ring can be created from Windows WDM. May need Windows WDDM miniport (not WDM) for proper ring support.

3. **No GART/VM** — RADV needs VRAM/GART management. Our driver doesn't have this. Without it, no BOs can be allocated.

4. **KMD vs WDDM** — RADV on Windows typically interfaces via WDDM (DxgkDdi). Our driver is WDM. The interface model is fundamentally different.

5. **Vulkan ICD needs WDDM for display** — On Windows, Vulkan ICDs are typically paired with WDDM miniports for display. Our KMDOD is separate.

## Immediate Actions

1. **Build minimal ICD stub** — prove Vulkan loader can load our ICD
2. **Map RADV ioctl interface** — document full mapping to our IOCTLs
3. **Design memory subsystem** — how to allocate BOs via our KMD
4. **Research Valve/Collabora RADV Windows** — find upstream progress
5. **Evaluate WDDM vs WDM for ICD** — can our WDM driver work as ICD backend?

## References

- Vulkan ICD interface: `F:\VulkanSDK\1.4.341.1\Include\vulkan\vk_icd.h`
- Vulkan SDK: `F:\VulkanSDK\1.4.341.1`
- RADV source: `https://gitlab.freedesktop.org/mesa/mesa/-/tree/main/src/amd/vulkan` (Mesa main branch)
- Valve Proton WGP: `https://github.com/ValveSoftware/Proton-WGP` (if exists)
- amdgpu kernel interface: Linux `drivers/gpu/drm/amd/amdgpu/` (ioctl definitions in `amdgpu_drm.h`)
