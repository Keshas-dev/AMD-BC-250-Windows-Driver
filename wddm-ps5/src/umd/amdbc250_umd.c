/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    amdbc250_umd.c

Abstract:
    User-Mode Display Driver (UMD) reference implementation for AMD BC-250.

    This implementation focuses on:
      - Stable D3D9 entry points
      - Deterministic command-buffer handling
      - Fast-fail behavior for unsupported DX10/11/12 paths

Environment:
    User mode

--*/

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9types.h>

/* Keep user-mode build self-contained when NTSTATUS isn't surfaced by includes. */
#ifndef _NTDEF_
typedef LONG NTSTATUS;
#endif

#include <d3dumddi.h>
#include <d3d10umddi.h>
#include <d3d12umddi.h>

/*
 * Recent SDKs don't always ship d3d11umddi.h. Keep the DX11 entry point as a
 * no-op with a minimal local type so the reference DLL still links.
 */
typedef struct _D3D11DDIARG_OPENADAPTER {
    UINT Reserved;
} D3D11DDIARG_OPENADAPTER;

/* Minimal PM4 helpers used by the D3D9 draw path */
#define AMDBC250_PM4_IT_DRAW_INDEX_AUTO 0x2D
#define AMDBC250_PM4_TYPE3_HDR(op, count) \
    (((3U) << 30) | ((((count) - 2U) & 0x3FFFU) << 16) | (((op) & 0xFFU) << 8))

#define AMDBC250_UMD_COMMAND_BUFFER_SIZE (64 * 1024)

typedef struct _AMDBC250_UMD_DEVICE {
    HANDLE           hDevice;
    HANDLE           hAdapter;
    PVOID            pCommandBuffer;
    UINT             CommandBufferSize;
    UINT             CommandBufferUsed;
    UINT64           FenceValue;
    ULONG_PTR        NextHandleValue;
    CRITICAL_SECTION DeviceLock;
} AMDBC250_UMD_DEVICE, *PAMDBC250_UMD_DEVICE;

typedef struct _AMDBC250_UMD_ADAPTER {
    D3DDDI_ADAPTERCALLBACKS Callbacks;
} AMDBC250_UMD_ADAPTER, *PAMDBC250_UMD_ADAPTER;

typedef struct _AMDBC250_UMD_RESOURCE {
    UINT Dummy;
} AMDBC250_UMD_RESOURCE, *PAMDBC250_UMD_RESOURCE;

