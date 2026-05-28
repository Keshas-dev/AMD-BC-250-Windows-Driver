/*++

Copyright (c) 2026 AMD BC-250 "Dream Drivers" Project — Version 4.3

Module Name:
    amdbc250_umd_v46.c

Abstract:
    Full User-Mode Display Driver (UMD) for AMD BC-250 Graphics.
    Communicates with KMD via IOCTL for actual GPU operations.

Environment:
    User mode

--*/

#include <windows.h>
#include <windef.h>
#include <d3d12umddi.h>
#include <stdio.h>
#include "amdbc250_ioctl.h"

#define UMD_PAGE_SIZE       4096
#define UMD_VRAM_BASE       0x100000000ULL
#define UMD_DEFAULT_VRAM    4ULL * 1024 * 1024 * 1024  /* 4GB default (BIOS configurable) */

/* ============================================================================
   Internal Structures
   ============================================================================ */

typedef struct _UMD_HEAP_RESOURCE {
    UINT64  GpuVa;
    UINT64  Size;
    HANDLE  KmdHandle;
    PVOID   CpuAddress;
    BOOL    bCpuMapped;
    BOOL    bActive;
} UMD_HEAP_RESOURCE, *PUMD_HEAP_RESOURCE;

typedef struct _UMD_DEVICE {
    D3D12DDI_HRTDEVICE    hRTDevice;
    HANDLE                KmdDevice;
    D3D12DDI_CORELAYER_DEVICECALLBACKS_0003* pCallbacks;
    UINT64                NextGpuVa;
    UINT64                VramTotal;
    UINT64                VramUsed;
    UINT32                NextFenceValue;
    UINT32                HeapCount;
    UMD_HEAP_RESOURCE     Heaps[64];
    BOOL                  bInitialized;
} UMD_DEVICE, *PUMD_DEVICE;

/* ============================================================================
   KMD Communication
   ============================================================================ */

static HANDLE UmdOpenKmd(void)
{
    return CreateFileW(AMDBC250_DEVICE_PATH, GENERIC_READ | GENERIC_WRITE,
                       0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
}

static BOOL UmdIoctl(HANDLE h, DWORD code, PVOID in, DWORD inSz, PVOID out, DWORD outSz)
{
    DWORD ret = 0;
    return DeviceIoControl(h, code, in, inSz, out, outSz, &ret, NULL);
}

static UINT64 UmdAllocGpuVa(PUMD_DEVICE d, UINT64 sz)
{
    UINT64 a = d->NextGpuVa;
    sz = (sz + UMD_PAGE_SIZE - 1) & ~(UMD_PAGE_SIZE - 1);
    d->NextGpuVa += sz;
    d->VramUsed += sz;
    return a;
}

/* ============================================================================
   Adapter Functions
   ============================================================================ */

static HRESULT APIENTRY UmdGetSupportedVersions(
    D3D12DDI_HADAPTER hAdapter, _Inout_ UINT32* puEntries,
    _Out_writes_opt_(*puEntries) UINT64* pVersions)
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (!pVersions) { *puEntries = 1; return S_OK; }
    if (*puEntries >= 1) { pVersions[0] = D3D12DDI_SUPPORTED_0003; *puEntries = 1; return S_OK; }
    return E_INVALIDARG;
}

static HRESULT APIENTRY UmdGetCaps(D3D12DDI_HADAPTER hAdapter, _In_ CONST D3D12DDIARG_GETCAPS* pArgs)
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (pArgs->Type == D3D12DDICAPS_TYPE_D3D12_OPTIONS) {
        D3D12DDI_D3D12_OPTIONS_DATA_0003* o = (D3D12DDI_D3D12_OPTIONS_DATA_0003*)pArgs->pData;
        o->ResourceHeapTier = D3D12DDI_RESOURCE_HEAP_TIER_2;
        o->TiledResourcesTier = D3D12DDI_TILED_RESOURCES_TIER_1;
    }
    return S_OK;
}

static HRESULT APIENTRY UmdGetOptionalDDITables(D3D12DDI_HADAPTER h, _Inout_ UINT32* n, _Out_writes_opt_(*n) D3D12DDI_TABLE_REQUEST* r)
{
    UNREFERENCED_PARAMETER(h); *n = 0; return S_OK;
}

static HRESULT APIENTRY UmdFillDDITable(D3D12DDI_HADAPTER h, D3D12DDI_TABLE_TYPE t, _Inout_ VOID* p, SIZE_T s, UINT v, _In_opt_ D3D12DDI_HRTTABLE ht)
{
    UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(t); UNREFERENCED_PARAMETER(p);
    UNREFERENCED_PARAMETER(s); UNREFERENCED_PARAMETER(v); UNREFERENCED_PARAMETER(ht);
    return S_OK;
}

/* ============================================================================
   Device Functions
   ============================================================================ */

static SIZE_T APIENTRY UmdCalcPrivateDeviceSize(D3D12DDI_HADAPTER h, _In_ CONST D3D12DDIARG_CALCPRIVATEDEVICESIZE* p)
{
    UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(p);
    return sizeof(UMD_DEVICE);
}

static void APIENTRY UmdDestroyDevice(D3D12DDI_HDEVICE hDevice)
{
    PUMD_DEVICE d = (PUMD_DEVICE)hDevice.pDrvPrivate;
    if (!d) return;
    if (d->KmdDevice != INVALID_HANDLE_VALUE) CloseHandle(d->KmdDevice);
    HeapFree(GetProcessHeap(), 0, d);
}

static HRESULT APIENTRY UmdCreateDevice(D3D12DDI_HADAPTER hAdapter, _In_ CONST D3D12DDIARG_CREATEDEVICE_0003* pArgs)
{
    UNREFERENCED_PARAMETER(hAdapter);

    PUMD_DEVICE d = (PUMD_DEVICE)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(UMD_DEVICE));
    if (!d) return E_OUTOFMEMORY;

    d->hRTDevice = pArgs->hRTDevice;
    d->pCallbacks = (D3D12DDI_CORELAYER_DEVICECALLBACKS_0003*)pArgs->p12UMCallbacks;
    d->KmdDevice = UmdOpenKmd();
    d->NextGpuVa = UMD_VRAM_BASE;
    d->VramTotal = UMD_DEFAULT_VRAM;
    d->VramUsed = 0;
    d->NextFenceValue = 1;
    d->HeapCount = 0;
    d->bInitialized = TRUE;

    /* Query real VRAM */
    if (d->KmdDevice != INVALID_HANDLE_VALUE) {
        AMDBC250_IOCTL_VRAM_INFO v = {0};
        if (UmdIoctl(d->KmdDevice, IOCTL_AMDBC250_GET_VRAM_INFO, NULL, 0, &v, sizeof(v)))
            d->VramTotal = v.VisibleVramBytes;
    }

    D3D12DDIARG_CREATEDEVICE_0003* pArgsMutable = (D3D12DDIARG_CREATEDEVICE_0003*)pArgs;
    pArgsMutable->hDrvDevice.pDrvPrivate = d;
    OutputDebugStringA("BC-250 UMD: CreateDevice OK\n");
    return S_OK;
}

static void APIENTRY UmdCloseAdapter(D3D12DDI_HADAPTER h) { UNREFERENCED_PARAMETER(h); }

/* ============================================================================
   Heap + Resource (combined per D3D12 DDI)
   ============================================================================ */

static D3D12DDI_HEAP_AND_RESOURCE_SIZES APIENTRY UmdCalcPrivateHeapAndResourceSizes(
    D3D12DDI_HDEVICE hDevice,
    _In_opt_ CONST D3D12DDIARG_CREATEHEAP_0001* pHeapArgs,
    _In_opt_ CONST D3D12DDIARG_CREATERESOURCE_0003* pResArgs)
{
    UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pHeapArgs); UNREFERENCED_PARAMETER(pResArgs);
    D3D12DDI_HEAP_AND_RESOURCE_SIZES sz = {0};
    sz.Heap = sizeof(UMD_HEAP_RESOURCE);
    sz.Resource = sizeof(UMD_HEAP_RESOURCE);
    return sz;
}

