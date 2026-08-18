/**
 * DDI Stubs for GPU driver (atikmdag.sys)
 * Display-only DDI functions required by DxgkInitializeDisplayOnlyDriver
 */

#include <ntddk.h>
#include <dispmprt.h>

/* Stub: QueryVidPnHWCapability */
NTSTATUS APIENTRY
DreamV3DdiQueryVidPnHwCapability(
    _In_ CONST HANDLE hAdapter,
    _Inout_ PVOID pVidPnHWCaps)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pVidPnHWCaps);
    return STATUS_NOT_IMPLEMENTED;
}

/* Stub: PresentDisplayOnly */
NTSTATUS APIENTRY
DreamV3DdiPresentDisplayOnly(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_PRESENT_DISPLAYONLY *pPresentDisplayOnly)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pPresentDisplayOnly);
    return STATUS_NOT_IMPLEMENTED;
}

/* Stub: StopDeviceAndReleasePostDisplayOwnership */
NTSTATUS APIENTRY
DreamV3DdiStopDeviceAndReleasePostDisplayOwnership(
    _In_ PVOID pDeviceContext,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _Out_ PDXGK_DISPLAY_INFORMATION DisplayInfo)
{
    UNREFERENCED_PARAMETER(pDeviceContext);
    UNREFERENCED_PARAMETER(TargetId);
    UNREFERENCED_PARAMETER(DisplayInfo);
    return STATUS_NOT_IMPLEMENTED;
}

/* Stub: SystemDisplayEnable */
NTSTATUS APIENTRY
DreamV3DdiSystemDisplayEnable(
    _In_ PVOID pDeviceContext,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _In_ PVOID Flags,
    _Out_ UINT* Width,
    _Out_ UINT* Height,
    _Out_ D3DDDIFORMAT* ColorFormat)
{
    UNREFERENCED_PARAMETER(pDeviceContext);
    UNREFERENCED_PARAMETER(TargetId);
    UNREFERENCED_PARAMETER(Flags);
    if (Width) *Width = 0;
    if (Height) *Height = 0;
    if (ColorFormat) *ColorFormat = D3DDDIFMT_UNKNOWN;
    return STATUS_NOT_IMPLEMENTED;
}

/* Stub: SystemDisplayWrite */
VOID APIENTRY
DreamV3DdiSystemDisplayWrite(
    _In_ PVOID pDeviceContext,
    _In_ VOID* Source,
    _In_ UINT SourceWidth,
    _In_ UINT SourceHeight,
    _In_ UINT SourceStride,
    _In_ UINT PositionX,
    _In_ UINT PositionY)
{
    UNREFERENCED_PARAMETER(pDeviceContext);
    UNREFERENCED_PARAMETER(Source);
    UNREFERENCED_PARAMETER(SourceWidth);
    UNREFERENCED_PARAMETER(SourceHeight);
    UNREFERENCED_PARAMETER(SourceStride);
    UNREFERENCED_PARAMETER(PositionX);
    UNREFERENCED_PARAMETER(PositionY);
}