/* D3D9 forward declarations */
static HRESULT APIENTRY UmdOpenAdapter(D3DDDIARG_OPENADAPTER *pOpenAdapter);
static HRESULT APIENTRY UmdGetCaps(HANDLE hAdapter, CONST D3DDDIARG_GETCAPS *pGetCaps);
static HRESULT APIENTRY UmdCloseAdapter(HANDLE hAdapter);
static HRESULT APIENTRY Umd10OpenAdapter(D3D10DDIARG_OPENADAPTER *pOpenAdapter, BOOL UseAdapterFuncs2);
static SIZE_T APIENTRY Umd10CalcPrivateDeviceSize(D3D10DDI_HADAPTER hAdapter, CONST D3D10DDIARG_CALCPRIVATEDEVICESIZE *pData);
static HRESULT APIENTRY Umd10CreateDevice(D3D10DDI_HADAPTER hAdapter, D3D10DDIARG_CREATEDEVICE *pCreateData);
static HRESULT APIENTRY Umd10CloseAdapter(D3D10DDI_HADAPTER hAdapter);
static HRESULT APIENTRY Umd10GetSupportedVersions(D3D10DDI_HADAPTER hAdapter, UINT32 *pNumVersions, UINT64 *pSupportedDDIInterfaceVersions);
static HRESULT APIENTRY Umd10GetCaps(D3D10DDI_HADAPTER hAdapter, CONST D3D10_2DDIARG_GETCAPS *pCaps);
static HRESULT APIENTRY UmdCreateDevice(HANDLE hAdapter, D3DDDIARG_CREATEDEVICE *pCreateData);
static HRESULT APIENTRY UmdDestroyDevice(HANDLE hDevice);
static HRESULT APIENTRY UmdCreateResource(HANDLE hDevice, D3DDDIARG_CREATERESOURCE *pResource);
static HRESULT APIENTRY UmdDestroyResource(HANDLE hDevice, HANDLE hResource);
static HRESULT APIENTRY UmdSetRenderState(HANDLE hDevice, CONST D3DDDIARG_RENDERSTATE *pData);
static HRESULT APIENTRY UmdDrawPrimitive(HANDLE hDevice, CONST D3DDDIARG_DRAWPRIMITIVE *pData, CONST UINT *pFlagBuffer);
static HRESULT APIENTRY UmdPresent(HANDLE hDevice, CONST D3DDDIARG_PRESENT *pData);
static HRESULT APIENTRY UmdFlush(HANDLE hDevice);
static HRESULT APIENTRY UmdLock(HANDLE hDevice, D3DDDIARG_LOCK *pData);
static HRESULT APIENTRY UmdUnlock(HANDLE hDevice, CONST D3DDDIARG_UNLOCK *pData);
static HRESULT APIENTRY UmdSetStreamSource(HANDLE hDevice, CONST D3DDDIARG_SETSTREAMSOURCE *pData);
static HRESULT APIENTRY UmdSetIndices(HANDLE hDevice, CONST D3DDDIARG_SETINDICES *pData);
static HRESULT APIENTRY UmdSetViewport(HANDLE hDevice, CONST D3DDDIARG_VIEWPORTINFO *pData);
static HRESULT APIENTRY UmdSetScissorRect(HANDLE hDevice, CONST RECT *pRect);
static HRESULT APIENTRY UmdSetRenderTarget(HANDLE hDevice, CONST D3DDDIARG_SETRENDERTARGET *pData);
static HRESULT APIENTRY UmdClear(HANDLE hDevice, CONST D3DDDIARG_CLEAR *pData, UINT NumRect, CONST RECT *pRect);
static HRESULT APIENTRY UmdCreateVertexShaderDecl(HANDLE hDevice, D3DDDIARG_CREATEVERTEXSHADERDECL *pData, CONST D3DDDIVERTEXELEMENT *pVertexElements);
static HRESULT APIENTRY UmdSetVertexShaderDecl(HANDLE hDevice, HANDLE hShaderHandle);
static HRESULT APIENTRY UmdDeleteVertexShaderDecl(HANDLE hDevice, HANDLE hShaderHandle);
static HRESULT APIENTRY UmdCreateVertexShaderFunc(HANDLE hDevice, D3DDDIARG_CREATEVERTEXSHADERFUNC *pData, CONST UINT *pCode);
static HRESULT APIENTRY UmdSetVertexShaderFunc(HANDLE hDevice, HANDLE hShaderHandle);
static HRESULT APIENTRY UmdDeleteVertexShaderFunc(HANDLE hDevice, HANDLE hShaderHandle);
static HRESULT APIENTRY UmdCreatePixelShader(HANDLE hDevice, D3DDDIARG_CREATEPIXELSHADER *pData, CONST UINT *pCode);
static HRESULT APIENTRY UmdSetPixelShader(HANDLE hDevice, HANDLE hShaderHandle);
static HRESULT APIENTRY UmdDeletePixelShader(HANDLE hDevice, HANDLE hShaderHandle);
static HRESULT APIENTRY UmdSetTexture(HANDLE hDevice, UINT Stage, HANDLE hTexture);
static HRESULT APIENTRY UmdSetTextureStageState(HANDLE hDevice, CONST D3DDDIARG_TEXTURESTAGESTATE *pData);
static HRESULT APIENTRY UmdSetSamplerState(HANDLE hDevice, UINT Sampler, D3DDDITEXTUREFILTERTYPE State, UINT Value);
static HRESULT APIENTRY UmdQueryGetData(HANDLE hDevice, CONST D3DDDIARG_GETQUERYDATA *pData);
static HRESULT APIENTRY UmdIssueQuery(HANDLE hDevice, CONST D3DDDIARG_ISSUEQUERY *pData);
static HRESULT APIENTRY UmdCreateQuery(HANDLE hDevice, D3DDDIARG_CREATEQUERY *pData);
static HRESULT APIENTRY UmdDestroyQuery(HANDLE hDevice, HANDLE hQuery);

