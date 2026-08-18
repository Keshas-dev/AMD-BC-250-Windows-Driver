/******************************Module*Header*******************************\
* Module Name: BDD_DDI.cxx
*
* Basic Display Driver DDI entry points redirects
* BC-250 Enhanced: Added comprehensive DbgPrint diagnostics
*
*
* Copyright (c) 2010 Microsoft Corporation
\**************************************************************************/

#include "BDD.hxx"


//
// BC-250: Escape command protocol (user-mode D3DKMTEscape -> pPrivateDriverData).
// The Escape DDI can be called at DISPATCH_LEVEL, so all SMU/register access
// below is Non-Paged and uses volatile READ/WRITE_REGISTER intrinsics only.
//
#define BC250_ESC_MAGIC 0xBC2501CA

typedef enum _BC250_ESC_CMD {
    BC250_ESC_READ_REG = 1,     // Arg1=Offset -> Result
    BC250_ESC_WRITE_REG = 2,    // Arg1=Offset, Arg2=Value
    BC250_ESC_READ_SMN = 3,     // Arg1=SmnAddr -> Result
    BC250_ESC_WRITE_SMN = 4,    // Arg1=SmnAddr, Arg2=Value
    BC250_ESC_SMU_QUERY = 5,    // Arg1=Msg -> Result (arg=0, Q0)
    BC250_ESC_SMU_QUERY_PARAM = 6, // Arg1=Msg, Arg2=Param -> Result (Q0)
    BC250_ESC_GET_GPU_ID = 7,   // -> Result
    BC250_ESC_GET_STATUS = 8,   // Arg1=0: SCRATCH/GRBM_STATUS/GPU_ID/SMU version
    BC250_ESC_SMU_SEND = 9,     // Arg1=(Queue<<16)|Msg, Arg2=Param -> Result
    BC250_ESC_CORE_UNLOCK = 10  // -> Result (0x00FF write to core mask)
} BC250_ESC_CMD;

typedef struct _BC250_ESC_BUFFER {
    ULONG Magic;
    ULONG Command;
    ULONG Arg1;
    ULONG Arg2;
    ULONG Result;
    NTSTATUS Status;
} BC250_ESC_BUFFER, *PBC250_ESC_BUFFER;

C_ASSERT(sizeof(BC250_ESC_BUFFER) == 24);

NTSTATUS
APIENTRY
BddDdiEscape(
    _In_ VOID*                     pDeviceContext,
    _In_ CONST DXGKARG_ESCAPE*     pEscape);

#pragma code_seg(push)
#pragma code_seg("INIT")
// BEGIN: Init Code

//
// Driver Entry point
//

extern "C"
NTSTATUS
DriverEntry(
    _In_  DRIVER_OBJECT*  pDriverObject,
    _In_  UNICODE_STRING* pRegistryPath)
{
    PAGED_CODE();

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: DriverEntry called\n");

    // Initialize DDI function pointers and dxgkrnl
    KMDDOD_INITIALIZATION_DATA InitialData = {0};

    InitialData.Version = DXGKDDI_INTERFACE_VERSION;

    InitialData.DxgkDdiAddDevice                    = BddDdiAddDevice;
    InitialData.DxgkDdiStartDevice                  = BddDdiStartDevice;
    InitialData.DxgkDdiStopDevice                   = BddDdiStopDevice;
    InitialData.DxgkDdiResetDevice                  = BddDdiResetDevice;
    InitialData.DxgkDdiRemoveDevice                 = BddDdiRemoveDevice;
    InitialData.DxgkDdiDispatchIoRequest            = BddDdiDispatchIoRequest;
    InitialData.DxgkDdiInterruptRoutine             = BddDdiInterruptRoutine;
    InitialData.DxgkDdiDpcRoutine                   = BddDdiDpcRoutine;
    InitialData.DxgkDdiQueryChildRelations          = BddDdiQueryChildRelations;
    InitialData.DxgkDdiQueryChildStatus             = BddDdiQueryChildStatus;
    InitialData.DxgkDdiQueryDeviceDescriptor        = BddDdiQueryDeviceDescriptor;
    InitialData.DxgkDdiSetPowerState                = BddDdiSetPowerState;
    InitialData.DxgkDdiUnload                       = BddDdiUnload;
    InitialData.DxgkDdiQueryAdapterInfo             = BddDdiQueryAdapterInfo;
    InitialData.DxgkDdiSetPointerPosition           = BddDdiSetPointerPosition;
    InitialData.DxgkDdiSetPointerShape              = BddDdiSetPointerShape;
    InitialData.DxgkDdiIsSupportedVidPn             = BddDdiIsSupportedVidPn;
    InitialData.DxgkDdiRecommendFunctionalVidPn     = BddDdiRecommendFunctionalVidPn;
    InitialData.DxgkDdiEnumVidPnCofuncModality      = BddDdiEnumVidPnCofuncModality;
    InitialData.DxgkDdiSetVidPnSourceVisibility     = BddDdiSetVidPnSourceVisibility;
    InitialData.DxgkDdiCommitVidPn                  = BddDdiCommitVidPn;
    InitialData.DxgkDdiUpdateActiveVidPnPresentPath = BddDdiUpdateActiveVidPnPresentPath;
    InitialData.DxgkDdiRecommendMonitorModes        = BddDdiRecommendMonitorModes;
    InitialData.DxgkDdiQueryVidPnHWCapability       = BddDdiQueryVidPnHWCapability;
    InitialData.DxgkDdiPresentDisplayOnly           = BddDdiPresentDisplayOnly;
    InitialData.DxgkDdiStopDeviceAndReleasePostDisplayOwnership = BddDdiStopDeviceAndReleasePostDisplayOwnership;
    InitialData.DxgkDdiSystemDisplayEnable          = BddDdiSystemDisplayEnable;
    InitialData.DxgkDdiSystemDisplayWrite           = BddDdiSystemDisplayWrite;
    InitialData.DxgkDdiEscape                       = BddDdiEscape;

    NTSTATUS Status = DxgkInitializeDisplayOnlyDriver(pDriverObject, pRegistryPath, &InitialData);
    if (!NT_SUCCESS(Status))
    {
        BDD_LOG_ERROR1("DxgkInitializeDisplayOnlyDriver failed with Status: 0x%I64x", Status);
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: DriverEntry - DxgkInitializeDisplayOnlyDriver FAILED with Status: 0x%08lX\n", Status);
        return Status;
    }

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: DriverEntry - SUCCESS, Status: 0x%08lX\n", Status);

    return Status;
}
// END: Init Code
#pragma code_seg(pop)

