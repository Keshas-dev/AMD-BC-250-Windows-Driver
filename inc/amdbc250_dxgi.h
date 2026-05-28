/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    amdbc250_dxgi.h

Abstract:
    Definitions for the Microsoft DirectX Graphics Infrastructure (DXGI) DDI.

    This header defines the structures and function prototypes required for the
    User-Mode Display Driver (UMD) to implement DXGI functionality, including
    adapter enumeration, swap chain creation, and presentation.

Environment:
    User mode (loaded by dxgkrnl.sys thunks)

--*/

#ifndef _AMDBC250_DXGI_H_
#define _AMDBC250_DXGI_H_

#include <d3dkmthk.h>
#include <dxgiddi.h>

// Forward declaration of the UMD device context
typedef struct _AMDBC250_UMD_DEVICE *PAMDBC250_UMD_DEVICE;

//=============================================================================
// DXGI DDI Functions
//=============================================================================

// DDI_DXGKDDI_QUERYADAPTERINFO
// Queries for adapter capabilities and information.
NTSTATUS APIENTRY
Bc250DxgiQueryAdapterInfo(
    _In_ CONST HANDLE hAdapter,
    _Inout_ DXGI_DDI_ARG_QUERYADAPTERINFO* pQueryAdapterInfo
    );

// DDI_DXGKDDI_CREATEDEVICE
// Creates a logical DXGI device.
NTSTATUS APIENTRY
Bc250DxgiCreateDevice(
    _In_ CONST HANDLE hAdapter,
    _Inout_ DXGI_DDI_ARG_CREATEDEVICE* pCreateDevice
    );

// DDI_DXGKDDI_DESTROYDEVICE
// Destroys a logical DXGI device.
VOID APIENTRY
Bc250DxgiDestroyDevice(
    _In_ CONST HANDLE hDevice
    );

// DDI_DXGKDDI_PRESENT
// Presents rendered content to a display output.
NTSTATUS APIENTRY
Bc250DxgiPresent(
    _In_ CONST HANDLE hDevice,
    _In_ CONST DXGI_DDI_ARG_PRESENT* pPresent
    );

// DDI_DXGKDDI_GETSCANLINEDDI
// Queries for information about the current scan line.
NTSTATUS APIENTRY
Bc250DxgiGetScanLine(
    _In_ CONST HANDLE hDevice,
    _Inout_ DXGI_DDI_ARG_GETSCANLINE* pGetScanLine
    );

// DDI_DXGKDDI_SETRESOURCEPRIORITY
// Sets the priority of a resource.
NTSTATUS APIENTRY
Bc250DxgiSetResourcePriority(
    _In_ CONST HANDLE hDevice,
    _In_ CONST DXGI_DDI_ARG_SETRESOURCEPRIORITY* pSetResourcePriority
    );

// DDI_DXGKDDI_QUERYRESOURCERESIDENCY
// Queries for the residency status of a list of resources.
NTSTATUS APIENTRY
Bc250DxgiQueryResourceResidency(
    _In_ CONST HANDLE hDevice,
    _In_ CONST DXGI_DDI_ARG_QUERYRESOURCERESIDENCY* pQueryResourceResidency
    );

// DDI_DXGKDDI_ROTATEPRESENT
// Rotates content before presenting.
NTSTATUS APIENTRY
Bc250DxgiRotatePresent(
    _In_ CONST HANDLE hDevice,
    _In_ CONST DXGI_DDI_ARG_ROTATEPRESENT* pRotatePresent
    );

// DDI_DXGKDDI_SETDISPLAYMODE
// Sets the display mode.
NTSTATUS APIENTRY
Bc250DxgiSetDisplayMode(
    _In_ CONST HANDLE hDevice,
    _In_ CONST DXGI_DDI_ARG_SETDISPLAYMODE* pSetDisplayMode
    );

// DDI_DXGKDDI_SETGAMMA
// Sets the gamma ramp.
NTSTATUS APIENTRY
Bc250DxgiSetGamma(
    _In_ CONST HANDLE hDevice,
    _In_ CONST DXGI_DDI_ARG_SETGAMMA* pSetGamma
    );

// DDI_DXGKDDI_SETVIDPNSOURCEADDRESS
// Sets the address of the video present network (VidPN) source.
NTSTATUS APIENTRY
Bc250DxgiSetVidPnSourceAddress(
    _In_ CONST HANDLE hDevice,
    _In_ CONST DXGI_DDI_ARG_SETVIDPNSOURCEADDRESS* pSetVidPnSourceAddress
    );

// DDI_DXGKDDI_WAITFORVERTICALBLANK
// Waits for the vertical blanking interval to occur.
NTSTATUS APIENTRY
Bc250DxgiWaitForVerticalBlank(
    _In_ CONST HANDLE hDevice,
    _In_ CONST DXGI_DDI_ARG_WAITFORVERTICALBLANK* pWaitForVerticalBlank
    );

// DDI_DXGKDDI_OFFERRESOURCES
// Offers video memory resources for reuse.
NTSTATUS APIENTRY
Bc250DxgiOfferResources(
    _In_ CONST HANDLE hDevice,
    _In_ CONST DXGI_DDI_ARG_OFFERRESOURCES* pOfferResources
    );