static HRESULT
Bc250FlushLocked(
    _Inout_ PAMDBC250_UMD_DEVICE pDevice
    )
{
    if (pDevice->CommandBufferUsed == 0) {
        return S_OK;
    }

    /* Reference implementation: treat command buffer as consumed. */
    pDevice->CommandBufferUsed = 0;
    pDevice->FenceValue++;
    return S_OK;
}

static HRESULT
Bc250EnsureCommandSpaceLocked(
    _Inout_ PAMDBC250_UMD_DEVICE pDevice,
    _In_ UINT RequiredBytes
    )
{
    if (RequiredBytes > pDevice->CommandBufferSize) {
        return E_OUTOFMEMORY;
    }

    if ((pDevice->CommandBufferUsed + RequiredBytes) <= pDevice->CommandBufferSize) {
        return S_OK;
    }

    if (FAILED(Bc250FlushLocked(pDevice))) {
        return E_FAIL;
    }

    return ((pDevice->CommandBufferUsed + RequiredBytes) <= pDevice->CommandBufferSize)
        ? S_OK
        : E_OUTOFMEMORY;
}

static HANDLE
Bc250AllocateFakeHandle(
    _Inout_ PAMDBC250_UMD_DEVICE pDevice
    )
{
    pDevice->NextHandleValue++;
    if (pDevice->NextHandleValue == 0) {
        pDevice->NextHandleValue++;
    }
    return (HANDLE)pDevice->NextHandleValue;
}

BOOL WINAPI
DllMain(
    HINSTANCE hinstDLL,
    DWORD fdwReason,
    LPVOID lpvReserved
    )
{
    UNREFERENCED_PARAMETER(lpvReserved);

    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
    }

    return TRUE;
}

HRESULT APIENTRY
OpenAdapter(
    D3DDDIARG_OPENADAPTER *pOpenAdapter
    )
{
    return UmdOpenAdapter(pOpenAdapter);
}

HRESULT APIENTRY
OpenAdapter10(
    D3D10DDIARG_OPENADAPTER *pOpenAdapter
    )
{
    return Umd10OpenAdapter(pOpenAdapter, FALSE);
}

HRESULT APIENTRY
OpenAdapter10_2(
    D3D10DDIARG_OPENADAPTER *pOpenAdapter
    )
{
    return Umd10OpenAdapter(pOpenAdapter, TRUE);
}

HRESULT APIENTRY
OpenAdapter11(
    D3D11DDIARG_OPENADAPTER *pOpenAdapter
    )
{
    /*
     * Keep D3D11 entry point aligned with the D3D10.2 adapter bootstrap
     * path used by this minimal UMD, instead of hard failing at load.
     */
    return Umd10OpenAdapter((D3D10DDIARG_OPENADAPTER *)pOpenAdapter, TRUE);
}

HRESULT APIENTRY
OpenAdapter12(
    D3D12DDIARG_OPENADAPTER *pOpenAdapter
    )
{
    UNREFERENCED_PARAMETER(pOpenAdapter);
    return E_NOTIMPL;
}