#pragma code_seg(push)
#pragma code_seg("PAGE")

//
// PnP DDIs
//

VOID
BddDdiUnload(VOID)
{
    PAGED_CODE();
    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiUnload entered\n");
}

NTSTATUS
BddDdiAddDevice(
    _In_ DEVICE_OBJECT* pPhysicalDeviceObject,
    _Outptr_ PVOID*  ppDeviceContext)
{
    PAGED_CODE();

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiAddDevice entered - pPhysicalDeviceObject: 0x%p\n", pPhysicalDeviceObject);

    if ((pPhysicalDeviceObject == NULL) ||
        (ppDeviceContext == NULL))
    {
        BDD_LOG_ERROR2("One of pPhysicalDeviceObject (0x%I64x), ppDeviceContext (0x%I64x) is NULL",
                        pPhysicalDeviceObject, ppDeviceContext);
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiAddDevice - NULL parameter, returning STATUS_INVALID_PARAMETER\n");
        return STATUS_INVALID_PARAMETER;
    }
    *ppDeviceContext = NULL;

    // BC-250: Properly allocate BASIC_DISPLAY_DRIVER via new operator
    // The operator new is already defined in memory.cxx to use ExAllocatePool2
    BASIC_DISPLAY_DRIVER* pBDD = new (NonPagedPool) BASIC_DISPLAY_DRIVER(pPhysicalDeviceObject);
    if (pBDD == NULL)
    {
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiAddDevice - new BASIC_DISPLAY_DRIVER FAILED\n");
        return STATUS_NO_MEMORY;
    }

    *ppDeviceContext = pBDD;
    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiAddDevice - SUCCESS, pBDD: 0x%p\n", pBDD);

    return STATUS_SUCCESS;

}

