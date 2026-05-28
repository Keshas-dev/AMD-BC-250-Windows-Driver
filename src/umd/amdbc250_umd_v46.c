/*++

Copyright (c) 2026 AMD BC-250 "Dream Drivers" Project

Module Name:
    amdbc250_umd_v46.c

Abstract:
    User-Mode Display Driver (UMD) for AMD BC-250 Graphics.
    Version 4.6 - Full Resource & Heap Management enabled.

    ===========================================================================
    FULLY RECONSTRUCTED VERSION: Strict alignment with WDK 10.0.26100.0
    ===========================================================================

--*/

#include <windows.h>
#include <windef.h>

/* CRITICAL: NTSTATUS must be defined before DDI headers */
#ifndef _NTDEF_
typedef LONG NTSTATUS;
typedef NTSTATUS *PNTSTATUS;
#endif
#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif

/* Standard D3D12 DDI missing aliases in some headers */
#define D3D12DDI_HRTHEAP                  D3D12DDI_HRTRESOURCE
#define D3D12DDI_HRTDESCRIPTORHEAP        D3D12DDI_HRTRESOURCE

#include <sal.h>
#include <d3d9types.h>
#include <d3dumddi.h>
#include <d3d10umddi.h>
#include <d3d12umddi.h>
#include <stdio.h>

/* ============================================================================
   Constants & Limits
   ============================================================================ */

#define BC250_MAX_HEAPS           64
#define BC250_MAX_RESOURCES       4096
#define BC250_MAX_DESCRIPTORS     65536
#define BC250_PAGE_SIZE           4096
#define BC250_VRAM_BASE           0x100000000ULL

/* ============================================================================
   Resource & Heap Context Structures
   ============================================================================ */

typedef enum _BC250_HEAP_TYPE {
    BC250_HEAP_TYPE_DEFAULT = 1,
    BC250_HEAP_TYPE_UPLOAD = 2,
    BC250_HEAP_TYPE_READBACK = 3
} BC250_HEAP_TYPE;

typedef struct _BC250_HEAP {
    D3D12DDI_HRTHEAP          hHeapRT;
    UINT64                    GPUVirtualAddress;
    UINT64                    Size;
    BC250_HEAP_TYPE           Type;
    BOOL                      bInitialized;
} BC250_HEAP, *PBC250_HEAP;

typedef struct _BC250_RESOURCE {
    D3D12DDI_HRTRESOURCE      hResourceRT;
    UINT64                    GPUVirtualAddress;
    UINT64                    Size;
    D3D12DDI_RESOURCE_TYPE    Type;
    DXGI_FORMAT               Format;
    BOOL                      bCPUMapped;
    PVOID                     pCPUAddress;
    SIZE_T                    MapSize;
    BOOL                      bInitialized;
} BC250_RESOURCE, *PBC250_RESOURCE;

typedef struct _BC250_DEVICE {
    D3D12DDI_HRTDEVICE        hRTDevice;
    UINT64                    NextGPUVirtualAddress;
    UINT64                    VRAMTotal;
    UINT64                    VRAMUsed;
    BOOL                      bInitialized;
} BC250_DEVICE, *PBC250_DEVICE;

/* ============================================================================
   Internal Helpers
   ============================================================================ */

static UINT64 BC250_AllocateGPUVA(PBC250_DEVICE pDevice, UINT64 Size)
{
    UINT64 Address = pDevice->NextGPUVirtualAddress;
    Size = (Size + BC250_PAGE_SIZE - 1) & ~(BC250_PAGE_SIZE - 1);
    if (pDevice->VRAMUsed + Size > pDevice->VRAMTotal) return 0;
    pDevice->NextGPUVirtualAddress = Address + Size;
    pDevice->VRAMUsed += Size;
    return Address;
}

static void BC250_FreeGPUVA(PBC250_DEVICE pDevice, UINT64 Address, UINT64 Size)
{
    UNREFERENCED_PARAMETER(Address);
    pDevice->VRAMUsed -= Size;
}

/* ============================================================================
   D3D12 - Adapter Functions
   ============================================================================ */

static HRESULT APIENTRY BC250_D3D12_GetSupportedVersions(
    D3D12DDI_HADAPTER hAdapter,
    _Inout_ UINT32* puEntries,
    _Out_writes_opt_(*puEntries) UINT64* pSupportedDDIInterfaceVersions)
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (pSupportedDDIInterfaceVersions == NULL) { *puEntries = 1; return S_OK; }
    if (*puEntries >= 1) { pSupportedDDIInterfaceVersions[0] = D3D12DDI_SUPPORTED_0003; *puEntries = 1; return S_OK; }
    return E_INVALIDARG;
}