static HRESULT APIENTRY
Umd10OpenAdapter(
    D3D10DDIARG_OPENADAPTER *pOpenAdapter,
    BOOL UseAdapterFuncs2
    )
{
    PAMDBC250_UMD_ADAPTER pAdapter;

    if (pOpenAdapter == NULL ||
        pOpenAdapter->pAdapterCallbacks == NULL ||
        pOpenAdapter->pAdapterFuncs == NULL) {
        return E_INVALIDARG;
    }

    pAdapter = (PAMDBC250_UMD_ADAPTER)HeapAlloc(
        GetProcessHeap(),
        HEAP_ZERO_MEMORY,
        sizeof(AMDBC250_UMD_ADAPTER));
    if (pAdapter == NULL) {
        return E_OUTOFMEMORY;
    }

    pAdapter->Callbacks = *pOpenAdapter->pAdapterCallbacks;
    pOpenAdapter->hAdapter.pDrvPrivate = pAdapter;

    if (UseAdapterFuncs2) {
        pOpenAdapter->pAdapterFuncs_2->pfnCalcPrivateDeviceSize = Umd10CalcPrivateDeviceSize;
        pOpenAdapter->pAdapterFuncs_2->pfnCreateDevice = Umd10CreateDevice;
        pOpenAdapter->pAdapterFuncs_2->pfnCloseAdapter = Umd10CloseAdapter;
        pOpenAdapter->pAdapterFuncs_2->pfnGetSupportedVersions = Umd10GetSupportedVersions;
        pOpenAdapter->pAdapterFuncs_2->pfnGetCaps = Umd10GetCaps;
    } else {
        pOpenAdapter->pAdapterFuncs->pfnCalcPrivateDeviceSize = Umd10CalcPrivateDeviceSize;
        pOpenAdapter->pAdapterFuncs->pfnCreateDevice = Umd10CreateDevice;
        pOpenAdapter->pAdapterFuncs->pfnCloseAdapter = Umd10CloseAdapter;
    }

    return S_OK;
}

static SIZE_T APIENTRY
Umd10CalcPrivateDeviceSize(
    D3D10DDI_HADAPTER hAdapter,
    CONST D3D10DDIARG_CALCPRIVATEDEVICESIZE *pData
    )
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pData);
    return sizeof(AMDBC250_UMD_DEVICE);
}

static HRESULT APIENTRY
Umd10CreateDevice(
    D3D10DDI_HADAPTER hAdapter,
    D3D10DDIARG_CREATEDEVICE *pCreateData
    )
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pCreateData);
    return E_NOTIMPL;
}

static HRESULT APIENTRY
Umd10CloseAdapter(
    D3D10DDI_HADAPTER hAdapter
    )
{
    return UmdCloseAdapter((HANDLE)hAdapter.pDrvPrivate);
}

static HRESULT APIENTRY
Umd10GetSupportedVersions(
    D3D10DDI_HADAPTER hAdapter,
    UINT32 *pNumVersions,
    UINT64 *pSupportedDDIInterfaceVersions
    )
{
    UNREFERENCED_PARAMETER(hAdapter);

    if (pNumVersions == NULL) {
        return E_INVALIDARG;
    }

    if (pSupportedDDIInterfaceVersions == NULL || *pNumVersions == 0) {
        *pNumVersions = 1;
        return S_OK;
    }

    pSupportedDDIInterfaceVersions[0] = D3D11_0_7_DDI_SUPPORTED;
    *pNumVersions = 1;
    return S_OK;
}

static HRESULT APIENTRY
Umd10GetCaps(
    D3D10DDI_HADAPTER hAdapter,
    CONST D3D10_2DDIARG_GETCAPS *pCaps
    )
{
    UNREFERENCED_PARAMETER(hAdapter);

    if (pCaps == NULL) {
        return E_INVALIDARG;
    }

    if (pCaps->DataSize != 0 && pCaps->pData == NULL) {
        return E_INVALIDARG;
    }

    if (pCaps->pData != NULL && pCaps->DataSize != 0) {
        ZeroMemory(pCaps->pData, pCaps->DataSize);
    }

    return S_OK;
}

static HRESULT APIENTRY
UmdOpenAdapter(
    D3DDDIARG_OPENADAPTER *pOpenAdapter
    )
{
    PAMDBC250_UMD_ADAPTER pAdapter;

    if (pOpenAdapter == NULL ||
        pOpenAdapter->pAdapterCallbacks == NULL ||
        pOpenAdapter->pAdapterFuncs == NULL) {
        return E_INVALIDARG;
    }

    pAdapter = (PAMDBC250_UMD_ADAPTER)HeapAlloc(
        GetProcessHeap(),
        HEAP_ZERO_MEMORY,
        sizeof(AMDBC250_UMD_ADAPTER));
    if (pAdapter == NULL) {
        return E_OUTOFMEMORY;
    }

    pAdapter->Callbacks = *pOpenAdapter->pAdapterCallbacks;

    pOpenAdapter->hAdapter = (HANDLE)pAdapter;
    pOpenAdapter->pAdapterFuncs->pfnGetCaps = UmdGetCaps;
    pOpenAdapter->pAdapterFuncs->pfnCreateDevice = UmdCreateDevice;
    pOpenAdapter->pAdapterFuncs->pfnCloseAdapter = UmdCloseAdapter;

    return S_OK;
}