NTSTATUS
BddDdiRemoveDevice(
    _In_  VOID* pDeviceContext)
{
    PAGED_CODE();

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiRemoveDevice entered - pDeviceContext: 0x%p\n", pDeviceContext);

    BASIC_DISPLAY_DRIVER* pBDD = reinterpret_cast<BASIC_DISPLAY_DRIVER*>(pDeviceContext);

    if (pBDD)
    {
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiRemoveDevice - deleting pBDD: 0x%p\n", pBDD);
        delete pBDD;
        pBDD = NULL;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
BddDdiStartDevice(
    _In_  VOID*              pDeviceContext,
    _In_  DXGK_START_INFO*   pDxgkStartInfo,
    _In_  DXGKRNL_INTERFACE* pDxgkInterface,
    _Out_ ULONG*             pNumberOfViews,
    _Out_ ULONG*             pNumberOfChildren)
{
    PAGED_CODE();
    BDD_ASSERT_CHK(pDeviceContext != NULL);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiStartDevice entered\n");

    BASIC_DISPLAY_DRIVER* pBDD = reinterpret_cast<BASIC_DISPLAY_DRIVER*>(pDeviceContext);
    // Call the real StartDevice which sets MAX_VIEWS/MAX_CHILDREN (1/1) and
    // handles BC-250 headless mode (no POST display) gracefully.
    NTSTATUS Status = pBDD->StartDevice(pDxgkStartInfo, pDxgkInterface, pNumberOfViews, pNumberOfChildren);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiStartDevice - Status: 0x%08lX, Views: %lu, Children: %lu\n",
             Status,
             pNumberOfViews ? *pNumberOfViews : 0,
             pNumberOfChildren ? *pNumberOfChildren : 0);

    return Status;
}

NTSTATUS
BddDdiStopDevice(
    _In_  VOID* pDeviceContext)
{
    PAGED_CODE();
    BDD_ASSERT_CHK(pDeviceContext != NULL);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiStopDevice entered\n");

    BASIC_DISPLAY_DRIVER* pBDD = reinterpret_cast<BASIC_DISPLAY_DRIVER*>(pDeviceContext);
    NTSTATUS Status = pBDD->StopDevice();

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiStopDevice - Status: 0x%08lX\n", Status);

    return Status;
}


NTSTATUS
BddDdiDispatchIoRequest(
    _In_  VOID*                 pDeviceContext,
    _In_  ULONG                 VidPnSourceId,
    _In_  VIDEO_REQUEST_PACKET* pVideoRequestPacket)
{
    PAGED_CODE();
    BDD_ASSERT_CHK(pDeviceContext != NULL);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiDispatchIoRequest entered - VidPnSourceId: %lu\n", VidPnSourceId);

    BASIC_DISPLAY_DRIVER* pBDD = reinterpret_cast<BASIC_DISPLAY_DRIVER*>(pDeviceContext);
    if (!pBDD->IsDriverActive())
    {
        BDD_LOG_ASSERTION1("BDD (0x%I64x) is being called when not active!", pBDD);
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiDispatchIoRequest - Driver NOT active\n");
        return STATUS_UNSUCCESSFUL;
    }
    NTSTATUS Status = pBDD->DispatchIoRequest(VidPnSourceId, pVideoRequestPacket);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiDispatchIoRequest - Status: 0x%08lX\n", Status);

    return Status;
}

NTSTATUS
BddDdiSetPowerState(
    _In_  VOID*              pDeviceContext,
    _In_  ULONG              HardwareUid,
    _In_  DEVICE_POWER_STATE DevicePowerState,
    _In_  POWER_ACTION       ActionType)
{
    PAGED_CODE();
    BDD_ASSERT_CHK(pDeviceContext != NULL);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiSetPowerState entered - HardwareUid: %lu, PowerState: %d, ActionType: %d\n",
             HardwareUid, DevicePowerState, ActionType);

    BASIC_DISPLAY_DRIVER* pBDD = reinterpret_cast<BASIC_DISPLAY_DRIVER*>(pDeviceContext);
    if (!pBDD->IsDriverActive())
    {
        // If the driver isn't active, SetPowerState can still be called, however in BDD's case
        // this shouldn't do anything, as it could for instance be called on BDD Fallback after
        // Fallback has been stopped and BDD PnP is being started. Fallback doesn't have control
        // of the hardware in this case.
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiSetPowerState - Driver NOT active, returning success\n");
        return STATUS_SUCCESS;
    }
    NTSTATUS Status = pBDD->SetPowerState(HardwareUid, DevicePowerState, ActionType);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiSetPowerState - Status: 0x%08lX\n", Status);

    return Status;
}

NTSTATUS
BddDdiQueryChildRelations(
    _In_                             VOID*                  pDeviceContext,
    _Out_writes_bytes_(ChildRelationsSize) DXGK_CHILD_DESCRIPTOR* pChildRelations,
    _In_                             ULONG                  ChildRelationsSize)
{
    PAGED_CODE();
    BDD_ASSERT_CHK(pDeviceContext != NULL);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiQueryChildRelations entered - ChildRelationsSize: %lu\n", ChildRelationsSize);

    BASIC_DISPLAY_DRIVER* pBDD = reinterpret_cast<BASIC_DISPLAY_DRIVER*>(pDeviceContext);
    NTSTATUS Status = pBDD->QueryChildRelations(pChildRelations, ChildRelationsSize);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiQueryChildRelations - Status: 0x%08lX\n", Status);

    return Status;
}

NTSTATUS
BddDdiQueryChildStatus(
    _In_    VOID*              pDeviceContext,
    _Inout_ DXGK_CHILD_STATUS* pChildStatus,
    _In_    BOOLEAN            NonDestructiveOnly)
{
    PAGED_CODE();
    BDD_ASSERT_CHK(pDeviceContext != NULL);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiQueryChildStatus entered - NonDestructiveOnly: %d\n", NonDestructiveOnly);

    BASIC_DISPLAY_DRIVER* pBDD = reinterpret_cast<BASIC_DISPLAY_DRIVER*>(pDeviceContext);
    NTSTATUS Status = pBDD->QueryChildStatus(pChildStatus, NonDestructiveOnly);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiQueryChildStatus - Status: 0x%08lX\n", Status);

    return Status;
}