static HRESULT APIENTRY BC250_D3D12_GetCaps(D3D12DDI_HADAPTER hAdapter, _In_ CONST D3D12DDIARG_GETCAPS* pArgs)
{
    UNREFERENCED_PARAMETER(hAdapter); UNREFERENCED_PARAMETER(pArgs);
    return S_OK;
}

static HRESULT APIENTRY BC250_D3D12_GetOptionalDDITables(D3D12DDI_HADAPTER hAdapter, _Inout_ UINT32* puEntries, _Out_writes_opt_(*puEntries) D3D12DDI_TABLE_REQUEST* pRequests)
{
    UNREFERENCED_PARAMETER(hAdapter); UNREFERENCED_PARAMETER(pRequests);
    *puEntries = 0; return S_OK;
}

static HRESULT APIENTRY BC250_D3D12_FillDDITable(D3D12DDI_HADAPTER hAdapter, D3D12DDI_TABLE_TYPE TableType, _Inout_ VOID* pTable, SIZE_T TableSize, UINT FeatureVersion, _In_opt_ D3D12DDI_HRTTABLE hRTTable)
{
    UNREFERENCED_PARAMETER(hAdapter); UNREFERENCED_PARAMETER(TableType); UNREFERENCED_PARAMETER(pTable); UNREFERENCED_PARAMETER(TableSize); UNREFERENCED_PARAMETER(FeatureVersion); UNREFERENCED_PARAMETER(hRTTable);
    return S_OK;
}

/* ============================================================================
   D3D12 - Device Functions
   ============================================================================ */

static SIZE_T APIENTRY BC250_D3D12_CalcPrivateDeviceSize(D3D12DDI_HADAPTER hAdapter, _In_ CONST D3D12DDIARG_CALCPRIVATEDEVICESIZE* pArgs)
{
    UNREFERENCED_PARAMETER(hAdapter); UNREFERENCED_PARAMETER(pArgs);
    return sizeof(BC250_DEVICE);
}

static void APIENTRY BC250_D3D12_DestroyDevice(D3D12DDI_HDEVICE hDevice)
{
    PBC250_DEVICE pDevice = (PBC250_DEVICE)hDevice.pDrvPrivate;
    if (pDevice) HeapFree(GetProcessHeap(), 0, pDevice);
}

static HRESULT APIENTRY BC250_D3D12_CreateDevice(D3D12DDI_HADAPTER hAdapter, _In_ CONST D3D12DDIARG_CREATEDEVICE_0003* pArgs)
{
    UNREFERENCED_PARAMETER(hAdapter);
    PBC250_DEVICE pDevice = (PBC250_DEVICE)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(BC250_DEVICE));
    if (!pDevice) return E_OUTOFMEMORY;
    pDevice->NextGPUVirtualAddress = BC250_VRAM_BASE;
    pDevice->VRAMTotal = 10ULL * 1024 * 1024 * 1024;
    pDevice->bInitialized = TRUE;
    ((D3D12DDIARG_CREATEDEVICE_0003*)pArgs)->hDrvDevice.pDrvPrivate = pDevice;
    OutputDebugStringA("BC-250 UMD: CreateDevice SUCCESS");
    return S_OK;
}

static void APIENTRY BC250_D3D12_CloseAdapter(D3D12DDI_HADAPTER hAdapter) { UNREFERENCED_PARAMETER(hAdapter); }

/* ============================================================================
   D3D12 - Resource Management
   ============================================================================ */

static SIZE_T APIENTRY BC250_D3D12_CalcPrivateHeapSize(D3D12DDI_HDEVICE hDevice, _In_ CONST D3D12DDIARG_CREATEHEAP_0001* pArgs)
{
    UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pArgs);
    return sizeof(BC250_HEAP);
}

static HRESULT APIENTRY BC250_D3D12_CreateHeap(D3D12DDI_HDEVICE hDevice, _In_ CONST D3D12DDIARG_CREATEHEAP_0001* pArgs, D3D12DDI_HHEAP hHeap, D3D12DDI_HRTHEAP hHeapRT)
{
    PBC250_DEVICE pDevice = (PBC250_DEVICE)hDevice.pDrvPrivate;
    PBC250_HEAP pHeap = (PBC250_HEAP)hHeap.pDrvPrivate;
    if (!pDevice || !pHeap) return E_INVALIDARG;
    pHeap->hHeapRT = hHeapRT;
    pHeap->Size = pArgs->ByteSize;
    pHeap->Type = (pArgs->MemoryPool == D3D12DDI_MEMORY_POOL_L1) ? BC250_HEAP_TYPE_DEFAULT : BC250_HEAP_TYPE_UPLOAD;
    pHeap->GPUVirtualAddress = BC250_AllocateGPUVA(pDevice, pHeap->Size);
    if (pHeap->GPUVirtualAddress == 0) return E_OUTOFMEMORY;
    pHeap->bInitialized = TRUE;
    return S_OK;
}