static HRESULT APIENTRY
UmdGetCaps(
    HANDLE hAdapter,
    CONST D3DDDIARG_GETCAPS *pGetCaps
    )
{
    UNREFERENCED_PARAMETER(hAdapter);

    if (pGetCaps == NULL) {
        return E_INVALIDARG;
    }

    if (pGetCaps->DataSize > 0 && pGetCaps->pData == NULL) {
        return E_INVALIDARG;
    }

    if (pGetCaps->pData != NULL && pGetCaps->DataSize > 0) {
        ZeroMemory(pGetCaps->pData, pGetCaps->DataSize);
    }

    switch (pGetCaps->Type) {
    case D3DDDICAPS_GETFORMATCOUNT:
        if (pGetCaps->DataSize < sizeof(UINT)) {
            return E_INVALIDARG;
        }
        *(UINT *)pGetCaps->pData = 1;
        return S_OK;

    default:
        /*
         * Keep capability queries conservative and deterministic.
         * Returning S_OK with a zeroed payload avoids runtime hard-fail paths.
         */
        return S_OK;
    }
}

static HRESULT APIENTRY
UmdCloseAdapter(
    HANDLE hAdapter
    )
{
    PAMDBC250_UMD_ADAPTER pAdapter = (PAMDBC250_UMD_ADAPTER)hAdapter;

    if (pAdapter != NULL) {
        HeapFree(GetProcessHeap(), 0, pAdapter);
    }

    return S_OK;
}

static HRESULT APIENTRY
UmdCreateDevice(
    HANDLE hAdapter,
    D3DDDIARG_CREATEDEVICE *pCreateData
    )
{
    PAMDBC250_UMD_DEVICE pDevice;

    if (hAdapter == NULL || pCreateData == NULL || pCreateData->pDeviceFuncs == NULL) {
        return E_INVALIDARG;
    }

    pDevice = (PAMDBC250_UMD_DEVICE)HeapAlloc(
        GetProcessHeap(),
        HEAP_ZERO_MEMORY,
        sizeof(AMDBC250_UMD_DEVICE));
    if (pDevice == NULL) {
        return E_OUTOFMEMORY;
    }

    pDevice->hDevice = pCreateData->hDevice;
    pDevice->hAdapter = hAdapter;
    pDevice->CommandBufferSize = AMDBC250_UMD_COMMAND_BUFFER_SIZE;
    pDevice->pCommandBuffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, pDevice->CommandBufferSize);
    pDevice->FenceValue = 0;
    pDevice->NextHandleValue = 1;

    if (pDevice->pCommandBuffer == NULL) {
        HeapFree(GetProcessHeap(), 0, pDevice);
        return E_OUTOFMEMORY;
    }

    InitializeCriticalSection(&pDevice->DeviceLock);

    pCreateData->pDeviceFuncs->pfnDestroyDevice = UmdDestroyDevice;
    pCreateData->pDeviceFuncs->pfnCreateResource = UmdCreateResource;
    pCreateData->pDeviceFuncs->pfnDestroyResource = UmdDestroyResource;
    pCreateData->pDeviceFuncs->pfnSetRenderState = UmdSetRenderState;
    pCreateData->pDeviceFuncs->pfnDrawPrimitive = UmdDrawPrimitive;
    pCreateData->pDeviceFuncs->pfnPresent = UmdPresent;
    pCreateData->pDeviceFuncs->pfnFlush = UmdFlush;
    pCreateData->pDeviceFuncs->pfnLock = UmdLock;
    pCreateData->pDeviceFuncs->pfnUnlock = UmdUnlock;
    pCreateData->pDeviceFuncs->pfnSetStreamSource = UmdSetStreamSource;
    pCreateData->pDeviceFuncs->pfnSetIndices = UmdSetIndices;
    pCreateData->pDeviceFuncs->pfnSetViewport = UmdSetViewport;
    pCreateData->pDeviceFuncs->pfnSetScissorRect = UmdSetScissorRect;
    pCreateData->pDeviceFuncs->pfnSetRenderTarget = UmdSetRenderTarget;
    pCreateData->pDeviceFuncs->pfnClear = UmdClear;
    pCreateData->pDeviceFuncs->pfnCreateVertexShaderDecl = UmdCreateVertexShaderDecl;
    pCreateData->pDeviceFuncs->pfnSetVertexShaderDecl = UmdSetVertexShaderDecl;
    pCreateData->pDeviceFuncs->pfnDeleteVertexShaderDecl = UmdDeleteVertexShaderDecl;
    pCreateData->pDeviceFuncs->pfnCreateVertexShaderFunc = UmdCreateVertexShaderFunc;
    pCreateData->pDeviceFuncs->pfnSetVertexShaderFunc = UmdSetVertexShaderFunc;
    pCreateData->pDeviceFuncs->pfnDeleteVertexShaderFunc = UmdDeleteVertexShaderFunc;
    pCreateData->pDeviceFuncs->pfnCreatePixelShader = UmdCreatePixelShader;
    pCreateData->pDeviceFuncs->pfnSetPixelShader = UmdSetPixelShader;
    pCreateData->pDeviceFuncs->pfnDeletePixelShader = UmdDeletePixelShader;
    pCreateData->pDeviceFuncs->pfnSetTexture = UmdSetTexture;
    pCreateData->pDeviceFuncs->pfnSetTextureStageState = UmdSetTextureStageState;
    pCreateData->pDeviceFuncs->pfnGetQueryData = UmdQueryGetData;
    pCreateData->pDeviceFuncs->pfnIssueQuery = UmdIssueQuery;
    pCreateData->pDeviceFuncs->pfnCreateQuery = UmdCreateQuery;
    pCreateData->pDeviceFuncs->pfnDestroyQuery = UmdDestroyQuery;

    pCreateData->hDevice = (HANDLE)pDevice;
    return S_OK;
}