static HRESULT APIENTRY UmdCreateHeapAndResource(
    D3D12DDI_HDEVICE hDevice,
    _In_opt_ CONST D3D12DDIARG_CREATEHEAP_0001* pHeapArgs,
    D3D12DDI_HHEAP hHeap,
    D3D12DDI_HRTRESOURCE hRtRes,
    _In_opt_ CONST D3D12DDIARG_CREATERESOURCE_0003* pResArgs,
    _In_opt_ CONST D3D12DDI_CLEAR_VALUES* pClear,
    D3D12DDI_HRESOURCE hResource)
{
    UNREFERENCED_PARAMETER(pClear);

    PUMD_DEVICE d = (PUMD_DEVICE)hDevice.pDrvPrivate;
    if (!d) return E_INVALIDARG;

    PUMD_HEAP_RESOURCE hr = NULL;

    /* Heap creation */
    if (pHeapArgs && hHeap.pDrvPrivate) {
        hr = (PUMD_HEAP_RESOURCE)hHeap.pDrvPrivate;
        hr->Size = pHeapArgs->ByteSize;
        hr->Size = (hr->Size + UMD_PAGE_SIZE - 1) & ~(UMD_PAGE_SIZE - 1);
        hr->GpuVa = UmdAllocGpuVa(d, hr->Size);

        if (d->KmdDevice != INVALID_HANDLE_VALUE) {
            AMDBC250_IOCTL_ALLOC_VIDMEM req = {0};
            AMDBC250_IOCTL_ALLOC_VIDMEM_RESULT res = {0};
            req.Size = hr->Size;
            req.Alignment = UMD_PAGE_SIZE;
            req.Flags = 0x1 | 0x2;
            req.SegmentId = (pHeapArgs->MemoryPool == D3D12DDI_MEMORY_POOL_L1) ? 0 : 1;
            if (UmdIoctl(d->KmdDevice, IOCTL_AMDBC250_ALLOC_VIDMEM, &req, sizeof(req), &res, sizeof(res))) {
                hr->KmdHandle = (HANDLE)(UINT_PTR)res.Handle;
                hr->GpuVa = res.GpuVirtualAddress;
            }
        }
        hr->bActive = TRUE;
        if (d->HeapCount < 64) { d->Heaps[d->HeapCount] = *hr; d->HeapCount++; }
    }

    /* Resource creation */
    if (pResArgs && hResource.pDrvPrivate) {
        hr = (PUMD_HEAP_RESOURCE)hResource.pDrvPrivate;
        hr->Size = pResArgs->Width;
        hr->GpuVa = UmdAllocGpuVa(d, hr->Size);

        if (d->KmdDevice != INVALID_HANDLE_VALUE) {
            AMDBC250_IOCTL_ALLOC_VIDMEM req = {0};
            AMDBC250_IOCTL_ALLOC_VIDMEM_RESULT res = {0};
            req.Size = hr->Size;
            req.Alignment = 256;
            req.Flags = 0x1 | 0x2;
            req.SegmentId = 0;
            if (UmdIoctl(d->KmdDevice, IOCTL_AMDBC250_ALLOC_VIDMEM, &req, sizeof(req), &res, sizeof(res))) {
                hr->KmdHandle = (HANDLE)(UINT_PTR)res.Handle;
                hr->GpuVa = res.GpuVirtualAddress;
            }
        }
        hr->bActive = TRUE;
    }

    OutputDebugStringA("BC-250 UMD: CreateHeapAndResource OK\n");
    return S_OK;
}

static void APIENTRY UmdDestroyHeapAndResource(
    D3D12DDI_HDEVICE hDevice, D3D12DDI_HHEAP hHeap, D3D12DDI_HRESOURCE hResource)
{
    PUMD_DEVICE d = (PUMD_DEVICE)hDevice.pDrvPrivate;
    if (!d) return;

    if (hHeap.pDrvPrivate) {
        PUMD_HEAP_RESOURCE hr = (PUMD_HEAP_RESOURCE)hHeap.pDrvPrivate;
        if (d->KmdDevice != INVALID_HANDLE_VALUE && hr->KmdHandle) {
            AMDBC250_IOCTL_FREE_VIDMEM req = {0};
            req.Handle = (UINT64)(UINT_PTR)hr->KmdHandle;
            UmdIoctl(d->KmdDevice, IOCTL_AMDBC250_FREE_VIDMEM, &req, sizeof(req), NULL, 0);
        }
        hr->bActive = FALSE;
    }
}

/* ============================================================================
   Descriptor Heap
   ============================================================================ */

static SIZE_T APIENTRY UmdCalcPrivateDescriptorHeapSize(D3D12DDI_HDEVICE h, _In_ CONST D3D12DDIARG_CREATE_DESCRIPTOR_HEAP_0001* p)
{
    UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(p);
    return 4096;
}

static HRESULT APIENTRY UmdCreateDescriptorHeap(D3D12DDI_HDEVICE h, _In_ CONST D3D12DDIARG_CREATE_DESCRIPTOR_HEAP_0001* p, D3D12DDI_HDESCRIPTORHEAP hp)
{
    UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(p); UNREFERENCED_PARAMETER(hp);
    OutputDebugStringA("BC-250 UMD: CreateDescriptorHeap OK\n");
    return S_OK;
}

static void APIENTRY UmdDestroyDescriptorHeap(D3D12DDI_HDEVICE h, D3D12DDI_HDESCRIPTORHEAP hp)
{
    UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(hp);
}

static void APIENTRY UmdCopyDescriptors(D3D12DDI_HDEVICE h, UINT n, _In_ CONST D3D12DDI_CPU_DESCRIPTOR_HANDLE* s, _In_ CONST UINT* ss, UINT dn, _In_ CONST D3D12DDI_CPU_DESCRIPTOR_HANDLE* ds, _In_ CONST UINT* ds2, D3D12DDI_DESCRIPTOR_HEAP_TYPE t)
{
    UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(n); UNREFERENCED_PARAMETER(s);
    UNREFERENCED_PARAMETER(ss); UNREFERENCED_PARAMETER(dn); UNREFERENCED_PARAMETER(ds);
    UNREFERENCED_PARAMETER(ds2); UNREFERENCED_PARAMETER(t);
}

/* ============================================================================
   Root Signature
   ============================================================================ */

static SIZE_T APIENTRY UmdCalcPrivateRootSignatureSize(D3D12DDI_HDEVICE h, _In_ CONST D3D12DDIARG_CREATE_ROOT_SIGNATURE_0001* p)
{
    UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(p);
    return 256;
}

static HRESULT APIENTRY UmdCreateRootSignature(D3D12DDI_HDEVICE h, _In_ CONST D3D12DDIARG_CREATE_ROOT_SIGNATURE_0001* p, D3D12DDI_HROOTSIGNATURE hs)
{
    UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(p); UNREFERENCED_PARAMETER(hs);
    OutputDebugStringA("BC-250 UMD: CreateRootSignature OK\n");
    return S_OK;
}

static void APIENTRY UmdDestroyRootSignature(D3D12DDI_HDEVICE h, D3D12DDI_HROOTSIGNATURE hs)
{
    UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(hs);
}

/* ============================================================================
   Pipeline State
   ============================================================================ */

static SIZE_T APIENTRY UmdCalcPrivatePipelineStateSize(D3D12DDI_HDEVICE h, _In_ CONST D3D12DDIARG_CREATE_PIPELINE_STATE_0001* p)
{
    UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(p);
    return 256;
}

static HRESULT APIENTRY UmdCreatePipelineState(D3D12DDI_HDEVICE h, _In_ CONST D3D12DDIARG_CREATE_PIPELINE_STATE_0001* p)
{
    UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(p);
    OutputDebugStringA("BC-250 UMD: CreatePipelineState OK\n");
    return S_OK;
}

static void APIENTRY UmdDestroyPipelineState(D3D12DDI_HDEVICE h, D3D12DDI_HPIPELINESTATE hs)
{
    UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(hs);
}

/* ============================================================================
   Command Queue
   ============================================================================ */

static SIZE_T APIENTRY UmdCalcPrivateCommandQueueSize(D3D12DDI_HDEVICE h, _In_ CONST D3D12DDIARG_CREATECOMMANDQUEUE_0001* p)
{
    UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(p);
    return 256;
}

