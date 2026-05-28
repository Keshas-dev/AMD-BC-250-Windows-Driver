# D3D12 UMD Tyrimo Rezultatai (2026-04-13)

## Santrauka

Atlikome išsamų tyrimą apie atviro kodo AMD Windows tvarkykles ir D3D12 UMD kūrimo galimybes BC-250 projektui.

## 📊 Rasti Šaltiniai

### 1. BC-250 VBIOS / Firmware
**GitHub:** https://github.com/MrrZed0/bc-250-bios
- ✅ `BC250_3.00.ROM` - originali VBIOS
- ✅ `BC250_3.00_CHIPSETMENU.ROM` - modifikuota su atrakintu Chipset Menu
- 📍 Naudinga: GPU config, UMA settings, NBIO options

### 2. Microsoft WDDM / D3D12 DDI Dokumentacija
| Tema | Nuoroda | Turinys |
|------|---------|---------|
| **WDDM Overview** | https://learn.microsoft.com/en-us/windows-hardware/drivers/display/windows-vista-display-driver-model-design-guide | WDDM architektūra |
| **D3D12 UMD DDI** | https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/ | Visos D3D12 UMD funkcijos |
| **KMD DDI** | https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dxgkddi/ | Kernel mode sąsajos |
| **INF Files** | https://learn.microsoft.com/en-us/windows-hardware/drivers/install/general-guidelines-for-inf-files | INF formato gairės |
| **Registry Settings** | https://learn.microsoft.com/en-us/windows-hardware/drivers/display/adding-software-registry-settings | UMD registry konfigūracija |

### 3. GitHub Microsoft Docs Repo
- **Repo:** https://github.com/MicrosoftDocs/windows-driver-docs-ddi
- **Failai:** `wdk-ddi-src/content/d3d12umddi/` - visa D3D12 UMD DDI dokumentacija Markdown
- **Naudinga:** `d3d12ddi_device_funcs_core_*.md` - visos privalomos UMD callback

### 4. Linux amdgpu (Geriausias Hardware Šaltinis)
- `drivers/gpu/drm/amd/amdgpu/gfx_v10_0.c` - GFX10 command processor
- `drivers/gpu/drm/amd/amdgpu/nv.c` - Navi init (turi Cyan Skillfish)
- `drivers/gpu/drm/amd/display/dc/dcn20/` - DCN 2.1 display engine

### 5. Microsoft Graphics Driver Samples
- **Repo:** https://github.com/microsoft/graphics-driver-samples
- **Turinys:** Bendri WDDM driver pavyzdžiai (Raspberry Pi 2)
- **Nauda:** Geras pavyzdys KMD/UMD struktūrai, bet ne AMD specifinis

## ⚠️ D3D12 UMD Kūrimo Problemos

### DDI Versijų Nesuderinamumas

**Problema:** D3D12 DDI struktūros skiriasi priklausomai nuo WDDM versijos:
- WDDM 2.0 (Windows 10 1507) - `D3D12DDIARG_CREATEDEVICE`
- WDDM 2.3 (Windows 10 1703) - `D3D12DDIARG_CREATEDEVICE_0003`
- WDDM 3.0 (Windows 11) - `D3D12DDIARG_CREATEDEVICE_0010`

**Pasekmės:**
1. Funkcijų parašai keičiasi tarp versijų
2. Struktūrų laukai skiriasi
3. Callback funkcijų sąrašas plečiasi

**Sprendimas:** Reikia tiksliai žinoti kurią WDDM versiją targetuojame ir naudoti atitinkamus DDI apibrėžimus.

### Funkcijų Struktūros Pokyčiai

**Pavyzdys - CheckFormatSupport:**
```c
// WDDM 2.0
HRESULT CheckFormatSupport(DXGI_FORMAT Format, D3D12DDI_FORMAT_SUPPORT_FLAGS* pFlags);

// WDDM 3.0
HRESULT CheckFormatSupport(DXGI_FORMAT Format, UINT* pFlags);
```

**Pavyzdys - CreateResource:**
```c
// WDDM 2.0
HRESULT CreateResource(D3D12DDIARG_CREATERESOURCE* pArgs, ...);

// WDDM 3.0
HRESULT CreateResource(D3D12DDI_HRESOURCE hResource, D3D12DDIARG_CREATERESOURCE* pArgs);
```

## 🔍 D3D12 UMD - Privalomos Funkcijos (~80)

### Device Management (5)
- `pfnCalcPrivateDeviceSize`
- `pfnCreateDevice`
- `pfnDestroyDevice`
- `pfnCloseAdapter`
- `pfnGetSupportedVersions`

### Format & Resource Support (3)
- `pfnCheckFormatSupport`
- `pfnCheckMultisampleQualityLevels`
- `pfnGetMipPacking`

### Heap & Resource Management (8)
- `pfnCalcPrivateResourceSize`
- `pfnCalcPrivateHeapSize`
- `pfnCreateHeap`
- `pfnDestroyHeap`
- `pfnCreateResource`
- `pfnDestroyResource`
- `pfnMapHeap`
- `pfnUnmapHeap`
- `pfnMakeResident`
- `pfnEvict`

### Command Queue (3)
- `pfnCalcPrivateCommandQueueSize`
- `pfnCreateCommandQueue`
- `pfnDestroyCommandQueue`

### Command List (6)
- `pfnCalcPrivateCommandListSize`
- `pfnCreateCommandList`
- `pfnDestroyCommandList`
- `pfnCloseCommandList`
- `pfnResetCommandList`
- `pfnExecuteCommandLists`