NTSTATUS
BddDdiQueryDeviceDescriptor(
    _In_  VOID*                     pDeviceContext,
    _In_  ULONG                     ChildUid,
    _Inout_ DXGK_DEVICE_DESCRIPTOR* pDeviceDescriptor)
{
    PAGED_CODE();
    BDD_ASSERT_CHK(pDeviceContext != NULL);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiQueryDeviceDescriptor entered - ChildUid: %lu\n", ChildUid);

    BASIC_DISPLAY_DRIVER* pBDD = reinterpret_cast<BASIC_DISPLAY_DRIVER*>(pDeviceContext);
    if (!pBDD->IsDriverActive())
    {
        // During stress testing of PnPStop, it is possible for BDD Fallback to get called to start then stop in quick succession.
        // The first call queues a worker thread item indicating that it now has a child device, the second queues a worker thread
        // item that it no longer has any child device. This function gets called based on the first worker thread item, but after
        // the driver has been stopped. Therefore instead of asserting like other functions, we only warn.
        BDD_LOG_WARNING1("BDD (0x%I64x) is being called when not active!", pBDD);
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiQueryDeviceDescriptor - Driver NOT active\n");
        return STATUS_UNSUCCESSFUL;
    }
    NTSTATUS Status = pBDD->QueryDeviceDescriptor(ChildUid, pDeviceDescriptor);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiQueryDeviceDescriptor - Status: 0x%08lX\n", Status);

    return Status;
}


//
// WDDM Display Only Driver DDIs
//

NTSTATUS
APIENTRY
BddDdiQueryAdapterInfo(
    _In_ CONST HANDLE                    hAdapter,
    _In_ CONST DXGKARG_QUERYADAPTERINFO* pQueryAdapterInfo)
{
    PAGED_CODE();
    BDD_ASSERT_CHK(hAdapter != NULL);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiQueryAdapterInfo entered - hAdapter: 0x%p\n", hAdapter);

    BASIC_DISPLAY_DRIVER* pBDD = reinterpret_cast<BASIC_DISPLAY_DRIVER*>(hAdapter);
    NTSTATUS Status = pBDD->QueryAdapterInfo(pQueryAdapterInfo);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiQueryAdapterInfo - Status: 0x%08lX\n", Status);

    return Status;
}

NTSTATUS
APIENTRY
BddDdiSetPointerPosition(
    _In_ CONST HANDLE                      hAdapter,
    _In_ CONST DXGKARG_SETPOINTERPOSITION* pSetPointerPosition)
{
    PAGED_CODE();
    BDD_ASSERT_CHK(hAdapter != NULL);

    // BC-250: pointer position fires on every mouse move, gate the log
    static LONG PointerPosLogCount = 0;
    if (InterlockedIncrement(&PointerPosLogCount) == 1)
    {
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiSetPointerPosition entered (first) - hAdapter: 0x%p\n", hAdapter);
    }

    BASIC_DISPLAY_DRIVER* pBDD = reinterpret_cast<BASIC_DISPLAY_DRIVER*>(hAdapter);
    if (!pBDD->IsDriverActive())
    {
        BDD_LOG_ASSERTION1("BDD (0x%I64x) is being called when not active!", pBDD);
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiSetPointerPosition - Driver NOT active\n");
        return STATUS_UNSUCCESSFUL;
    }
    NTSTATUS Status = pBDD->SetPointerPosition(pSetPointerPosition);

    // BC-250: only log status on failure
    if (!NT_SUCCESS(Status))
    {
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiSetPointerPosition - Status: 0x%08lX\n", Status);
    }

    return Status;
}

NTSTATUS
APIENTRY
BddDdiSetPointerShape(
    _In_ CONST HANDLE                   hAdapter,
    _In_ CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape)
{
    PAGED_CODE();
    BDD_ASSERT_CHK(hAdapter != NULL);

    // BC-250: pointer shape fires on every pointer shape change, gate the log
    static LONG PointerShapeLogCount = 0;
    if (InterlockedIncrement(&PointerShapeLogCount) == 1)
    {
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiSetPointerShape entered (first) - hAdapter: 0x%p\n", hAdapter);
    }

    BASIC_DISPLAY_DRIVER* pBDD = reinterpret_cast<BASIC_DISPLAY_DRIVER*>(hAdapter);
    if (!pBDD->IsDriverActive())
    {
        BDD_LOG_ASSERTION1("BDD (0x%I64x) is being called when not active!", pBDD);
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiSetPointerShape - Driver NOT active\n");
        return STATUS_UNSUCCESSFUL;
    }
    NTSTATUS Status = pBDD->SetPointerShape(pSetPointerShape);

    // BC-250: only log status on failure
    if (!NT_SUCCESS(Status))
    {
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiSetPointerShape - Status: 0x%08lX\n", Status);
    }

    return Status;
}