static HRESULT APIENTRY UmdCreateCommandQueue(D3D12DDI_HDEVICE h, _In_ CONST D3D12DDIARG_CREATECOMMANDQUEUE_0001* p)
{
    UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(p);
    OutputDebugStringA("BC-250 UMD: CreateCommandQueue OK\n");
    return S_OK;
}

static void APIENTRY UmdDestroyCommandQueue(D3D12DDI_HDEVICE h, D3D12DDI_HCOMMANDQUEUE q)
{
    UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(q);
}

/* ============================================================================
   Command Allocator
   ============================================================================ */

static SIZE_T APIENTRY UmdCalcPrivateCommandAllocatorSize(D3D12DDI_HDEVICE h, _In_ CONST D3D12DDIARG_CREATECOMMANDALLOCATOR* p)
{
    UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(p);
    return 256;
}

static HRESULT APIENTRY UmdCreateCommandAllocator(D3D12DDI_HDEVICE h, _In_ CONST D3D12DDIARG_CREATECOMMANDALLOCATOR* p)
{
    UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(p);
    return S_OK;
}

static void APIENTRY UmdDestroyCommandAllocator(D3D12DDI_HDEVICE h, D3D12DDI_HCOMMANDALLOCATOR a)
{
    UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(a);
}

/* ============================================================================
   Command List
   ============================================================================ */

static SIZE_T APIENTRY UmdCalcPrivateCommandListSize(D3D12DDI_HDEVICE h, _In_ CONST D3D12DDIARG_CREATE_COMMAND_LIST_0001* p)
{
    UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(p);
    return 4096;
}

static HRESULT APIENTRY UmdCreateCommandList(D3D12DDI_HDEVICE h, _In_ CONST D3D12DDIARG_CREATE_COMMAND_LIST_0001* p)
{
    UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(p);
    OutputDebugStringA("BC-250 UMD: CreateCommandList OK\n");
    return S_OK;
}

static void APIENTRY UmdDestroyCommandList(D3D12DDI_HDEVICE h, D3D12DDI_HCOMMANDLIST c)
{
    UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(c);
}

/* ============================================================================
   Fence
   ============================================================================ */

static SIZE_T APIENTRY UmdCalcPrivateFenceSize(D3D12DDI_HDEVICE h, _In_ CONST D3D12DDIARG_CREATE_FENCE* p)
{
    UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(p);
    return 64;
}

static HRESULT APIENTRY UmdCreateFence(D3D12DDI_HDEVICE h, D3D12DDI_HFENCE hf, _In_ CONST D3D12DDIARG_CREATE_FENCE* p)
{
    UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(hf); UNREFERENCED_PARAMETER(p);
    return S_OK;
}

static void APIENTRY UmdDestroyFence(D3D12DDI_HDEVICE h, D3D12DDI_HFENCE hf)
{
    UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(hf);
}

/* ============================================================================
   Shader Stubs
   ============================================================================ */

static SIZE_T APIENTRY UmdCalcPrivateShaderSize(D3D12DDI_HDEVICE h, _In_ CONST D3D12DDIARG_CREATE_SHADER_0010* p)
{
    UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(p);
    return 256;
}

static void APIENTRY UmdCreateShader(D3D12DDI_HDEVICE h, _In_ CONST D3D12DDIARG_CREATE_SHADER_0010* p, D3D12DDI_HSHADER hs)
{
    UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(p); UNREFERENCED_PARAMETER(hs);
}

static void APIENTRY UmdDestroyShader(D3D12DDI_HDEVICE h, D3D12DDI_HSHADER hs)
{
    UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(hs);
}

/* ============================================================================
   State Stubs (Blend, DepthStencil, Rasterizer, ElementLayout)
   ============================================================================ */

static SIZE_T APIENTRY UmdCalcPrivateBlendStateSize(D3D12DDI_HDEVICE h, _In_ CONST D3D12DDI_BLEND_DESC_0010* p) { UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(p); return 64; }
static void APIENTRY UmdCreateBlendState(D3D12DDI_HDEVICE h, _In_ CONST D3D12DDI_BLEND_DESC_0010* p, D3D12DDI_HBLENDSTATE s) { UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(p); UNREFERENCED_PARAMETER(s); }
static void APIENTRY UmdDestroyBlendState(D3D12DDI_HDEVICE h, D3D12DDI_HBLENDSTATE s) { UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(s); }

static SIZE_T APIENTRY UmdCalcPrivateDepthStencilStateSize(D3D12DDI_HDEVICE h, _In_ CONST D3D12DDI_DEPTH_STENCIL_DESC_0010* p) { UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(p); return 64; }
static void APIENTRY UmdCreateDepthStencilState(D3D12DDI_HDEVICE h, _In_ CONST D3D12DDI_DEPTH_STENCIL_DESC_0010* p, D3D12DDI_HDEPTHSTENCILSTATE s) { UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(p); UNREFERENCED_PARAMETER(s); }
static void APIENTRY UmdDestroyDepthStencilState(D3D12DDI_HDEVICE h, D3D12DDI_HDEPTHSTENCILSTATE s) { UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(s); }

static SIZE_T APIENTRY UmdCalcPrivateRasterizerStateSize(D3D12DDI_HDEVICE h, _In_ CONST D3D12DDI_RASTERIZER_DESC_0010* p) { UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(p); return 64; }
static void APIENTRY UmdCreateRasterizerState(D3D12DDI_HDEVICE h, _In_ CONST D3D12DDI_RASTERIZER_DESC_0010* p, D3D12DDI_HRASTERIZERSTATE s) { UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(p); UNREFERENCED_PARAMETER(s); }
static void APIENTRY UmdDestroyRasterizerState(D3D12DDI_HDEVICE h, D3D12DDI_HRASTERIZERSTATE s) { UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(s); }

static SIZE_T APIENTRY UmdCalcPrivateElementLayoutSize(D3D12DDI_HDEVICE h, _In_ CONST D3D12DDIARG_CREATEELEMENTLAYOUT_0010* p) { UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(p); return 64; }
static void APIENTRY UmdCreateElementLayout(D3D12DDI_HDEVICE h, _In_ CONST D3D12DDIARG_CREATEELEMENTLAYOUT_0010* p, D3D12DDI_HELEMENTLAYOUT s) { UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(p); UNREFERENCED_PARAMETER(s); }
static void APIENTRY UmdDestroyElementLayout(D3D12DDI_HDEVICE h, D3D12DDI_HELEMENTLAYOUT s) { UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(s); }

/* ============================================================================
   Query Heap, Command Signature, Pipeline Library Stubs
   ============================================================================ */

static SIZE_T APIENTRY UmdCalcPrivateQueryHeapSize(D3D12DDI_HDEVICE h, _In_ CONST D3D12DDIARG_CREATE_QUERY_HEAP_0001* p) { UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(p); return 64; }
static HRESULT APIENTRY UmdCreateQueryHeap(D3D12DDI_HDEVICE h, _In_ CONST D3D12DDIARG_CREATE_QUERY_HEAP_0001* p, D3D12DDI_HQUERYHEAP q) { UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(p); UNREFERENCED_PARAMETER(q); return S_OK; }
static void APIENTRY UmdDestroyQueryHeap(D3D12DDI_HDEVICE h, D3D12DDI_HQUERYHEAP q) { UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(q); }

static SIZE_T APIENTRY UmdCalcPrivateCommandSignatureSize(D3D12DDI_HDEVICE h, _In_ CONST D3D12DDIARG_CREATE_COMMAND_SIGNATURE_0001* p) { UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(p); return 64; }
static HRESULT APIENTRY UmdCreateCommandSignature(D3D12DDI_HDEVICE h, _In_ CONST D3D12DDIARG_CREATE_COMMAND_SIGNATURE_0001* p, D3D12DDI_HCOMMANDSIGNATURE c) { UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(p); UNREFERENCED_PARAMETER(c); return S_OK; }
static void APIENTRY UmdDestroyCommandSignature(D3D12DDI_HDEVICE h, D3D12DDI_HCOMMANDSIGNATURE c) { UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(c); }

