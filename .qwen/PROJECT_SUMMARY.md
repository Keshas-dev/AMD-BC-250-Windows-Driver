# BC-250 UMD v4.6 Resource Management - Project Summary

## Overall Goal
Implementuoti pilną **D3D12 Resource Management** AMD BC-250 (RDNA2/GFX1013) User-Mode Driver'iuje - GPU memory allocation, resource lifecycle, heap valdymas, CPU mapping ir descriptor heap support.

## Key Knowledge

### GPU Hardware
- **GPU:** AMD Radeon R7 M260/M265 (BC-250), Device ID: `PCI\VEN_1002&DEV_13FE`
- **Architecture:** CYAN SKILLFISH (RDNA2/GFX1013)
- **Memory:** 16GB GDDR6, 24 CUs (1536 SP)
- **Critical:** VRAM visible limited to ~10GB (hardware quirk)

### Project Structure
- **Project Root:** `C:\AMD-BC-250-Windows-Driver\`
- **UMD Source Files:**
  - `src\umd\amdbc250_umd_v4.c` (1187 lines - v4.0 base)
  - `src\umd\amdbc250_umd_v44.c` (v4.4 build)
  - `src\umd\amdbc250_umd_v45.c` (v4.5 - GetCaps + GetOptionalDDITables)
  - `src\umd\amdbc250_umd_v46.c` (1041 lines - **CURRENT ACTIVE** - Full Resource Management)
- **KMD Source:** `src\kmd\amdbc250_dream_v3_kmd.c` + `amdbc250_dream_v3_hw_init.c` + `amdbc250_dream_v3_power.c` + `amdbc250_dream_v3_vm.c`
- **Build Script:** `build.bat` (current workspace root)
- **Output Directory:** `output\` (current build target)

### Current UMD v4.6 Capabilities (COMPLETE)
✅ OpenAdapter12, CalcPrivateDeviceSize, CreateDevice, CloseAdapter
✅ GetCaps - Memory Architecture reporting (Discrete GPU, not UMA)
✅ GetOptionalDDITables - Reports DEVICE_CORE + COMMAND_LIST_3D support
✅ FillDDITable - DDI table registration
✅ **CreateResource / DestroyResource** - GPU memory allocation with VA tracking
✅ **CreateHeap / DestroyHeap** - VRAM reservation (DEFAULT, UPLOAD, READBACK)
✅ **MapResource / UnmapResource** - CPU access to GPU memory
✅ **CreateDescriptorHeap / DestroyDescriptorHeap** - CBV, SRV, RTV, DSV support
✅ **CopyDescriptors** - Descriptor table copying between heaps
✅ CreateCommandList stub
❌ **No Command Queue** - negalima submitinti command list
❌ **No Draw/Dispatch** - funkcijos neregistruotos OpenAdapter12
❌ **No Pipeline State** - PSO creation stub only
❌ **No Root Signature** - root signature stub only
❌ **GetCaps incomplete** - tik MEMORY_ARCHITECTURE, trūksta SHADER, FEATURE_LEVELS

### Critical Build Command (UMD v4.6)
```powershell
cl.exe /c /D_AMD64_ /DWIN64 /DAMDBC250_UMD /TC ^
  /I"C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um" ^
  /I"C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared" ^
  /I"inc" src\umd\amdbc250_umd_v46.c

link.exe /DLL /OUT:output\amdbc250umd64.dll amdbc250_umd_v46.obj ^
  d3d12.lib dxgi.lib user32.lib kernel32.lib gdi32.lib
```

### Certificate
- CN=BC250TestDriver, SHA1: `22313795FA2CA96ECB495F4B1983E4EA0335452A`
- Test signing ENABLED (`bcdedit /set testsigning on`)

### v4.6 Architecture Details

#### Resource Management Structures
```c
BC250_DEVICE - Device context with:
  - pHeaps[64] - heap tracking
  - pResources[4096] - resource tracking
  - pDescriptorHeaps[64] - descriptor heap tracking
  - NextGPUVirtualAddress - VA allocator
  - VRAMUsed / VRAMTotal (10GB limit)

BC250_HEAP - Heap context:
  - GPUVirtualAddress, Size, AllocatedSize
  - Type (DEFAULT/UPLOAD/READBACK)
  - bCPUAccessible flag
  - pCPUBaseAddress for mapped heaps

BC250_RESOURCE - Resource context:
  - GPUVirtualAddress, Size, Type, Dimension
  - Width/Height/Depth/MipLevels/SampleCount/Format
  - HeapType, hHeap (parent)
  - bCPUMapped, pCPUAddress, MapOffset, MapSize

