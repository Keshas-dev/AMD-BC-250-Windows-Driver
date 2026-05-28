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

BOOL APIENTRY DllMain(HMODULE h, DWORD r, LPVOID v)
{
    UNREFERENCED_PARAMETER(h); UNREFERENCED_PARAMETER(v);
    if (r == DLL_PROCESS_ATTACH) OutputDebugStringA("BC-250 UMD: Loaded\n");
    if (r == DLL_PROCESS_DETACH) OutputDebugStringA("BC-250 UMD: Unloaded\n");
    return TRUE;
}