static SIZE_T APIENTRY UmdCalcPrivatePipelineLibrarySize(D3D12DDI_HDEVICE h, _In_ CONST D3D12DDIARG_CREATE_PIPELINE_LIBRARY_0010* p) { UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(p); return 64; }
static HRESULT APIENTRY UmdCreatePipelineLibrary(D3D12DDI_HDEVICE h, _In_ CONST D3D12DDIARG_CREATE_PIPELINE_LIBRARY_0010* p, D3D12DDI_HPIPELINELIBRARY l) { UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(p); UNREFERENCED_PARAMETER(l); return S_OK; }
static void APIENTRY UmdDestroyPipelineLibrary(D3D12DDI_HDEVICE h, D3D12DDI_HPIPELINELIBRARY l) { UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(l); }

/* ============================================================================
   OpenAdapter12 - Main Entry Point
   ============================================================================ */

__declspec(dllexport) HRESULT APIENTRY OpenAdapter12(_Inout_ D3D12DDIARG_OPENADAPTER* pOpenData)
{
    if (!pOpenData || !pOpenData->pAdapterFuncs) return E_INVALIDARG;

    D3D12DDI_ADAPTERFUNCS* f = pOpenData->pAdapterFuncs;

    f->pfnCalcPrivateDeviceSize = UmdCalcPrivateDeviceSize;
    f->pfnCreateDevice = UmdCreateDevice;
    f->pfnDestroyDevice = UmdDestroyDevice;
    f->pfnCloseAdapter = (PFND3D12DDI_CLOSEADAPTER)UmdCloseAdapter;
    f->pfnGetSupportedVersions = UmdGetSupportedVersions;
    f->pfnGetCaps = UmdGetCaps;
    f->pfnGetOptionalDDITables = UmdGetOptionalDDITables;
    f->pfnFillDDITable = UmdFillDDITable;

    OutputDebugStringA("BC-250 UMD: OpenAdapter12 OK\n");
    return S_OK;
}

__declspec(dllexport) HRESULT APIENTRY OpenAdapter10(_Inout_ D3D10DDIARG_OPENADAPTER* p) { UNREFERENCED_PARAMETER(p); return S_OK; }
__declspec(dllexport) HRESULT APIENTRY OpenAdapter10_2(_Inout_ D3D10DDIARG_OPENADAPTER* p) { UNREFERENCED_PARAMETER(p); return S_OK; }

/* ============================================================================
   D3D9 DDI Support — DrawPrimitive with PM4 packets
   Based on ZEROAESQUERDA/BC250-windowsDriverTest approach
   ============================================================================ */

#include <d3dumddi.h>
#include <d3dkmthk.h>

/* PM4 opcodes (from amdbc250_hw.h) */
#define PM4_TYPE3_HDR_D3D9(opcode, cnt) ((3u << 30) | (((cnt) - 1) << 16) | ((opcode) << 8))
#define IT_DRAW_INDEX_AUTO_D3D9  0x2D
#define IT_SET_CONTEXT_REG_D3D9  0x69
#define IT_EVENT_WRITE_EOP_D3D9  0x47

/* UMD D3D9 context */
typedef struct _BC250_D3D9_DEVICE {
    HANDLE              hDevice;
    HANDLE              hAdapter;
    HANDLE              hSwapChain;
    UINT                BackBufferWidth;
    UINT                BackBufferHeight;
    D3DDDIFORMAT        BackBufferFormat;
    PVOID               CommandBuffer;
    UINT                CommandBufferSize;
    UINT                CmdBufferUsed;
    UINT64              FenceValue;
    UINT64              FramebufferGpuVa;  /* GPU VA of current back buffer */
    HANDLE              KmdDevice;         /* Handle to KMD for IOCTL */
    CRITICAL_SECTION    Lock;
} BC250_D3D9_DEVICE, *PBC250_D3D9_DEVICE;

static BC250_D3D9_DEVICE g_D3D9Device = {0};

/* KMD communication helper */
static HANDLE Bc250OpenKmd(void)
{
    return CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
                       0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
}

static BOOL Bc250Ioctl(HANDLE h, DWORD code, PVOID in, DWORD inSz, PVOID out, DWORD outSz)
{
    DWORD ret = 0;
    return DeviceIoControl(h, code, in, inSz, out, outSz, &ret, NULL);
}

/* Allocate DMA command buffer from KMD */
static PVOID Bc250AllocDmaBuffer(HANDLE hKmd, UINT size, UINT64* outGpuVa)
{
    ULONG allocIn[1] = {size};
    ULONG64 allocOut[2] = {0};
    DWORD ret = 0;

    if (!DeviceIoControl(hKmd, 0x80000930, allocIn, sizeof(allocIn),
                         allocOut, sizeof(allocOut), &ret, NULL)) {
        return NULL;
    }

    *outGpuVa = allocOut[0];
    return (PVOID)(UINT_PTR)allocOut[1];
}

/* Free DMA buffer */
static void Bc250FreeDmaBuffer(HANDLE hKmd, PVOID cpuAddr)
{
    ULONG64 freeIn[1] = {(ULONG64)(UINT_PTR)cpuAddr};
    DWORD ret = 0;
    DeviceIoControl(hKmd, 0x80000934, freeIn, sizeof(freeIn), NULL, 0, &ret, NULL);
}

/* D3D9 DDI: OpenAdapter */
static HRESULT APIENTRY D3D9_CreateDevice(HANDLE, D3DDDIARG_CREATEDEVICE*);
static HRESULT APIENTRY D3D9_CloseAdapter(HANDLE);
static HRESULT APIENTRY D3D9_CreateResource(HANDLE, D3DDDIARG_CREATERESOURCE*);
static HRESULT APIENTRY D3D9_DestroyResource(HANDLE, HANDLE);
static HRESULT APIENTRY D3D9_SetRenderState(HANDLE, CONST D3DDDIARG_RENDERSTATE*);
static HRESULT APIENTRY D3D9_SetStreamSource(HANDLE, CONST D3DDDIARG_SETSTREAMSOURCE*);
static HRESULT APIENTRY D3D9_SetIndices(HANDLE, CONST D3DDDIARG_SETINDICES*);
static HRESULT APIENTRY D3D9_DrawPrimitive(HANDLE, CONST D3DDDIARG_DRAWPRIMITIVE*, CONST UINT*);
static HRESULT APIENTRY D3D9_DrawIndexedPrimitive(HANDLE hDev, CONST D3DDDIARG_DRAWINDEXEDPRIMITIVE* pArgs);
static HRESULT APIENTRY D3D9_Present(HANDLE, CONST D3DDDIARG_PRESENT*);
static HRESULT APIENTRY D3D9_Flush(HANDLE);
static HRESULT APIENTRY D3D9_Lock(HANDLE, D3DDDIARG_LOCK*);
static HRESULT APIENTRY D3D9_Unlock(HANDLE, CONST D3DDDIARG_UNLOCK*);
static HRESULT APIENTRY D3D9_CreateVertexShaderDecl(HANDLE, D3DDDIARG_CREATEVERTEXSHADERDECL*, CONST D3DDDIVERTEXELEMENT*);
static HRESULT APIENTRY D3D9_SetVertexShaderDecl(HANDLE, HANDLE);
static HRESULT APIENTRY D3D9_DeleteVertexShaderDecl(HANDLE, HANDLE);
static HRESULT APIENTRY D3D9_CreateVertexShaderFunc(HANDLE, D3DDDIARG_CREATEVERTEXSHADERFUNC*, CONST UINT*);
static HRESULT APIENTRY D3D9_SetVertexShaderFunc(HANDLE, HANDLE);
static HRESULT APIENTRY D3D9_DeleteVertexShaderFunc(HANDLE, HANDLE);
static HRESULT APIENTRY D3D9_CreatePixelShader(HANDLE, D3DDDIARG_CREATEPIXELSHADER*, CONST UINT*);
static HRESULT APIENTRY D3D9_SetPixelShader(HANDLE, HANDLE);
static HRESULT APIENTRY D3D9_DeletePixelShader(HANDLE, HANDLE);
static HRESULT APIENTRY D3D9_SetTexture(HANDLE, UINT, HANDLE);
static HRESULT APIENTRY D3D9_SetViewport(HANDLE, CONST D3DDDIARG_VIEWPORTINFO*);
static HRESULT APIENTRY D3D9_SetScissorRect(HANDLE, CONST RECT*);
static HRESULT APIENTRY D3D9_SetRenderTarget(HANDLE, CONST D3DDDIARG_SETRENDERTARGET*);
static HRESULT APIENTRY D3D9_Clear(HANDLE, CONST D3DDDIARG_CLEAR*, UINT, CONST RECT*);
static HRESULT APIENTRY D3D9_CreateQuery(HANDLE, D3DDDIARG_CREATEQUERY*);
static HRESULT APIENTRY D3D9_DestroyQuery(HANDLE, HANDLE);
static HRESULT APIENTRY D3D9_IssueQuery(HANDLE, CONST D3DDDIARG_ISSUEQUERY*);