BC250_DESCRIPTOR_HEAP - Descriptor heap:
  - Type (CBV_SRV_UAV, SAMPLER, RTV, DSV)
  - NumDescriptors, DescriptorSize
  - pCPUStart, pGPUStart, bShaderVisible
```

#### Helper Functions
- `BC250_AlignUp(Size, Alignment)` - align up to boundary
- `BC250_AllocateGPUVA(Device, Size)` - allocate GPU VA from pool
- `BC250_FreeGPUVA(Device, Address, Size)` - free GPU VA
- `BC250_AddHeap/RemoveHeap` - device heap tracking
- `BC250_AddResource/RemoveResource` - device resource tracking

#### Resource Size Calculation
- Buffer: `Width` (straight from args)
- Texture: `Width * Height * Depth * BytesPerPixel`
  - R32G32B32A32_FLOAT: 16 bytes/pixel
  - R16G16B16A16_FLOAT: 8 bytes/pixel
  - R8G8B8A8_UNORM: 4 bytes/pixel
- All sizes aligned to 4KB page boundary

#### GPU VA Allocation
- Base address: `0x100000000` (4GB)
- Maximum: 10GB (`pDevice->VRAMTotal`)
- Simple bump allocator (no coalescing yet)

#### Map/Unmap Behavior
- DEFAULT heap: **NOT mappable** (GPU only)
- UPLOAD heap: `PAGE_READWRITE` (CPU can write, GPU reads)
- READBACK heap: `PAGE_READONLY` (CPU can read, GPU writes)

#### Descriptor Sizes
- CBV/SRV/UAV: 32 bytes
- RTV/DSV: 16 bytes
- Sampler: 16 bytes

## Recent Actions (Before This Session)

1. ✅ Created `amdbc250_umd_v46.c` (1041 lines) with full resource management
2. ✅ Created `build.bat` build script (current workspace root)
3. ✅ Implemented GPU VA allocation/tracking system
4. ✅ Implemented CreateResource with metadata extraction
5. ✅ Implemented CreateHeap with CPU accessibility flags
6. ✅ Implemented MapResource/UnmapResource with VirtualAlloc/VirtualFree
7. ✅ Implemented CreateDescriptorHeap with shader visible flag
8. ✅ Implemented CopyDescriptors with range validation
9. ✅ Registered all resource functions in OpenAdapter12
10. ⚠️ Build not yet tested

## Current Plan (Updated)

### ✅ COMPLETED - Resource Management (v4.6)
- [x] Resource Context Structure (BC250_RESOURCE)
- [x] CreateResource - GPU memory allocation with VA tracking
- [x] DestroyResource - cleanup with unmap support
- [x] CreateHeap/DestroyHeap - VRAM reservation (DEFAULT, UPLOAD, READBACK)
- [x] Resource Map/Unmap - CPU access (UPLOAD=RW, READBACK=RO, DEFAULT=none)
- [x] Descriptor Heap Support - CBV, SRV, RTV, DSV creation and copying
- [x] Build Script - build.bat created

### 🔄 IN PROGRESS - Build & Test v4.6
- [ ] Compile v4.6 UMD and verify no errors
- [ ] Sign CAT file
- [ ] Install driver via Device Manager
- [ ] Reboot and verify with Get-PnpDevice
- [ ] Test with dxdiag to check D3D12 support

### 🔜 NEXT - Command Queue & Draw/Dispatch (v4.7)
1. **Register Draw/Dispatch Functions in OpenAdapter12**
   - BC250_D3D12_DrawInstanced
   - BC250_D3D12_DrawIndexedInstanced
   - BC250_D3D12_Dispatch
   - BC250_D3D12_Present

2. **Command Queue Implementation**
   - CreateCommandQueue / DestroyCommandQueue
   - SubmitCommandLists - submit to GPU via KMD
   - ExecuteCommandLists - ring buffer submission

3. **Pipeline State & Root Signature**
   - CreatePipelineState - PSO with vertex/pixel shaders
   - CreateRootSignature - root signature parsing
   - SetPipelineState - bind PSO to command list

4. **Enhanced GetCaps**
   - D3D12DDICAPS_TYPE_SHADER - shader model 6.0+
   - D3D12DDICAPS_TYPE_FEATURE_LEVELS - 12_0, 12_1 support
   - D3D12DDICAPS_TYPE_FORMAT_SUPPORT - texture format support

### 🔮 FUTURE - Real GPU Execution (v5.0+)
1. **Command Buffer Generation**
   - PM4 packet builders (GFX10/RDNA2 format)
   - Ring buffer submission via KMD
   - Doorbell notification to GPU

2. **Shader Compilation**
   - DXBC/SPIR-V to AMDGCN translation
   - Register allocation for RDNA2 VGPR/SGPR
   - Wave32/Wave64 support

3. **Memory Management Integration**
   - KMD IOCTL for actual VRAM allocation
   - GART/System memory mapping
   - Page table programming (AMDGPU VM)

## Constraints & Gotchas
- ⚠️ Compile as  C++ - D3D12 DDI headers have C++ syntax
- ⚠️ NTSTATUS must be defined before DDI headers
- ⚠️ VOID/APIENTRY need proper Windows type includes
- ⚠️ Resource allocation in UMD is **STUB** - actual VRAM allocation happens in KMD via kernel calls
- ⚠️ GPU VA allocation is simple bump allocator - no coalescing or fragmentation handling
- ⚠️ MapResource uses VirtualAlloc, not real GPU mapping - **data not actually accessible by GPU**
- ⚠️ No real command submission - Draw/Dispatch/Present are stubs
- ⚠️ D3D12 runtime version matters - using `D3D12DDIARG_CREATEDEVICE_0003`
- ⚠️ Descriptor heaps allocate CPU memory with VirtualAlloc - **no GPU address space**

## Critical Code Patterns

### OpenAdapter12 Function Registration (v4.6)
```c
pOpenData->pAdapterFuncs->pfnCreateResource = BC250_D3D12_CreateResource;
pOpenData->pAdapterFuncs->pfnDestroyResource = BC250_D3D12_DestroyResource;
pOpenData->pAdapterFuncs->pfnMapResource = BC250_D3D12_MapResource;
pOpenData->pAdapterFuncs->pfnUnmapResource = BC250_D3D12_UnmapResource;
pOpenData->pAdapterFuncs->pfnCreateHeap = BC250_D3D12_CreateHeap;
pOpenData->pAdapterFuncs->pfnDestroyHeap = BC250_D3D12_DestroyHeap;
pOpenData->pAdapterFuncs->pfnCreateDescriptorHeap = BC250_D3D12_CreateDescriptorHeap;
pOpenData->pAdapterFuncs->pfnCopyDescriptors = BC250_D3D12_CopyDescriptors;
```

### GPU VA Allocation Pattern
```c
static UINT64 BC250_AllocateGPUVA(PBC250_DEVICE pDevice, UINT64 Size) {
    UINT64 Address = pDevice->NextGPUVirtualAddress;
    Size = BC250_AlignUp(Size, BC250_PAGE_SIZE);
    
    if (pDevice->VRAMUsed + Size > pDevice->VRAMTotal) {
        return 0;  // Out of VRAM
    }
    
    pDevice->NextGPUVirtualAddress = Address + Size;
    pDevice->VRAMUsed += Size;
    return Address;
}
```

### Resource Creation Pattern
```c
static HRESULT BC250_D3D12_CreateResource(...) {
    PBC250_RESOURCE pResource = HeapAlloc(..., sizeof(BC250_RESOURCE));
    
    /* Set metadata from args */
    pResource->Type = pArgs->ResourceDimension;
    pResource->Size = /* calculate from dimensions */;
    pResource->Size = BC250_AlignUp(pResource->Size, BC250_PAGE_SIZE);
    
    /* Allocate GPU VA */
    pResource->GPUVirtualAddress = BC250_AllocateGPUVA(pDevice, pResource->Size);
    
    hResource.pDrvPrivate = pResource;
    return S_OK;
}
```

## Installation Instructions (v4.6)
1. Run: `build.bat` (compile UMD + KMD)
2. Sign CAT: `Create-SignedCatalog.ps1` in `output\` or workspace root if it exists
3. Device Manager → Display adapters → Update Driver
4. Browse → Let me pick → Have Disk
5. Select: `C:\AMD-BC-250-Windows-Driver\output\amdbc250_dream_v3.inf`
6. Choose: "AMD Radeon BC-250 Graphics (Dream Drivers v4.6)"
7. REBOOT computer
8. Verify: `Get-PnpDevice -Class Display | Select Status, FriendlyName, ConfigManagerErrorCode`
   - Expected: Status=OK, FriendlyName=AMD Radeon BC-250 Graphics, ConfigManagerErrorCode=0

## Next Immediate Steps
1. **Build v4.6** - run `build.bat` and fix any compilation errors
2. **Test installation** - install driver and verify with dxdiag
3. **Implement v4.7** - add Draw/Dispatch registration + Command Queue
4. **Debug logging** - all functions have OutputDebugStringA for DbgView monitoring

---

**Update time**: 2026-04-14T00:00:00.000Z
**Status**: v4.6 Resource Management IMPLEMENTED, needs BUILD & TEST
**Next version**: v4.7 - Command Queue + Draw/Dispatch registration
