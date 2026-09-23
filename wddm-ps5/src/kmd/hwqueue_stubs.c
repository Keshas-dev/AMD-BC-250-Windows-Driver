/*===========================================================================
   WDDM 3.2 Required Hardware Queue Callbacks
   dxgkrnl on Win11 26100 requires these for WDDM 3.2 registration.
===========================================================================*/

NTSTATUS
APIENTRY
Bc250DdiCreateHwQueue(
    _In_  PVOID  MiniportDeviceContext,
    _In_  PDXGKARG_CREATEHWQUEUE  CreateHwQueue
    )
{
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)MiniportDeviceContext;
    UNREFERENCED_PARAMETER(CreateHwQueue);
    KdPrint(("AMDBC250: DxgkDdiCreateHwQueue called\n"));
    CreateHwQueue->QueueHandle = (PVOID)0xDEADBEEF;
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Bc250DdiDestroyHwQueue(
    _In_  PVOID  MiniportDeviceContext,
    _In_  PVOID  QueueHandle
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(QueueHandle);
    KdPrint(("AMDBC250: DxgkDdiDestroyHwQueue called\n"));
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Bc250DdiSubmitCommandToHwQueue(
    _In_  PVOID  MiniportDeviceContext,
    _In_  PDXGKARG_SUBMITCOMMANDTOHWQUEUE  SubmitCommandToHwQueue
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(SubmitCommandToHwQueue);
    KdPrint(("AMDBC250: DxgkDdiSubmitCommandToHwQueue called\n"));
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Bc250DdiSubmitCommandVirtual(
    _In_  PVOID  MiniportDeviceContext,
    _In_  PDXGKARG_SUBMITCOMMANDVIRTUAL  SubmitCommandVirtual
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(SubmitCommandVirtual);
    KdPrint(("AMDBC250: DxgkDdiSubmitCommandVirtual called\n"));
    return STATUS_SUCCESS;
}