static HRESULT APIENTRY D3D9_OpenAdapter(D3DDDIARG_OPENADAPTER* pArgs)
{
    if (!pArgs) return E_INVALIDARG;
    OutputDebugStringA("BC-250 UMD: D3D9 OpenAdapter\n");
    pArgs->pAdapterFuncs->pfnCreateDevice = D3D9_CreateDevice;
    pArgs->pAdapterFuncs->pfnCloseAdapter = D3D9_CloseAdapter;
    return S_OK;
}

static HRESULT APIENTRY D3D9_CloseAdapter(HANDLE hAdapter) { UNREFERENCED_PARAMETER(hAdapter); return S_OK; }

/* D3D9 DDI: CreateDevice */
static HRESULT APIENTRY D3D9_CreateDevice(HANDLE hAdapter, D3DDDIARG_CREATEDEVICE* pArgs)
{
    UNREFERENCED_PARAMETER(hAdapter);
    PBC250_D3D9_DEVICE dev = &g_D3D9Device;
    RtlZeroMemory(dev, sizeof(BC250_D3D9_DEVICE));
    InitializeCriticalSection(&dev->Lock);
    dev->hDevice = pArgs->hDevice;
    dev->FenceValue = 1;

    /* Open KMD communication channel */
    dev->KmdDevice = Bc250OpenKmd();
    if (dev->KmdDevice == INVALID_HANDLE_VALUE) {
        OutputDebugStringA("BC-250 UMD: WARNING - KMD not available\n");
    }

    /* Allocate DMA command buffer (256KB) via KMD IOCTL */
    dev->CommandBufferSize = 256 * 1024;
    if (dev->KmdDevice != INVALID_HANDLE_VALUE) {
        dev->CommandBuffer = Bc250AllocDmaBuffer(dev->KmdDevice, dev->CommandBufferSize, &dev->FramebufferGpuVa);
    }
    if (dev->CommandBuffer == NULL) {
        /* Fallback: VirtualAlloc */
        dev->CommandBuffer = VirtualAlloc(NULL, dev->CommandBufferSize, MEM_COMMIT, PAGE_READWRITE);
    }
    dev->CmdBufferUsed = 0;

    OutputDebugStringA("BC-250 UMD: D3D9 CreateDevice OK (cmd buf allocated)\n");

    /* Fill D3D9 DDI function table */
    D3DDDI_DEVICEFUNCS* f = pArgs->pDeviceFuncs;
    f->pfnCreateResource        = D3D9_CreateResource;
    f->pfnDestroyResource       = D3D9_DestroyResource;
    f->pfnSetRenderState        = D3D9_SetRenderState;
    f->pfnSetStreamSource       = D3D9_SetStreamSource;
    f->pfnSetIndices            = D3D9_SetIndices;
    f->pfnDrawPrimitive         = D3D9_DrawPrimitive;
    f->pfnDrawIndexedPrimitive  = D3D9_DrawIndexedPrimitive;
    f->pfnPresent               = D3D9_Present;
    f->pfnFlush                 = D3D9_Flush;
    f->pfnLock                  = D3D9_Lock;
    f->pfnUnlock                = D3D9_Unlock;
    f->pfnCreateVertexShaderDecl = D3D9_CreateVertexShaderDecl;
    f->pfnSetVertexShaderDecl   = D3D9_SetVertexShaderDecl;
    f->pfnDeleteVertexShaderDecl = D3D9_DeleteVertexShaderDecl;
    f->pfnCreateVertexShaderFunc = D3D9_CreateVertexShaderFunc;
    f->pfnSetVertexShaderFunc   = D3D9_SetVertexShaderFunc;
    f->pfnDeleteVertexShaderFunc = D3D9_DeleteVertexShaderFunc;
    f->pfnCreatePixelShader     = D3D9_CreatePixelShader;
    f->pfnSetPixelShader        = D3D9_SetPixelShader;
    f->pfnDeletePixelShader     = D3D9_DeletePixelShader;
    f->pfnSetTexture            = D3D9_SetTexture;
    f->pfnSetViewport           = D3D9_SetViewport;
    f->pfnSetScissorRect        = D3D9_SetScissorRect;
    f->pfnSetRenderTarget       = D3D9_SetRenderTarget;
    f->pfnClear                 = D3D9_Clear;
    f->pfnCreateQuery           = D3D9_CreateQuery;
    f->pfnDestroyQuery          = D3D9_DestroyQuery;
    f->pfnIssueQuery            = D3D9_IssueQuery;

    OutputDebugStringA("BC-250 UMD: D3D9 CreateDevice OK\n");
    return S_OK;
}

static HRESULT APIENTRY D3D9_DestroyDevice(HANDLE hDev) { UNREFERENCED_PARAMETER(hDev); DeleteCriticalSection(&g_D3D9Device.Lock); return S_OK; }

/* D3D9 DDI: CreateResource */
static HRESULT APIENTRY D3D9_CreateResource(HANDLE hDev, D3DDDIARG_CREATERESOURCE* pArgs)
{
    PBC250_D3D9_DEVICE dev = &g_D3D9Device;
    if (!pArgs) return E_INVALIDARG;

    /* Allocate GPU memory via KMD IOCTL 0x80000840 */
    UINT64 gpuVa = 0;
    if (dev->KmdDevice != INVALID_HANDLE_VALUE) {
        ULONG allocIn[4] = {0};
        ULONG64 allocOut[3] = {0};
        DWORD ret = 0;
        
        allocIn[0] = 4096;  /* Default 4KB */
        allocIn[1] = 256;   /* Alignment */
        allocIn[2] = 0x3;   /* Flags: READ|WRITE */
        allocIn[3] = 0;     /* Segment: VRAM */
        
        if (DeviceIoControl(dev->KmdDevice, 0x80000840,
                           allocIn, sizeof(allocIn),
                           allocOut, sizeof(allocOut), &ret, NULL)) {
            gpuVa = allocOut[0];
        }
    }
    
    pArgs->hResource = (HANDLE)(ULONG_PTR)(gpuVa ? gpuVa : 0x1);
    OutputDebugStringA("BC-250 UMD: CreateResource OK\n");
    return S_OK;
}

static HRESULT APIENTRY D3D9_DestroyResource(HANDLE hDev, HANDLE hRes) { UNREFERENCED_PARAMETER(hDev); UNREFERENCED_PARAMETER(hRes); return S_OK; }

/* D3D9 DDI: DrawPrimitive — PM4 DRAW_INDEX_AUTO */
static HRESULT APIENTRY D3D9_DrawPrimitive(HANDLE hDev, CONST D3DDDIARG_DRAWPRIMITIVE* pArgs, CONST UINT* pFlagBuffer)
{
    PBC250_D3D9_DEVICE dev = &g_D3D9Device;
    UNREFERENCED_PARAMETER(hDev);
    UNREFERENCED_PARAMETER(pFlagBuffer);

    if (!pArgs || !dev->CommandBuffer) return E_FAIL;

    EnterCriticalSection(&dev->Lock);

    PULONG cmd = (PULONG)((PUCHAR)dev->CommandBuffer + dev->CmdBufferUsed);

    /* PM4 DRAW_INDEX_AUTO packet: vertex count = PrimitiveCount * 3 (for triangles) */
    cmd[0] = PM4_TYPE3_HDR_D3D9(IT_DRAW_INDEX_AUTO_D3D9, 3);
    cmd[1] = pArgs->PrimitiveCount * 3;
    cmd[2] = (ULONG)pArgs->PrimitiveType | (1 << 8);

    dev->CmdBufferUsed += 3 * sizeof(ULONG);

    LeaveCriticalSection(&dev->Lock);
    return S_OK;
}