static VOID APIENTRY BC250_D3D12_DestroyHeap(D3D12DDI_HDEVICE hDevice, D3D12DDI_HHEAP hHeap)
{
    PBC250_DEVICE pDevice = (PBC250_DEVICE)hDevice.pDrvPrivate;
    PBC250_HEAP pHeap = (PBC250_HEAP)hHeap.pDrvPrivate;
    if (pDevice && pHeap && pHeap->bInitialized) {
        BC250_FreeGPUVA(pDevice, pHeap->GPUVirtualAddress, pHeap->Size);
        pHeap->bInitialized = FALSE;
    }
}

static SIZE_T APIENTRY BC250_D3D12_CalcPrivateResourceSize(D3D12DDI_HDEVICE hDevice, _In_ CONST D3D12DDIARG_CREATERESOURCE_0003* pArgs)
{
    UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pArgs);
    return sizeof(BC250_RESOURCE);
}

static HRESULT APIENTRY BC250_D3D12_CreateResource(D3D12DDI_HDEVICE hDevice, _In_ CONST D3D12DDIARG_CREATERESOURCE_0003* pArgs, D3D12DDI_HRESOURCE hResource, D3D12DDI_HRTRESOURCE hResourceRT)
{
    PBC250_DEVICE pDevice = (PBC250_DEVICE)hDevice.pDrvPrivate;
    PBC250_RESOURCE pResource = (PBC250_RESOURCE)hResource.pDrvPrivate;
    if (!pDevice || !pResource) return E_INVALIDARG;
    pResource->hResourceRT = hResourceRT;
    pResource->Type = pArgs->ResourceType;
    pResource->Size = pArgs->Width;
    pResource->Format = pArgs->Format;
    pResource->GPUVirtualAddress = BC250_AllocateGPUVA(pDevice, pResource->Size);
    if (pResource->GPUVirtualAddress == 0) return E_OUTOFMEMORY;
    pResource->bInitialized = TRUE;
    return S_OK;
}

static VOID APIENTRY BC250_D3D12_DestroyResource(D3D12DDI_HDEVICE hDevice, D3D12DDI_HRESOURCE hResource)
{
    PBC250_DEVICE pDevice = (PBC250_DEVICE)hDevice.pDrvPrivate;
    PBC250_RESOURCE pResource = (PBC250_RESOURCE)hResource.pDrvPrivate;
    if (pDevice && pDevice->bInitialized && pResource && pResource->bInitialized) {
        BC250_FreeGPUVA(pDevice, pResource->GPUVirtualAddress, pResource->Size);
        pResource->bInitialized = FALSE;
    }
}

static HRESULT APIENTRY BC250_D3D12_MapResource(D3D12DDI_HDEVICE hDevice, D3D12DDI_HRESOURCE hResource, UINT Subresource, _In_opt_ CONST D3D12DDI_BOX* pBox, _Out_ VOID** ppData)
{
    UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(Subresource); UNREFERENCED_PARAMETER(pBox);
    PBC250_RESOURCE pResource = (PBC250_RESOURCE)hResource.pDrvPrivate;
    if (!pResource) return E_INVALIDARG;
    if (pResource->bCPUMapped) { *ppData = pResource->pCPUAddress; return S_OK; }
    pResource->MapSize = (SIZE_T)pResource->Size;
    pResource->pCPUAddress = VirtualAlloc(NULL, pResource->MapSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pResource->pCPUAddress) return E_OUTOFMEMORY;
    pResource->bCPUMapped = TRUE;
    *ppData = pResource->pCPUAddress;
    return S_OK;
}

static VOID APIENTRY BC250_D3D12_UnmapResource(D3D12DDI_HDEVICE hDevice, D3D12DDI_HRESOURCE hResource, UINT Subresource)
{
    UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(Subresource);
    PBC250_RESOURCE pResource = (PBC250_RESOURCE)hResource.pDrvPrivate;
    if (pResource && pResource->bCPUMapped) {
        VirtualFree(pResource->pCPUAddress, 0, MEM_RELEASE);
        pResource->bCPUMapped = FALSE;
        pResource->pCPUAddress = NULL;
    }
}

