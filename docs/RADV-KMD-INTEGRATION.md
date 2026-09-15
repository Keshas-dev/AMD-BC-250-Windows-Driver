# RADV+KMD Integration Guide
# BC-250 Windows Driver
# Generated: 2026-09-15

## Overview

This document describes how RADV (Mesa's Vulkan UMD) would interface with our
AMD BC-250 Windows kernel driver (atikmdag.sys) via IOCTL calls.

## Architecture

```
+---------------------------+
| Vulkan App (e.g. Steam)   |
|   vkCreateInstance        |
|   vkCreateDevice          |
|   vkAllocateMemory        |---> ICD Stub (bc250_icd_stub.dll)
|   vkCreateBuffer          |--->          |
|   vkQueueSubmit           |--->          |
|   vkWaitForFences         |--->          |
+---------------------------+              |
                                           | DeviceIoControl
                                           | (\\.\AMDBC250DreamV43)
                                           v
                                  +--------+--------+
                                  | KMD (atikmdag.sys)|
                                  |                   |
                                  | ALLOC_VIDMEM      |---> VRAM (UserMode VA)
                                  | FREE_VIDMEM       |---> 
                                  | MAP_VIDMEM        |---> 
                                  | SUBMIT_COMMANDS   |---> GPU (if WGP unlocked)
                                  | READ_REG/WRITE_REG|--> BAR5 (0xFE800000)
                                  | SMN_ACCESS        |---> SMN (NBIO 0x38/0x3C)
                                  | SMU_MSG           |---> SMU (via SMN)
                                  | GET_VRAM_INFO     |---> VRAM info
                                  | CMOS_ACCESS       |---> CMOS (ports 0x72/0x73)
                                  +-------------------+
```

## Current Working Path (Vulkan API → KMD IOCTL)

| Vulkan Call | ICD Stub Function | KMD IOCTL | Status |
|-------------|-------------------|-----------|--------|
| `vkCreateInstance` | `bc250_vkCreateInstance` | — | ✅ Works |
| `vkEnumeratePhysicalDevices` | `bc250_vkEnumeratePhysicalDevices` | GET_VRAM_INFO (0x80000804) | ✅ Works |
| `vkGetPhysicalDeviceProperties` | `bc250_vkGetPhysicalDeviceProperties` | — | ✅ Hardcoded |
| `vkGetPhysicalDeviceMemoryProperties` | `bc250_vkGetPhysicalDeviceMemoryProperties` | GET_VRAM_INFO (0x80000804) | ✅ Works (16GB after KMD fix) |
| `vkCreateDevice` | `bc250_vkCreateDevice` | — | ✅ Works |
| `vkAllocateMemory` | `bc250_vkAllocateMemory` | ALLOC_VIDMEM (0x80000820) | ✅ Works (UserMode VA, 256MB limit) |
| `vkFreeMemory` | `bc250_vkFreeMemory` | FREE_VIDMEM (0x80000824) | ✅ Works |
| `vkMapMemory` | `bc250_vkMapMemory` | MAP_VIDMEM (0x80000828) | ✅ Works |
| `vkCreateBuffer` | `bc250_vkCreateBuffer` | KMD alloc + bind | ✅ Works |
| `vkDestroyBuffer` | `bc250_vkDestroyBuffer` | KMD free | ✅ Works |
| `vkQueueSubmit` | `bc250_vkQueueSubmit` | SUBMIT_COMMANDS (0x80000880) | ✅ API works, GPU no-exec (WGP lock) |
| `vkWaitForFences` | `bc250_vkWaitForFences` | KMD fence | ✅ Works (immediate) |
| `vkCreateSemaphore` | `bc250_vkCreateSemaphore` | — | ✅ Stub |
| `vkCreateFence` | `bc250_vkCreateFence` | — | ✅ Works |
| `vkDeviceWaitIdle` | `bc250_vkDeviceWaitIdle` | — | ✅ Stub |

## What RADV Needs vs What We Have

| RADV Requirement | Our Implementation | Gap |
|------------------|--------------------|-----|
| **BO allocation** | ALLOC_VIDMEM (UserMode VA) | No VRAM/GART BO management; 256MB limit |
| **BO mapping** | MAP_VIDMEM (UserMode mapping) | ✅ Works for staging |
| **BO import (Vulkan)** | KMD buffer table | Stub — needs real BO handle mapping |
| **Command submission** | SUBMIT_COMMANDS (IOCTL 0x80000880) | GPU doesn't execute (WGP locked) |
| **Sync objects** | Fence IOCTLs | Stub — needs GPU timeline |
| **Shader compilation** | ACO wrapper (bc250_aco_wrapper) | Stub — Mesa ACO not linked |
| **Memory types** | GET_VRAM_INFO + hardcoded | 1 heap (DEVICE_LOCAL), 2 types |
| **Device queries** | SMU via SMN | ✅ Full (freq, VID, temp, features) |
| **Display/swapchain** | KMDOD (separate driver) | ✅ Separate driver works |

## WGP Unlock Requirement

**CRITICAL**: Without WGP unlock, RADV (or any GPU compute/3D) CANNOT execute.
The GPU has 0 WGPs active (SPI_PG=0, SOS-locked from host BAR5).

Possible unlock paths:
1. **EFI Shell pre-boot** (`third-party/EFI_Boot/WGP_unlock.nsh`) — requires BIOS NBIO unlock
2. **Linux kernel** (`bc250-40cu-unlock` kernel patch) — has debugfs privilege
3. **Windows WDDM miniport** — would need full init order like Linux (GART+PSP ring before SPI_PG)

Our current WDM driver CANNOT unlock WGP on Windows. The KMD init order skips GART/VM
(0x1A BSOD guard), so SPI_PG is always SOS-locked.

## Memory Management Design (Future)

For RADV to use our KMD for BO allocation, we need:

1. **BO_CREATE IOCTL** — allocate VRAM/GART memory with handle
2. **BO_MAP IOCTL** — CPU mapping of BO
3. **BO_BIND IOCTL** — bind BO to buffer/memory object
4. **BO_DESTROY IOCTL** — free BO

Currently, ALLOC_VIDMEM provides UserMode VA for staging buffers, but lacks:
- VRAM-bound allocation (GART/VM not initialized)
- BO handle management (RADV expects handles, not raw pointers)
- Domain switching (VRAM ↔ GTT)

### Proposed IOCTL Interface

```c
// BO allocation
typedef struct {
    uint32_t size;           // BO size in bytes
    uint32_t domain;         // AMDGPU_GEM_DOMAIN_VRAM | AMDGPU_GEM_DOMAIN_GTT
    uint64_t bo_handle;      // OUT: BO handle (opaque)
    uint64_t gpu_address;    // OUT: GPU virtual address
    uint64_t cpu_address;    // OUT: CPU pointer (if mappable)
} AMDBC250_IOCTL_BO_CREATE;

// BO destroy
typedef struct {
    uint64_t bo_handle;      // IN: BO handle
} AMDBC250_IOCTL_BO_DESTROY;

// BO map
typedef struct {
    uint64_t bo_handle;      // IN: BO handle
    uint64_t offset;         // IN: offset within BO
    uint64_t size;           // IN: size to map
    uint64_t cpu_addr;       // OUT: CPU mapping address
} AMDBC250_IOCTL_BO_MAP;
```

## Command Submission Design (Future)

RADV submits PM4 commands in IBs (Indirect Buffer). Our SUBMIT_COMMANDS IOCTL
accepts PM4 commands but requires:
1. A valid ring buffer (BASE registers SOS-locked → cannot create)
2. WGP powered on (SPI_PG=0 → no execution)

Once WGP is unlocked and ring BASE is programmable, SUBMIT_COMMANDS would handle:
1. IB chain parsing (RADV sends indirect buffers)
2. PM4 packet validation (MOVE, NOP, WAIT, WRITE, CONDWRITE)
3. Fence signaling (for sync)

## Key IOCTL Codes

| IOCTL | Code | Purpose |
|-------|------|---------|
| INIT_HARDWARE | 0x80000B80 | Map BAR5, init hardware |
| ALLOC_VIDMEM | 0x80000820 | Allocate UserMode VRAM |
| FREE_VIDMEM | 0x80000824 | Free UserMode VRAM |
| MAP_VIDMEM | 0x80000828 | Map VRAM to UserMode |
| READ_REG | 0x80000010 | Read GPU register |
| WRITE_REG | 0x80000014 | Write GPU register |
| SMN_READ | 0x80000C30 | Read SMN |
| SMN_WRITE | 0x80000C34 | Write SMN |
| SMU_MSG | 0x80000924 | SMU mailbox message |
| SUBMIT_COMMANDS | 0x80000880 | Submit PM4 commands |
| GET_VRAM_INFO | 0x80000804 | VRAM info (Total, Visible, Used) |
| GET_TEMP_INFO | 0x80000808 | Temperature info |
| CMOS_ACCESS | 0x80000C28 | CMOS read/write |
| SMU_CPU_MSG | 0x80000C2C | SMU CPU mailbox |

## Registry/ICD Registration

Current ICD JSON: `C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\amdbc250_icd.json`
```json
{"file_format_version":"1.0.0","ICD":{"library_path":"C:/AMD-BC-250/AMD-BC-250-Windows-Driver-main/output/bc250_icd_stub.dll","api_version":"1.0.0"}}
```

Registry entries (HKLM\SOFTWARE\Khronos\Vulkan\Drivers):
- `C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\amdbc250_icd.json` = 0
- `C:\Windows\System32\amdbc250_icd.json` = 0

For testing, use `VK_ICD_FILENAMES` env var pointing to the JSON.

## Testing

```powershell
# Force Vulkan to use our ICD
$env:VULKAN_SDK = "F:\VulkanSDK\1.4.341.1"
$env:VK_ICD_FILENAMES = "C:\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\amdbc250_icd.json"
F:\VulkanSDK\1.4.341.1\Bin\vulkaninfoSDK.exe --summary
```

## Next Steps

1. ~~Build minimal ICD stub~~ ✅
2. ~~Map RADV ioctl interface~~ ✅
3. **Design memory subsystem** — BO allocation via KMD
4. **Research WGP unlock** — EFI/Linux path
5. **Find Valve/Collabora RADV WDM** — upstream WDM port
6. **Evaluate WDDM vs WDM** — for full Vulkan support