NTSTATUS
APIENTRY
BddDdiPresentDisplayOnly(
    _In_ CONST HANDLE                       hAdapter,
    _In_ CONST DXGKARG_PRESENT_DISPLAYONLY* pPresentDisplayOnly)
{
    PAGED_CODE();
    BDD_ASSERT_CHK(hAdapter != NULL);

    // BC-250: Only log the first present to avoid per-frame DbgPrint spam (this runs every vsync)
    static LONG PresentLogCount = 0;
    if (InterlockedIncrement(&PresentLogCount) == 1)
    {
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiPresentDisplayOnly entered (first) - hAdapter: 0x%p\n", hAdapter);
    }

    BASIC_DISPLAY_DRIVER* pBDD = reinterpret_cast<BASIC_DISPLAY_DRIVER*>(hAdapter);
    if (!pBDD->IsDriverActive())
    {
        BDD_LOG_ASSERTION1("BDD (0x%I64x) is being called when not active!", pBDD);
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiPresentDisplayOnly - Driver NOT active\n");
        return STATUS_UNSUCCESSFUL;
    }
    NTSTATUS Status = pBDD->PresentDisplayOnly(pPresentDisplayOnly);

    // BC-250: log status only on failure to avoid per-frame spam
    if (!NT_SUCCESS(Status))
    {
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiPresentDisplayOnly - Status: 0x%08lX\n", Status);
    }

    return Status;
}

NTSTATUS
APIENTRY
BddDdiStopDeviceAndReleasePostDisplayOwnership(
    _In_  VOID*                          pDeviceContext,
    _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _Out_ DXGK_DISPLAY_INFORMATION*      DisplayInfo)
{
    PAGED_CODE();
    BDD_ASSERT_CHK(pDeviceContext != NULL);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiStopDeviceAndReleasePostDisplayOwnership entered - TargetId: %lu\n", TargetId);

    BASIC_DISPLAY_DRIVER* pBDD = reinterpret_cast<BASIC_DISPLAY_DRIVER*>(pDeviceContext);
    NTSTATUS Status = pBDD->StopDeviceAndReleasePostDisplayOwnership(TargetId, DisplayInfo);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiStopDeviceAndReleasePostDisplayOwnership - Status: 0x%08lX\n", Status);

    return Status;
}

NTSTATUS
APIENTRY
BddDdiIsSupportedVidPn(
    _In_ CONST HANDLE                 hAdapter,
    _Inout_ DXGKARG_ISSUPPORTEDVIDPN* pIsSupportedVidPn)
{
    PAGED_CODE();
    BDD_ASSERT_CHK(hAdapter != NULL);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiIsSupportedVidPn entered\n");

    BASIC_DISPLAY_DRIVER* pBDD = reinterpret_cast<BASIC_DISPLAY_DRIVER*>(hAdapter);
    if (!pBDD->IsDriverActive())
    {
        // This path might hit because win32k/dxgport doesn't check that an adapter is active when taking the adapter lock.
        // The adapter lock is the main thing BDD Fallback relies on to not be called while it's inactive. It is still a rare
        // timing issue around PnpStart/Stop and isn't expected to have any effect on the stability of the system.
        BDD_LOG_WARNING1("BDD (0x%I64x) is being called when not active!", pBDD);
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiIsSupportedVidPn - Driver NOT active\n");
        return STATUS_UNSUCCESSFUL;
    }
    NTSTATUS Status = pBDD->IsSupportedVidPn(pIsSupportedVidPn);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiIsSupportedVidPn - Status: 0x%08lX\n", Status);

    return Status;
}

NTSTATUS
APIENTRY
BddDdiRecommendFunctionalVidPn(
    _In_ CONST HANDLE                                  hAdapter,
    _In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN* CONST pRecommendFunctionalVidPn)
{
    PAGED_CODE();
    BDD_ASSERT_CHK(hAdapter != NULL);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiRecommendFunctionalVidPn entered - hAdapter: 0x%p\n", hAdapter);

    BASIC_DISPLAY_DRIVER* pBDD = reinterpret_cast<BASIC_DISPLAY_DRIVER*>(hAdapter);
    if (!pBDD->IsDriverActive())
    {
        BDD_LOG_ASSERTION1("BDD (0x%I64x) is being called when not active!", pBDD);
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiRecommendFunctionalVidPn - Driver NOT active\n");
        return STATUS_UNSUCCESSFUL;
    }
    NTSTATUS Status = pBDD->RecommendFunctionalVidPn(pRecommendFunctionalVidPn);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiRecommendFunctionalVidPn - Status: 0x%08lX\n", Status);

    return Status;
}