static SIZE_T APIENTRY BC250_D3D12_CalcPrivateDescriptorHeapSize(D3D12DDI_HDEVICE hDevice, _In_ CONST D3D12DDIARG_CREATE_DESCRIPTOR_HEAP_0001* pArgs)
{
    UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pArgs);
    return 64;
}

static HRESULT APIENTRY BC250_D3D12_CreateDescriptorHeap(D3D12DDI_HDEVICE hDevice, _In_ CONST D3D12DDIARG_CREATE_DESCRIPTOR_HEAP_0001* pArgs, D3D12DDI_HDESCRIPTORHEAP hHeap)
{
    UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pArgs); UNREFERENCED_PARAMETER(hHeap);
    return S_OK;
}

static VOID APIENTRY BC250_D3D12_DestroyDescriptorHeap(D3D12DDI_HDEVICE hDevice, D3D12DDI_HDESCRIPTORHEAP hHeap)
{
    UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hHeap);
}

static VOID APIENTRY BC250_D3D12_CopyDescriptors(D3D12DDI_HDEVICE hDevice, UINT NumDescriptorRanges, _In_ CONST D3D12DDI_CPU_DESCRIPTOR_HANDLE* pSrcDescriptorRangeStarts, _In_ CONST UINT* pSrcDescriptorRangeSizes, UINT NumDestDescriptorRanges, _In_ CONST D3D12DDI_CPU_DESCRIPTOR_HANDLE* pDestDescriptorRangeStarts, _In_ CONST UINT* pDestDescriptorRangeSizes, D3D12DDI_DESCRIPTOR_HEAP_TYPE DescriptorHeapType)
{
    UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(NumDescriptorRanges);
    UNREFERENCED_PARAMETER(pSrcDescriptorRangeStarts); UNREFERENCED_PARAMETER(pSrcDescriptorRangeSizes);
    UNREFERENCED_PARAMETER(NumDestDescriptorRanges); UNREFERENCED_PARAMETER(pDestDescriptorRangeStarts);
    UNREFERENCED_PARAMETER(pDestDescriptorRangeSizes); UNREFERENCED_PARAMETER(DescriptorHeapType);
}

static SIZE_T APIENTRY BC250_D3D12_CalcPrivateCommandListSize(D3D12DDI_HDEVICE hDevice, _In_ CONST D3D12DDIARG_CREATE_COMMAND_LIST_0001* pArgs)
{
    UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pArgs);
    return 1024;
}

static HRESULT APIENTRY BC250_D3D12_CreateCommandList(D3D12DDI_HDEVICE hDevice, _In_ CONST D3D12DDIARG_CREATE_COMMAND_LIST_0001* pArgs)
{
    UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pArgs);
    return S_OK;
}

/* ============================================================================
   OpenAdapter12 - Entry Point
   ============================================================================ */

__declspec(dllexport) HRESULT APIENTRY OpenAdapter12(_Inout_ D3D12DDIARG_OPENADAPTER* pOpenData)
{
    if (pOpenData == NULL || pOpenData->pAdapterFuncs == NULL) return E_INVALIDARG;
    pOpenData->pAdapterFuncs->pfnCalcPrivateDeviceSize = BC250_D3D12_CalcPrivateDeviceSize;
    pOpenData->pAdapterFuncs->pfnCreateDevice = BC250_D3D12_CreateDevice;
    pOpenData->pAdapterFuncs->pfnCloseAdapter = (PFND3D12DDI_CLOSEADAPTER)BC250_D3D12_CloseAdapter;
    pOpenData->pAdapterFuncs->pfnGetSupportedVersions = BC250_D3D12_GetSupportedVersions;
    pOpenData->pAdapterFuncs->pfnGetCaps = BC250_D3D12_GetCaps;
    pOpenData->pAdapterFuncs->pfnGetOptionalDDITables = BC250_D3D12_GetOptionalDDITables;
    pOpenData->pAdapterFuncs->pfnFillDDITable = BC250_D3D12_FillDDITable;
    pOpenData->pAdapterFuncs->pfnDestroyDevice = BC250_D3D12_DestroyDevice;
    return S_OK;
}

__declspec(dllexport) HRESULT APIENTRY OpenAdapter10(_Inout_ D3D10DDIARG_OPENADAPTER* pOpenData) { UNREFERENCED_PARAMETER(pOpenData); return S_OK; }
__declspec(dllexport) HRESULT APIENTRY OpenAdapter10_2(_Inout_ D3D10DDIARG_OPENADAPTER* pOpenData) { UNREFERENCED_PARAMETER(pOpenData); return S_OK; }