static HRESULT APIENTRY
UmdDestroyDevice(
    HANDLE hDevice
    )
{
    PAMDBC250_UMD_DEVICE pDevice = (PAMDBC250_UMD_DEVICE)hDevice;

    if (pDevice != NULL) {
        EnterCriticalSection(&pDevice->DeviceLock);
        pDevice->CommandBufferUsed = 0;
        if (pDevice->pCommandBuffer != NULL) {
            HeapFree(GetProcessHeap(), 0, pDevice->pCommandBuffer);
            pDevice->pCommandBuffer = NULL;
        }
        LeaveCriticalSection(&pDevice->DeviceLock);
        DeleteCriticalSection(&pDevice->DeviceLock);
        HeapFree(GetProcessHeap(), 0, pDevice);
    }

    return S_OK;
}

static HRESULT APIENTRY
UmdCreateResource(
    HANDLE hDevice,
    D3DDDIARG_CREATERESOURCE *pResource
    )
{
    PAMDBC250_UMD_DEVICE pDevice = (PAMDBC250_UMD_DEVICE)hDevice;
    PAMDBC250_UMD_RESOURCE pUmdResource;

    if (pDevice == NULL || pResource == NULL) {
        return E_INVALIDARG;
    }

    pUmdResource = (PAMDBC250_UMD_RESOURCE)HeapAlloc(
        GetProcessHeap(),
        HEAP_ZERO_MEMORY,
        sizeof(AMDBC250_UMD_RESOURCE));
    if (pUmdResource == NULL) {
        return E_OUTOFMEMORY;
    }

    pResource->hResource = (HANDLE)pUmdResource;
    return S_OK;
}

static HRESULT APIENTRY
UmdDestroyResource(
    HANDLE hDevice,
    HANDLE hResource
    )
{
    UNREFERENCED_PARAMETER(hDevice);

    if (hResource != NULL) {
        HeapFree(GetProcessHeap(), 0, hResource);
    }

    return S_OK;
}