NTSTATUS
APIENTRY
BddDdiRecommendVidPnTopology(
    _In_ CONST HANDLE                                 hAdapter,
    _In_ CONST DXGKARG_RECOMMENDVIDPNTOPOLOGY* CONST  pRecommendVidPnTopology)
{
    PAGED_CODE();
    BDD_ASSERT_CHK(hAdapter != NULL);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiRecommendVidPnTopology entered\n");

    BASIC_DISPLAY_DRIVER* pBDD = reinterpret_cast<BASIC_DISPLAY_DRIVER*>(hAdapter);
    if (!pBDD->IsDriverActive())
    {
        BDD_LOG_ASSERTION1("BDD (0x%I64x) is being called when not active!", pBDD);
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiRecommendVidPnTopology - Driver NOT active\n");
        return STATUS_UNSUCCESSFUL;
    }
    NTSTATUS Status = pBDD->RecommendVidPnTopology(pRecommendVidPnTopology);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiRecommendVidPnTopology - Status: 0x%08lX\n", Status);

    return Status;
}

NTSTATUS
APIENTRY
BddDdiRecommendMonitorModes(
    _In_ CONST HANDLE                                hAdapter,
    _In_ CONST DXGKARG_RECOMMENDMONITORMODES* CONST  pRecommendMonitorModes)
{
    PAGED_CODE();
    BDD_ASSERT_CHK(hAdapter != NULL);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiRecommendMonitorModes entered - hAdapter: 0x%p\n", hAdapter);

    BASIC_DISPLAY_DRIVER* pBDD = reinterpret_cast<BASIC_DISPLAY_DRIVER*>(hAdapter);
    if (!pBDD->IsDriverActive())
    {
        BDD_LOG_ASSERTION1("BDD (0x%I64x) is being called when not active!", pBDD);
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiRecommendMonitorModes - Driver NOT active\n");
        return STATUS_UNSUCCESSFUL;
    }
    NTSTATUS Status = pBDD->RecommendMonitorModes(pRecommendMonitorModes);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiRecommendMonitorModes - Status: 0x%08lX\n", Status);

    return Status;
}

NTSTATUS
APIENTRY
BddDdiEnumVidPnCofuncModality(
    _In_ CONST HANDLE                                 hAdapter,
    _In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY* CONST pEnumCofuncModality)
{
    PAGED_CODE();
    BDD_ASSERT_CHK(hAdapter != NULL);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiEnumVidPnCofuncModality entered - hAdapter: 0x%p\n", hAdapter);

    BASIC_DISPLAY_DRIVER* pBDD = reinterpret_cast<BASIC_DISPLAY_DRIVER*>(hAdapter);
    if (!pBDD->IsDriverActive())
    {
        BDD_LOG_ASSERTION1("BDD (0x%I64x) is being called when not active!", pBDD);
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiEnumVidPnCofuncModality - Driver NOT active\n");
        return STATUS_UNSUCCESSFUL;
    }
    NTSTATUS Status = pBDD->EnumVidPnCofuncModality(pEnumCofuncModality);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiEnumVidPnCofuncModality - Status: 0x%08lX\n", Status);

    return Status;
}

NTSTATUS
APIENTRY
BddDdiSetVidPnSourceVisibility(
    _In_ CONST HANDLE                            hAdapter,
    _In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY* pSetVidPnSourceVisibility)
{
    PAGED_CODE();
    BDD_ASSERT_CHK(hAdapter != NULL);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiSetVidPnSourceVisibility entered - hAdapter: 0x%p\n", hAdapter);

    BASIC_DISPLAY_DRIVER* pBDD = reinterpret_cast<BASIC_DISPLAY_DRIVER*>(hAdapter);
    if (!pBDD->IsDriverActive())
    {
        BDD_LOG_ASSERTION1("BDD (0x%I64x) is being called when not active!", pBDD);
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiSetVidPnSourceVisibility - Driver NOT active\n");
        return STATUS_UNSUCCESSFUL;
    }
    NTSTATUS Status = pBDD->SetVidPnSourceVisibility(pSetVidPnSourceVisibility);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiSetVidPnSourceVisibility - Status: 0x%08lX\n", Status);

    return Status;
}

NTSTATUS
APIENTRY
BddDdiCommitVidPn(
    _In_ CONST HANDLE                     hAdapter,
    _In_ CONST DXGKARG_COMMITVIDPN* CONST pCommitVidPn)
{
    PAGED_CODE();
    BDD_ASSERT_CHK(hAdapter != NULL);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiCommitVidPn entered - hAdapter: 0x%p\n", hAdapter);

    BASIC_DISPLAY_DRIVER* pBDD = reinterpret_cast<BASIC_DISPLAY_DRIVER*>(hAdapter);
    if (!pBDD->IsDriverActive())
    {
        BDD_LOG_ASSERTION1("BDD (0x%I64x) is being called when not active!", pBDD);
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiCommitVidPn - Driver NOT active\n");
        return STATUS_UNSUCCESSFUL;
    }
    NTSTATUS Status = pBDD->CommitVidPn(pCommitVidPn);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiCommitVidPn - Status: 0x%08lX\n", Status);

    return Status;
}