// DDI_DXGKDDI_RECLAIMRESOURCES
// Reclaims video memory resources that were previously offered.
NTSTATUS APIENTRY
Bc250DxgiReclaimResources(
    _In_ CONST HANDLE hDevice,
    _In_ CONST DXGI_DDI_ARG_RECLAIMRESOURCES* pReclaimResources
    );

// DDI_DXGKDDI_GETDEVICEREMOVALREASON
// Retrieves the reason for device removal.
NTSTATUS APIENTRY
Bc250DxgiGetDeviceRemovalReason(
    _In_ CONST HANDLE hDevice,
    _Inout_ DXGI_DDI_ARG_GETDEVICEREMOVALREASON* pGetDeviceRemovalReason
    );

// DDI_DXGKDDI_BLT
// Performs a bit-block transfer (blit) operation.
NTSTATUS APIENTRY
Bc250DxgiBlt(
    _In_ CONST HANDLE hDevice,
    _In_ CONST DXGI_DDI_ARG_BLT* pBlt
    );

// DDI_DXGKDDI_RESOLVESHAREDRESOURCE
// Resolves a shared resource.
NTSTATUS APIENTRY
Bc250DxgiResolveSharedResource(
    _In_ CONST HANDLE hDevice,
    _In_ CONST DXGI_DDI_ARG_RESOLVESHAREDRESOURCE* pResolveSharedResource
    );

// DDI_DXGKDDI_GETFORCEDPRESENTINTERVAL
// Retrieves the forced present interval.
NTSTATUS APIENTRY
Bc250DxgiGetForcedPresentInterval(
    _In_ CONST HANDLE hDevice,
    _Inout_ DXGI_DDI_ARG_GETFORCEDPRESENTINTERVAL* pGetForcedPresentInterval
    );

// DDI_DXGKDDI_PRESENTMULTIPLANEOVERLAY
// Presents content from multiple overlay planes.
NTSTATUS APIENTRY
Bc250DxgiPresentMultiPlaneOverlay(
    _In_ CONST HANDLE hDevice,
    _In_ CONST DXGI_DDI_ARG_PRESENTMULTIPLANEOVERLAY* pPresentMultiPlaneOverlay
    );

// DDI_DXGKDDI_GETMULTIPLANEOVERLAYCAPS
// Retrieves multi-plane overlay capabilities.
NTSTATUS APIENTRY
Bc250DxgiGetMultiPlaneOverlayCaps(
    _In_ CONST HANDLE hAdapter,
    _Inout_ DXGI_DDI_ARG_GETMULTIPLANEOVERLAYCAPS* pGetMultiPlaneOverlayCaps
    );

// DDI_DXGKDDI_CHECKMULTIPLANEOVERLAYSUPPORT
// Checks for multi-plane overlay support.
NTSTATUS APIENTRY
Bc250DxgiCheckMultiPlaneOverlaySupport(
    _In_ CONST HANDLE hDevice,
    _Inout_ DXGI_DDI_ARG_CHECKMULTIPLANEOVERLAYSUPPORT* pCheckMultiPlaneOverlaySupport
    );

// DDI_DXGKDDI_PRESENTMULTIPLANEOVERLAY2
// Presents content from multiple overlay planes (version 2).
NTSTATUS APIENTRY
Bc250DxgiPresentMultiPlaneOverlay2(
    _In_ CONST HANDLE hDevice,
    _In_ CONST DXGI_DDI_ARG_PRESENTMULTIPLANEOVERLAY2* pPresentMultiPlaneOverlay2
    );

// DDI_DXGKDDI_CHECKMULTIPLANEOVERLAYSUPPORT2
// Checks for multi-plane overlay support (version 2).
NTSTATUS APIENTRY
Bc250DxgiCheckMultiPlaneOverlaySupport2(
    _In_ CONST HANDLE hDevice,
    _Inout_ DXGI_DDI_ARG_CHECKMULTIPLANEOVERLAYSUPPORT2* pCheckMultiPlaneOverlaySupport2
    );

// DDI_DXGKDDI_PRESENTMULTIPLANEOVERLAY3
// Presents content from multiple overlay planes (version 3).
NTSTATUS APIENTRY
Bc250DxgiPresentMultiPlaneOverlay3(
    _In_ CONST HANDLE hDevice,
    _In_ CONST DXGI_DDI_ARG_PRESENTMULTIPLANEOVERLAY3* pPresentMultiPlaneOverlay3
    );

// DDI_DXGKDDI_CHECKMULTIPLANEOVERLAYSUPPORT3
// Checks for multi-plane overlay support (version 3).
NTSTATUS APIENTRY
Bc250DxgiCheckMultiPlaneOverlaySupport3(
    _In_ CONST HANDLE hDevice,
    _Inout_ DXGI_DDI_ARG_CHECKMULTIPLANEOVERLAYSUPPORT3* pCheckMultiPlaneOverlaySupport3
    );

#endif // _AMDBC250_DXGI_H_