static HRESULT APIENTRY
UmdDrawPrimitive(
    HANDLE hDevice,
    CONST D3DDDIARG_DRAWPRIMITIVE *pData,
    CONST UINT *pFlagBuffer
    )
{
    PAMDBC250_UMD_DEVICE pDevice = (PAMDBC250_UMD_DEVICE)hDevice;
    PULONG pCmd;
    HRESULT Hr;

    UNREFERENCED_PARAMETER(pFlagBuffer);

    if (pDevice == NULL || pData == NULL) {
        return E_INVALIDARG;
    }

    EnterCriticalSection(&pDevice->DeviceLock);
    Hr = Bc250EnsureCommandSpaceLocked(pDevice, 3 * sizeof(ULONG));
    if (FAILED(Hr)) {
        LeaveCriticalSection(&pDevice->DeviceLock);
        return Hr;
    }

    pCmd = (PULONG)((PUCHAR)pDevice->pCommandBuffer + pDevice->CommandBufferUsed);
    pCmd[0] = AMDBC250_PM4_TYPE3_HDR(AMDBC250_PM4_IT_DRAW_INDEX_AUTO, 3);
    pCmd[1] = pData->PrimitiveCount;
    pCmd[2] = (ULONG)pData->PrimitiveType;
    pDevice->CommandBufferUsed += 3 * sizeof(ULONG);
    LeaveCriticalSection(&pDevice->DeviceLock);

    return S_OK;
}

static HRESULT APIENTRY
UmdPresent(
    HANDLE hDevice,
    CONST D3DDDIARG_PRESENT *pData
    )
{
    UNREFERENCED_PARAMETER(pData);
    return UmdFlush(hDevice);
}

static HRESULT APIENTRY
UmdFlush(
    HANDLE hDevice
    )
{
    PAMDBC250_UMD_DEVICE pDevice = (PAMDBC250_UMD_DEVICE)hDevice;
    HRESULT Hr;

    if (pDevice == NULL) {
        return E_INVALIDARG;
    }

    EnterCriticalSection(&pDevice->DeviceLock);
    Hr = Bc250FlushLocked(pDevice);
    LeaveCriticalSection(&pDevice->DeviceLock);
    return Hr;
}

static HRESULT APIENTRY UmdLock(HANDLE hDevice, D3DDDIARG_LOCK *pData)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pData); return S_OK; }

static HRESULT APIENTRY UmdUnlock(HANDLE hDevice, CONST D3DDDIARG_UNLOCK *pData)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pData); return S_OK; }

static HRESULT APIENTRY UmdSetRenderState(HANDLE hDevice, CONST D3DDDIARG_RENDERSTATE *pData)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pData); return S_OK; }

static HRESULT APIENTRY UmdSetStreamSource(HANDLE hDevice, CONST D3DDDIARG_SETSTREAMSOURCE *pData)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pData); return S_OK; }

static HRESULT APIENTRY UmdSetIndices(HANDLE hDevice, CONST D3DDDIARG_SETINDICES *pData)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pData); return S_OK; }

static HRESULT APIENTRY UmdSetViewport(HANDLE hDevice, CONST D3DDDIARG_VIEWPORTINFO *pData)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pData); return S_OK; }

static HRESULT APIENTRY UmdSetScissorRect(HANDLE hDevice, CONST RECT *pRect)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pRect); return S_OK; }

static HRESULT APIENTRY UmdSetRenderTarget(HANDLE hDevice, CONST D3DDDIARG_SETRENDERTARGET *pData)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pData); return S_OK; }

static HRESULT APIENTRY UmdClear(HANDLE hDevice, CONST D3DDDIARG_CLEAR *pData, UINT NumRect, CONST RECT *pRect)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pData); UNREFERENCED_PARAMETER(NumRect); UNREFERENCED_PARAMETER(pRect); return S_OK; }

static HRESULT APIENTRY UmdCreateVertexShaderDecl(HANDLE hDevice, D3DDDIARG_CREATEVERTEXSHADERDECL *pData, CONST D3DDDIVERTEXELEMENT *pVertexElements)
{
    PAMDBC250_UMD_DEVICE pDevice = (PAMDBC250_UMD_DEVICE)hDevice;
    UNREFERENCED_PARAMETER(pVertexElements);
    if (pDevice == NULL || pData == NULL) {
        return E_INVALIDARG;
    }
    pData->ShaderHandle = Bc250AllocateFakeHandle(pDevice);
    return S_OK;
}