### Fence & Synchronization (4)
- `pfnCalcPrivateFenceSize`
- `pfnCreateFence`
- `pfnDestroyFence`
- `pfnSetFenceValue`
- `pfnWaitForFence`

### Descriptor Heaps (6)
- `pfnCalcPrivateDescriptorHeapSize`
- `pfnCreateDescriptorHeap`
- `pfnDestroyDescriptorHeap`
- `pfnGetDescriptorSizeInBytes`
- `pfnGetCPUDescriptorHandleForHeapStart`
- `pfnGetGPUDescriptorHandleForHeapStart`

### Views (6)
- `pfnCreateShaderResourceView`
- `pfnCreateConstantBufferView`
- `pfnCreateUnorderedAccessView`
- `pfnCreateRenderTargetView`
- `pfnCreateDepthStencilView`
- `pfnCreateSampler`

### Descriptor Copying (2)
- `pfnCopyDescriptors`
- `pfnCopyDescriptorsSimple`

### Pipeline State (3)
- `pfnCalcPrivatePipelineStateSize`
- `pfnCreatePipelineState`
- `pfnDestroyPipelineState`

### Root Signature (3)
- `pfnCalcPrivateRootSignatureSize`
- `pfnCreateRootSignature`
- `pfnDestroyRootSignature`

### Shaders (8)
- `pfnCalcPrivateShaderSize`
- `pfnCreateVertexShader`
- `pfnCreatePixelShader`
- `pfnCreateGeometryShader`
- `pfnCreateComputeShader`
- `pfnCreateHullShader`
- `pfnCreateDomainShader`
- `pfnDestroyShader`

### Command Signature (3)
- `pfnCalcPrivateCommandSignatureSize`
- `pfnCreateCommandSignature`
- `pfnDestroyCommandSignature`

### Drawing & Dispatch (4)
- `pfnDrawInstanced`
- `pfnDrawIndexedInstanced`
- `pfnDispatch`
- `pfnExecuteIndirect`

### Resource Operations (2)
- `pfnResourceBarrier`
- `pfnResolveSubresource`

### Clear Operations (2)
- `pfnClearRenderTargetView`
- `pfnClearDepthStencilView`

### Pipeline State Setting (8)
- `pfnSetPipelineState`
- `pfnSetGraphicsRootSignature`
- `pfnSetComputeRootSignature`
- `pfnSetGraphicsRoot32BitConstant`
- `pfnSetComputeRoot32BitConstant`
- `pfnSetGraphicsRoot32BitConstants`
- `pfnSetComputeRoot32BitConstants`
- `pfnSetGraphicsRootDescriptorTable`
- `pfnSetComputeRootDescriptorTable`

### Input Assembly (3)
- `pfnIASetPrimitiveTopology`
- `pfnIASetVertexBuffers`
- `pfnIASetIndexBuffer`

### Output Merger (1)
- `pfnOMSetRenderTargets`

### Viewport & Scissor (2)
- `pfnSetViewport`
- `pfnSetScissorRect`

### Present (1)
- `pfnPresent`

### Memory Management (2)
- `pfnOfferResources`
- `pfnReclaimResources`

### Adapter Info (3)
- `pfnGetImplicitPhysicalAdapterMask`
- `pfnGetPresentPrivateDriverDataSize`
- `pfnQueryNodeMap`

## 📋 INF File Requirements

### Privalomi Skyriai
```inf
[Version]
Signature="$Windows NT$"
Class=Display
ClassGUID={4D36E968-E325-11CE-BFC1-08002BE10318}
DriverVer=xx/xx/xxxx,x.xx.xx.xx
CatalogFile=driver.CAT

[SourceDisksFiles]
atikmdag.sys = 1
amdbc250umd64.dll = 1

[AMD.NTamd64]
%DeviceDesc% = Install, PCI\VEN_1002&DEV_13FE

[Install]
CopyFiles = Miniport, UMD
AddReg = Registry

[Registry]
HKR,, InstalledDisplayDrivers, %REG_MULTI_SZ%, amdbc250umd64
HKR,, D3D12UMD, %REG_SZ%, amdbc250umd64.dll
HKR,, VgaCompatible, %REG_DWORD%, 0
```

## 🎯 Išvados ir Rekomendacijos

### Kodėl Nėra Atviro Kodo AMD Windows Driverių:
1. **AMD oficialiai nepalaiko** - uždaros komercinės tvarkyklės
2. **Microsoft WDK licencijos** - DDI headers yra proprietary
3. **Nėra GitHub repo** su Windows driver šaltiniais

### Geriausia Strategija BC-250 Projektui:
1. ✅ **Linux amdgpu** → Hardware sequences, register definitions
2. ✅ **Microsoft WDK headers** → DDI interfaces (`d3d12umddi.h`)
3. ✅ **Adrenalin 18.5.1** → Binary reference (DLL names, INF structure)
4. ✅ **Mesa RADV** → Shader compilation, command buffer logic

### Ką Toliau Daryti:
1. Naudoti veikiančią v4.1 UMD bazę
2. Palaipsniui pridėti daugiau D3D12 funkcijų
3. Testuoti su realiomis D3D12 aplikacijomis
4. Implementuoti command buffer execution
5. pridėti shader compilation support

## 📚 Nuorodos

- Microsoft D3D12 DDI Docs: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/
- Linux amdgpu Source: https://github.com/torvalds/linux/tree/master/drivers/gpu/drm/amd/amdgpu
- Mesa RADV: https://gitlab.freedesktop.org/mesa/mesa/-/tree/main/src/amd/vulkan
- BC-250 Community: https://elektricm.github.io/amd-bc250-docs/
- Microsoft Driver Samples: https://github.com/microsoft/graphics-driver-samples