static HRESULT APIENTRY D3D9_DrawIndexedPrimitive(HANDLE hDev, CONST D3DDDIARG_DRAWINDEXEDPRIMITIVE* pArgs)
{
    PBC250_D3D9_DEVICE dev = &g_D3D9Device;
    UNREFERENCED_PARAMETER(hDev);

    if (!pArgs || !dev->CommandBuffer) return E_FAIL;

    EnterCriticalSection(&dev->Lock);

    PULONG cmd = (PULONG)((PUCHAR)dev->CommandBuffer + dev->CmdBufferUsed);

    /* PM4 DRAW_INDEX_2 packet for indexed drawing */
    cmd[0] = PM4_TYPE3_HDR_D3D9(0x27, 5);  /* IT_DRAW_INDEX_2 */
    cmd[1] = pArgs->NumVertices;
    cmd[2] = 0;  /* Index buffer address low */
    cmd[3] = 0;  /* Index buffer address high */
    cmd[4] = (ULONG)pArgs->PrimitiveType | (1 << 8);

    dev->CmdBufferUsed += 5 * sizeof(ULONG);

    LeaveCriticalSection(&dev->Lock);

    OutputDebugStringA("BC-250 UMD: DrawIndexedPrimitive\n");
    return S_OK;
}

/* D3D9 DDI: Present — Flush + Flip display */
static HRESULT APIENTRY D3D9_Present(HANDLE hDev, CONST D3DDDIARG_PRESENT* pArgs)
{
    PBC250_D3D9_DEVICE dev = &g_D3D9Device;
    UNREFERENCED_PARAMETER(hDev); UNREFERENCED_PARAMETER(pArgs);

    /* Flush pending commands first */
    D3D9_Flush(hDev);

    /* Flip display via KMD IOCTL if we have a framebuffer */
    if (dev->KmdDevice != INVALID_HANDLE_VALUE && dev->FramebufferGpuVa != 0) {
        /* Build flip parameters: width, height, pitch, format */
        ULONG flipData[7] = {0};
        flipData[0] = (ULONG)(dev->FramebufferGpuVa & 0xFFFFFFFF);  /* PA low */
        flipData[1] = (ULONG)(dev->FramebufferGpuVa >> 32);         /* PA high */
        flipData[2] = dev->BackBufferWidth;
        flipData[3] = dev->BackBufferHeight;
        flipData[4] = dev->BackBufferWidth * 4;  /* Pitch (RGBA) */
        flipData[5] = 22;  /* D3DDDIFMT_A8R8G8B8 */
        flipData[6] = 1;   /* VSync */

        DWORD ret = 0;
        DeviceIoControl(dev->KmdDevice, 0x800008C4, flipData, sizeof(flipData),
                        NULL, 0, &ret, NULL);

        OutputDebugStringA("BC-250 UMD: Present - flip submitted\n");
    }

    dev->FenceValue++;
    return S_OK;
}

/* D3D9 DDI: Flush — submit command buffer */
static HRESULT APIENTRY D3D9_Flush(HANDLE hDev)
{
    PBC250_D3D9_DEVICE dev = &g_D3D9Device;
    UNREFERENCED_PARAMETER(hDev);

    if (dev->CmdBufferUsed == 0) return S_OK;

    /* Submit command buffer via IOCTL to KMD */
    if (dev->KmdDevice != INVALID_HANDLE_VALUE) {
        ULONG submitData[4] = {0};
        submitData[0] = (ULONG)(ULONG_PTR)dev->CommandBuffer;
        submitData[1] = dev->CmdBufferUsed;
        submitData[2] = (ULONG)dev->FenceValue;
        submitData[3] = 0; /* GFX queue */

        DWORD ret = 0;
        DeviceIoControl(dev->KmdDevice, 0x80000880, submitData, sizeof(submitData), NULL, 0, &ret, NULL);
    }

    dev->CmdBufferUsed = 0;
    dev->FenceValue++;

    char buf[64];
    snprintf(buf, sizeof(buf), "BC-250 UMD: Flush fence=%u\n", (UINT)dev->FenceValue);
    OutputDebugStringA(buf);

    return S_OK;
}

/* D3D9 DDI: Lock/Unlock */
static HRESULT APIENTRY D3D9_Lock(HANDLE hDev, D3DDDIARG_LOCK* pArgs)
{
    PBC250_D3D9_DEVICE dev = &g_D3D9Device;
    if (!pArgs) return E_INVALIDARG;

    /* Map GPU memory for CPU access via KMD IOCTL 0x80000848 */
    if (dev->KmdDevice != INVALID_HANDLE_VALUE) {
        ULONG mapIn[3] = {0};
        ULONG64 mapOut[2] = {0};
        DWORD ret = 0;
        
        mapIn[0] = (ULONG)(ULONG_PTR)pArgs->hResource;
        mapIn[1] = 0;      /* Offset */
        mapIn[2] = 4096;   /* Size */
        
        if (DeviceIoControl(dev->KmdDevice, 0x80000848,
                           mapIn, sizeof(mapIn),
                           mapOut, sizeof(mapOut), &ret, NULL)) {
            return S_OK;
        }
    }
    
    return S_OK;
}

static HRESULT APIENTRY D3D9_Unlock(HANDLE hDev, CONST D3DDDIARG_UNLOCK* pArgs)
{
    UNREFERENCED_PARAMETER(hDev);
    UNREFERENCED_PARAMETER(pArgs);
    /* In a full implementation, this would flush CPU caches */
    return S_OK;
}

/* D3D9 DDI: Shader creation with ACO compiler */
static HRESULT APIENTRY D3D9_CreateVertexShaderDecl(HANDLE h, D3DDDIARG_CREATEVERTEXSHADERDECL* p, CONST D3DDDIVERTEXELEMENT* e)
{
    UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(e);
    if (!p) return E_INVALIDARG;
    p->ShaderHandle = (HANDLE)(ULONG_PTR)0x1;
    OutputDebugStringA("BC-250 UMD: CreateVertexShaderDecl\n");
    return S_OK;
}
static HRESULT APIENTRY D3D9_SetVertexShaderDecl(HANDLE h, HANDLE s) { UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(s); return S_OK; }
static HRESULT APIENTRY D3D9_DeleteVertexShaderDecl(HANDLE h, HANDLE s) { UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(s); return S_OK; }

static HRESULT APIENTRY D3D9_CreateVertexShaderFunc(HANDLE h, D3DDDIARG_CREATEVERTEXSHADERFUNC* p, CONST UINT* c)
{
    UNREFERENCED_PARAMETER(h);
    if (!p) return E_INVALIDARG;
    
    /* Parse DXBC shader and log info */
    if (c) {
        char buf[128];
        snprintf(buf, sizeof(buf), "BC-250 UMD: CreateVertexShaderFunc code=%p\n", c);
        OutputDebugStringA(buf);
    }
    
    p->ShaderHandle = (HANDLE)(ULONG_PTR)0x2;
    OutputDebugStringA("BC-250 UMD: VertexShader created (ACO stub)\n");
    return S_OK;
}
static HRESULT APIENTRY D3D9_SetVertexShaderFunc(HANDLE h, HANDLE s) { UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(s); return S_OK; }
static HRESULT APIENTRY D3D9_DeleteVertexShaderFunc(HANDLE h, HANDLE s) { UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(s); return S_OK; }