static HRESULT APIENTRY UmdSetVertexShaderDecl(HANDLE hDevice, HANDLE hShaderHandle)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hShaderHandle); return S_OK; }

static HRESULT APIENTRY UmdDeleteVertexShaderDecl(HANDLE hDevice, HANDLE hShaderHandle)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hShaderHandle); return S_OK; }

static HRESULT APIENTRY UmdCreateVertexShaderFunc(HANDLE hDevice, D3DDDIARG_CREATEVERTEXSHADERFUNC *pData, CONST UINT *pCode)
{
    PAMDBC250_UMD_DEVICE pDevice = (PAMDBC250_UMD_DEVICE)hDevice;
    UNREFERENCED_PARAMETER(pCode);
    if (pDevice == NULL || pData == NULL) {
        return E_INVALIDARG;
    }
    pData->ShaderHandle = Bc250AllocateFakeHandle(pDevice);
    return S_OK;
}

static HRESULT APIENTRY UmdSetVertexShaderFunc(HANDLE hDevice, HANDLE hShaderHandle)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hShaderHandle); return S_OK; }

static HRESULT APIENTRY UmdDeleteVertexShaderFunc(HANDLE hDevice, HANDLE hShaderHandle)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hShaderHandle); return S_OK; }

static HRESULT APIENTRY UmdCreatePixelShader(HANDLE hDevice, D3DDDIARG_CREATEPIXELSHADER *pData, CONST UINT *pCode)
{
    PAMDBC250_UMD_DEVICE pDevice = (PAMDBC250_UMD_DEVICE)hDevice;
    UNREFERENCED_PARAMETER(pCode);
    if (pDevice == NULL || pData == NULL) {
        return E_INVALIDARG;
    }
    pData->ShaderHandle = Bc250AllocateFakeHandle(pDevice);
    return S_OK;
}

static HRESULT APIENTRY UmdSetPixelShader(HANDLE hDevice, HANDLE hShaderHandle)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hShaderHandle); return S_OK; }

static HRESULT APIENTRY UmdDeletePixelShader(HANDLE hDevice, HANDLE hShaderHandle)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hShaderHandle); return S_OK; }

static HRESULT APIENTRY UmdSetTexture(HANDLE hDevice, UINT Stage, HANDLE hTexture)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(Stage); UNREFERENCED_PARAMETER(hTexture); return S_OK; }

static HRESULT APIENTRY UmdSetTextureStageState(HANDLE hDevice, CONST D3DDDIARG_TEXTURESTAGESTATE *pData)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pData); return S_OK; }

static HRESULT APIENTRY UmdSetSamplerState(HANDLE hDevice, UINT Sampler, D3DDDITEXTUREFILTERTYPE State, UINT Value)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(Sampler); UNREFERENCED_PARAMETER(State); UNREFERENCED_PARAMETER(Value); return S_OK; }

static HRESULT APIENTRY UmdQueryGetData(HANDLE hDevice, CONST D3DDDIARG_GETQUERYDATA *pData)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pData); return S_OK; }

static HRESULT APIENTRY UmdIssueQuery(HANDLE hDevice, CONST D3DDDIARG_ISSUEQUERY *pData)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(pData); return S_OK; }

static HRESULT APIENTRY UmdCreateQuery(HANDLE hDevice, D3DDDIARG_CREATEQUERY *pData)
{
    PAMDBC250_UMD_DEVICE pDevice = (PAMDBC250_UMD_DEVICE)hDevice;
    if (pDevice == NULL || pData == NULL) {
        return E_INVALIDARG;
    }
    pData->hQuery = Bc250AllocateFakeHandle(pDevice);
    return S_OK;
}

static HRESULT APIENTRY UmdDestroyQuery(HANDLE hDevice, HANDLE hQuery)
{ UNREFERENCED_PARAMETER(hDevice); UNREFERENCED_PARAMETER(hQuery); return S_OK; }