NTSTATUS
APIENTRY
BddDdiUpdateActiveVidPnPresentPath(
    _In_ CONST HANDLE                                      hAdapter,
    _In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH* CONST pUpdateActiveVidPnPresentPath)
{
    PAGED_CODE();
    BDD_ASSERT_CHK(hAdapter != NULL);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiUpdateActiveVidPnPresentPath entered\n");

    BASIC_DISPLAY_DRIVER* pBDD = reinterpret_cast<BASIC_DISPLAY_DRIVER*>(hAdapter);
    if (!pBDD->IsDriverActive())
    {
        BDD_LOG_ASSERTION1("BDD (0x%I64x) is being called when not active!", pBDD);
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiUpdateActiveVidPnPresentPath - Driver NOT active\n");
        return STATUS_UNSUCCESSFUL;
    }
    NTSTATUS Status = pBDD->UpdateActiveVidPnPresentPath(pUpdateActiveVidPnPresentPath);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiUpdateActiveVidPnPresentPath - Status: 0x%08lX\n", Status);

    return Status;
}

NTSTATUS
APIENTRY
BddDdiQueryVidPnHWCapability(
    _In_ CONST HANDLE                       hAdapter,
    _Inout_ DXGKARG_QUERYVIDPNHWCAPABILITY* pVidPnHWCaps)
{
    PAGED_CODE();
    BDD_ASSERT_CHK(hAdapter != NULL);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiQueryVidPnHWCapability entered - hAdapter: 0x%p\n", hAdapter);

    BASIC_DISPLAY_DRIVER* pBDD = reinterpret_cast<BASIC_DISPLAY_DRIVER*>(hAdapter);
    if (!pBDD->IsDriverActive())
    {
        BDD_LOG_ASSERTION1("BDD (0x%I64x) is being called when not active!", pBDD);
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiQueryVidPnHWCapability - Driver NOT active\n");
        return STATUS_UNSUCCESSFUL;
    }
    NTSTATUS Status = pBDD->QueryVidPnHWCapability(pVidPnHWCaps);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiQueryVidPnHWCapability - Status: 0x%08lX\n", Status);

    return Status;
}
//END: Paged Code
#pragma code_seg(pop)

#pragma code_seg(push)
#pragma code_seg()
// BEGIN: Non-Paged Code