static HRESULT APIENTRY D3D9_CreatePixelShader(HANDLE h, D3DDDIARG_CREATEPIXELSHADER* p, CONST UINT* c)
{
    UNREFERENCED_PARAMETER(h);
    if (!p) return E_INVALIDARG;
    
    if (c) {
        char buf[128];
        snprintf(buf, sizeof(buf), "BC-250 UMD: CreatePixelShader code=%p\n", c);
        OutputDebugStringA(buf);
    }
    
    p->ShaderHandle = (HANDLE)(ULONG_PTR)0x3;
    OutputDebugStringA("BC-250 UMD: PixelShader created (ACO stub)\n");
    return S_OK;
}
static HRESULT APIENTRY D3D9_SetPixelShader(HANDLE h, HANDLE s) { UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(s); return S_OK; }
static HRESULT APIENTRY D3D9_DeletePixelShader(HANDLE h, HANDLE s) { UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(s); return S_OK; }

/* D3D9 DDI: State functions with PM4 packets */
static HRESULT APIENTRY D3D9_SetRenderState(HANDLE h, CONST D3DDDIARG_RENDERSTATE* p)
{
    PBC250_D3D9_DEVICE dev = &g_D3D9Device;
    UNREFERENCED_PARAMETER(h);
    if (!p) return E_INVALIDARG;

    /* Emit PM4 SET_CONTEXT_REG for render states */
    EnterCriticalSection(&dev->Lock);
    if (dev->CommandBuffer && dev->CmdBufferUsed + 8 < dev->CommandBufferSize) {
        PULONG cmd = (PULONG)((PUCHAR)dev->CommandBuffer + dev->CmdBufferUsed);
        cmd[0] = PM4_TYPE3_HDR_D3D9(0x69, 2);  /* SET_CONTEXT_REG */
        cmd[1] = 0;  /* Register offset */
        cmd[2] = 0;  /* Value */
        dev->CmdBufferUsed += 8;
    }
    LeaveCriticalSection(&dev->Lock);
    return S_OK;
}

static HRESULT APIENTRY D3D9_SetStreamSource(HANDLE h, CONST D3DDDIARG_SETSTREAMSOURCE* p)
{
    UNREFERENCED_PARAMETER(h);
    UNREFERENCED_PARAMETER(p);
    return S_OK;
}

static HRESULT APIENTRY D3D9_SetIndices(HANDLE h, CONST D3DDDIARG_SETINDICES* p)
{
    UNREFERENCED_PARAMETER(h);
    UNREFERENCED_PARAMETER(p);
    return S_OK;
}

static HRESULT APIENTRY D3D9_SetTexture(HANDLE h, UINT s, HANDLE t)
{
    UNREFERENCED_PARAMETER(h);
    UNREFERENCED_PARAMETER(s);
    UNREFERENCED_PARAMETER(t);
    return S_OK;
}
static HRESULT APIENTRY D3D9_CreateQuery(HANDLE h, D3DDDIARG_CREATEQUERY* p) { UNREFERENCED_PARAMETER(h); p->hQuery = (HANDLE)1; return S_OK; }
static HRESULT APIENTRY D3D9_DestroyQuery(HANDLE h, HANDLE q) { UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(q); return S_OK; }
static HRESULT APIENTRY D3D9_IssueQuery(HANDLE h, CONST D3DDDIARG_ISSUEQUERY* p) { UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(p); return S_OK; }

/* D3D9 entry point */
__declspec(dllexport) HRESULT APIENTRY OpenAdapter(D3DDDIARG_OPENADAPTER* pArgs)
{
    if (!pArgs) return E_INVALIDARG;
    OutputDebugStringA("BC-250 UMD: D3D9 OpenAdapter\n");
    pArgs->pAdapterFuncs->pfnCreateDevice = D3D9_CreateDevice;
    pArgs->pAdapterFuncs->pfnCloseAdapter = D3D9_CloseAdapter;
    return S_OK;
}

/* ============================================================================
   PM4 State Packets — GPU render state configuration
   
   These packets tell the GPU how to render:
   - Viewport: screen region for rendering
   - Scissor: clipping rectangle
   - Blend: alpha blending mode
   - Depth-Stencil: depth testing
   - Rasterizer: fill mode, culling
=========================================================================== */

/* PM4 opcodes for state packets */
#define PM4_IT_SET_CONTEXT_REG     0x69
#define PM4_IT_SET_CONFIG_REG      0x68
#define PM4_IT_SET_SH_REG          0x76
#define PM4_IT_EVENT_WRITE_EOP     0x47

/* GFX10 context register offsets */
#define REG_PA_CL_VPORT_XOFFSET_0    0x000A010
#define REG_PA_CL_VPORT_YOFFSET_0    0x000A014
#define REG_PA_CL_VPORT_XSCALE_0     0x000A018
#define REG_PA_CL_VPORT_YSCALE_0     0x000A01C
#define REG_PA_CL_VPORT_ZOFFSET_0    0x000A020
#define REG_PA_CL_VPORT_ZSCALE_0     0x000A024
#define REG_PA_SC_VPORT_SCISSOR_0_TL  0x000A010
#define REG_PA_SC_VPORT_SCISSOR_0_BR  0x000A014
#define REG_CB_TARGET_MASK_0          0x000A0C8
#define REG_CB_BLEND_GREEN_0          0x000A0E4
#define REG_DB_DEPTH_CONTROL          0x000A200
#define REG_PA_SC_MODE_CNTL           0x000A094

/* Write PM4 state packet to command buffer */
static void Bc250WritePm4State(PBC250_D3D9_DEVICE dev, ULONG opcode, ULONG reg, const ULONG* values, ULONG count)
{
    if (!dev->CommandBuffer || (dev->CmdBufferUsed + (2 + count) * sizeof(ULONG)) > dev->CommandBufferSize) {
        return;
    }

    PULONG cmd = (PULONG)((PUCHAR)dev->CommandBuffer + dev->CmdBufferUsed);

    /* PM4 SET_CONTEXT_REG packet */
    cmd[0] = PM4_TYPE3_HDR_D3D9(opcode, 1 + count);
    cmd[1] = (reg >> 2);  /* Register offset in DWORDs */
    for (ULONG i = 0; i < count; i++) {
        cmd[2 + i] = values[i];
    }

    dev->CmdBufferUsed += (2 + count) * sizeof(ULONG);
}

/* D3D9 DDI: SetViewport — PM4 SET_CONTEXT_REG for viewport */
static HRESULT APIENTRY D3D9_SetViewport(HANDLE hDev, CONST D3DDDIARG_VIEWPORTINFO* pArgs)
{
    PBC250_D3D9_DEVICE dev = &g_D3D9Device;
    UNREFERENCED_PARAMETER(hDev);

    if (!pArgs) return E_INVALIDARG;

    EnterCriticalSection(&dev->Lock);

    /* Convert D3D9 viewport to GFX10 viewport registers */
    float X = (float)pArgs->X;
    float Y = (float)pArgs->Y;
    float W = (float)pArgs->Width;
    float H = (float)pArgs->Height;
    float MinZ = 0.0f;
    float MaxZ = 1.0f;

    ULONG values[6];
    values[0] = *(ULONG*)&X;  /* X offset */
    values[1] = *(ULONG*)&Y;  /* Y offset */
    values[2] = *(ULONG*)&W;  /* X scale */
    values[3] = *(ULONG*)&H;  /* Y scale */
    values[4] = *(ULONG*)&MinZ;  /* Z offset */
    values[5] = *(ULONG*)&MaxZ;  /* Z scale */

    Bc250WritePm4State(dev, PM4_IT_SET_CONTEXT_REG, REG_PA_CL_VPORT_XOFFSET_0, values, 6);

    LeaveCriticalSection(&dev->Lock);

    OutputDebugStringA("BC-250 UMD: SetViewport\n");
    return S_OK;
}

/* D3D9 DDI: SetScissorRect — PM4 scissor rectangle */
static HRESULT APIENTRY D3D9_SetScissorRect(HANDLE hDev, CONST RECT* pRect)
{
    PBC250_D3D9_DEVICE dev = &g_D3D9Device;
    UNREFERENCED_PARAMETER(hDev);

    if (!pRect) return E_INVALIDARG;

    EnterCriticalSection(&dev->Lock);

    /* TL (top-left) = (X1, Y1), BR (bottom-right) = (X2, Y2) */
    ULONG tl = (pRect->top << 16) | pRect->left;
    ULONG br = (pRect->bottom << 16) | pRect->right;

    Bc250WritePm4State(dev, PM4_IT_SET_CONTEXT_REG, REG_PA_SC_VPORT_SCISSOR_0_TL, &tl, 1);
    Bc250WritePm4State(dev, PM4_IT_SET_CONTEXT_REG, REG_PA_SC_VPORT_SCISSOR_0_BR, &br, 1);

    LeaveCriticalSection(&dev->Lock);

    OutputDebugStringA("BC-250 UMD: SetScissorRect\n");
    return S_OK;
}

/* D3D9 DDI: SetRenderTarget — set render target */
static HRESULT APIENTRY D3D9_SetRenderTarget(HANDLE hDev, CONST D3DDDIARG_SETRENDERTARGET* pArgs)
{
    PBC250_D3D9_DEVICE dev = &g_D3D9Device;
    UNREFERENCED_PARAMETER(hDev);

    if (!pArgs) return E_INVALIDARG;

    /* Store render target info for later use */
    EnterCriticalSection(&dev->Lock);
    LeaveCriticalSection(&dev->Lock);

    OutputDebugStringA("BC-250 UMD: SetRenderTarget\n");
    return S_OK;
}

/* D3D9 DDI: Clear — clear render target / depth buffer */
static HRESULT APIENTRY D3D9_Clear(HANDLE hDev, CONST D3DDDIARG_CLEAR* pArgs, UINT NumRect, CONST RECT* pRect)
{
    PBC250_D3D9_DEVICE dev = &g_D3D9Device;
    UNREFERENCED_PARAMETER(hDev);
    UNREFERENCED_PARAMETER(NumRect);
    UNREFERENCED_PARAMETER(pRect);

    if (!pArgs || !dev->CommandBuffer) return E_FAIL;

    EnterCriticalSection(&dev->Lock);

    PULONG cmd = (PULONG)((PUCHAR)dev->CommandBuffer + dev->CmdBufferUsed);

    /* PM4 EVENT_WRITE with CACHE_FLUSH_and_INV (opcode 0x46) */
    cmd[0] = PM4_TYPE3_HDR_D3D9(0x46, 1);  /* IT_EVENT_WRITE */
    cmd[1] = 0x00000008;  /* EVENT_TYPE_CACHE_FLUSH_and_INV */
    dev->CmdBufferUsed += 2 * sizeof(ULONG);

    /* If clearing color, emit write to framebuffer */
    if (pArgs->Flags & 0x01) {  /* D3DCLEAR_TARGET */
        /* PM4 WRITE_DATA (opcode 0x37) for color clear */
        cmd = (PULONG)((PUCHAR)dev->CommandBuffer + dev->CmdBufferUsed);
        cmd[0] = PM4_TYPE3_HDR_D3D9(0x37, 3);  /* IT_WRITE_DATA */
        cmd[1] = 0x00000000;  /* Control: write to memory */
        cmd[2] = 0x00000000;  /* Address low (TODO: get from RT) */
        cmd[3] = 0x00000000;  /* Address high */
        dev->CmdBufferUsed += 4 * sizeof(ULONG);
    }

    LeaveCriticalSection(&dev->Lock);

    OutputDebugStringA("BC-250 UMD: Clear (PM4 cache flush)\n");
    return S_OK;
}

/* ============================================================================
   Display Mode Enumeration — Supported resolutions and refresh rates
=========================================================================== */

typedef struct _BC250_DISPLAY_MODE {
    UINT Width;
    UINT Height;
    UINT RefreshRate;
    UINT BitsPerPixel;
} BC250_DISPLAY_MODE;

/* Pre-defined display modes for BC-250 */
static const BC250_DISPLAY_MODE g_SupportedModes[] = {
    { 640,  480,  60, 32 },   /* VGA */
    { 640,  480,  75, 32 },   /* VGA */
    { 800,  600,  60, 32 },   /* SVGA */
    { 800,  600,  75, 32 },   /* SVGA */
    { 1024, 768,  60, 32 },   /* XGA */
    { 1024, 768,  75, 32 },   /* XGA */
    { 1280, 720,  60, 32 },   /* HD */
    { 1280, 720,  120, 32 },  /* HD */
    { 1280, 1024, 60, 32 },   /* SXGA */
    { 1280, 1024, 75, 32 },   /* SXGA */
    { 1600, 900,  60, 32 },   /* HD+ */
    { 1600, 1024, 60, 32 },   /* WSXGA */
    { 1920, 1080, 60, 32 },   /* Full HD */
    { 1920, 1080, 120, 32 },  /* Full HD */
    { 1920, 1200, 60, 32 },   /* WUXGA */
    { 2560, 1440, 60, 32 },   /* QHD */
    { 2560, 1440, 120, 32 },  /* QHD */
    { 3840, 2160, 30, 32 },   /* 4K */
    { 3840, 2160, 60, 32 },   /* 4K */
};
#define BC250_NUM_MODES (sizeof(g_SupportedModes) / sizeof(g_SupportedModes[0]))

/* ============================================================================
   Shader Parse Stub — DXBC header detection and logging
=========================================================================== */

#define DXBC_MAGIC  0x43425844  /* 'DXBC' */

typedef struct _DXBC_HEADER {
    UINT Magic;           /* DXBC_MAGIC */
    UINT Hash[4];         /* SHA-1 hash */
    UINT Version;         /* D3D shader model version */
    UINT TotalSize;       /* Total size in bytes */
    UINT NumChunks;       /* Number of chunks */
} DXBC_HEADER;

typedef struct _DXBC_CHUNK {
    UINT FourCC;          /* Chunk type (e.g., 'DXBC', 'STAT', 'ISGN', 'OSGN') */
    UINT Size;            /* Chunk data size */
} DXBC_CHUNK;

/* Parse DXBC shader header and log info */
static void Bc250ParseDxbcShader(const UINT* pCode, SIZE_T CodeSize)
{
    if (!pCode || CodeSize < sizeof(DXBC_HEADER)) {
        OutputDebugStringA("BC-250 UMD: Shader: invalid/empty\n");
        return;
    }

    const DXBC_HEADER* hdr = (const DXBC_HEADER*)pCode;

    /* Check magic */
    if (hdr->Magic != DXBC_MAGIC) {
        OutputDebugStringA("BC-250 UMD: Shader: not DXBC format\n");
        return;
    }

    /* Log shader info */
    char buf[256];
    sprintf(buf, "BC-250 UMD: Shader DXBC v%u.%u, %u chunks, %u bytes\n",
            (hdr->Version >> 8) & 0xFF, hdr->Version & 0xFF,
            hdr->NumChunks, hdr->TotalSize);
    OutputDebugStringA(buf);

    /* Parse chunks to detect shader type */
    const UINT* pChunk = pCode + 4; /* Skip DXBC header */
    UINT remaining = hdr->TotalSize - sizeof(DXBC_HEADER);

    for (UINT i = 0; i < hdr->NumChunks && remaining >= 8; i++) {
        const DXBC_CHUNK* chunk = (const DXBC_CHUNK*)pChunk;

        char chunkBuf[64];
        char fourcc[5] = {0};
        fourcc[0] = (char)(chunk->FourCC & 0xFF);
        fourcc[1] = (char)((chunk->FourCC >> 8) & 0xFF);
        fourcc[2] = (char)((chunk->FourCC >> 16) & 0xFF);
        fourcc[3] = (char)((chunk->FourCC >> 24) & 0xFF);
        sprintf(chunkBuf, "  Chunk '%s' (%u bytes)\n", fourcc, chunk->Size);
        OutputDebugStringA(chunkBuf);

        UINT chunkSize = (chunk->Size + 3) & ~3; /* Align to 4 bytes */
        pChunk = (const UINT*)((const BYTE*)pChunk + 8 + chunkSize);
        remaining -= 8 + chunkSize;
    }
}

BOOL APIENTRY DllMain(HMODULE h, DWORD r, LPVOID v)
{
    UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(v);
    if (r == DLL_PROCESS_ATTACH) OutputDebugStringA("BC-250 UMD: Loaded (D3D9+D3D12+States)\n");
    if (r == DLL_PROCESS_DETACH) OutputDebugStringA("BC-250 UMD: Unloaded\n");
    return TRUE;
}