NTSTATUS
APIENTRY
BddDdiEscape(
    _In_ VOID*                     pDeviceContext,
    _In_ CONST DXGKARG_ESCAPE*     pEscape)
{
    PBC250_ESC_BUFFER pBuf;

    if (pEscape == NULL || pDeviceContext == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    pBuf = (PBC250_ESC_BUFFER)pEscape->pPrivateDriverData;
    if (pBuf == NULL || pEscape->PrivateDriverDataSize < sizeof(BC250_ESC_BUFFER))
    {
        return STATUS_INVALID_BUFFER_SIZE;
    }

    if (pBuf->Magic != BC250_ESC_MAGIC)
    {
        return STATUS_INVALID_PARAMETER;
    }

    BASIC_DISPLAY_DRIVER* pBDD = reinterpret_cast<BASIC_DISPLAY_DRIVER*>(pDeviceContext);

    switch (pBuf->Command)
    {
    case BC250_ESC_READ_REG:
        pBuf->Result = pBDD->ReadReg(pBuf->Arg1);
        pBuf->Status = STATUS_SUCCESS;
        break;
    case BC250_ESC_WRITE_REG:
        pBDD->WriteReg(pBuf->Arg1, pBuf->Arg2);
        pBuf->Result = pBDD->ReadReg(pBuf->Arg1);
        pBuf->Status = STATUS_SUCCESS;
        break;
    case BC250_ESC_READ_SMN:
        pBuf->Result = pBDD->SmnRead(pBuf->Arg1);
        pBuf->Status = STATUS_SUCCESS;
        break;
    case BC250_ESC_WRITE_SMN:
        pBDD->SmnWrite(pBuf->Arg1, pBuf->Arg2);
        pBuf->Result = pBDD->SmnRead(pBuf->Arg1);
        pBuf->Status = STATUS_SUCCESS;
        break;
    case BC250_ESC_SMU_QUERY:
        pBuf->Result = pBDD->SmuQuery((USHORT)pBuf->Arg1);
        pBuf->Status = STATUS_SUCCESS;
        break;
    case BC250_ESC_SMU_QUERY_PARAM:
        pBuf->Result = pBDD->SmuQueryParam((USHORT)pBuf->Arg1, pBuf->Arg2);
        pBuf->Status = STATUS_SUCCESS;
        break;
    case BC250_ESC_GET_GPU_ID:
        pBuf->Result = pBDD->ReadReg(0x0000);
        pBuf->Status = STATUS_SUCCESS;
        break;
    case BC250_ESC_GET_STATUS:
        pBuf->Result = pBDD->ReadReg(0x0000);               // GPU_ID
        pBuf->Arg2 = pBDD->ReadReg(0x3260);                 // GRBM_STATUS
        pBuf->Arg1 = pBDD->SmuQuery(0x02);                  // GetSmuVersion
        pBuf->Status = STATUS_SUCCESS;
        break;
    case BC250_ESC_SMU_SEND:
        // Arg1 = (Queue << 16) | Msg
        pBuf->Result = pBDD->SmuSendQueue(pBuf->Arg1 >> 16, (USHORT)(pBuf->Arg1 & 0xFFFF), pBuf->Arg2);
        pBuf->Status = STATUS_SUCCESS;
        break;
    case BC250_ESC_CORE_UNLOCK:
        pBuf->Status = pBDD->CoreUnlock();
        pBuf->Result = pBDD->SmnRead(0x0115A870);
        break;
    default:
        pBuf->Status = STATUS_NOT_SUPPORTED;
        break;
    }

    return pBuf->Status;
}

VOID
BddDdiDpcRoutine(
    _In_  VOID* pDeviceContext)
{
    BDD_ASSERT_CHK(pDeviceContext != NULL);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiDpcRoutine entered\n");

    BASIC_DISPLAY_DRIVER* pBDD = reinterpret_cast<BASIC_DISPLAY_DRIVER*>(pDeviceContext);
    if (!pBDD->IsDriverActive())
    {
        BDD_LOG_ASSERTION1("BDD (0x%I64x) is being called when not active!", pBDD);
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiDpcRoutine - Driver NOT active\n");
        return;
    }
    pBDD->DpcRoutine();
}

BOOLEAN
BddDdiInterruptRoutine(
    _In_  VOID* pDeviceContext,
    _In_  ULONG MessageNumber)
{
    BDD_ASSERT_CHK(pDeviceContext != NULL);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiInterruptRoutine entered - MessageNumber: %lu\n", MessageNumber);

    BASIC_DISPLAY_DRIVER* pBDD = reinterpret_cast<BASIC_DISPLAY_DRIVER*>(pDeviceContext);
    BOOLEAN Result = pBDD->InterruptRoutine(MessageNumber);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiInterruptRoutine - Result: %d\n", Result);

    return Result;
}

VOID
BddDdiResetDevice(
    _In_  VOID* pDeviceContext)
{
    BDD_ASSERT_CHK(pDeviceContext != NULL);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiResetDevice entered\n");

    BASIC_DISPLAY_DRIVER* pBDD = reinterpret_cast<BASIC_DISPLAY_DRIVER*>(pDeviceContext);
    pBDD->ResetDevice();

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiResetDevice completed\n");
}

NTSTATUS
APIENTRY
BddDdiSystemDisplayEnable(
    _In_  VOID* pDeviceContext,
    _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _In_  PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
    _Out_ UINT* Width,
    _Out_ UINT* Height,
    _Out_ D3DDDIFORMAT* ColorFormat)
{
    BDD_ASSERT_CHK(pDeviceContext != NULL);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiSystemDisplayEnable entered - TargetId: %lu\n", TargetId);

    BASIC_DISPLAY_DRIVER* pBDD = reinterpret_cast<BASIC_DISPLAY_DRIVER*>(pDeviceContext);
    NTSTATUS Status = pBDD->SystemDisplayEnable(TargetId, Flags, Width, Height, ColorFormat);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiSystemDisplayEnable - Status: 0x%08lX, Width: %u, Height: %u, Format: %d\n",
             Status, Width ? *Width : 0, Height ? *Height : 0, ColorFormat ? *ColorFormat : 0);

    return Status;
}

VOID
APIENTRY
BddDdiSystemDisplayWrite(
    _In_  VOID* pDeviceContext,
    _In_  VOID* Source,
    _In_  UINT  SourceWidth,
    _In_  UINT  SourceHeight,
    _In_  UINT  SourceStride,
    _In_  UINT  PositionX,
    _In_  UINT  PositionY)
{
    BDD_ASSERT_CHK(pDeviceContext != NULL);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiSystemDisplayWrite entered - Width: %u, Height: %u, Stride: %u, X: %u, Y: %u\n",
             SourceWidth, SourceHeight, SourceStride, PositionX, PositionY);

    BASIC_DISPLAY_DRIVER* pBDD = reinterpret_cast<BASIC_DISPLAY_DRIVER*>(pDeviceContext);
    pBDD->SystemDisplayWrite(Source, SourceWidth, SourceHeight, SourceStride, PositionX, PositionY);

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "BC-250: BddDdiSystemDisplayWrite completed\n");
}

// END: Non-Paged Code
#pragma code_seg(pop)
