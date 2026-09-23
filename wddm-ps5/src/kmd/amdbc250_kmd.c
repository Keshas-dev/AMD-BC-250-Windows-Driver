/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    amdbc250_kmd.c

Abstract:
    Kernel-Mode Display Miniport Driver (KMD) main entry point and
    WDDM DDI callback implementations for the AMD BC-250 APU.

    This file implements:
      - DriverEntry: registers all WDDM DDI callbacks with Dxgkrnl
      - DxgkDdiAddDevice: allocates and initializes device extension
      - DxgkDdiStartDevice: maps hardware resources, initializes GPU
      - DxgkDdiStopDevice: stops GPU and releases resources
      - DxgkDdiRemoveDevice: final cleanup
      - DxgkDdiInterruptRoutine: ISR for GPU interrupts
      - DxgkDdiDpcRoutine: DPC for deferred interrupt processing
      - DxgkDdiQueryAdapterInfo: reports adapter capabilities to WDDM
      - DxgkDdiCreateDevice / DestroyDevice: per-process GPU context
      - DxgkDdiSubmitCommand: submits command buffers to GPU ring
      - DxgkDdiQueryCurrentFence: reports GPU progress fence value
      - Display DDI callbacks for VidPN management

Environment:
    Kernel mode (IRQL varies per callback)

--*/

#include "amdbc250_kmd.h"

/* Reference driver currently supports one BC-250 adapter instance. */
static PAMDBC250_DEVICE_EXTENSION g_Bc250PrimaryDevice = NULL;
static volatile LONG g_Bc250DriverEntryFirstFailCaptured = 0;
static volatile LONG g_Bc250DriverEntryFirstFailStatus = 0;
static volatile LONG g_DeCalledDriverEntry = 0;
static volatile LONG g_DeDxgkInitializeSuccess = 0;
static volatile LONG g_DeAddDevice = 0;
static volatile LONG g_DeStartDevice = 0;
static volatile LONG g_DeQueryAdapterInfo = 0;
static volatile LONG g_DeStopDevice = 0;
static volatile LONG g_DeRemoveDevice = 0;
static volatile LONG g_DeQaiSeqCounter = 0;
#define BC250_QAI_HISTORY_DEPTH 20
static volatile LONG g_DeQaiTypeHistory[BC250_QAI_HISTORY_DEPTH];
static volatile LONG g_DeQaiOutHistory[BC250_QAI_HISTORY_DEPTH];
static volatile LONG g_DeQaiStatusHistory[BC250_QAI_HISTORY_DEPTH];
static volatile LONG g_Bc250LastCallbackId = 0;
static volatile LONG g_Bc250LastQaiType = 0;
static volatile LONG g_Bc250LastVidPnCallbackId = 0;

/* Forward declarations for WDDM 3.2 required callbacks */
NTSTATUS NTAPI Bc250DdiCreateHwQueue(IN_CONST_HANDLE hHwContext, INOUT_PDXGKARG_CREATEHWQUEUE pCreateHwQueue);
NTSTATUS NTAPI Bc250DdiDestroyHwQueue(IN_CONST_HANDLE hHwQueue);
NTSTATUS NTAPI Bc250DdiSubmitCommandToHwQueue(PVOID MiniportDeviceContext, IN_CONST_PDXGKARG_SUBMITCOMMANDTOHWQUEUE SubmitCommandToHwQueue);
NTSTATUS NTAPI Bc250DdiSubmitCommandVirtual(PVOID MiniportDeviceContext, IN_CONST_PDXGKARG_SUBMITCOMMANDVIRTUAL SubmitCommandVirtual);

/*
 * Persistent telemetry: release builds compile out KdPrint, so the ONLY way
 * to see what dxgkrnl does after a reboot is to write registry values. Every
 * persist call goes to HKLM\SYSTEM\CurrentControlSet\Services\amdbc250kmd\
 * Parameters (the driver's own service key). Values are cheap, single DWORDs
 * and only written from PASSIVE_LEVEL callbacks.
 */
#define BC250_DIAG_REG_SVC L"amdbc250kmd"
#define BC250_DIAG_REG_PATH L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\amdbc250kmd\\Parameters"

VOID
Bc250DiagWriteDword(
    _In_z_ PCWSTR ValueName,
    _In_ ULONG Value
    )
{
    UNICODE_STRING PathU, NameU;
    OBJECT_ATTRIBUTES Attrs;
    HANDLE KeyHandle = NULL;
    NTSTATUS Status;
    ULONG Data = Value;
    ULONG Disposition;

    RtlInitUnicodeString(&PathU, BC250_DIAG_REG_PATH);
    RtlInitUnicodeString(&NameU, ValueName);
    InitializeObjectAttributes(&Attrs, &PathU, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    Status = ZwCreateKey(&KeyHandle, KEY_SET_VALUE, &Attrs, 0, NULL, REG_OPTION_NON_VOLATILE, &Disposition);
    if (!NT_SUCCESS(Status)) {
        return;
    }

    ZwSetValueKey(KeyHandle, &NameU, 0, REG_DWORD, &Data, sizeof(Data));
    ZwClose(KeyHandle);
}

ULONG
Bc250DiagReadDword(
    _In_z_ PCWSTR ValueName,
    _In_ ULONG DefaultValue
    )
{
    UNICODE_STRING PathU, NameU;
    OBJECT_ATTRIBUTES Attrs;
    HANDLE KeyHandle = NULL;
    NTSTATUS Status;
    ULONG Value = DefaultValue;
    ULONG Disposition;
    KEY_VALUE_PARTIAL_INFORMATION Kvp;
    ULONG ResultLength = 0;

    RtlInitUnicodeString(&PathU, BC250_DIAG_REG_PATH);
    RtlInitUnicodeString(&NameU, ValueName);
    InitializeObjectAttributes(&Attrs, &PathU, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    Status = ZwCreateKey(&KeyHandle, KEY_QUERY_VALUE | KEY_SET_VALUE, &Attrs, 0, NULL, REG_OPTION_NON_VOLATILE, &Disposition);
    if (!NT_SUCCESS(Status)) {
        return DefaultValue;
    }

    Status = ZwQueryValueKey(KeyHandle, &NameU, KeyValuePartialInformation, &Kvp, sizeof(Kvp), &ResultLength);
    if (NT_SUCCESS(Status) && Kvp.Type == REG_DWORD && Kvp.DataLength >= sizeof(ULONG)) {
        RtlCopyMemory(&Value, Kvp.Data, sizeof(ULONG));
    }
    ZwClose(KeyHandle);
    return Value;
}

static VOID
Bc250DiagSnapshot(
    _In_ ULONG LastDisplayDdi
    )
{
    Bc250DiagWriteDword(L"DiagLastDisplayDdi", LastDisplayDdi);
    Bc250DiagWriteDword(L"DiagDeStartDevice", (ULONG)g_DeStartDevice);
    Bc250DiagWriteDword(L"DiagLastCallbackId", (ULONG)g_Bc250LastCallbackId);
    Bc250DiagWriteDword(L"DiagLastVidPnCallbackId", (ULONG)g_Bc250LastVidPnCallbackId);
    Bc250DiagWriteDword(L"DiagLastQaiType", (ULONG)g_Bc250LastQaiType);
}

static VOID
Bc250DiagStartSnapshot(
    _In_ NTSTATUS StartStatus,
    _In_ NTSTATUS AcquireStatus,
    _In_ ULONG    FbActive,
    _In_ ULONG    FbMapped
    )
{
    Bc250DiagWriteDword(L"DiagStartStatus", (ULONG)StartStatus);
    Bc250DiagWriteDword(L"DiagAcquireStatus", (ULONG)AcquireStatus);
    Bc250DiagWriteDword(L"DiagFbIsActive", FbActive);
    Bc250DiagWriteDword(L"DiagFbMapped", FbMapped);
}

#define AMDBC250_HEADLESS_RENDER_ONLY 0
#define AMDBC250_SKIP_HW_INIT 0
#define AMDBC250_REPORTED_VRAM_MB_OVERRIDE 8192
#define AMDBC250_QSEG3_PAGING_BUFFER_BYTES_OVERRIDE 0
#define AMDBC250_FORCE_WDDM_CAPS_LEVEL 13
#define AMDBC250_UMA_FLAGS_PROFILE 2
#define AMDBC250_K0_STRICT_DRIVERENTRY 0
#define AMDBC250_BUILD_MARKER "GEMINI_DDI_EXPANSION_20260514_1200"
#define AMDBC250_DRIVERENTRY_MINIMAL_CALLBACK_SET 1
#define AMDBC250_A7_MICROSTEP 0
#define AMDBC250_UPDATEPATH_PROBE_MODE 0
#define AMDBC250_QAI_UNKNOWN_NOTSUPPORTED 1
#define AMDBC250_SAFE_DISPLAY_PROFILE 1
#define AMDBC250_SAFE_DISPLAY_INCLUDE_UPDATEPATH 1
#define AMDBC250_SAFE_DISPLAY_ENABLE_CTRLINT 0
#define AMDBC250_DUMP_QAI_ON_STOPREMOVE 1
#define AMDBC250_QAI_SEGMENT_V1_ONLY 0
#define AMDBC250_QAI_DOD_STRICT 1
#define AMDBC250_QSEG3_FORCE_ZERO_PAGING 0
#define AMDBC250_QSEG_TWO_SEGMENTS 0
#define AMDBC250_REPORTED_GTT_MB_OVERRIDE 4096
#define AMDBC250_H_UPDATEPATH_MODE 2
#define AMDBC250_H_SETSOURCEADDR_MODE 2
#define AMDBC250_H_CHILDLESS_MODE 0
#define AMDBC250_SAFE_MODE_WIDTH 1024
#define AMDBC250_SAFE_MODE_HEIGHT 768
#define AMDBC250_SAFE_MODE_STRIDE (AMDBC250_SAFE_MODE_WIDTH * 4)
#define AMDBC250_SAFE_MODE_PIXEL_RATE 65000000ULL
#define AMDBC250_DEFAULT_PAGING_BUFFER_BYTES (64 * 1024)
#define AMDBC250_MAX_FB_MAP_BYTES (16 * 1024 * 1024)

static volatile LONG g_Bc250LastCommitSourceId = -1;
static volatile LONG g_Bc250LastCommitFlags = 0;
static volatile LONG g_Bc250LastCommitConnectivityChecks = 0;
static volatile LONG g_Bc250LastCallbackStatus = 0;

const char g_Amdbc250BuildMarker[] =
    "AMDBC250_BUILD_MARKER=K0_STRICT_DRIVERENTRY_REVALIDATE_20260508_1648";

#define BC250_CB_DRIVERENTRY 1
#define BC250_CB_ADDDEVICE 2
#define BC250_CB_STARTDEVICE 3
#define BC250_CB_QUERYADAPTERINFO 4
#define BC250_CB_ISSUPPORTEDVIDPN 5
#define BC250_CB_ENUMVIDPN 6
#define BC250_CB_RECOMMENDFUNCVIDPN 7
#define BC250_CB_COMMITVIDPN 8
#define BC250_CB_UPDATEACTIVEPATH 9
#define BC250_CB_SETVIDPNSOURCEADDR 10
#define BC250_CB_STOPDEVICE 11
#define BC250_CB_REMOVEDEVICE 12
#define BC250_CB_UNLOAD 13

static volatile LONG g_Bc250DiagCallbackSeq = 0;

static VOID
Bc250MarkCallbackEnter(_In_ LONG CallbackId)
{
    InterlockedExchange(&g_Bc250LastCallbackId, CallbackId);
    if (CallbackId >= BC250_CB_ISSUPPORTEDVIDPN && CallbackId <= BC250_CB_SETVIDPNSOURCEADDR) {
        InterlockedExchange(&g_Bc250LastVidPnCallbackId, CallbackId);
    }
    if (CallbackId >= 1 && CallbackId <= 16) {
        InterlockedOr(&g_Bc250DiagCallbackSeq, (1UL << (CallbackId - 1)));
        Bc250DiagWriteDword(L"DiagCallbackSeq", (ULONG)g_Bc250DiagCallbackSeq);
    }
    KdPrint(("AMDBC250: CB enter id=%ld\n", CallbackId));
}

static VOID
Bc250MarkCallbackExit(_In_ LONG CallbackId, _In_ NTSTATUS Status)
{
    InterlockedExchange(&g_Bc250LastCallbackId, CallbackId);
    InterlockedExchange(&g_Bc250LastCallbackStatus, (LONG)Status);
    Bc250DiagWriteDword(L"DiagLastCallbackId", (ULONG)CallbackId);
    Bc250DiagWriteDword(L"DiagLastCallbackStatus", (ULONG)Status);
    KdPrint(("AMDBC250: CB exit id=%ld status=0x%08X lastVidPnCb=%ld lastQai=%ld\n",
             CallbackId, (ULONG)Status, g_Bc250LastVidPnCallbackId, g_Bc250LastQaiType));
}

/* StartDevice stage breadcrumbs for post-failure triage. */
#define BC250_START_STAGE_ENTRY                 1
#define BC250_START_STAGE_VALIDATE_PARAMS       2
#define BC250_START_STAGE_COPY_DXGK_INTERFACE   3
#define BC250_START_STAGE_GET_DEVICE_INFO       4
#define BC250_START_STAGE_PARSE_RESOURCES       5
#define BC250_START_STAGE_MAP_MMIO              6
#define BC250_START_STAGE_MAP_DOORBELL          7
#define BC250_START_STAGE_HW_INIT               8
#define BC250_START_STAGE_COMPAT_FALLBACK       9
#define BC250_START_STAGE_SUCCESS               10
#define BC250_FAIL_STAGE_QUERYADAPTERINFO       100

/* DDI identifiers for first-failure telemetry. */
#define BC250_DDI_ID_NONE                       0
#define BC250_DDI_ID_DRIVERENTRY                1
#define BC250_DDI_ID_STARTDEVICE                2
#define BC250_DDI_ID_QUERYADAPTERINFO           3

#define BC250_TRACE_START(_devext, _stage, _status, _msg)                             \
    do {                                                                               \
        (_devext)->DebugStartStage = (_stage);                                         \
        (_devext)->DebugStartStatus = (_status);                                       \
        KdPrint(("AMDBC250: STARTTRACE stage=%lu status=0x%08X %s\n",                 \
                 (ULONG)(_stage),                                                      \
                 (ULONG)(_status),                                                     \
                 (_msg)));                                                             \
    } while (0)

#define BC250_SET_UMA_SEGMENT_FLAGS(_flags)                                            \
    do {                                                                               \
        (_flags).CpuVisible = 1;                                                       \
        (_flags).Aperture = 0;                                                         \
        (_flags).CacheCoherent = 0;                                                    \
        (_flags).PopulatedFromSystemMemory = 1;                                        \
        if (AMDBC250_UMA_FLAGS_PROFILE == 1) {                                         \
            /* D1: aperture-forward probe */                                            \
            (_flags).Aperture = 1;                                                     \
        } else if (AMDBC250_UMA_FLAGS_PROFILE == 2) {                                  \
            /* D2: UMA/aperture coherent profile */                                     \
            (_flags).Aperture = 1;                                                     \
            (_flags).CacheCoherent = 1;                                                \
        } else if (AMDBC250_UMA_FLAGS_PROFILE == 3) {                                  \
            /* Conservative local-ish profile */                                        \
            (_flags).PopulatedFromSystemMemory = 0;                                    \
        }                                                                              \
    } while (0)

static VOID
Bc250CaptureFirstFailureEx(
    _In_ PAMDBC250_DEVICE_EXTENSION DevExt,
    _In_ ULONG Stage,
    _In_ NTSTATUS Status,
    _In_z_ PCSTR Reason,
    _In_ ULONG DdiId,
    _In_ ULONG QueryType,
    _In_ SIZE_T OutputSize
    );

static VOID
Bc250CaptureFirstFailure(
    _In_ PAMDBC250_DEVICE_EXTENSION DevExt,
    _In_ ULONG Stage,
    _In_ NTSTATUS Status,
    _In_z_ PCSTR Reason
    )
{
    Bc250CaptureFirstFailureEx(DevExt,
                               Stage,
                               Status,
                               Reason,
                               BC250_DDI_ID_NONE,
                               0,
                               0);
}

static PCSTR
Bc250GetDdiName(
    _In_ ULONG DdiId
    )
{
    switch (DdiId) {
    case BC250_DDI_ID_DRIVERENTRY:
        return "DriverEntry";
    case BC250_DDI_ID_STARTDEVICE:
        return "DxgkDdiStartDevice";
    case BC250_DDI_ID_QUERYADAPTERINFO:
        return "DxgkDdiQueryAdapterInfo";
    default:
        return "unknown";
    }
}

static VOID
Bc250CaptureFirstFailureEx(
    _In_ PAMDBC250_DEVICE_EXTENSION DevExt,
    _In_ ULONG Stage,
    _In_ NTSTATUS Status,
    _In_z_ PCSTR Reason,
    _In_ ULONG DdiId,
    _In_ ULONG QueryType,
    _In_ SIZE_T OutputSize
    )
{
    LONG captured;

    if (DevExt == NULL) {
        return;
    }

    captured = InterlockedCompareExchange(
        (volatile LONG*)&DevExt->DebugFirstFailStage,
        (LONG)Stage,
        0);

    if (captured == 0) {
        DevExt->DebugFirstFailStatus = Status;
        DevExt->DebugFirstFailDdiId = DdiId;
        DevExt->DebugFirstFailQueryType = QueryType;
        DevExt->DebugFirstFailOutSize = (ULONGLONG)OutputSize;
    }

    KdPrint(("AMDBC250: FAILTRACE stage=%lu status=0x%08X ddi=%lu(%s) qType=%lu out=%llu firstStage=%lu firstStatus=0x%08X firstDdi=%lu(%s) firstQType=%lu firstOut=%llu reason=%s\n",
             Stage,
             (ULONG)Status,
             DdiId,
             Bc250GetDdiName(DdiId),
             QueryType,
             (ULONGLONG)OutputSize,
             DevExt->DebugFirstFailStage,
             (ULONG)DevExt->DebugFirstFailStatus,
             DevExt->DebugFirstFailDdiId,
             Bc250GetDdiName(DevExt->DebugFirstFailDdiId),
             DevExt->DebugFirstFailQueryType,
             DevExt->DebugFirstFailOutSize,
             Reason));
}

static VOID
Bc250TraceQueryResult(
    _In_ PAMDBC250_DEVICE_EXTENSION DevExt,
    _In_ ULONG QueryType,
    _In_ SIZE_T OutputSize,
    _In_ NTSTATUS Status,
    _In_ SIZE_T ReportedVramBytes,
    _In_z_ PCSTR Note
    )
{
    if (DevExt == NULL) {
        return;
    }

    DevExt->DebugQueryCount += 1;
    DevExt->DebugQueryLastType = QueryType;
    DevExt->DebugQueryLastOutSize = (ULONGLONG)OutputSize;
    DevExt->DebugQueryLastStatus = Status;
    DevExt->DebugQueryLastVramBytes = (ULONGLONG)ReportedVramBytes;

    KdPrint(("AMDBC250: QAITRACE count=%lu type=%lu out=%llu status=0x%08X vramMB=%llu note=%s\n",
             DevExt->DebugQueryCount,
             QueryType,
             (ULONGLONG)OutputSize,
             (ULONG)Status,
             (ULONGLONG)(ReportedVramBytes / (1024ULL * 1024ULL)),
             Note));
}

static PCSTR
Bc250GetQaiTypeName(
    _In_ ULONG QueryType
    )
{
    switch (QueryType) {
    case DXGKQAITYPE_UMDRIVERPRIVATE:
        return "UMDRIVERPRIVATE";
    case DXGKQAITYPE_DRIVERCAPS:
        return "DRIVERCAPS";
    case DXGKQAITYPE_QUERYSEGMENT:
        return "QUERYSEGMENT";
#if defined(DXGKQAITYPE_QUERYSEGMENT2)
    case DXGKQAITYPE_QUERYSEGMENT2:
        return "QUERYSEGMENT2";
#endif
#if defined(DXGKQAITYPE_QUERYSEGMENT3)
    case DXGKQAITYPE_QUERYSEGMENT3:
        return "QUERYSEGMENT3";
#endif
#if defined(DXGKQAITYPE_QUERYSEGMENTCOUNT)
    case DXGKQAITYPE_QUERYSEGMENTCOUNT:
        return "QUERYSEGMENTCOUNT";
#endif
#if defined(DXGKQAITYPE_WDDMDEVICECAPS)
    case DXGKQAITYPE_WDDMDEVICECAPS:
        return "WDDMDEVICECAPS";
#endif
#if defined(DXGKQAITYPE_GPUPCAPS)
    case DXGKQAITYPE_GPUPCAPS:
        return "GPUPCAPS";
#endif
#if defined(DXGKQAITYPE_DEVICE_TYPE_CAPS)
    case DXGKQAITYPE_DEVICE_TYPE_CAPS:
        return "DEVICE_TYPE_CAPS";
#endif
    default:
        return "UNKNOWN";
    }
}

static VOID
Bc250RecordGlobalQai(
    _In_ ULONG QueryType,
    _In_ SIZE_T OutputSize,
    _In_ NTSTATUS Status
    )
{
    LONG seq = InterlockedIncrement(&g_DeQaiSeqCounter) - 1;
    LONG slot = seq % BC250_QAI_HISTORY_DEPTH;
    if (slot < 0) {
        slot += BC250_QAI_HISTORY_DEPTH;
    }

    g_DeQaiTypeHistory[slot] = (LONG)QueryType;
    g_DeQaiOutHistory[slot] = (LONG)((OutputSize > 0x7fffffffULL) ? 0x7fffffff : OutputSize);
    g_DeQaiStatusHistory[slot] = (LONG)Status;
}

static VOID
Bc250DumpGlobalQaiHistory(
    _In_z_ PCSTR Reason
    )
{
    LONG seq = g_DeQaiSeqCounter;
    LONG begin = seq - BC250_QAI_HISTORY_DEPTH;
    LONG i;

    if (begin < 0) {
        begin = 0;
    }

    KdPrint(("AMDBC250: QAIDUMP reason=%s total=%ld window=[%ld,%ld)\n",
             Reason,
             seq,
             begin,
             seq));

    for (i = begin; i < seq; ++i) {
        LONG slot = i % BC250_QAI_HISTORY_DEPTH;
        if (slot < 0) {
            slot += BC250_QAI_HISTORY_DEPTH;
        }
        KdPrint(("AMDBC250: QAIDUMP seq=%ld type=%lu(%s) out=%lu status=0x%08X\n",
                 i,
                 (ULONG)g_DeQaiTypeHistory[slot],
                 Bc250GetQaiTypeName((ULONG)g_DeQaiTypeHistory[slot]),
                 (ULONG)g_DeQaiOutHistory[slot],
                 (ULONG)g_DeQaiStatusHistory[slot]));
    }
}

static VOID
Bc250LogGlobalCounters(
    _In_z_ PCSTR Tag,
    _In_ NTSTATUS Status
    )
{
    KdPrint(("AMDBC250: GLOBALCTR tag=%s status=0x%08X de=%ld initOk=%ld add=%ld start=%ld qai=%ld stop=%ld remove=%ld qaiSeq=%ld\n",
             Tag,
             (ULONG)Status,
             g_DeCalledDriverEntry,
             g_DeDxgkInitializeSuccess,
             g_DeAddDevice,
             g_DeStartDevice,
             g_DeQueryAdapterInfo,
             g_DeStopDevice,
             g_DeRemoveDevice,
             g_DeQaiSeqCounter));
}

static VOID
Bc250DumpLifecycleCounters(
    _In_z_ PCSTR Where
    )
{
    Bc250LogGlobalCounters(Where, STATUS_SUCCESS);
}

/*
 * Compile-time ABI checks: force exact function-pointer type compatibility
 * against WDK DDI typedefs. Any mismatch here should fail the build.
 */
static VOID
Bc250AbiTypeChecks(VOID)
{
    PDXGKDDI_ADD_DEVICE            pAdd        = Bc250DdiAddDevice;
    PDXGKDDI_START_DEVICE          pStart      = Bc250DdiStartDevice;
    PDXGKDDI_STOP_DEVICE           pStop       = Bc250DdiStopDevice;
    PDXGKDDI_REMOVE_DEVICE         pRemove     = Bc250DdiRemoveDevice;
    PDXGKDDI_QUERYADAPTERINFO      pQai        = Bc250DdiQueryAdapterInfo;
    PDXGKDDI_CREATEALLOCATION      pCreateAlloc = Bc250DdiCreateAllocation;
    PDXGKDDI_DESTROYALLOCATION     pDestroyAlloc = Bc250DdiDestroyAllocation;
    PDXGKDDI_BUILDPAGINGBUFFER     pBuildPb    = Bc250DdiBuildPagingBuffer;
    PDXGKDDI_SUBMITCOMMAND         pSubmit     = Bc250DdiSubmitCommand;
    PDXGKDDI_QUERYCURRENTFENCE     pFence      = Bc250DdiQueryCurrentFence;
    PDXGKDDI_PRESENT               pPresent    = Bc250DdiPresent;
    PDXGKDDI_RENDER                pRender     = Bc250DdiRender;
    PDXGKDDI_ESCAPE                pEscape     = Bc250DdiEscape;

    UNREFERENCED_PARAMETER(pAdd);
    UNREFERENCED_PARAMETER(pStart);
    UNREFERENCED_PARAMETER(pStop);
    UNREFERENCED_PARAMETER(pRemove);
    UNREFERENCED_PARAMETER(pQai);
    UNREFERENCED_PARAMETER(pCreateAlloc);
    UNREFERENCED_PARAMETER(pDestroyAlloc);
    UNREFERENCED_PARAMETER(pBuildPb);
    UNREFERENCED_PARAMETER(pSubmit);
    UNREFERENCED_PARAMETER(pFence);
    UNREFERENCED_PARAMETER(pPresent);
    UNREFERENCED_PARAMETER(pRender);
    UNREFERENCED_PARAMETER(pEscape);
}

static VOID
Bc250TraceDriverInitContract(
    _In_ const DRIVER_INITIALIZATION_DATA* DriverInitData
    )
{
    ULONG nonNullCount = 0;

#define BC250_COUNT_NON_NULL(_field) \
    do { if ((DriverInitData->_field) != NULL) { nonNullCount += 1; } } while (0)

    if (DriverInitData == NULL) {
        return;
    }

    BC250_COUNT_NON_NULL(DxgkDdiAddDevice);
    BC250_COUNT_NON_NULL(DxgkDdiStartDevice);
    BC250_COUNT_NON_NULL(DxgkDdiStopDevice);
    BC250_COUNT_NON_NULL(DxgkDdiRemoveDevice);
    BC250_COUNT_NON_NULL(DxgkDdiDispatchIoRequest);
    BC250_COUNT_NON_NULL(DxgkDdiResetDevice);
    BC250_COUNT_NON_NULL(DxgkDdiUnload);
    BC250_COUNT_NON_NULL(DxgkDdiSetPowerState);
    BC250_COUNT_NON_NULL(DxgkDdiInterruptRoutine);
    BC250_COUNT_NON_NULL(DxgkDdiDpcRoutine);
    BC250_COUNT_NON_NULL(DxgkDdiQueryAdapterInfo);
    BC250_COUNT_NON_NULL(DxgkDdiQueryInterface);
    BC250_COUNT_NON_NULL(DxgkDdiPresent);
    BC250_COUNT_NON_NULL(DxgkDdiRender);
    BC250_COUNT_NON_NULL(DxgkDdiSubmitCommand);
    BC250_COUNT_NON_NULL(DxgkDdiCreateDevice);
    BC250_COUNT_NON_NULL(DxgkDdiEscape);

#if !AMDBC250_HEADLESS_RENDER_ONLY
    BC250_COUNT_NON_NULL(DxgkDdiQueryChildRelations);
    BC250_COUNT_NON_NULL(DxgkDdiQueryChildStatus);
    BC250_COUNT_NON_NULL(DxgkDdiQueryDeviceDescriptor);
    BC250_COUNT_NON_NULL(DxgkDdiSetPalette);
    BC250_COUNT_NON_NULL(DxgkDdiSetPointerPosition);
    BC250_COUNT_NON_NULL(DxgkDdiSetPointerShape);
    BC250_COUNT_NON_NULL(DxgkDdiIsSupportedVidPn);
    BC250_COUNT_NON_NULL(DxgkDdiRecommendFunctionalVidPn);
    BC250_COUNT_NON_NULL(DxgkDdiEnumVidPnCofuncModality);
    BC250_COUNT_NON_NULL(DxgkDdiSetVidPnSourceVisibility);
    BC250_COUNT_NON_NULL(DxgkDdiCommitVidPn);
    BC250_COUNT_NON_NULL(DxgkDdiUpdateActiveVidPnPresentPath);
    BC250_COUNT_NON_NULL(DxgkDdiRecommendMonitorModes);
    BC250_COUNT_NON_NULL(DxgkDdiGetScanLine);
    BC250_COUNT_NON_NULL(DxgkDdiQueryVidPnHWCapability);
    BC250_COUNT_NON_NULL(DxgkDdiControlInterrupt);
#endif

    KdPrint(("AMDBC250: INITCONTRACT size=%llu version=0x%08X nonNullDdi=%lu headless=%d skipHw=%d vramOvMb=%d pageBufOv=%d wddmCaps=%d umaFlags=%d\n",
             (ULONGLONG)sizeof(KMDDOD_INITIALIZATION_DATA),
             (ULONG)DriverInitData->Version,
             nonNullCount,
             AMDBC250_HEADLESS_RENDER_ONLY,
             AMDBC250_SKIP_HW_INIT,
             AMDBC250_REPORTED_VRAM_MB_OVERRIDE,
             AMDBC250_QSEG3_PAGING_BUFFER_BYTES_OVERRIDE,
             AMDBC250_FORCE_WDDM_CAPS_LEVEL,
             AMDBC250_UMA_FLAGS_PROFILE));
    KdPrint(("AMDBC250: INITPTR core add=%p start=%p stop=%p remove=%p dispatch=%p reset=%p unload=%p\n",
             DriverInitData->DxgkDdiAddDevice,
             DriverInitData->DxgkDdiStartDevice,
             DriverInitData->DxgkDdiStopDevice,
             DriverInitData->DxgkDdiRemoveDevice,
             DriverInitData->DxgkDdiDispatchIoRequest,
             DriverInitData->DxgkDdiResetDevice,
             DriverInitData->DxgkDdiUnload));
    KdPrint(("AMDBC250: INITPTR render present=%p submit=%p render_cb=%p escape=%p create_dev=%p intr=%p dpc=%p\n",
             DriverInitData->DxgkDdiPresent,
             DriverInitData->DxgkDdiSubmitCommand,
             DriverInitData->DxgkDdiRender,
             DriverInitData->DxgkDdiEscape,
             DriverInitData->DxgkDdiCreateDevice,
             DriverInitData->DxgkDdiInterruptRoutine,
             DriverInitData->DxgkDdiDpcRoutine));

#undef BC250_COUNT_NON_NULL
}

/*
 * Keep reported VRAM size consistent across QUERYSEGMENT variants.
 * Some DXGK paths consume 32-bit sized fields; cap uniformly to avoid
 * cross-query mismatches that can trigger post-start failures.
 */
static SIZE_T
Bc250GetReportedVramSizeBytes(
    _In_ const AMDBC250_DEVICE_EXTENSION* DevExt
    )
{
    SIZE_T vramSize;

#if (AMDBC250_REPORTED_VRAM_MB_OVERRIDE > 0)
    vramSize = (SIZE_T)AMDBC250_REPORTED_VRAM_MB_OVERRIDE * 1024ULL * 1024ULL;
#else
    vramSize = (DevExt->TotalVramBytes != 0)
        ? DevExt->TotalVramBytes
        : ((SIZE_T)AMDBC250_DEFAULT_VRAM_MB * 1024 * 1024);
#endif

    if (vramSize > (SIZE_T)0xFFFFFFFFULL) {
        vramSize = (SIZE_T)0xFFFFFFFFULL;
    }
    return vramSize;
}

static ULONG
Bc250GetReportedPagingBufferSizeBytes(VOID)
{
    ULONG pagingBufferSize = AMDBC250_DEFAULT_PAGING_BUFFER_BYTES;

#if AMDBC250_QSEG3_FORCE_ZERO_PAGING
    return 0;
#endif
#if (AMDBC250_QSEG3_PAGING_BUFFER_BYTES_OVERRIDE > 0)
    pagingBufferSize = (ULONG)AMDBC250_QSEG3_PAGING_BUFFER_BYTES_OVERRIDE;
#endif
    if (pagingBufferSize < (16 * 1024)) {
        pagingBufferSize = 16 * 1024;
    }
    return pagingBufferSize;
}

static SIZE_T
Bc250GetReportedGttSizeBytes(VOID)
{
#if (AMDBC250_REPORTED_GTT_MB_OVERRIDE > 0)
    return (SIZE_T)AMDBC250_REPORTED_GTT_MB_OVERRIDE * 1024ULL * 1024ULL;
#else
    return 0;
#endif
}

static UINT
Bc250GetReportedWddmCapsVersion(VOID)
{
#if (AMDBC250_FORCE_WDDM_CAPS_LEVEL == 13)
    return DXGKDDI_WDDMv1_3;
#elif (AMDBC250_FORCE_WDDM_CAPS_LEVEL == 20)
#if defined(DXGKDDI_WDDMv2_0)
    return DXGKDDI_WDDMv2_0;
#else
    return DXGKDDI_WDDMv1_3;
#endif
#else
#if defined(DXGKDDI_WDDMv2_0)
    return DXGKDDI_WDDMv2_0;
#else
    return DXGKDDI_WDDMv1_3;
#endif
#endif
}

/*===========================================================================
  DriverEntry
  Called by the OS when the driver is loaded. Registers all WDDM DDI
  callback functions with the DirectX graphics kernel subsystem.
===========================================================================*/

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    Bc250MarkCallbackEnter(BC250_CB_DRIVERENTRY);
    DRIVER_INITIALIZATION_DATA DriverInitData;
    NTSTATUS Status;

    InterlockedIncrement(&g_DeCalledDriverEntry);
    Bc250DiagWriteDword(L"DiagDriverEntryEntered", 1);

    RtlZeroMemory(&DriverInitData, sizeof(DriverInitData));
    Bc250LogGlobalCounters("DriverEntry-enter", STATUS_SUCCESS);

    KdPrint(("AMDBC250: DriverEntry - AMD BC-250 WDDM Driver v%d.%d\n",
             AMDBC250_DRIVER_MAJOR_VERSION,
             AMDBC250_DRIVER_MINOR_VERSION));

    /*
     * Full WDDM adapter (not display-only). Register all render DDI
     * callbacks so dxgkrnl drives the render + present path.
     * Pin to WDDM 2.6 — the SDK default is WDDM 3.2 which requires
     * additional DDI callbacks (CreateHwQueue, SubmitCommandVirtual, etc.)
     * that we don't implement. dxgkrnl rejects the registration if those
     * slots are NULL at WDDM 3.2.
     */
     DriverInitData.Version = DXGKDDI_INTERFACE_VERSION_WDDM3_2;

     KdPrint(("AMDBC250: DriverEntry interface-version selected=0x%08X\n",
              (ULONG)DriverInitData.Version));
    KdPrint(("AMDBC250: BUILD_MARKER %s\n", AMDBC250_BUILD_MARKER));

    /* Core lifecycle */
    DriverInitData.DxgkDdiAddDevice                 = Bc250DdiAddDevice;
    DriverInitData.DxgkDdiStartDevice               = Bc250DdiStartDevice;
    DriverInitData.DxgkDdiStopDevice                = Bc250DdiStopDevice;
    DriverInitData.DxgkDdiRemoveDevice              = Bc250DdiRemoveDevice;
    DriverInitData.DxgkDdiDispatchIoRequest         = Bc250DdiDispatchIoRequest;
    DriverInitData.DxgkDdiResetDevice               = Bc250DdiResetDevice;
    DriverInitData.DxgkDdiSetPowerState              = Bc250DdiSetPowerState;
    DriverInitData.DxgkDdiUnload                     = Bc250DdiUnload;

    /* Interrupt/DPC handling */
    DriverInitData.DxgkDdiInterruptRoutine           = Bc250DdiInterruptRoutine;
    DriverInitData.DxgkDdiDpcRoutine                 = Bc250DdiDpcRoutine;

    /* Display/query essentials */
    DriverInitData.DxgkDdiQueryAdapterInfo           = Bc250DdiQueryAdapterInfo;
    DriverInitData.DxgkDdiQueryChildRelations        = Bc250DdiQueryChildRelations;
    DriverInitData.DxgkDdiQueryChildStatus           = Bc250DdiQueryChildStatus;
    DriverInitData.DxgkDdiQueryDeviceDescriptor      = Bc250DdiQueryDeviceDescriptor;
    DriverInitData.DxgkDdiSetPointerPosition         = Bc250DdiSetPointerPosition;
    DriverInitData.DxgkDdiSetPointerShape            = Bc250DdiSetPointerShape;

    /* VidPN management */
    DriverInitData.DxgkDdiIsSupportedVidPn           = Bc250DdiIsSupportedVidPn;
    DriverInitData.DxgkDdiRecommendFunctionalVidPn   = Bc250DdiRecommendFunctionalVidPn;
    DriverInitData.DxgkDdiEnumVidPnCofuncModality    = Bc250DdiEnumVidPnCofuncModality;
    DriverInitData.DxgkDdiCommitVidPn                = Bc250DdiCommitVidPn;
    DriverInitData.DxgkDdiUpdateActiveVidPnPresentPath = Bc250DdiUpdateActiveVidPnPresentPath;
    DriverInitData.DxgkDdiRecommendMonitorModes      = Bc250DdiRecommendMonitorModes;
    DriverInitData.DxgkDdiQueryVidPnHWCapability     = Bc250DdiQueryVidPnHwCapability;
    DriverInitData.DxgkDdiSetVidPnSourceVisibility   = Bc250DdiSetVidPnSourceVisibility;
    DriverInitData.DxgkDdiSetVidPnSourceAddress      = Bc250DdiSetVidPnSourceAddress;

     /* Render DDI - full WDDM stack */
     DriverInitData.DxgkDdiCreateDevice               = Bc250DdiCreateDevice;
     DriverInitData.DxgkDdiDestroyDevice              = Bc250DdiDestroyDevice;
     DriverInitData.DxgkDdiCreateContext              = Bc250DdiCreateContext;
     DriverInitData.DxgkDdiDestroyContext             = Bc250DdiDestroyContext;
     DriverInitData.DxgkDdiCreateAllocation           = Bc250DdiCreateAllocation;
     DriverInitData.DxgkDdiDestroyAllocation          = Bc250DdiDestroyAllocation;
     DriverInitData.DxgkDdiOpenAllocation             = Bc250DdiOpenAllocation;
     DriverInitData.DxgkDdiCloseAllocation            = Bc250DdiCloseAllocation;
     DriverInitData.DxgkDdiSubmitCommand              = Bc250DdiSubmitCommand;
     DriverInitData.DxgkDdiSubmitCommandVirtual       = Bc250DdiSubmitCommandVirtual;
     DriverInitData.DxgkDdiSubmitCommandToHwQueue     = Bc250DdiSubmitCommandToHwQueue;
     DriverInitData.DxgkDdiCreateHwQueue              = Bc250DdiCreateHwQueue;
     DriverInitData.DxgkDdiDestroyHwQueue             = Bc250DdiDestroyHwQueue;
     DriverInitData.DxgkDdiPreemptCommand             = Bc250DdiPreemptCommand;
     DriverInitData.DxgkDdiQueryCurrentFence          = Bc250DdiQueryCurrentFence;
     DriverInitData.DxgkDdiBuildPagingBuffer          = Bc250DdiBuildPagingBuffer;
     DriverInitData.DxgkDdiPatch                      = Bc250DdiPatch;
     DriverInitData.DxgkDdiPresent                    = Bc250DdiPresent;
     DriverInitData.DxgkDdiRender                     = Bc250DdiRender;
     DriverInitData.DxgkDdiEscape                     = Bc250DdiEscape;
     DriverInitData.DxgkDdiCreateOverlay              = Bc250DdiCreateOverlay;
     DriverInitData.DxgkDdiDestroyOverlay             = Bc250DdiDestroyOverlay;
     DriverInitData.DxgkDdiControlInterrupt           = Bc250DdiControlInterrupt;
     DriverInitData.DxgkDdiQueryInterface             = Bc250DdiQueryInterface;

    /* System display (WDDM 1.2+) */
    DriverInitData.DxgkDdiStopDeviceAndReleasePostDisplayOwnership = Bc250DdiStopDeviceAndReleasePostDisplayOwnership;
    DriverInitData.DxgkDdiSystemDisplayEnable        = Bc250DdiSystemDisplayEnable;
    DriverInitData.DxgkDdiSystemDisplayWrite         = Bc250DdiSystemDisplayWrite;

    Bc250AbiTypeChecks();
    Bc250TraceDriverInitContract(&DriverInitData);

    KdPrint(("AMDBC250: DriverEntry pre-DxgkInitialize version=0x%08X size=%llu\n",
             (ULONG)DriverInitData.Version,
             (ULONGLONG)sizeof(DriverInitData)));

    /* Register with Dxgkrnl as a full WDDM adapter */
    Status = DxgkInitialize(DriverObject, RegistryPath, &DriverInitData);

    KdPrint(("AMDBC250: DriverEntry post-DxgkInitialize status=0x%08X\n", (ULONG)Status));
    if (NT_SUCCESS(Status)) {
        InterlockedIncrement(&g_DeDxgkInitializeSuccess);
    }
    Bc250LogGlobalCounters("DriverEntry-postDxgkInitialize", Status);
    Bc250DumpLifecycleCounters("DriverEntry-post-DxgkInitialize");

    if (!NT_SUCCESS(Status)) {
        /*
         * Keep DriverEntry-stage failure isolated from later runtime failures
         * (e.g., QueryAdapterInfo/start-device INTERNAL_ERROR class).
         */
        if (InterlockedCompareExchange(&g_Bc250DriverEntryFirstFailCaptured, 1, 0) == 0) {
            g_Bc250DriverEntryFirstFailStatus = (LONG)Status;
        }
        KdPrint(("AMDBC250: DRIVERENTRY_FAIL status=0x%08X firstStatus=0x%08X ddi=%lu(%s)\n",
                 (ULONG)Status,
                 (ULONG)g_Bc250DriverEntryFirstFailStatus,
                 (ULONG)BC250_DDI_ID_DRIVERENTRY,
                 Bc250GetDdiName(BC250_DDI_ID_DRIVERENTRY)));
        KdPrint(("AMDBC250: DxgkInitialize failed with status 0x%08X\n", Status));
    } else {
        KdPrint(("AMDBC250: DriverEntry successful\n"));
    }

    return Status;
}

/*===========================================================================
  DxgkDdiAddDevice
  Called when PnP manager detects a matching PCI device.
  Allocates and initializes the device extension structure.
===========================================================================*/

NTSTATUS
APIENTRY
Bc250DdiAddDevice(
    _In_  CONST PDEVICE_OBJECT  PhysicalDeviceObject,
    _Out_ PVOID                 *MiniportDeviceContext
    )
{
    Bc250MarkCallbackEnter(BC250_CB_ADDDEVICE);
    PAMDBC250_DEVICE_EXTENSION DevExt;
    LONG AddCount = InterlockedIncrement(&g_DeAddDevice);

    if (MiniportDeviceContext == NULL) {
        KdPrint(("AMDBC250: AddDevice invalid MiniportDeviceContext (NULL)\n"));
        Bc250LogGlobalCounters("AddDevice-invalid-parameter", STATUS_INVALID_PARAMETER);
        Bc250MarkCallbackExit(BC250_CB_ADDDEVICE, STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }

    KdPrint(("AMDBC250: DxgkDdiAddDevice called count=%ld PDO=%p CtxOut=%p\n",
             AddCount,
             PhysicalDeviceObject,
             MiniportDeviceContext));
    Bc250DiagWriteDword(L"DiagAddDeviceEntered", 1);

    if (g_Bc250PrimaryDevice != NULL) {
        KdPrint(("AMDBC250: Only one adapter instance is supported by this reference driver\n"));
        Bc250MarkCallbackExit(BC250_CB_ADDDEVICE, STATUS_DEVICE_BUSY);
        return STATUS_DEVICE_BUSY;
    }

    /* Allocate the device extension from non-paged pool */
    DevExt = (PAMDBC250_DEVICE_EXTENSION)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        sizeof(AMDBC250_DEVICE_EXTENSION),
        AMDBC250_TAG_DEVICE_EXT
        );

    if (DevExt == NULL) {
        KdPrint(("AMDBC250: Failed to allocate device extension\n"));
        Bc250MarkCallbackExit(BC250_CB_ADDDEVICE, STATUS_NO_MEMORY);
        return STATUS_NO_MEMORY;
    }

    /* Zero-initialize the extension */
    RtlZeroMemory(DevExt, sizeof(AMDBC250_DEVICE_EXTENSION));

    /* Initialize synchronization primitives */
    ExInitializeFastMutex(&DevExt->DeviceMutex);
    KeInitializeEvent(&DevExt->ResetCompleteEvent, NotificationEvent, FALSE);
    KeInitializeSpinLock(&DevExt->ContextListLock);
    KeInitializeSpinLock(&DevExt->AllocationListLock);
    KeInitializeSpinLock(&DevExt->GfxRing.Lock);
    KeInitializeSpinLock(&DevExt->SdmaRing.Lock);

    /* Initialize allocation list */
    InitializeListHead(&DevExt->AllocationList);

    /* Store the PDO (not used directly but useful for diagnostics) */
    UNREFERENCED_PARAMETER(PhysicalDeviceObject);

    *MiniportDeviceContext = DevExt;
    g_Bc250PrimaryDevice = DevExt;

    KdPrint(("AMDBC250: Device extension allocated at %p\n", DevExt));
    KdPrint(("AMDBC250: AddDevice returning STATUS_SUCCESS\n"));
    Bc250LogGlobalCounters("AddDevice-success", STATUS_SUCCESS);
    Bc250MarkCallbackExit(BC250_CB_ADDDEVICE, STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

/*===========================================================================
  Bc250DdiStartDevice
  Called after AddDevice to start the device. Maps PCI BARs, initializes
  hardware, registers interrupt, and sets up GPU rings.
===========================================================================*/

/*
 * Bc250AcquirePostDisplay
 * Acquire the POST (firmware) display framebuffer from dxgkrnl and map it.
 * Returns STATUS_SUCCESS if a display was acquired and mapped. On failure we
 * run headless (present is a no-op) so the adapter still binds.
 */
static NTSTATUS
Bc250AcquirePostDisplay(
    _In_ PAMDBC250_DEVICE_EXTENSION DevExt
    )
{
    DXGK_DISPLAY_INFORMATION DispInfo;
    NTSTATUS Status;

    RtlZeroMemory(&DispInfo, sizeof(DispInfo));
    Bc250DiagWriteDword(L"DiagAcquirePostEntered", 1);

    Status = DevExt->DxgkInterface.DxgkCbAcquirePostDisplayOwnership(
        DevExt->DeviceHandle,
        &DispInfo);
    Bc250DiagWriteDword(L"DiagAcquirePostStatus", (ULONG)Status);
    if (!NT_SUCCESS(Status)) {
        KdPrint(("AMDBC250: AcquirePostDisplay failed 0x%08X (headless)\n", (ULONG)Status));
        return Status;
    }

    /*
     * Match the proven-working KMDOD (bdd.cxx StartDevice): FrameBufferIsActive
     * is set based ONLY on the reported width, and the framebuffer is NOT
     * mapped here. The DOD keeps the flag TRUE regardless of PhysicAddress so
     * dxgkrnl always reports the monitor as connected and drives the VidPn
     * path; the actual FB mapping happens later (SetSourceModeAndPath/CommitVidPn).
     * Requiring a non-zero PhysicAddress here caused FbIsActive=FALSE ->
     * QueryChildStatus connected=false -> dxgkrnl never built the display path.
     */
    DevExt->FbWidth = DispInfo.Width;
    DevExt->FbHeight = DispInfo.Height;
    DevExt->FbPitch = DispInfo.Pitch;
    DevExt->FbColorFormat = DispInfo.ColorFormat;
    DevExt->FbPhysicalBase = DispInfo.PhysicAddress;

    Bc250DiagWriteDword(L"DiagFbWidth", DevExt->FbWidth);
    Bc250DiagWriteDword(L"DiagFbHeight", DevExt->FbHeight);
    Bc250DiagWriteDword(L"DiagFbPitch", DevExt->FbPitch);
    Bc250DiagWriteDword(L"DiagFbPhysLow", (ULONG)DevExt->FbPhysicalBase.QuadPart);
    Bc250DiagWriteDword(L"DiagFbPhysHigh", (ULONG)(DevExt->FbPhysicalBase.QuadPart >> 32));

    if (DispInfo.Width > 0 && DispInfo.Height > 0) {
        DevExt->FbIsActive = TRUE;
    }
    Bc250DiagWriteDword(L"DiagFbIsActive", DevExt->FbIsActive ? 1 : 0);

    KdPrint(("AMDBC250: POST display fb=%ux%u pitch=%u fmt=%u PA=0x%llX\n",
             DevExt->FbWidth,
             DevExt->FbHeight,
             DevExt->FbPitch,
             (ULONG)DevExt->FbColorFormat,
             DevExt->FbPhysicalBase.QuadPart));
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Bc250DdiStartDevice(
    _In_  PVOID                     MiniportDeviceContext,
    _In_  PDXGK_START_INFO          DxgkStartInfo,
    _In_  PDXGKRNL_INTERFACE        DxgkInterface,
    _Out_ PULONG                    NumberOfVideoPresentSources,
    _Out_ PULONG                    NumberOfChildren
    )
{
    Bc250MarkCallbackEnter(BC250_CB_STARTDEVICE);
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)MiniportDeviceContext;
    LONG StartCount = InterlockedIncrement(&g_DeStartDevice);
#if !AMDBC250_SKIP_HW_INIT
    NTSTATUS Status;
    DXGK_DEVICE_INFO DeviceInfo;
    PCM_PARTIAL_RESOURCE_LIST ResourceList;
    ULONG DescriptorIndex;
#endif

    KdPrint(("AMDBC250: DxgkDdiStartDevice called count=%ld Ctx=%p StartInfo=%p Iface=%p SrcOut=%p ChildOut=%p\n",
             StartCount,
             MiniportDeviceContext,
             DxgkStartInfo,
             DxgkInterface,
             NumberOfVideoPresentSources,
             NumberOfChildren));

    if (DevExt == NULL || DxgkStartInfo == NULL || DxgkInterface == NULL ||
        NumberOfVideoPresentSources == NULL || NumberOfChildren == NULL) {
        if (DevExt != NULL) {
            Bc250CaptureFirstFailureEx(DevExt,
                                       BC250_START_STAGE_VALIDATE_PARAMS,
                                       STATUS_INVALID_PARAMETER,
                                       "start-invalid-parameter",
                                       BC250_DDI_ID_STARTDEVICE,
                                       0,
                                       0);
        }
        KdPrint(("AMDBC250: StartDevice invalid parameter\n"));
        Bc250LogGlobalCounters("StartDevice-invalid-parameter", STATUS_INVALID_PARAMETER);
        Bc250MarkCallbackExit(BC250_CB_STARTDEVICE, STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }
    BC250_TRACE_START(DevExt, BC250_START_STAGE_ENTRY, STATUS_SUCCESS, "entry");
    Bc250DiagWriteDword(L"DiagStartEntered", 1);
    Bc250DumpLifecycleCounters("StartDevice-entry");
    BC250_TRACE_START(DevExt, BC250_START_STAGE_VALIDATE_PARAMS, STATUS_SUCCESS, "validated-params");
    Bc250DiagWriteDword(L"DiagParamsValid", 1);

    /*
     * Try full hardware initialization first. If bring-up fails on this
     * platform, gracefully fall back to compatibility mode so the adapter
     * remains bound and testable without reboot loops.
     */
    /*
     * Copy only the interface bytes provided by dxgkrnl. The struct grows
     * across WDDM revisions, so blindly copying sizeof(DXGKRNL_INTERFACE)
     * can read beyond the caller-provided buffer on older kernels.
     */
    RtlZeroMemory(&DevExt->DxgkInterface, sizeof(DXGKRNL_INTERFACE));
    {
        SIZE_T cbIface = DxgkInterface->Size;
        if (cbIface > sizeof(DXGKRNL_INTERFACE)) {
            cbIface = sizeof(DXGKRNL_INTERFACE);
        }
        RtlCopyMemory(&DevExt->DxgkInterface, DxgkInterface, cbIface);
    }
    DevExt->DeviceHandle = DxgkInterface->DeviceHandle;
    DevExt->VendorId = AMD_VENDOR_ID;
    DevExt->DeviceId = AMDBC250_DEVICE_ID_PRIMARY;
    DevExt->PowerState = PowerDeviceD0;
    DevExt->CurrentMode.Width = AMDBC250_SAFE_MODE_WIDTH;
    DevExt->CurrentMode.Height = AMDBC250_SAFE_MODE_HEIGHT;
    DevExt->CurrentMode.RefreshRate = 60;
    DevExt->CurrentMode.BitsPerPixel = 32;
    DevExt->CurrentMode.PixelClock = (ULONG)(AMDBC250_SAFE_MODE_PIXEL_RATE / 1000);
    DevExt->CurrentMode.Format = D3DDDIFMT_A8R8G8B8;
    DevExt->CurrentMode.IsInterlaced = FALSE;
    DevExt->NumSupportedModes = 1;
    BC250_TRACE_START(DevExt, BC250_START_STAGE_COPY_DXGK_INTERFACE, STATUS_SUCCESS, "copied-dxgk-interface");

#if AMDBC250_SKIP_HW_INIT
    KdPrint(("AMDBC250: Skipping MMIO/hardware init for compatibility-start isolation test\n"));
    if (DevExt->TotalVramBytes == 0) {
        DevExt->TotalVramBytes = (SIZE_T)AMDBC250_TOTAL_MEMORY_MB * 1024ULL * 1024ULL;
    }

    DevExt->HardwareInitialized = TRUE;
    DevExt->PowerState = PowerDeviceD0;
    *NumberOfVideoPresentSources = AMDBC250_HEADLESS_RENDER_ONLY ? 0 : 1;
    *NumberOfChildren = AMDBC250_HEADLESS_RENDER_ONLY ? 0 : (AMDBC250_H_CHILDLESS_MODE ? 0 : 1);
    KdPrint(("AMDBC250: StartDevice using compatibility fallback path\n"));
    KdPrint(("AMDBC250: StartDevice return STATUS_SUCCESS Sources=%lu Children=%lu\n",
             *NumberOfVideoPresentSources,
             *NumberOfChildren));
    Bc250DumpLifecycleCounters("StartDevice-success-skiphw");
    Bc250LogGlobalCounters("StartDevice-success-skiphw", STATUS_SUCCESS);

    /*
     * Like the proven KMDOD sample, skip GPU HW init but still map the POST
     * display framebuffer so PresentDisplayOnly has pixels to write into while
     * keeping the firmware-initialized scanout alive.
     */
    {
        NTSTATUS AcquireStatus = Bc250AcquirePostDisplay(DevExt);
        Bc250DiagStartSnapshot(STATUS_SUCCESS,
                               AcquireStatus,
                               DevExt->FbIsActive ? 1 : 0,
                               DevExt->FbMapped ? 1 : 0);
    }

    return STATUS_SUCCESS;
#else
    /* Query device information (PCI BARs, interrupt resources) */
    BC250_TRACE_START(DevExt, BC250_START_STAGE_GET_DEVICE_INFO, STATUS_SUCCESS, "query-device-info");
    RtlZeroMemory(&DeviceInfo, sizeof(DeviceInfo));
    Status = DxgkInterface->DxgkCbGetDeviceInformation(
        DevExt->DeviceHandle,
        &DeviceInfo
        );

    if (!NT_SUCCESS(Status)) {
        DevExt->DebugStartStatus = Status;
        Bc250CaptureFirstFailureEx(DevExt,
                                   BC250_START_STAGE_GET_DEVICE_INFO,
                                   Status,
                                   "get-device-info-failed",
                                   BC250_DDI_ID_STARTDEVICE,
                                   0,
                                   0);
        KdPrint(("AMDBC250: STARTTRACE fail stage=GET_DEVICE_INFO status=0x%08X\n", Status));
        KdPrint(("AMDBC250: DxgkCbGetDeviceInformation failed: 0x%08X\n", Status));
        goto CompatibilityStart;
    }

    if (DeviceInfo.TranslatedResourceList == NULL ||
        DeviceInfo.TranslatedResourceList->Count == 0) {
        DevExt->DebugStartStatus = STATUS_NOT_FOUND;
        Bc250CaptureFirstFailureEx(DevExt,
                                   BC250_START_STAGE_GET_DEVICE_INFO,
                                   STATUS_NOT_FOUND,
                                   "resource-list-empty",
                                   BC250_DDI_ID_STARTDEVICE,
                                   0,
                                   0);
        KdPrint(("AMDBC250: STARTTRACE fail stage=GET_DEVICE_INFO status=STATUS_NOT_FOUND resource-list-empty\n"));
        KdPrint(("AMDBC250: No translated resources reported by dxgk\n"));
        goto CompatibilityStart;
    }

    /*
     * DXGK_DEVICE_INFO no longer exposes PCI vendor/device IDs directly in
     * recent WDK headers. Keep known BC-250 identifiers for diagnostics.
     */
    DevExt->VendorId = AMD_VENDOR_ID;
    DevExt->DeviceId = AMDBC250_DEVICE_ID_PRIMARY;

    /* Parse translated resources and map memory regions */
    ResourceList = &DeviceInfo.TranslatedResourceList->List[0].PartialResourceList;
    BC250_TRACE_START(DevExt, BC250_START_STAGE_PARSE_RESOURCES, STATUS_SUCCESS, "parse-resources");

    /*
     * BC-250: GPU MMIO registers live behind BAR5 (physical 0xFE800000),
     * NOT BAR0. The translated resource list is enumerated in BAR order so
     * BAR0 is listed first. Prefer the descriptor that matches the known
     * BAR5 physical base so we map the real GPU register window.
     */
    {
        BOOLEAN Bar5Found = FALSE;
        PHYSICAL_ADDRESS Bar5Start;
        ULONG Bar5Length = 0;

        for (DescriptorIndex = 0; DescriptorIndex < ResourceList->Count; DescriptorIndex++) {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR Bar5Desc;
            Bar5Desc = &ResourceList->PartialDescriptors[DescriptorIndex];

            if (Bar5Desc->Type != CmResourceTypeMemory) {
                continue;
            }

            if (Bar5Desc->u.Memory.Start.QuadPart == (LONGLONG)AMDBC250_BAR5_MMIO_PHYSICAL_BASE) {
                Bar5Found = TRUE;
                Bar5Start = Bar5Desc->u.Memory.Start;
                Bar5Length = Bar5Desc->u.Memory.Length;
                KdPrint(("AMDBC250: Found BAR5 MMIO at PA=0x%llX size=0x%X\n",
                         Bar5Start.QuadPart, Bar5Length));
                break;
            }
        }

        if (Bar5Found) {
            DevExt->MmioPhysicalBase = Bar5Start;
            DevExt->MmioSize = Bar5Length;
        }
    }

    if (DevExt->MmioSize == 0) {
        for (DescriptorIndex = 0; DescriptorIndex < ResourceList->Count; DescriptorIndex++) {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
            Descriptor = &ResourceList->PartialDescriptors[DescriptorIndex];

            if (Descriptor->Type != CmResourceTypeMemory) {
                continue;
            }

            if (DevExt->MmioSize == 0) {
                DevExt->MmioPhysicalBase = Descriptor->u.Memory.Start;
                DevExt->MmioSize = Descriptor->u.Memory.Length;
            } else if (DevExt->DoorbellSize == 0) {
                DevExt->DoorbellPhysicalBase = Descriptor->u.Memory.Start;
                DevExt->DoorbellSize = Descriptor->u.Memory.Length;
            }
        }
    }

    if (DevExt->MmioSize == 0) {
        DevExt->DebugStartStatus = STATUS_DEVICE_CONFIGURATION_ERROR;
        Bc250CaptureFirstFailureEx(DevExt,
                                   BC250_START_STAGE_PARSE_RESOURCES,
                                   STATUS_DEVICE_CONFIGURATION_ERROR,
                                   "no-mmio-resource",
                                   BC250_DDI_ID_STARTDEVICE,
                                   0,
                                   0);
        KdPrint(("AMDBC250: STARTTRACE fail stage=PARSE_RESOURCES no-mmio\n"));
        KdPrint(("AMDBC250: No MMIO resource found\n"));
        goto CompatibilityStart;
    }

    BC250_TRACE_START(DevExt, BC250_START_STAGE_MAP_MMIO, STATUS_SUCCESS, "map-mmio");
    DevExt->MmioVirtualBase = MmMapIoSpace(
        DevExt->MmioPhysicalBase,
        DevExt->MmioSize,
        MmNonCached
        );

    if (DevExt->MmioVirtualBase == NULL) {
        DevExt->DebugStartStatus = STATUS_INSUFFICIENT_RESOURCES;
        Bc250CaptureFirstFailureEx(DevExt,
                                   BC250_START_STAGE_MAP_MMIO,
                                   STATUS_INSUFFICIENT_RESOURCES,
                                   "mmio-map-failed",
                                   BC250_DDI_ID_STARTDEVICE,
                                   0,
                                   0);
        KdPrint(("AMDBC250: STARTTRACE fail stage=MAP_MMIO MmMapIoSpace-null\n"));
        KdPrint(("AMDBC250: Failed to map MMIO BAR\n"));
        goto CompatibilityStart;
    }

    KdPrint(("AMDBC250: MMIO mapped at VA=%p, PA=0x%llX, size=0x%llX\n",
             DevExt->MmioVirtualBase,
             DevExt->MmioPhysicalBase.QuadPart,
             (ULONGLONG)DevExt->MmioSize));

    /*
     * Acquire and map the POST display framebuffer so the display-only
     * present path can actually write pixels. Runs in both the full-hw and
     * compatibility fallback paths; failure just means headless.
     */
    (VOID)Bc250AcquirePostDisplay(DevExt);

    if (DevExt->DoorbellSize != 0) {
        BC250_TRACE_START(DevExt, BC250_START_STAGE_MAP_DOORBELL, STATUS_SUCCESS, "map-doorbell");
        DevExt->DoorbellVirtualBase = MmMapIoSpace(
            DevExt->DoorbellPhysicalBase,
            DevExt->DoorbellSize,
            MmNonCached
            );

        if (DevExt->DoorbellVirtualBase == NULL) {
            KdPrint(("AMDBC250: Failed to map doorbell BAR, using MMIO fallback\n"));
            DevExt->DoorbellSize = 0;
        }
    }

    /* Initialize hardware */
    BC250_TRACE_START(DevExt, BC250_START_STAGE_HW_INIT, STATUS_SUCCESS, "enter-hw-init");
    Status = Bc250HwInitialize(DevExt);
    Bc250DiagWriteDword(L"DiagHwInitStatus", (ULONG)Status);
    if (!NT_SUCCESS(Status)) {
        DevExt->DebugStartStatus = Status;
        Bc250CaptureFirstFailureEx(DevExt,
                                   BC250_START_STAGE_HW_INIT,
                                   Status,
                                   "hw-init-failed",
                                   BC250_DDI_ID_STARTDEVICE,
                                   0,
                                   0);
        KdPrint(("AMDBC250: STARTTRACE fail stage=HW_INIT status=0x%08X\n", Status));
        KdPrint(("AMDBC250: Hardware initialization failed: 0x%08X\n", Status));
        if (DevExt->DoorbellVirtualBase != NULL) {
            MmUnmapIoSpace(DevExt->DoorbellVirtualBase, DevExt->DoorbellSize);
            DevExt->DoorbellVirtualBase = NULL;
        }
        MmUnmapIoSpace(DevExt->MmioVirtualBase, DevExt->MmioSize);
        DevExt->MmioVirtualBase = NULL;
        goto CompatibilityStart;
    }

    *NumberOfVideoPresentSources = AMDBC250_HEADLESS_RENDER_ONLY ? 0 : 1;
    *NumberOfChildren = AMDBC250_HEADLESS_RENDER_ONLY ? 0 : (AMDBC250_H_CHILDLESS_MODE ? 0 : 1);

    DevExt->HardwareInitialized = TRUE;
    BC250_TRACE_START(DevExt, BC250_START_STAGE_SUCCESS, STATUS_SUCCESS, "full-hw-init-success");

    KdPrint(("AMDBC250: StartDevice completed with full hardware initialization\n"));
    KdPrint(("AMDBC250: StartDevice breadcrumbs Stage=%lu Status=0x%08X HwStage=%lu HwStatus=0x%08X\n",
             DevExt->DebugStartStage,
             DevExt->DebugStartStatus,
             DevExt->DebugHwInitStage,
             DevExt->DebugHwInitStatus));
    KdPrint(("AMDBC250: StartDevice failmap firstStage=%lu firstStatus=0x%08X firstDdi=%lu(%s) firstQType=%lu firstOut=%llu qaiCount=%lu qaiLastType=%lu qaiLastStatus=0x%08X qaiLastVramMB=%llu\n",
             DevExt->DebugFirstFailStage,
             (ULONG)DevExt->DebugFirstFailStatus,
             DevExt->DebugFirstFailDdiId,
             Bc250GetDdiName(DevExt->DebugFirstFailDdiId),
             DevExt->DebugFirstFailQueryType,
             DevExt->DebugFirstFailOutSize,
             DevExt->DebugQueryCount,
             DevExt->DebugQueryLastType,
             (ULONG)DevExt->DebugQueryLastStatus,
             (ULONGLONG)(DevExt->DebugQueryLastVramBytes / (1024ULL * 1024ULL))));
    KdPrint(("AMDBC250: StartDevice return STATUS_SUCCESS Sources=%lu Children=%lu\n",
             *NumberOfVideoPresentSources,
             *NumberOfChildren));
    Bc250LogGlobalCounters("StartDevice-success-fullhw", STATUS_SUCCESS);
    Bc250MarkCallbackExit(BC250_CB_STARTDEVICE, STATUS_SUCCESS);
    return STATUS_SUCCESS;

CompatibilityStart:
    BC250_TRACE_START(DevExt, BC250_START_STAGE_COMPAT_FALLBACK, DevExt->DebugStartStatus, "compat-fallback");
    if (DevExt->TotalVramBytes == 0) {
        DevExt->TotalVramBytes = (SIZE_T)AMDBC250_DEFAULT_VRAM_MB * 1024 * 1024;
        DevExt->UsedVramBytes = 0;
    }
    DevExt->HardwareInitialized = TRUE;
    DevExt->PowerState = PowerDeviceD0;
    *NumberOfVideoPresentSources = AMDBC250_HEADLESS_RENDER_ONLY ? 0 : 1;
    *NumberOfChildren = AMDBC250_HEADLESS_RENDER_ONLY ? 0 : (AMDBC250_H_CHILDLESS_MODE ? 0 : 1);
    KdPrint(("AMDBC250: StartDevice using compatibility fallback path\n"));
    KdPrint(("AMDBC250: StartDevice breadcrumbs Stage=%lu Status=0x%08X HwStage=%lu HwStatus=0x%08X\n",
             DevExt->DebugStartStage,
             DevExt->DebugStartStatus,
             DevExt->DebugHwInitStage,
             DevExt->DebugHwInitStatus));
    KdPrint(("AMDBC250: StartDevice failmap firstStage=%lu firstStatus=0x%08X firstDdi=%lu(%s) firstQType=%lu firstOut=%llu qaiCount=%lu qaiLastType=%lu qaiLastStatus=0x%08X qaiLastVramMB=%llu\n",
             DevExt->DebugFirstFailStage,
             (ULONG)DevExt->DebugFirstFailStatus,
             DevExt->DebugFirstFailDdiId,
             Bc250GetDdiName(DevExt->DebugFirstFailDdiId),
             DevExt->DebugFirstFailQueryType,
             DevExt->DebugFirstFailOutSize,
             DevExt->DebugQueryCount,
             DevExt->DebugQueryLastType,
             (ULONG)DevExt->DebugQueryLastStatus,
             (ULONGLONG)(DevExt->DebugQueryLastVramBytes / (1024ULL * 1024ULL))));
    KdPrint(("AMDBC250: StartDevice return STATUS_SUCCESS Sources=%lu Children=%lu\n",
             *NumberOfVideoPresentSources,
             *NumberOfChildren));
    Bc250LogGlobalCounters("StartDevice-success-compat", STATUS_SUCCESS);
    Bc250MarkCallbackExit(BC250_CB_STARTDEVICE, STATUS_SUCCESS);
    return STATUS_SUCCESS;
#endif
}

/*===========================================================================
  DxgkDdiStopDevice
  Called when the device is being stopped (e.g., driver update, shutdown).
  Stops GPU operations and releases hardware resources.
===========================================================================*/

NTSTATUS
APIENTRY
Bc250DdiStopDevice(
    _In_ PVOID MiniportDeviceContext
    )
{
    Bc250MarkCallbackEnter(BC250_CB_STOPDEVICE);
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)MiniportDeviceContext;
    ULONG ContextIndex;
    KIRQL OldIrql;
    LONG StopCount = InterlockedIncrement(&g_DeStopDevice);

    if (DevExt == NULL) {
        Bc250LogGlobalCounters("StopDevice-invalid-parameter", STATUS_INVALID_PARAMETER);
        Bc250MarkCallbackExit(BC250_CB_STOPDEVICE, STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }

    KdPrint(("AMDBC250: DxgkDdiStopDevice called count=%ld lastCb=%ld lastStatus=0x%08X lastVidPnCb=%ld lastQai=%ld\n",
             StopCount, g_Bc250LastCallbackId, (ULONG)g_Bc250LastCallbackStatus, g_Bc250LastVidPnCallbackId, g_Bc250LastQaiType));

    if (DevExt->HardwareInitialized) {
        Bc250HwShutdown(DevExt);
        DevExt->HardwareInitialized = FALSE;
    }

    KeAcquireSpinLock(&DevExt->ContextListLock, &OldIrql);
    for (ContextIndex = 0; ContextIndex < RTL_NUMBER_OF(DevExt->Contexts); ContextIndex++) {
        PAMDBC250_GPU_CONTEXT Context = DevExt->Contexts[ContextIndex];
        if (Context != NULL) {
            Context->IsValid = FALSE;
            DevExt->Contexts[ContextIndex] = NULL;
            ExFreePoolWithTag(Context, AMDBC250_TAG_CONTEXT);
        }
    }
    DevExt->NumContexts = 0;
    KeReleaseSpinLock(&DevExt->ContextListLock, OldIrql);

    /* Unmap MMIO */
    if (DevExt->MmioVirtualBase != NULL) {
        MmUnmapIoSpace(DevExt->MmioVirtualBase, DevExt->MmioSize);
        DevExt->MmioVirtualBase = NULL;
    }

    /* Unmap framebuffer */
    if (DevExt->FbVirtualBase != NULL) {
        MmUnmapIoSpace(DevExt->FbVirtualBase, DevExt->FbSize);
        DevExt->FbVirtualBase = NULL;
        DevExt->FbMapped = FALSE;
        DevExt->FbIsActive = FALSE;
        DevExt->SourceIsVisible = FALSE;
    }

    /* Unmap Doorbell */
    if (DevExt->DoorbellVirtualBase != NULL) {
        MmUnmapIoSpace(DevExt->DoorbellVirtualBase, DevExt->DoorbellSize);
        DevExt->DoorbellVirtualBase = NULL;
    }

    KdPrint(("AMDBC250: StopDevice completed\n"));
#if AMDBC250_DUMP_QAI_ON_STOPREMOVE
    Bc250DumpGlobalQaiHistory("StopDevice");
#endif
    Bc250LogGlobalCounters("StopDevice-success", STATUS_SUCCESS);
    Bc250MarkCallbackExit(BC250_CB_STOPDEVICE, STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

/*===========================================================================
  DxgkDdiRemoveDevice
  Final cleanup when device is removed from the system.
===========================================================================*/

NTSTATUS
APIENTRY
Bc250DdiRemoveDevice(
    _In_ PVOID MiniportDeviceContext
    )
{
    Bc250MarkCallbackEnter(BC250_CB_REMOVEDEVICE);
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)MiniportDeviceContext;
    LONG RemoveCount = InterlockedIncrement(&g_DeRemoveDevice);

    KdPrint(("AMDBC250: DxgkDdiRemoveDevice called count=%ld lastCb=%ld lastStatus=0x%08X lastVidPnCb=%ld lastQai=%ld\n",
             RemoveCount, g_Bc250LastCallbackId, (ULONG)g_Bc250LastCallbackStatus, g_Bc250LastVidPnCallbackId, g_Bc250LastQaiType));

    if (DevExt != NULL) {
        while (!IsListEmpty(&DevExt->AllocationList)) {
            PLIST_ENTRY Entry = RemoveHeadList(&DevExt->AllocationList);
            PAMDBC250_ALLOCATION Alloc = CONTAINING_RECORD(Entry, AMDBC250_ALLOCATION, ListEntry);
            ExFreePoolWithTag(Alloc, AMDBC250_TAG_ALLOCATION);
        }

        if (DevExt->HardwareInitialized) {
            Bc250HwShutdown(DevExt);
            DevExt->HardwareInitialized = FALSE;
        }
        if (DevExt->MmioVirtualBase != NULL) {
            MmUnmapIoSpace(DevExt->MmioVirtualBase, DevExt->MmioSize);
            DevExt->MmioVirtualBase = NULL;
        }
        if (DevExt->DoorbellVirtualBase != NULL) {
            MmUnmapIoSpace(DevExt->DoorbellVirtualBase, DevExt->DoorbellSize);
            DevExt->DoorbellVirtualBase = NULL;
        }
        if (g_Bc250PrimaryDevice == DevExt) {
            g_Bc250PrimaryDevice = NULL;
        }
        ExFreePoolWithTag(DevExt, AMDBC250_TAG_DEVICE_EXT);
    }

#if AMDBC250_DUMP_QAI_ON_STOPREMOVE
    Bc250DumpGlobalQaiHistory("RemoveDevice");
#endif
    Bc250LogGlobalCounters("RemoveDevice-success", STATUS_SUCCESS);
    Bc250MarkCallbackExit(BC250_CB_REMOVEDEVICE, STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Bc250DdiDispatchIoRequest(
    _In_ CONST PVOID MiniportDeviceContext,
    _In_ ULONG VidPnSourceId,
    _In_ PVIDEO_REQUEST_PACKET VideoRequestPacket
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    KdPrint(("AMDBC250: DispatchIoRequest VidPnSourceId=%lu Vrq=%p\n",
             VidPnSourceId,
             VideoRequestPacket));
    if (VideoRequestPacket != NULL && VideoRequestPacket->StatusBlock != NULL) {
        VideoRequestPacket->StatusBlock->Status = 1;
        VideoRequestPacket->StatusBlock->Information = 0;
    }
    return STATUS_SUCCESS;
}

/*===========================================================================
  DxgkDdiResetDevice
  Called during a TDR (Timeout Detection and Recovery) reset.
  Must reset the GPU to a known good state.
===========================================================================*/

VOID
APIENTRY
Bc250DdiResetDevice(
    _In_ PVOID MiniportDeviceContext
    )
{
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)MiniportDeviceContext;

    if (DevExt == NULL) {
        return;
    }

    KdPrint(("AMDBC250: DxgkDdiResetDevice called (TDR)\n"));

    DevExt->GpuResetInProgress = TRUE;
    DevExt->ResetCount++;

    if (DevExt->HardwareInitialized) {
        Bc250HwReset(DevExt);
    }

    DevExt->GpuResetInProgress = FALSE;
    KeSetEvent(&DevExt->ResetCompleteEvent, 0, FALSE);
}

/*===========================================================================
  DxgkDdiUnload
  Called when the driver is being unloaded from memory.
===========================================================================*/

VOID
APIENTRY
Bc250DdiUnload(
    VOID
    )
{
    Bc250MarkCallbackEnter(BC250_CB_UNLOAD);
    KdPrint(("AMDBC250: DxgkDdiUnload called\n"));
    Bc250MarkCallbackExit(BC250_CB_UNLOAD, STATUS_SUCCESS);
}

/*===========================================================================
  DxgkDdiInterruptRoutine
  ISR (Interrupt Service Routine) - runs at DIRQL.
  Reads interrupt status, clears hardware interrupt, schedules DPC.
===========================================================================*/

BOOLEAN
APIENTRY
Bc250DdiInterruptRoutine(
    _In_ PVOID  MiniportDeviceContext,
    _In_ ULONG  MessageNumber
    )
{
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)MiniportDeviceContext;
    ULONG WPtr;
    BOOLEAN OurInterrupt = FALSE;

    UNREFERENCED_PARAMETER(MessageNumber);

    if (DevExt == NULL ||
        !DevExt->HardwareInitialized ||
        DevExt->IhRing.VirtualAddress == NULL ||
        DevExt->IhRing.SizeInBytes == 0) {
        return FALSE;
    }

    /* Read IH ring write pointer to check for new entries */
    WPtr = Bc250ReadMmio(DevExt, AMDBC250_REG_IH_RB_WPTR);
    WPtr &= (ULONG)(DevExt->IhRing.SizeInBytes - 1);

    if (WPtr != DevExt->IhRing.ReadPointer) {
        /* New interrupt entries in the IH ring */
        DevExt->LastInterruptStatus = WPtr;
        DevExt->InterruptCount++;
        OurInterrupt = TRUE;

        /* Schedule DPC for deferred processing */
        if (DevExt->DxgkInterface.DxgkCbQueueDpc != NULL) {
            DevExt->DxgkInterface.DxgkCbQueueDpc(DevExt->DeviceHandle);
        }
    }

    return OurInterrupt;
}

/*===========================================================================
  DxgkDdiDpcRoutine
  DPC (Deferred Procedure Call) - processes interrupt events at DISPATCH_LEVEL.
  Processes IH ring entries and notifies WDDM of completed operations.
===========================================================================*/

VOID
APIENTRY
Bc250DdiDpcRoutine(
    _In_ PVOID MiniportDeviceContext
    )
{
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)MiniportDeviceContext;
    PULONG IhBase;
    ULONG WPtr, RPtr;
    ULONG MaxEntries;
    ULONG ProcessedEntries;
    ULONG ClientId, SrcId;
    ULONG Entry[4];

    if (DevExt == NULL ||
        !DevExt->HardwareInitialized ||
        DevExt->IhRing.VirtualAddress == NULL ||
        DevExt->IhRing.SizeInBytes == 0) {
        return;
    }

    IhBase = (PULONG)DevExt->IhRing.VirtualAddress;
    WPtr = Bc250ReadMmio(DevExt, AMDBC250_REG_IH_RB_WPTR) &
           (ULONG)(DevExt->IhRing.SizeInBytes - 1);
    RPtr = DevExt->IhRing.ReadPointer;
    MaxEntries = (ULONG)(DevExt->IhRing.SizeInBytes / AMDBC250_IH_RING_ENTRY_SIZE);
    ProcessedEntries = 0;

    /* Process all pending IH ring entries */
    while (RPtr != WPtr && ProcessedEntries < MaxEntries) {
        ULONG EntryOffset = (RPtr / AMDBC250_IH_RING_ENTRY_SIZE) * 4;

        /* Read 4-DWORD IH entry */
        Entry[0] = IhBase[EntryOffset + 0];
        Entry[1] = IhBase[EntryOffset + 1];
        Entry[2] = IhBase[EntryOffset + 2];
        Entry[3] = IhBase[EntryOffset + 3];

        ClientId = (Entry[0] >> 8) & 0xFF;
        SrcId    = Entry[0] & 0xFF;

        switch (ClientId) {
        case AMDBC250_IH_CLIENTID_GFX:
            /* GFX engine interrupt - check for fence completion */
            if (SrcId == 0xE0) {
                /* EOP (End of Pipe) event - fence signaled */
                DXGKARGCB_NOTIFY_INTERRUPT_DATA NotifyData = {0};
                ULONG FenceValue = Entry[2];

                DevExt->GlobalFence.LastSignaledValue = FenceValue;
                if (DevExt->GlobalFence.VirtualAddress != NULL) {
                    *DevExt->GlobalFence.VirtualAddress = FenceValue;
                }

                NotifyData.InterruptType = DXGK_INTERRUPT_DMA_COMPLETED;
                NotifyData.DmaCompleted.SubmissionFenceId = FenceValue;
                if (DevExt->DxgkInterface.DxgkCbNotifyInterrupt != NULL) {
                    DevExt->DxgkInterface.DxgkCbNotifyInterrupt(
                        DevExt->DeviceHandle,
                        &NotifyData
                        );
                }
            }
            break;

        case AMDBC250_IH_CLIENTID_DCE:
            /* Display engine interrupt - VSYNC */
            {
                DXGKARGCB_NOTIFY_INTERRUPT_DATA NotifyData = {0};
                NotifyData.InterruptType = DXGK_INTERRUPT_CRTC_VSYNC;
                NotifyData.CrtcVsync.VidPnTargetId = 0;
                if (DevExt->DxgkInterface.DxgkCbNotifyInterrupt != NULL) {
                    DevExt->DxgkInterface.DxgkCbNotifyInterrupt(
                        DevExt->DeviceHandle,
                        &NotifyData
                        );
                }
            }
            break;

        default:
            break;
        }

        /* Advance read pointer */
        RPtr += AMDBC250_IH_RING_ENTRY_SIZE;
        if (RPtr >= (ULONG)DevExt->IhRing.SizeInBytes) {
            RPtr = 0;
        }
        ProcessedEntries++;
    }

    if (ProcessedEntries == MaxEntries && RPtr != WPtr) {
        KdPrint(("AMDBC250: IH ring processing hit safety limit; forcing RPTR sync\n"));
    }

    /* Update IH ring read pointer */
    DevExt->IhRing.ReadPointer = RPtr;
    Bc250WriteMmio(DevExt, AMDBC250_REG_IH_RB_RPTR, RPtr);

    /* Notify WDDM that DPC processing is complete */
    if (DevExt->DxgkInterface.DxgkCbNotifyDpc != NULL) {
        DevExt->DxgkInterface.DxgkCbNotifyDpc(DevExt->DeviceHandle);
    }
}

/*===========================================================================
  DxgkDdiQueryAdapterInfo
  Reports GPU capabilities and configuration to the WDDM framework.
===========================================================================*/

NTSTATUS
APIENTRY
Bc250DdiQueryAdapterInfo(
    _In_ CONST HANDLE               hAdapter,
    _In_ CONST DXGKARG_QUERYADAPTERINFO *pQueryAdapterInfo
    )
{
    Bc250MarkCallbackEnter(BC250_CB_QUERYADAPTERINFO);
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)hAdapter;
    SIZE_T ReportedVramBytes = 0;
    LONG QueryCount = InterlockedIncrement(&g_DeQueryAdapterInfo);

#define BC250_QAI_RETURN(_status, _vramBytes, _note)                                    \
    do {                                                                                 \
        Bc250RecordGlobalQai(                                                            \
            (pQueryAdapterInfo != NULL) ? pQueryAdapterInfo->Type : 0,                  \
            (pQueryAdapterInfo != NULL) ? pQueryAdapterInfo->OutputDataSize : 0,        \
            (_status));                                                                  \
        Bc250TraceQueryResult(DevExt,                                                    \
                               (pQueryAdapterInfo != NULL) ? pQueryAdapterInfo->Type : 0, \
                               (pQueryAdapterInfo != NULL) ? pQueryAdapterInfo->OutputDataSize : 0, \
                               (_status),                                                 \
                              (_vramBytes),                                              \
                              (_note));                                                  \
        if (!NT_SUCCESS(_status)) {                                                      \
            Bc250CaptureFirstFailureEx(DevExt,                                           \
                                       BC250_FAIL_STAGE_QUERYADAPTERINFO,                \
                                       (_status),                                         \
                                       (_note),                                           \
                                       BC250_DDI_ID_QUERYADAPTERINFO,                    \
                                       (pQueryAdapterInfo != NULL) ? pQueryAdapterInfo->Type : 0, \
                                       (pQueryAdapterInfo != NULL) ? pQueryAdapterInfo->OutputDataSize : 0); \
        }                                                                                \
        InterlockedExchange(&g_Bc250LastQaiType, (LONG)((pQueryAdapterInfo != NULL) ? pQueryAdapterInfo->Type : 0)); \
        Bc250MarkCallbackExit(BC250_CB_QUERYADAPTERINFO, (_status));                    \
        Bc250LogGlobalCounters("QueryAdapterInfo-return", (_status));                   \
        return (_status);                                                                \
    } while (0)

    if (DevExt == NULL || pQueryAdapterInfo == NULL || pQueryAdapterInfo->pOutputData == NULL) {
        if (DevExt != NULL) {
            Bc250CaptureFirstFailureEx(DevExt,
                                       BC250_FAIL_STAGE_QUERYADAPTERINFO,
                                       STATUS_INVALID_PARAMETER,
                                       "query-invalid-parameter",
                                       BC250_DDI_ID_QUERYADAPTERINFO,
                                       (pQueryAdapterInfo != NULL) ? pQueryAdapterInfo->Type : 0,
                                       (pQueryAdapterInfo != NULL) ? pQueryAdapterInfo->OutputDataSize : 0);
        }
        Bc250RecordGlobalQai((pQueryAdapterInfo != NULL) ? pQueryAdapterInfo->Type : 0,
                             (pQueryAdapterInfo != NULL) ? pQueryAdapterInfo->OutputDataSize : 0,
                             STATUS_INVALID_PARAMETER);
        Bc250LogGlobalCounters("QueryAdapterInfo-invalid-parameter", STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }

    KdPrint(("AMDBC250: QueryAdapterInfo enter count=%ld Type=%u(%s) OutSize=%llu\n",
             QueryCount,
             pQueryAdapterInfo->Type,
             Bc250GetQaiTypeName(pQueryAdapterInfo->Type),
             (ULONGLONG)pQueryAdapterInfo->OutputDataSize));
    Bc250DumpLifecycleCounters("QAI-enter");

    switch (pQueryAdapterInfo->Type) {

    case DXGKQAITYPE_UMDRIVERPRIVATE:
        if (pQueryAdapterInfo->OutputDataSize != 0) {
            RtlZeroMemory(pQueryAdapterInfo->pOutputData, pQueryAdapterInfo->OutputDataSize);
        }
        KdPrint(("AMDBC250: QueryAdapterInfo - UMDRIVERPRIVATE zeroed\n"));
        BC250_QAI_RETURN(STATUS_SUCCESS, 0, "umdriverprivate-zeroed");

    case DXGKQAITYPE_DRIVERCAPS: {
        DXGK_DRIVERCAPS *pCaps = (DXGK_DRIVERCAPS *)pQueryAdapterInfo->pOutputData;
        if (pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_DRIVERCAPS)) {
            BC250_QAI_RETURN(STATUS_BUFFER_TOO_SMALL, 0, "drivercaps-buffer-too-small");
        }
        RtlZeroMemory(pCaps, sizeof(DXGK_DRIVERCAPS));

        /*
         * Full WDDM adapter: advertise render capabilities.
         * WDDM version consistent with the interface we registered.
         */
        pCaps->WDDMVersion = DXGKDDI_WDDMv1_3;
        pCaps->HighestAcceptableAddress.QuadPart = -1;
        pCaps->SupportNonVGA = TRUE;
        pCaps->SupportSmoothRotation = TRUE;

        /* Advertise render support */

        KdPrint(("AMDBC250: QueryAdapterInfo - DRIVERCAPS reported (full WDDM)\n"));
        BC250_QAI_RETURN(STATUS_SUCCESS, 0, "drivercaps");
    }

    case DXGKQAITYPE_DISPLAY_DRIVERCAPS_EXTENSION: {
        DXGK_DISPLAY_DRIVERCAPS_EXTENSION *pDisplayCaps =
            (DXGK_DISPLAY_DRIVERCAPS_EXTENSION *)pQueryAdapterInfo->pOutputData;
        if (pQueryAdapterInfo->OutputDataSize < sizeof(*pDisplayCaps)) {
            BC250_QAI_RETURN(STATUS_BUFFER_TOO_SMALL, 0, "displaycaps-buffer-too-small");
        }
        RtlZeroMemory(pDisplayCaps, pQueryAdapterInfo->OutputDataSize);
        pDisplayCaps->VirtualModeSupport = 1;
        KdPrint(("AMDBC250: QueryAdapterInfo - DISPLAY_DRIVERCAPS_EXTENSION (virtual mode=1)\n"));
        BC250_QAI_RETURN(STATUS_SUCCESS, 0, "display-drivercaps-extension");
    }

    case DXGKQAITYPE_QUERYSEGMENT: {
#if AMDBC250_QAI_DOD_STRICT
        BC250_QAI_RETURN(STATUS_NOT_SUPPORTED, 0, "querysegment-dod-strict");
#else
        DXGK_QUERYSEGMENTOUT *pSegOut = (DXGK_QUERYSEGMENTOUT *)pQueryAdapterInfo->pOutputData;

        if (pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_QUERYSEGMENTOUT)) {
            BC250_QAI_RETURN(STATUS_BUFFER_TOO_SMALL, 0, "querysegment-buffer-too-small");
        }

        if (pSegOut->pSegmentDescriptor == NULL) {
            pSegOut->NbSegment = AMDBC250_QSEG_TWO_SEGMENTS ? 2 : 1;
            BC250_QAI_RETURN(STATUS_SUCCESS, 0, "querysegment-count-only");
        }

        /* Second call: report segment details */
        ReportedVramBytes = Bc250GetReportedVramSizeBytes(DevExt);
        KdPrint(("AMDBC250: QUERYSEGMENT mem total=%lluMB used=%lluMB reported=%lluMB\n",
                 (ULONGLONG)(DevExt->TotalVramBytes / (1024ULL * 1024ULL)),
                 (ULONGLONG)(DevExt->UsedVramBytes / (1024ULL * 1024ULL)),
                 (ULONGLONG)(ReportedVramBytes / (1024ULL * 1024ULL))));

        pSegOut->NbSegment = AMDBC250_QSEG_TWO_SEGMENTS ? 2 : 1;

        /* Segment 0: UMA local segment (CPU-visible and cache coherent).
           BC-250 dedicated VRAM is 256MB at MC 0xF400000000 (aper_base 0xC0000000). */
        pSegOut->pSegmentDescriptor[0].BaseAddress.QuadPart = 0xF400000000ULL;
        pSegOut->pSegmentDescriptor[0].Size = ReportedVramBytes;
        pSegOut->pSegmentDescriptor[0].CommitLimit = ReportedVramBytes;
        BC250_SET_UMA_SEGMENT_FLAGS(pSegOut->pSegmentDescriptor[0].Flags);
        if (AMDBC250_QSEG_TWO_SEGMENTS) {
            SIZE_T GttBytes = Bc250GetReportedGttSizeBytes();
            pSegOut->pSegmentDescriptor[1].BaseAddress.QuadPart = 0;
            pSegOut->pSegmentDescriptor[1].Size = GttBytes;
            pSegOut->pSegmentDescriptor[1].CommitLimit = GttBytes;
            BC250_SET_UMA_SEGMENT_FLAGS(pSegOut->pSegmentDescriptor[1].Flags);
        }

        KdPrint(("AMDBC250: QueryAdapterInfo - QUERYSEGMENT reported\n"));
        BC250_QAI_RETURN(STATUS_SUCCESS, ReportedVramBytes, "querysegment");
#endif
    }

#if defined(DXGKQAITYPE_QUERYSEGMENT2)
    case DXGKQAITYPE_QUERYSEGMENT2: {
#if AMDBC250_QAI_DOD_STRICT
        BC250_QAI_RETURN(STATUS_NOT_SUPPORTED, 0, "querysegment2-dod-strict");
#elif AMDBC250_QAI_SEGMENT_V1_ONLY
        BC250_QAI_RETURN(STATUS_NOT_SUPPORTED, 0, "querysegment2-disabled-v1-only");
#else
        DXGK_QUERYSEGMENTOUT2 *pSegOut2 = (DXGK_QUERYSEGMENTOUT2 *)pQueryAdapterInfo->pOutputData;

        if (pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_QUERYSEGMENTOUT2)) {
            BC250_QAI_RETURN(STATUS_BUFFER_TOO_SMALL, 0, "querysegment2-buffer-too-small");
        }

        if (pSegOut2->pSegmentDescriptor == NULL) {
            pSegOut2->SegmentCount = AMDBC250_QSEG_TWO_SEGMENTS ? 2 : 1;
            BC250_QAI_RETURN(STATUS_SUCCESS, 0, "querysegment2-count-only");
        }

        ReportedVramBytes = Bc250GetReportedVramSizeBytes(DevExt);
        KdPrint(("AMDBC250: QUERYSEGMENT2 mem reported=%lluMB segCount=%lu\n",
                 (ULONGLONG)(ReportedVramBytes / (1024ULL * 1024ULL)),
                 (ULONG)(AMDBC250_QSEG_TWO_SEGMENTS ? 2 : 1)));

        pSegOut2->SegmentCount = AMDBC250_QSEG_TWO_SEGMENTS ? 2 : 1;
        RtlZeroMemory(pSegOut2->pSegmentDescriptor, sizeof(DXGK_SEGMENTDESCRIPTOR2) * pSegOut2->SegmentCount);

        pSegOut2->pSegmentDescriptor[0].BaseAddress.QuadPart = 0xF400000000ULL;
        pSegOut2->pSegmentDescriptor[0].Size = ReportedVramBytes;
        BC250_SET_UMA_SEGMENT_FLAGS(pSegOut2->pSegmentDescriptor[0].Flags);
        if (AMDBC250_QSEG_TWO_SEGMENTS) {
            SIZE_T GttBytes = Bc250GetReportedGttSizeBytes();
            pSegOut2->pSegmentDescriptor[1].BaseAddress.QuadPart = 0;
            pSegOut2->pSegmentDescriptor[1].Size = GttBytes;
            BC250_SET_UMA_SEGMENT_FLAGS(pSegOut2->pSegmentDescriptor[1].Flags);
        }

        KdPrint(("AMDBC250: QueryAdapterInfo - QUERYSEGMENT2 reported\n"));
        BC250_QAI_RETURN(STATUS_SUCCESS, ReportedVramBytes, "querysegment2");
#endif
    }
#endif

#if defined(DXGKQAITYPE_QUERYSEGMENT3)
    case DXGKQAITYPE_QUERYSEGMENT3: {
#if AMDBC250_QAI_DOD_STRICT
        BC250_QAI_RETURN(STATUS_NOT_SUPPORTED, 0, "querysegment3-dod-strict");
#elif AMDBC250_QAI_SEGMENT_V1_ONLY
        BC250_QAI_RETURN(STATUS_NOT_SUPPORTED, 0, "querysegment3-disabled-v1-only");
#else
        DXGK_QUERYSEGMENTOUT3 *pSegOut3 = (DXGK_QUERYSEGMENTOUT3 *)pQueryAdapterInfo->pOutputData;

        if (pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_QUERYSEGMENTOUT3)) {
            BC250_QAI_RETURN(STATUS_BUFFER_TOO_SMALL, 0, "querysegment3-buffer-too-small");
        }

        if (pSegOut3->pSegmentDescriptor == NULL) {
            pSegOut3->NbSegment = AMDBC250_QSEG_TWO_SEGMENTS ? 2 : 1;
            pSegOut3->PagingBufferSegmentId = 0;
            pSegOut3->PagingBufferSize = Bc250GetReportedPagingBufferSizeBytes();
            pSegOut3->PagingBufferPrivateDataSize = 0;
            BC250_QAI_RETURN(STATUS_SUCCESS, 0, "querysegment3-count-only");
        }

        ReportedVramBytes = Bc250GetReportedVramSizeBytes(DevExt);
        KdPrint(("AMDBC250: QUERYSEGMENT3 mem reported=%lluMB pagingSeg=%lu pagingBuf=%lu\n",
                 (ULONGLONG)(ReportedVramBytes / (1024ULL * 1024ULL)),
                 (ULONG)pSegOut3->PagingBufferSegmentId,
                 (ULONG)pSegOut3->PagingBufferSize));

        pSegOut3->NbSegment = AMDBC250_QSEG_TWO_SEGMENTS ? 2 : 1;
        pSegOut3->PagingBufferSegmentId = 0;
        pSegOut3->PagingBufferSize = Bc250GetReportedPagingBufferSizeBytes();
        pSegOut3->PagingBufferPrivateDataSize = 0;
        RtlZeroMemory(pSegOut3->pSegmentDescriptor, sizeof(DXGK_SEGMENTDESCRIPTOR3) * pSegOut3->NbSegment);

        pSegOut3->pSegmentDescriptor[0].BaseAddress.QuadPart = 0xF400000000ULL;
        pSegOut3->pSegmentDescriptor[0].Size = ReportedVramBytes;
        pSegOut3->pSegmentDescriptor[0].CommitLimit = ReportedVramBytes;
        BC250_SET_UMA_SEGMENT_FLAGS(pSegOut3->pSegmentDescriptor[0].Flags);
        if (AMDBC250_QSEG_TWO_SEGMENTS) {
            SIZE_T GttBytes = Bc250GetReportedGttSizeBytes();
            pSegOut3->pSegmentDescriptor[1].BaseAddress.QuadPart = 0;
            pSegOut3->pSegmentDescriptor[1].Size = GttBytes;
            pSegOut3->pSegmentDescriptor[1].CommitLimit = GttBytes;
            BC250_SET_UMA_SEGMENT_FLAGS(pSegOut3->pSegmentDescriptor[1].Flags);
        }

        KdPrint(("AMDBC250: QueryAdapterInfo - QUERYSEGMENT3 reported\n"));
        BC250_QAI_RETURN(STATUS_SUCCESS, ReportedVramBytes, "querysegment3");
#endif
    }
#endif

#if defined(DXGKQAITYPE_WDDMDEVICECAPS)
    case DXGKQAITYPE_WDDMDEVICECAPS: {
#if AMDBC250_QAI_DOD_STRICT
        BC250_QAI_RETURN(STATUS_NOT_SUPPORTED, 0, "wddmdevicecaps-dod-strict");
#else
        DXGK_WDDMDEVICECAPS *pWddmCaps = (DXGK_WDDMDEVICECAPS *)pQueryAdapterInfo->pOutputData;
        if (pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_WDDMDEVICECAPS)) {
            BC250_QAI_RETURN(STATUS_BUFFER_TOO_SMALL, 0, "wddmdevicecaps-buffer-too-small");
        }
        RtlZeroMemory(pWddmCaps, sizeof(*pWddmCaps));
        pWddmCaps->WDDMVersion = Bc250GetReportedWddmCapsVersion();
        KdPrint(("AMDBC250: QueryAdapterInfo - WDDMDEVICECAPS reported\n"));
        BC250_QAI_RETURN(STATUS_SUCCESS, 0, "wddmdevicecaps");
#endif
    }
#endif

#if defined(DXGKQAITYPE_GPUPCAPS)
    case DXGKQAITYPE_GPUPCAPS: {
#if AMDBC250_QAI_DOD_STRICT
        BC250_QAI_RETURN(STATUS_NOT_SUPPORTED, 0, "gpupcaps-dod-strict");
#else
        DXGK_GPUPCAPS *pGpuPCaps = (DXGK_GPUPCAPS *)pQueryAdapterInfo->pOutputData;
        if (pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_GPUPCAPS)) {
            BC250_QAI_RETURN(STATUS_BUFFER_TOO_SMALL, 0, "gpupcaps-buffer-too-small");
        }
        RtlZeroMemory(pGpuPCaps, sizeof(*pGpuPCaps));
        KdPrint(("AMDBC250: QueryAdapterInfo - GPUPCAPS reported\n"));
        BC250_QAI_RETURN(STATUS_SUCCESS, 0, "gpupcaps");
#endif
    }
#endif

#if defined(DXGKQAITYPE_DEVICE_TYPE_CAPS)
    case DXGKQAITYPE_DEVICE_TYPE_CAPS: {
#if AMDBC250_QAI_DOD_STRICT
        BC250_QAI_RETURN(STATUS_NOT_SUPPORTED, 0, "device-type-caps-dod-strict");
#else
        DXGK_DEVICE_TYPE_CAPS *pDevTypeCaps = (DXGK_DEVICE_TYPE_CAPS *)pQueryAdapterInfo->pOutputData;
        if (pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_DEVICE_TYPE_CAPS)) {
            BC250_QAI_RETURN(STATUS_BUFFER_TOO_SMALL, 0, "device-type-caps-buffer-too-small");
        }
        RtlZeroMemory(pDevTypeCaps, sizeof(*pDevTypeCaps));
        pDevTypeCaps->Discrete = 0;
        pDevTypeCaps->Detachable = 0;
        KdPrint(("AMDBC250: QueryAdapterInfo - DEVICE_TYPE_CAPS reported\n"));
        BC250_QAI_RETURN(STATUS_SUCCESS, 0, "device-type-caps");
#endif
    }
#endif

#if defined(DXGKQAITYPE_QUERYSEGMENTCOUNT)
    case DXGKQAITYPE_QUERYSEGMENTCOUNT: {
#if AMDBC250_QAI_DOD_STRICT
        BC250_QAI_RETURN(STATUS_NOT_SUPPORTED, 0, "querysegmentcount-dod-strict");
#else
        DXGK_QUERYSEGMENTCOUNTOUT *pSegCountOut = (DXGK_QUERYSEGMENTCOUNTOUT *)pQueryAdapterInfo->pOutputData;
        if (pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_QUERYSEGMENTCOUNTOUT)) {
            BC250_QAI_RETURN(STATUS_BUFFER_TOO_SMALL, 0, "querysegmentcount-buffer-too-small");
        }
        RtlZeroMemory(pSegCountOut, sizeof(*pSegCountOut));
        pSegCountOut->SegmentCount = AMDBC250_QSEG_TWO_SEGMENTS ? 2 : 1;
        KdPrint(("AMDBC250: QueryAdapterInfo - QUERYSEGMENTCOUNT reported\n"));
        BC250_QAI_RETURN(STATUS_SUCCESS, 0, "querysegmentcount");
#endif
    }
#endif

    default:
        KdPrint(("AMDBC250: QueryAdapterInfo unsupported type %u out=%llu\n",
                 pQueryAdapterInfo->Type,
                 (ULONGLONG)pQueryAdapterInfo->OutputDataSize));
#if AMDBC250_QAI_UNKNOWN_NOTSUPPORTED
        BC250_QAI_RETURN(STATUS_NOT_SUPPORTED, 0, "unknown-not-supported");
#else
        if (pQueryAdapterInfo->OutputDataSize != 0) {
            RtlZeroMemory(pQueryAdapterInfo->pOutputData, pQueryAdapterInfo->OutputDataSize);
        }
        BC250_QAI_RETURN(STATUS_SUCCESS, 0, "unknown-zero-success");
#endif
    }

#undef BC250_QAI_RETURN
}

NTSTATUS
APIENTRY
Bc250DdiSetPalette(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_SETPALETTE *pSetPalette
    )
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (pSetPalette == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Bc250DdiSetPointerPosition(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_SETPOINTERPOSITION *pSetPointerPosition
    )
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (pSetPointerPosition == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Bc250DdiSetPointerShape(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_SETPOINTERSHAPE *pSetPointerShape
    )
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (pSetPointerShape == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Bc250DdiIsSupportedVidPn(
    _In_ CONST HANDLE hAdapter,
    _Inout_ DXGKARG_ISSUPPORTEDVIDPN *pIsSupportedVidPn
    )
{
    Bc250MarkCallbackEnter(BC250_CB_ISSUPPORTEDVIDPN);
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)hAdapter;
    const DXGK_VIDPN_INTERFACE *VidPnInterface;
    D3DKMDT_HVIDPNTOPOLOGY Topology;
    const DXGK_VIDPNTOPOLOGY_INTERFACE *TopologyInterface;
    SIZE_T NumPaths;
    NTSTATUS Status;

    if (pIsSupportedVidPn == NULL) {
        Bc250MarkCallbackExit(BC250_CB_ISSUPPORTEDVIDPN, STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }

    pIsSupportedVidPn->IsVidPnSupported = FALSE;

    if (AMDBC250_HEADLESS_RENDER_ONLY) {
        Bc250MarkCallbackExit(BC250_CB_ISSUPPORTEDVIDPN, STATUS_SUCCESS);
        return STATUS_SUCCESS;
    }

    if (DevExt == NULL) {
        Bc250MarkCallbackExit(BC250_CB_ISSUPPORTEDVIDPN, STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }

    Bc250DiagWriteDword(L"DiagIsSupVidPnHandle", (ULONG)(ULONG_PTR)pIsSupportedVidPn->hDesiredVidPn);

    if (pIsSupportedVidPn->hDesiredVidPn == 0) {
        pIsSupportedVidPn->IsVidPnSupported = TRUE;
        Bc250DiagWriteDword(L"DiagIsSupVidPnNull", 1);
        Bc250MarkCallbackExit(BC250_CB_ISSUPPORTEDVIDPN, STATUS_SUCCESS);
        return STATUS_SUCCESS;
    }

    Status = DevExt->DxgkInterface.DxgkCbQueryVidPnInterface(
        pIsSupportedVidPn->hDesiredVidPn,
        DXGK_VIDPN_INTERFACE_VERSION_V1,
        &VidPnInterface);
    if (!NT_SUCCESS(Status)) {
        Bc250MarkCallbackExit(BC250_CB_ISSUPPORTEDVIDPN, Status);
        return Status;
    }

    Status = VidPnInterface->pfnGetTopology(
        pIsSupportedVidPn->hDesiredVidPn,
        &Topology,
        &TopologyInterface);
    if (!NT_SUCCESS(Status)) {
        Bc250MarkCallbackExit(BC250_CB_ISSUPPORTEDVIDPN, Status);
        return Status;
    }

    /*
     * Mirror the proven-working KMDOD (bdd_dmm.cxx IsSupportedVidPn): accept
     * ANY topology that does not route more paths through a single source than
     * we have children (MAX_CHILDREN=1). Being too strict here (e.g. requiring
     * exactly one path with source==0 && target==0) made dxgkrnl retry with
     * alternate topologies forever, hanging before CommitVidPn. We have exactly
     * one child, so any single-source/single-target path counts as supported.
     */
    for (D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId = 0; SourceId < 1; ++SourceId) {
        SIZE_T NumPathsFromSource = 0;
        Status = TopologyInterface->pfnGetNumPathsFromSource(Topology, SourceId, &NumPathsFromSource);
        if (Status == STATUS_GRAPHICS_SOURCE_NOT_IN_TOPOLOGY) {
            continue;
        }
        if (!NT_SUCCESS(Status)) {
            Bc250MarkCallbackExit(BC250_CB_ISSUPPORTEDVIDPN, Status);
            return Status;
        }
        if (NumPathsFromSource > 1) {
            /* more paths than children: not supported (already default FALSE) */
            Bc250MarkCallbackExit(BC250_CB_ISSUPPORTEDVIDPN, STATUS_SUCCESS);
            return STATUS_SUCCESS;
        }
    }

    (VOID)TopologyInterface->pfnGetNumPaths(Topology, &NumPaths);

    /* All sources succeeded, so this VidPn is supported */
    pIsSupportedVidPn->IsVidPnSupported = TRUE;
    Bc250DiagWriteDword(L"DiagIsSupVidPnResult", pIsSupportedVidPn->IsVidPnSupported ? 1 : 0);
    Bc250DiagWriteDword(L"DiagIsSupVidPnNumPaths", (ULONG)NumPaths);
    Bc250MarkCallbackExit(BC250_CB_ISSUPPORTEDVIDPN, STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

/*===========================================================================
  DxgkDdiCreateDevice
  Creates a per-process device context (called when a D3D app opens the GPU).
===========================================================================*/

NTSTATUS
APIENTRY
Bc250DdiCreateDevice(
    _In_    CONST HANDLE        hAdapter,
    _Inout_ DXGKARG_CREATEDEVICE *pCreateDevice
    )
{
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)hAdapter;
    PAMDBC250_GPU_CONTEXT Context;
    KIRQL OldIrql;
    ULONG Slot;

    if (DevExt == NULL || pCreateDevice == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    KdPrint(("AMDBC250: DxgkDdiCreateDevice called\n"));

    Context = (PAMDBC250_GPU_CONTEXT)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        sizeof(AMDBC250_GPU_CONTEXT),
        AMDBC250_TAG_CONTEXT
        );

    if (Context == NULL) {
        return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(Context, sizeof(AMDBC250_GPU_CONTEXT));
    KeInitializeSpinLock(&Context->ContextLock);
    InitializeListHead(&Context->AllocationList);
    Context->IsValid = TRUE;
    Context->ContextId = 0;

    /* Track context in adapter state for diagnostics and teardown safety. */
    KeAcquireSpinLock(&DevExt->ContextListLock, &OldIrql);
    for (Slot = 0; Slot < RTL_NUMBER_OF(DevExt->Contexts); Slot++) {
        if (DevExt->Contexts[Slot] == NULL) {
            DevExt->Contexts[Slot] = Context;
            DevExt->NumContexts++;
            Context->ContextId = Slot + 1;
            break;
        }
    }
    KeReleaseSpinLock(&DevExt->ContextListLock, OldIrql);

    if (Context->ContextId == 0) {
        ExFreePoolWithTag(Context, AMDBC250_TAG_CONTEXT);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    pCreateDevice->hDevice = (HANDLE)Context;

    return STATUS_SUCCESS;
}

/*===========================================================================
  DxgkDdiDestroyDevice
  Destroys a per-process device context.
===========================================================================*/

NTSTATUS
APIENTRY
Bc250DdiDestroyDevice(
    _In_ CONST HANDLE hDevice
    )
{
    PAMDBC250_GPU_CONTEXT Context = (PAMDBC250_GPU_CONTEXT)hDevice;
    PAMDBC250_DEVICE_EXTENSION DevExt = g_Bc250PrimaryDevice;
    KIRQL OldIrql;
    ULONG Slot;

    KdPrint(("AMDBC250: DxgkDdiDestroyDevice called\n"));

    if (Context != NULL) {
        if (DevExt != NULL && Context->ContextId != 0) {
            KeAcquireSpinLock(&DevExt->ContextListLock, &OldIrql);
            for (Slot = 0; Slot < RTL_NUMBER_OF(DevExt->Contexts); Slot++) {
                if (DevExt->Contexts[Slot] == Context) {
                    DevExt->Contexts[Slot] = NULL;
                    if (DevExt->NumContexts > 0) {
                        DevExt->NumContexts--;
                    }
                    break;
                }
            }
            KeReleaseSpinLock(&DevExt->ContextListLock, OldIrql);
        }

        Context->IsValid = FALSE;
        ExFreePoolWithTag(Context, AMDBC250_TAG_CONTEXT);
    }

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Bc250DdiCreateContext(
    _In_    CONST HANDLE           hDevice,
    _Inout_ DXGKARG_CREATECONTEXT *pCreateContext
    )
{
    PAMDBC250_DEVICE_EXTENSION DevExt = g_Bc250PrimaryDevice;
    PAMDBC250_GPU_CONTEXT Context;
    KIRQL OldIrql;
    ULONG Slot;

    UNREFERENCED_PARAMETER(hDevice);

    if (DevExt == NULL || pCreateContext == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    Context = (PAMDBC250_GPU_CONTEXT)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        sizeof(AMDBC250_GPU_CONTEXT),
        AMDBC250_TAG_CONTEXT);
    if (Context == NULL) {
        return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(Context, sizeof(AMDBC250_GPU_CONTEXT));
    KeInitializeSpinLock(&Context->ContextLock);
    InitializeListHead(&Context->AllocationList);
    Context->IsValid = TRUE;

    KeAcquireSpinLock(&DevExt->ContextListLock, &OldIrql);
    for (Slot = 0; Slot < RTL_NUMBER_OF(DevExt->Contexts); Slot++) {
        if (DevExt->Contexts[Slot] == NULL) {
            DevExt->Contexts[Slot] = Context;
            DevExt->NumContexts++;
            Context->ContextId = Slot + 1;
            break;
        }
    }
    KeReleaseSpinLock(&DevExt->ContextListLock, OldIrql);

    if (Context->ContextId == 0) {
        ExFreePoolWithTag(Context, AMDBC250_TAG_CONTEXT);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    pCreateContext->hContext = (HANDLE)Context;
    pCreateContext->ContextInfo.DmaBufferSize = 64 * 1024;
    pCreateContext->ContextInfo.DmaBufferSegmentSet = 1;
    pCreateContext->ContextInfo.AllocationListSize = 64;
    pCreateContext->ContextInfo.PatchLocationListSize = 64;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    pCreateContext->ContextInfo.Caps.NoPatchingRequired = 1;
    pCreateContext->ContextInfo.PagingCompanionNodeId = 0;
#endif

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Bc250DdiDestroyContext(
    _In_ CONST HANDLE hContext
    )
{
    return Bc250DdiDestroyDevice(hContext);
}

/*===========================================================================
  DxgkDdiCreateAllocation
  Allocates GPU-accessible memory for textures, render targets, etc.
===========================================================================*/

NTSTATUS
APIENTRY
Bc250DdiCreateAllocation(
    _In_    CONST HANDLE                    hAdapter,
    _Inout_ DXGKARG_CREATEALLOCATION        *pCreateAllocation
    )
{
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)hAdapter;
    ULONG i;
    ULONG j;
    KIRQL OldIrql;

    if (DevExt == NULL || pCreateAllocation == NULL ||
        pCreateAllocation->pAllocationInfo == NULL ||
        pCreateAllocation->NumAllocations == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    KdPrint(("AMDBC250: DxgkDdiCreateAllocation - %d allocations\n",
             pCreateAllocation->NumAllocations));

    for (i = 0; i < pCreateAllocation->NumAllocations; i++) {
        DXGK_ALLOCATIONINFO *pAllocInfo = &pCreateAllocation->pAllocationInfo[i];
        PAMDBC250_ALLOCATION Alloc;

        Alloc = (PAMDBC250_ALLOCATION)ExAllocatePool2(
            POOL_FLAG_NON_PAGED,
            sizeof(AMDBC250_ALLOCATION),
            AMDBC250_TAG_ALLOCATION
            );

        if (Alloc == NULL) {
            for (j = 0; j < i; j++) {
                PAMDBC250_ALLOCATION RollbackAlloc;
                RollbackAlloc = (PAMDBC250_ALLOCATION)pCreateAllocation->pAllocationInfo[j].hAllocation;
                if (RollbackAlloc != NULL) {
                    KeAcquireSpinLock(&DevExt->AllocationListLock, &OldIrql);
                    if (RollbackAlloc->ListEntry.Flink != NULL && RollbackAlloc->ListEntry.Blink != NULL) {
                        RemoveEntryList(&RollbackAlloc->ListEntry);
                        RollbackAlloc->ListEntry.Flink = NULL;
                        RollbackAlloc->ListEntry.Blink = NULL;
                    }
                    if (RollbackAlloc->AllocationType == AllocTypeFrameBuffer &&
                        DevExt->UsedVramBytes >= RollbackAlloc->SizeInBytes) {
                        DevExt->UsedVramBytes -= RollbackAlloc->SizeInBytes;
                    }
                    KeReleaseSpinLock(&DevExt->AllocationListLock, OldIrql);
                    ExFreePoolWithTag(RollbackAlloc, AMDBC250_TAG_ALLOCATION);
                    pCreateAllocation->pAllocationInfo[j].hAllocation = NULL;
                }
            }
            return STATUS_NO_MEMORY;
        }

        RtlZeroMemory(Alloc, sizeof(AMDBC250_ALLOCATION));
        Alloc->SizeInBytes = (pAllocInfo->Size == 0) ? PAGE_SIZE : pAllocInfo->Size;
        Alloc->Alignment   = 4096;  /* 4 KB default alignment */
        InitializeListHead(&Alloc->ListEntry);

        /* Place in VRAM segment (segment 0) by default */
        pAllocInfo->Alignment         = 4096;
        pAllocInfo->Size              = (ULONG)Alloc->SizeInBytes;
        pAllocInfo->PitchAlignedSize  = (ULONG)Alloc->SizeInBytes;
        pAllocInfo->HintedBank.Value  = 0;
        pAllocInfo->PreferredSegment.Value = 0;
        pAllocInfo->SupportedReadSegmentSet  = 1;  /* Segment 0 */
        pAllocInfo->SupportedWriteSegmentSet = 1;  /* Segment 0 */
        pAllocInfo->EvictionSegmentSet       = 2;  /* Segment 1 (system) */
        pAllocInfo->MaximumRenamingListLength = 0;
        pAllocInfo->hAllocation       = (HANDLE)Alloc;
        pAllocInfo->Flags.Value       = 0;
        pAllocInfo->pAllocationUsageHint = NULL;

        KeAcquireSpinLock(&DevExt->AllocationListLock, &OldIrql);
        InsertTailList(&DevExt->AllocationList, &Alloc->ListEntry);
        if (DevExt->UsedVramBytes < DevExt->TotalVramBytes &&
            Alloc->SizeInBytes <= (DevExt->TotalVramBytes - DevExt->UsedVramBytes)) {
            Alloc->AllocationType = AllocTypeFrameBuffer;
            DevExt->UsedVramBytes += Alloc->SizeInBytes;
        } else {
            Alloc->AllocationType = AllocTypeSystemMemory;
            pAllocInfo->PreferredSegment.Value = 1;
            pAllocInfo->SupportedReadSegmentSet  = 2;
            pAllocInfo->SupportedWriteSegmentSet = 2;
            pAllocInfo->EvictionSegmentSet       = 1;
        }
        KeReleaseSpinLock(&DevExt->AllocationListLock, OldIrql);
    }

    return STATUS_SUCCESS;
}

/*===========================================================================
  DxgkDdiDestroyAllocation
  Frees GPU memory allocations.
===========================================================================*/

NTSTATUS
APIENTRY
Bc250DdiDestroyAllocation(
    _In_ CONST HANDLE                   hAdapter,
    _In_ CONST DXGKARG_DESTROYALLOCATION *pDestroyAllocation
    )
{
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)hAdapter;
    ULONG i;
    KIRQL OldIrql;

    if (DevExt == NULL || pDestroyAllocation == NULL || pDestroyAllocation->pAllocationList == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    for (i = 0; i < pDestroyAllocation->NumAllocations; i++) {
        PAMDBC250_ALLOCATION Alloc =
            (PAMDBC250_ALLOCATION)pDestroyAllocation->pAllocationList[i];

        if (Alloc != NULL) {
            KeAcquireSpinLock(&DevExt->AllocationListLock, &OldIrql);
            if (Alloc->ListEntry.Flink != NULL && Alloc->ListEntry.Blink != NULL) {
                RemoveEntryList(&Alloc->ListEntry);
                Alloc->ListEntry.Flink = NULL;
                Alloc->ListEntry.Blink = NULL;
            }
            if (Alloc->AllocationType == AllocTypeFrameBuffer &&
                DevExt->UsedVramBytes >= Alloc->SizeInBytes) {
                DevExt->UsedVramBytes -= Alloc->SizeInBytes;
            }
            KeReleaseSpinLock(&DevExt->AllocationListLock, OldIrql);

            ExFreePoolWithTag(Alloc, AMDBC250_TAG_ALLOCATION);
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Bc250DdiOpenAllocation(
    _In_ CONST HANDLE                 hDevice,
    _In_ CONST DXGKARG_OPENALLOCATION *pOpenAllocation
    )
{
    UINT i;

    UNREFERENCED_PARAMETER(hDevice);

    if (pOpenAllocation == NULL || pOpenAllocation->pOpenAllocation == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    for (i = 0; i < pOpenAllocation->NumAllocations; i++) {
        DXGK_OPENALLOCATIONINFO *Info = &pOpenAllocation->pOpenAllocation[i];
        Info->hDeviceSpecificAllocation =
            (Info->hAllocation != 0) ? (HANDLE)(ULONG_PTR)Info->hAllocation : Info->pPrivateDriverData;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Bc250DdiCloseAllocation(
    _In_ CONST HANDLE                  hDevice,
    _In_ CONST DXGKARG_CLOSEALLOCATION *pCloseAllocation
    )
{
    UNREFERENCED_PARAMETER(hDevice);
    UNREFERENCED_PARAMETER(pCloseAllocation);
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Bc250DdiPatch(
    _In_ CONST HANDLE         hAdapter,
    _In_ CONST DXGKARG_PATCH *pPatch
    )
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pPatch);
    return STATUS_SUCCESS;
}

/*===========================================================================
  DxgkDdiSubmitCommand
  Submits a command buffer (DMA buffer) to the GPU ring for execution.
  This is the core path for all GPU rendering and compute work.
===========================================================================*/

NTSTATUS
APIENTRY
Bc250DdiSubmitCommand(
    _In_ CONST HANDLE               hAdapter,
    _In_ CONST DXGKARG_SUBMITCOMMAND *pSubmitCommand
    )
{
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)hAdapter;

    if (DevExt == NULL || pSubmitCommand == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * Display-only WDDM adapter: GPU command processing is disabled (SPI_PG
     * locked). Simply record the submitted fence value and immediately signal
     * completion via the fence notification mechanism. dxgkrnl polls
     * QueryCurrentFence and will TDR if we don't advance.
     */
    DevExt->GlobalFence.LastSubmittedValue = (ULONG)pSubmitCommand->SubmissionFenceId;

    /* Immediately signal fence completion — no GPU needed */
    if (DevExt->GlobalFence.VirtualAddress != NULL) {
        *DevExt->GlobalFence.VirtualAddress = (ULONG)pSubmitCommand->SubmissionFenceId;
    }
    DevExt->GlobalFence.LastSignaledValue = (ULONG)pSubmitCommand->SubmissionFenceId;

    KdPrint(("AMDBC250: SubmitCommand - fence=%llu (display-only, signaled immediately)\n",
             pSubmitCommand->SubmissionFenceId));

    return STATUS_SUCCESS;
}

/*===========================================================================
  DxgkDdiPreemptCommand
  Requests preemption of the currently executing command buffer.
===========================================================================*/

NTSTATUS
APIENTRY
Bc250DdiPreemptCommand(
    _In_ CONST HANDLE               hAdapter,
    _In_ CONST DXGKARG_PREEMPTCOMMAND *pPreemptCommand
    )
{
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)hAdapter;

    UNREFERENCED_PARAMETER(pPreemptCommand);

    if (DevExt == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    /* Display-only: no GPU commands to preempt */
    return STATUS_SUCCESS;
}

/*===========================================================================
  DxgkDdiQueryCurrentFence
  Returns the current GPU fence value (progress indicator).
===========================================================================*/

NTSTATUS
APIENTRY
Bc250DdiQueryCurrentFence(
    _In_    CONST HANDLE                hAdapter,
    _Inout_ DXGKARG_QUERYCURRENTFENCE   *pCurrentFence
    )
{
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)hAdapter;

    if (DevExt == NULL || pCurrentFence == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * Display-only adapter: no GPU fence — return the last submitted value
     * so dxgkrnl thinks the fence is always immediately complete.
     */
    pCurrentFence->CurrentFence = DevExt->GlobalFence.LastSubmittedValue;

    return STATUS_SUCCESS;
}

/*===========================================================================
  DxgkDdiBuildPagingBuffer
  Builds DMA packets for memory paging operations (eviction/restore).
===========================================================================*/

NTSTATUS
APIENTRY
Bc250DdiBuildPagingBuffer(
    _In_    CONST HANDLE                hAdapter,
    _Inout_ DXGKARG_BUILDPAGINGBUFFER   *pBuildPagingBuffer
    )
{
    UNREFERENCED_PARAMETER(hAdapter);

    if (pBuildPagingBuffer == NULL || pBuildPagingBuffer->pDmaBuffer == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    switch (pBuildPagingBuffer->Operation) {

    case DXGK_OPERATION_TRANSFER: {
        /*
         * Build a DMA copy command to transfer allocation between
         * VRAM and system memory during eviction/restore.
         * Uses SDMA engine for efficient memory transfers.
         */
        PULONG DmaBuffer = (PULONG)pBuildPagingBuffer->pDmaBuffer;
        ULONG DmaOffset = 0;

        if (pBuildPagingBuffer->Transfer.TransferSize == 0) {
            return STATUS_INVALID_PARAMETER;
        }

        /* SDMA COPY LINEAR packet */
        DmaBuffer[DmaOffset++] = (SDMA_OP_COPY << 8) | 0x00000000;
        DmaBuffer[DmaOffset++] = (ULONG)pBuildPagingBuffer->Transfer.TransferSize - 1;
        DmaBuffer[DmaOffset++] = 0;
        DmaBuffer[DmaOffset++] = pBuildPagingBuffer->Transfer.Source.SegmentAddress.LowPart;
        DmaBuffer[DmaOffset++] = pBuildPagingBuffer->Transfer.Source.SegmentAddress.HighPart;
        DmaBuffer[DmaOffset++] = pBuildPagingBuffer->Transfer.Destination.SegmentAddress.LowPart;
        DmaBuffer[DmaOffset++] = pBuildPagingBuffer->Transfer.Destination.SegmentAddress.HighPart;

        pBuildPagingBuffer->pDmaBuffer = (PVOID)((PUCHAR)pBuildPagingBuffer->pDmaBuffer +
                                                  DmaOffset * sizeof(ULONG));
        break;
    }

    case DXGK_OPERATION_FILL: {
        /* Fill memory region with a constant value */
        PULONG DmaBuffer = (PULONG)pBuildPagingBuffer->pDmaBuffer;
        ULONG DmaOffset = 0;

        if (pBuildPagingBuffer->Fill.FillSize == 0) {
            return STATUS_INVALID_PARAMETER;
        }

        DmaBuffer[DmaOffset++] = (SDMA_OP_WRITE << 8) | 0x00000000;
        DmaBuffer[DmaOffset++] = pBuildPagingBuffer->Fill.Destination.SegmentAddress.LowPart;
        DmaBuffer[DmaOffset++] = pBuildPagingBuffer->Fill.Destination.SegmentAddress.HighPart;
        DmaBuffer[DmaOffset++] = (ULONG)pBuildPagingBuffer->Fill.FillSize - 1;
        DmaBuffer[DmaOffset++] = pBuildPagingBuffer->Fill.FillPattern;

        pBuildPagingBuffer->pDmaBuffer = (PVOID)((PUCHAR)pBuildPagingBuffer->pDmaBuffer +
                                                  DmaOffset * sizeof(ULONG));
        break;
    }

    default:
        break;
    }

    return STATUS_SUCCESS;
}

/*===========================================================================
  DxgkDdiQueryChildRelations
  Reports the display outputs (children) of this GPU adapter.
  The BC-250 has one DisplayPort output.
===========================================================================*/

NTSTATUS
APIENTRY
Bc250DdiQueryChildRelations(
    _In_    PVOID                   MiniportDeviceContext,
    _Inout_ PDXGK_CHILD_DESCRIPTOR  ChildRelations,
    _In_    ULONG                   ChildRelationsSize
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);

    if (ChildRelations == NULL ||
        ChildRelationsSize < sizeof(DXGK_CHILD_DESCRIPTOR)) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    RtlZeroMemory(ChildRelations, ChildRelationsSize);
    if (AMDBC250_HEADLESS_RENDER_ONLY) {
        KdPrint(("AMDBC250: QueryChildRelations - headless render-only mode\n"));
        return STATUS_SUCCESS;
    }
    if (AMDBC250_H_CHILDLESS_MODE) {
        KdPrint(("AMDBC250: QueryChildRelations - childless mode\n"));
        return STATUS_SUCCESS;
    }

    /*
     * Mirror the proven-working KMDOD (bdd.cxx QueryChildRelations): use
     * HpdAwarenessInterruptible (NOT AlwaysConnected) and InterfaceTechnology
     * VOT_OTHER. With AlwaysConnected dxgkrnl believes the child is always
     * attached and never queries child status, which starves the VidPn build
     * (no QueryChildStatus -> no functional VidPn -> hang before login).
     */
    ChildRelations[0].ChildDeviceType = TypeVideoOutput;
    ChildRelations[0].ChildCapabilities.Type.VideoOutput.InterfaceTechnology =
        D3DKMDT_VOT_OTHER;
    ChildRelations[0].ChildCapabilities.Type.VideoOutput.MonitorOrientationAwareness =
        D3DKMDT_MOA_NONE;
    ChildRelations[0].ChildCapabilities.Type.VideoOutput.SupportsSdtvModes = FALSE;
    ChildRelations[0].ChildCapabilities.HpdAwareness = HpdAwarenessInterruptible;
    ChildRelations[0].AcpiUid = 0;
    ChildRelations[0].ChildUid = 0;

    KdPrint(("AMDBC250: QueryChildRelations - 1 DisplayPort output reported\n"));
    return STATUS_SUCCESS;
}

/*===========================================================================
  DxgkDdiQueryChildStatus
  Reports whether a child device (monitor) is connected.
===========================================================================*/

NTSTATUS
APIENTRY
Bc250DdiQueryChildStatus(
    _In_    PVOID               MiniportDeviceContext,
    _Inout_ PDXGK_CHILD_STATUS  ChildStatus,
    _In_    BOOLEAN             NonDestructiveOnly
    )
{
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)MiniportDeviceContext;
    UNREFERENCED_PARAMETER(NonDestructiveOnly);

    if (ChildStatus == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (AMDBC250_HEADLESS_RENDER_ONLY) {
        return STATUS_NOT_SUPPORTED;
    }

    if (ChildStatus->Type == StatusConnection) {
        /*
         * Gate Connected on the POST framebuffer being acquired and active,
         * exactly like the proven-working KMDOD sample (bdd.cxx
         * QueryChildStatus: Connected = IsDriverActive() && FrameBufferIsActive).
         * Reporting a monitor as connected before we have a framebuffer to
         * present into makes dxgkrnl skip the VidPn/present path.
         */
        ChildStatus->HotPlug.Connected =
            (DevExt != NULL && DevExt->FbIsActive) ? TRUE : FALSE;
        Bc250DiagWriteDword(L"DiagQcsType", (ULONG)ChildStatus->Type);
        Bc250DiagWriteDword(L"DiagQcsConnected", ChildStatus->HotPlug.Connected ? 1 : 0);
        Bc250DiagWriteDword(L"DiagQcsChildUid", (ULONG)ChildStatus->ChildUid);
    }
    Bc250DiagWriteDword(L"DiagQcsAny", 1);

    return STATUS_SUCCESS;
}

/*===========================================================================
  DxgkDdiQueryDeviceDescriptor
  Returns EDID or other device descriptor for a child device.
===========================================================================*/

NTSTATUS
APIENTRY
Bc250DdiQueryDeviceDescriptor(
    _In_    PVOID                       MiniportDeviceContext,
    _In_    ULONG                       ChildUid,
    _Inout_ PDXGK_DEVICE_DESCRIPTOR     DeviceDescriptor
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);

    if (DeviceDescriptor == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (AMDBC250_HEADLESS_RENDER_ONLY) {
        DeviceDescriptor->DescriptorLength = 0;
        return STATUS_GRAPHICS_CHILD_DESCRIPTOR_NOT_SUPPORTED;
    }
    if (ChildUid != 0) {
        return STATUS_INVALID_PARAMETER;
    }

    DeviceDescriptor->DescriptorLength = 0;
    return STATUS_GRAPHICS_CHILD_DESCRIPTOR_NOT_SUPPORTED;
}

/*===========================================================================
  DxgkDdiSetPowerState
  Handles power state transitions (D0=active, D1/D2/D3=sleep/hibernate).
===========================================================================*/

NTSTATUS
APIENTRY
Bc250DdiSetPowerState(
    _In_ PVOID              MiniportDeviceContext,
    _In_ ULONG              DeviceUid,
    _In_ DEVICE_POWER_STATE DevicePowerState,
    _In_ POWER_ACTION       ActionType
    )
{
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)MiniportDeviceContext;

    UNREFERENCED_PARAMETER(DeviceUid);
    UNREFERENCED_PARAMETER(ActionType);

    if (DevExt == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    KdPrint(("AMDBC250: SetPowerState - state=%d\n", DevicePowerState));

    DevExt->PowerState = (ULONG)DevicePowerState;

    switch (DevicePowerState) {
    case PowerDeviceD0:
        /* Full power - resume GPU if needed */
        if (DevExt->HardwareInitialized && DevExt->MmioVirtualBase != NULL) {
            /* Re-enable clocks, restore GPU state */
            Bc250WriteMmio(DevExt, AMDBC250_REG_MP1_SMN_C2PMSG_66, 0x00000001);
        }
        break;

    case PowerDeviceD3:
        /* Lowest power state - save GPU state, power down */
        if (DevExt->HardwareInitialized && DevExt->MmioVirtualBase != NULL) {
            Bc250WriteMmio(DevExt, AMDBC250_REG_MP1_SMN_C2PMSG_66, 0x00000000);
        }
        break;

    default:
        break;
    }

    return STATUS_SUCCESS;
}

/*===========================================================================
  DxgkDdiNotifyAcpiEvent
  Handles ACPI events (lid close/open, AC/battery transitions, etc.)
===========================================================================*/

NTSTATUS
APIENTRY
Bc250DdiNotifyAcpiEvent(
    _In_  PVOID             MiniportDeviceContext,
    _In_  DXGK_EVENT_TYPE   EventType,
    _In_  ULONG             EventCode,
    _In_  PVOID             Argument,
    _Out_ PULONG            AcpiFlags
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(EventType);
    UNREFERENCED_PARAMETER(EventCode);
    UNREFERENCED_PARAMETER(Argument);

    if (AcpiFlags == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    *AcpiFlags = 0;
    return STATUS_SUCCESS;
}

/*===========================================================================
  DxgkDdiQueryInterface
  Returns interface pointers for optional driver capabilities.
===========================================================================*/

NTSTATUS
APIENTRY
Bc250DdiQueryInterface(
    _In_ PVOID              MiniportDeviceContext,
    _In_ PQUERY_INTERFACE   QueryInterface
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);

    if (QueryInterface == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    return STATUS_NOT_SUPPORTED;
}

/*===========================================================================
  Display DDI Stubs
  Full VidPN management implementation (mode setting, scan-out, etc.)
===========================================================================*/

static VOID
Bc250FillSafeSignalInfo(
    _Out_ D3DKMDT_VIDEO_SIGNAL_INFO *SignalInfo,
    _In_  PAMDBC250_DEVICE_EXTENSION DevExt
    )
{
    RtlZeroMemory(SignalInfo, sizeof(*SignalInfo));
    SignalInfo->VideoStandard = D3DKMDT_VSS_OTHER;
    SignalInfo->TotalSize.cx = DevExt->FbWidth  ? DevExt->FbWidth  : AMDBC250_SAFE_MODE_WIDTH;
    SignalInfo->TotalSize.cy = DevExt->FbHeight ? DevExt->FbHeight : AMDBC250_SAFE_MODE_HEIGHT;
    SignalInfo->ActiveSize.cx = SignalInfo->TotalSize.cx;
    SignalInfo->ActiveSize.cy = SignalInfo->TotalSize.cy;
    SignalInfo->VSyncFreq.Numerator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    SignalInfo->VSyncFreq.Denominator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    SignalInfo->HSyncFreq.Numerator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    SignalInfo->HSyncFreq.Denominator = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    SignalInfo->PixelRate = D3DKMDT_FREQUENCY_NOTSPECIFIED;
    SignalInfo->ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE;
}

static NTSTATUS
Bc250EnsureSourceModeSet(
    _In_ D3DKMDT_HVIDPN VidPn,
    _In_ const DXGK_VIDPN_INTERFACE *VidPnInterface,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId,
    _In_ PAMDBC250_DEVICE_EXTENSION DevExt
    )
{
    D3DKMDT_HVIDPNSOURCEMODESET ModeSet;
    const DXGK_VIDPNSOURCEMODESET_INTERFACE *ModeSetInterface;
    D3DKMDT_VIDPN_SOURCE_MODE *ModeInfo;
    NTSTATUS Status;

    /* Mirrors KMDOD AddSingleSourceMode: advertise all predefined modes that
     * fit within the POST framebuffer so dxgkrnl always has a boot-safe
     * resolution available (a single 2560x1440-only set stalls the initial
     * mode pick during boot). */
    static const struct { ULONG Width; ULONG Height; } Modes[] = {
        { 640, 480 },   { 800, 600 },   { 1024, 768 },
        { 1152, 864 },  { 1280, 720 },  { 1280, 800 },
        { 1280, 1024 }, { 1366, 768 },  { 1400, 1050 },
        { 1600, 1200 }, { 1680, 1050 }, { 1920, 1080 },
        { 1920, 1200 }, { 2560, 1440 }
    };
    ULONG WidthMax;
    ULONG HeightMax;
    ULONG i;

    Status = VidPnInterface->pfnAcquireSourceModeSet(
        VidPn,
        SourceId,
        &ModeSet,
        &ModeSetInterface);
    if (NT_SUCCESS(Status)) {
        /*
         * Mirrors KMDOD EnumVidPnCofuncModality: if a source mode set already
         * exists but has no pinned mode, we must release it and create a new
         * one populated with all supported modes. Returning early with the
         * existing (possibly empty) mode set starves dxgkrnl -- it has no
         * modes to pin for CommitVidPn and never proceeds to the login screen.
         */
        const D3DKMDT_VIDPN_SOURCE_MODE *PinnedMode = NULL;
        NTSTATUS PinStatus = ModeSetInterface->pfnAcquirePinnedModeInfo(
            ModeSet,
            &PinnedMode);
        if (NT_SUCCESS(PinStatus) && PinnedMode != NULL) {
            (VOID)ModeSetInterface->pfnReleaseModeInfo(ModeSet, PinnedMode);
            (VOID)VidPnInterface->pfnReleaseSourceModeSet(VidPn, ModeSet);
            return STATUS_SUCCESS;
        }
        (VOID)VidPnInterface->pfnReleaseSourceModeSet(VidPn, ModeSet);
    }

    Status = VidPnInterface->pfnCreateNewSourceModeSet(
        VidPn,
        SourceId,
        &ModeSet,
        &ModeSetInterface);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    WidthMax = DevExt->FbWidth ? DevExt->FbWidth : AMDBC250_SAFE_MODE_WIDTH;
    HeightMax = DevExt->FbHeight ? DevExt->FbHeight : AMDBC250_SAFE_MODE_HEIGHT;

    for (i = 0; i < ARRAYSIZE(Modes); ++i) {
        if (Modes[i].Width > WidthMax) {
            break;
        }
        if (Modes[i].Width == WidthMax && Modes[i].Height > HeightMax) {
            break;
        }
        if (Modes[i].Width < WidthMax && Modes[i].Height > HeightMax) {
            continue;
        }

        Status = ModeSetInterface->pfnCreateNewModeInfo(ModeSet, &ModeInfo);
        if (!NT_SUCCESS(Status)) {
            (VOID)VidPnInterface->pfnReleaseSourceModeSet(VidPn, ModeSet);
            return Status;
        }

        /*
         * Do NOT RtlZeroMemory here. pfnCreateNewModeInfo hands back a mode
         * whose Id is already assigned by dxgkrnl; zeroing it makes every
         * added mode share Id=0 and pfnAddMode fails with
         * STATUS_GRAPHICS_MODE_ID_MUST_BE_UNIQUE (0xC01E0324). KMDOD's
         * AddSingleSourceMode overwrites only the fields it needs and leaves
         * Id untouched (bdd_dmm.cxx:857-877).
         */
        ModeInfo->Type = D3DKMDT_RMT_GRAPHICS;
        ModeInfo->Format.Graphics.PrimSurfSize.cx = Modes[i].Width;
        ModeInfo->Format.Graphics.PrimSurfSize.cy = Modes[i].Height;
        ModeInfo->Format.Graphics.VisibleRegionSize.cx = Modes[i].Width;
        ModeInfo->Format.Graphics.VisibleRegionSize.cy = Modes[i].Height;
        ModeInfo->Format.Graphics.Stride = 4 * Modes[i].Width;
        ModeInfo->Format.Graphics.PixelFormat = D3DDDIFMT_A8R8G8B8;
        ModeInfo->Format.Graphics.ColorBasis = D3DKMDT_CB_SCRGB;
        ModeInfo->Format.Graphics.PixelValueAccessMode = D3DKMDT_PVAM_DIRECT;

        Status = ModeSetInterface->pfnAddMode(ModeSet, ModeInfo);
        if (!NT_SUCCESS(Status)) {
            (VOID)ModeSetInterface->pfnReleaseModeInfo(ModeSet, ModeInfo);
            (VOID)VidPnInterface->pfnReleaseSourceModeSet(VidPn, ModeSet);
            return Status;
        }
    }

    Status = VidPnInterface->pfnAssignSourceModeSet(VidPn, SourceId, ModeSet);
    if (!NT_SUCCESS(Status)) {
        (VOID)VidPnInterface->pfnReleaseSourceModeSet(VidPn, ModeSet);
        return Status;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS
Bc250EnsureTargetModeSet(
    _In_ D3DKMDT_HVIDPN VidPn,
    _In_ const DXGK_VIDPN_INTERFACE *VidPnInterface,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _In_ PAMDBC250_DEVICE_EXTENSION DevExt
    )
{
    D3DKMDT_HVIDPNTARGETMODESET ModeSet;
    const DXGK_VIDPNTARGETMODESET_INTERFACE *ModeSetInterface;
    D3DKMDT_VIDPN_TARGET_MODE *ModeInfo;
    NTSTATUS Status;

    Status = VidPnInterface->pfnAcquireTargetModeSet(
        VidPn,
        TargetId,
        &ModeSet,
        &ModeSetInterface);
    if (NT_SUCCESS(Status)) {
        /* Same pinned-mode logic as the source mode set (KMDOD behavior). */
        const D3DKMDT_VIDPN_TARGET_MODE *PinnedMode = NULL;
        NTSTATUS PinStatus = ModeSetInterface->pfnAcquirePinnedModeInfo(
            ModeSet,
            &PinnedMode);
        if (NT_SUCCESS(PinStatus) && PinnedMode != NULL) {
            (VOID)ModeSetInterface->pfnReleaseModeInfo(ModeSet, PinnedMode);
            (VOID)VidPnInterface->pfnReleaseTargetModeSet(VidPn, ModeSet);
            return STATUS_SUCCESS;
        }
        (VOID)VidPnInterface->pfnReleaseTargetModeSet(VidPn, ModeSet);
    }

    Status = VidPnInterface->pfnCreateNewTargetModeSet(
        VidPn,
        TargetId,
        &ModeSet,
        &ModeSetInterface);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    Status = ModeSetInterface->pfnCreateNewModeInfo(ModeSet, &ModeInfo);
    if (!NT_SUCCESS(Status)) {
        (VOID)VidPnInterface->pfnReleaseTargetModeSet(VidPn, ModeSet);
        return Status;
    }

    RtlZeroMemory(ModeInfo, sizeof(*ModeInfo));
    Bc250FillSafeSignalInfo(&ModeInfo->VideoSignalInfo, DevExt);
    ModeInfo->Preference = D3DKMDT_MP_PREFERRED;

    Status = ModeSetInterface->pfnAddMode(ModeSet, ModeInfo);
    if (!NT_SUCCESS(Status)) {
        (VOID)ModeSetInterface->pfnReleaseModeInfo(ModeSet, ModeInfo);
        (VOID)VidPnInterface->pfnReleaseTargetModeSet(VidPn, ModeSet);
        return Status;
    }

    Status = VidPnInterface->pfnAssignTargetModeSet(VidPn, TargetId, ModeSet);
    if (!NT_SUCCESS(Status)) {
        (VOID)VidPnInterface->pfnReleaseTargetModeSet(VidPn, ModeSet);
        return Status;
    }

    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DdiSetVidPnSourceAddress(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_SETVIDPNSOURCEADDRESS *pSetVidPnSourceAddress)
{
    Bc250MarkCallbackEnter(BC250_CB_SETVIDPNSOURCEADDR);
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)hAdapter;
    if (DevExt == NULL || pSetVidPnSourceAddress == NULL) {
        Bc250MarkCallbackExit(BC250_CB_SETVIDPNSOURCEADDR, STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }
    /*
     * Compatibility mode: accept scan-out programming requests even when
     * MMIO resources are intentionally not mapped yet.
     */
    KdPrint(("AMDBC250: SetVidPnSourceAddress src=%u seg=%u addr=0x%08X%08X\n",
             (ULONG)pSetVidPnSourceAddress->VidPnSourceId,
             (ULONG)pSetVidPnSourceAddress->PrimarySegment,
             (ULONG)pSetVidPnSourceAddress->PrimaryAddress.HighPart,
             (ULONG)pSetVidPnSourceAddress->PrimaryAddress.LowPart));

#if (AMDBC250_H_SETSOURCEADDR_MODE == 0)
    Bc250MarkCallbackExit(BC250_CB_SETVIDPNSOURCEADDR, STATUS_NOT_SUPPORTED);
    return STATUS_NOT_SUPPORTED;
#else
    Bc250MarkCallbackExit(BC250_CB_SETVIDPNSOURCEADDR, STATUS_SUCCESS);
    return STATUS_SUCCESS;
#endif
}

NTSTATUS APIENTRY Bc250DdiRecommendFunctionalVidPn(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN *pRecommendFunctionalVidPn)
{
    Bc250MarkCallbackEnter(BC250_CB_RECOMMENDFUNCVIDPN);
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)hAdapter;

    if (DevExt == NULL || pRecommendFunctionalVidPn == NULL) {
        Bc250MarkCallbackExit(BC250_CB_RECOMMENDFUNCVIDPN, STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * Mirror the proven-working KMDOD (bdd_dmm.cxx RecommendFunctionalVidPn):
     * a DOD cannot recommend a functional VidPn on its own; dxgkrnl builds the
     * topology from our source/target mode sets and calls CommitVidPn. Returning
     * STATUS_GRAPHICS_NO_RECOMMENDED_FUNCTIONAL_VIDPN here is the documented,
     * expected behaviour for a display-only driver. Building our own mode sets
     * here and returning STATUS_SUCCESS caused dxgkrnl to wait on a functional
     * VidPn it never got, hanging before the login screen.
     */
    Bc250MarkCallbackExit(BC250_CB_RECOMMENDFUNCVIDPN, STATUS_GRAPHICS_NO_RECOMMENDED_FUNCTIONAL_VIDPN);
    return STATUS_GRAPHICS_NO_RECOMMENDED_FUNCTIONAL_VIDPN;
}

NTSTATUS APIENTRY Bc250DdiEnumVidPnCofuncModality(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY *pEnumCofuncModality)
{
    Bc250MarkCallbackEnter(BC250_CB_ENUMVIDPN);
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)hAdapter;
    const DXGK_VIDPN_INTERFACE *VidPnInterface;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId = 0;
    D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId = 0;
    NTSTATUS Status;

    if (DevExt == NULL || pEnumCofuncModality == NULL) {
        Bc250MarkCallbackExit(BC250_CB_ENUMVIDPN, STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }
    if (AMDBC250_HEADLESS_RENDER_ONLY || pEnumCofuncModality->hConstrainingVidPn == 0) {
        Bc250MarkCallbackExit(BC250_CB_ENUMVIDPN, STATUS_SUCCESS);
        return STATUS_SUCCESS;
    }

    if (pEnumCofuncModality->EnumPivotType == D3DKMDT_EPT_VIDPNSOURCE) {
        SourceId = pEnumCofuncModality->EnumPivot.VidPnSourceId;
    } else if (pEnumCofuncModality->EnumPivotType == D3DKMDT_EPT_VIDPNTARGET) {
        TargetId = pEnumCofuncModality->EnumPivot.VidPnTargetId;
    }

    Status = DevExt->DxgkInterface.DxgkCbQueryVidPnInterface(
        pEnumCofuncModality->hConstrainingVidPn,
        DXGK_VIDPN_INTERFACE_VERSION_V1,
        &VidPnInterface);
    if (!NT_SUCCESS(Status)) {
        Bc250MarkCallbackExit(BC250_CB_ENUMVIDPN, Status);
        return Status;
    }

    Status = Bc250EnsureSourceModeSet(pEnumCofuncModality->hConstrainingVidPn, VidPnInterface, SourceId, DevExt);
    if (!NT_SUCCESS(Status)) {
        Bc250MarkCallbackExit(BC250_CB_ENUMVIDPN, Status);
        return Status;
    }

    Status = Bc250EnsureTargetModeSet(pEnumCofuncModality->hConstrainingVidPn, VidPnInterface, TargetId, DevExt);
    if (!NT_SUCCESS(Status)) {
        Bc250MarkCallbackExit(BC250_CB_ENUMVIDPN, Status);
        return Status;
    }

    KdPrint(("AMDBC250: EnumVidPnCofuncModality populated src=%u tgt=%u\n", (ULONG)SourceId, (ULONG)TargetId));
    Bc250MarkCallbackExit(BC250_CB_ENUMVIDPN, STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DdiSetVidPnSourceVisibility(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY *pSetVidPnSourceVisibility)
{
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)hAdapter;
    if (hAdapter == NULL || pSetVidPnSourceVisibility == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    DevExt->SourceIsVisible = pSetVidPnSourceVisibility->Visible ? TRUE : FALSE;
    Bc250DiagSnapshot(90);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DdiCommitVidPn(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_COMMITVIDPN *pCommitVidPn)
{
    Bc250MarkCallbackEnter(BC250_CB_COMMITVIDPN);
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)hAdapter;
    if (DevExt == NULL || pCommitVidPn == NULL) {
        Bc250MarkCallbackExit(BC250_CB_COMMITVIDPN, STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }
    InterlockedExchange(&g_Bc250LastCommitSourceId, (LONG)pCommitVidPn->AffectedVidPnSourceId);
    InterlockedExchange(&g_Bc250LastCommitFlags, (LONG)(pCommitVidPn->Flags.PathPowerTransition | (pCommitVidPn->Flags.PathPoweredOff << 1)));
    InterlockedExchange(&g_Bc250LastCommitConnectivityChecks, (LONG)pCommitVidPn->MonitorConnectivityChecks);
    Bc250DiagWriteDword(L"DiagCommitEntered", 1);
    Bc250DiagWriteDword(L"DiagCommitSource", (ULONG)pCommitVidPn->AffectedVidPnSourceId);
    Bc250DiagWriteDword(L"DiagCommitChecks", (ULONG)pCommitVidPn->MonitorConnectivityChecks);

    /*
     * Lazily map the POST framebuffer now that dxgkrnl is committing a real
     * VidPn. Mirrors the KMDOD SetSourceModeAndPath()/MapFrameBuffer() which
     * maps the acquired DispInfo.PhysicAddress before/by present time. Without
     * this, PresentDisplayOnly/SystemDisplayWrite have nothing to write into.
     */
    if (!DevExt->FbMapped && DevExt->FbPhysicalBase.QuadPart != 0 &&
        DevExt->FbWidth > 0 && DevExt->FbHeight > 0) {
        SIZE_T FbBytes = (SIZE_T)DevExt->FbPitch * (SIZE_T)DevExt->FbHeight;
        if (FbBytes > 0 && FbBytes <= AMDBC250_MAX_FB_MAP_BYTES) {
            DevExt->FbSize = FbBytes;
            DevExt->FbVirtualBase = MmMapIoSpace(DevExt->FbPhysicalBase, FbBytes, MmNonCached);
            if (DevExt->FbVirtualBase != NULL) {
                DevExt->FbMapped = TRUE;
                Bc250DiagWriteDword(L"DiagCommitFbMapped", 1);
                Bc250DiagWriteDword(L"DiagCommitFbBytes", (ULONG)FbBytes);
                KdPrint(("AMDBC250: CommitVidPn mapped FB PA=0x%llX bytes=%llu -> %p\n",
                         DevExt->FbPhysicalBase.QuadPart, (ULONGLONG)FbBytes, DevExt->FbVirtualBase));
            } else {
                Bc250DiagWriteDword(L"DiagCommitFbMapped", 0);
                Bc250DiagWriteDword(L"DiagCommitFbBytes", (ULONG)FbBytes);
            }
        } else {
            Bc250DiagWriteDword(L"DiagCommitFbMapped", 2);
            Bc250DiagWriteDword(L"DiagCommitFbBytes", (ULONG)FbBytes);
        }
    }

    KdPrint(("AMDBC250: CommitVidPn vidpn=0x%p source=%u checks=%u flags(pt=%u,poff=%u) primary=0x%p\n",
             pCommitVidPn->hFunctionalVidPn,
             (ULONG)pCommitVidPn->AffectedVidPnSourceId,
             (ULONG)pCommitVidPn->MonitorConnectivityChecks,
             (ULONG)pCommitVidPn->Flags.PathPowerTransition,
             (ULONG)pCommitVidPn->Flags.PathPoweredOff,
             pCommitVidPn->hPrimaryAllocation));
    Bc250MarkCallbackExit(BC250_CB_COMMITVIDPN, STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DdiUpdateActiveVidPnPresentPath(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH *pUpdateActiveVidPnPresentPath)
{
    Bc250MarkCallbackEnter(BC250_CB_UPDATEACTIVEPATH);
    if (hAdapter == NULL || pUpdateActiveVidPnPresentPath == NULL) {
        KdPrint(("AMDBC250: UpdateActivePath invalid hAdapter=%p p=%p\n",
                 hAdapter,
                 pUpdateActiveVidPnPresentPath));
        Bc250MarkCallbackExit(BC250_CB_UPDATEACTIVEPATH, STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }

#if (AMDBC250_H_UPDATEPATH_MODE == 0)
    Bc250MarkCallbackExit(BC250_CB_UPDATEACTIVEPATH, STATUS_NOT_SUPPORTED);
    return STATUS_NOT_SUPPORTED;
#elif (AMDBC250_H_UPDATEPATH_MODE == 1)
    KdPrint(("AMDBC250: UpdateActivePath NOT_SUPPORTED probe src=%u tgt=%u\n",
             (ULONG)pUpdateActiveVidPnPresentPath->VidPnPresentPathInfo.VidPnSourceId,
             (ULONG)pUpdateActiveVidPnPresentPath->VidPnPresentPathInfo.VidPnTargetId));
    Bc250MarkCallbackExit(BC250_CB_UPDATEACTIVEPATH, STATUS_NOT_SUPPORTED);
    return STATUS_NOT_SUPPORTED;
#elif (AMDBC250_H_UPDATEPATH_MODE == 2)
    {
        const D3DKMDT_VIDPN_PRESENT_PATH *Path =
            &pUpdateActiveVidPnPresentPath->VidPnPresentPathInfo;
        KdPrint(("AMDBC250: UpdateActivePath SUCCESS probe src=%u tgt=%u imp=%u scale=%u rot=%u content=%u color=%u\n",
                 (ULONG)Path->VidPnSourceId,
                 (ULONG)Path->VidPnTargetId,
                 (ULONG)Path->ImportanceOrdinal,
                 (ULONG)Path->ContentTransformation.Scaling,
                 (ULONG)Path->ContentTransformation.Rotation,
                 (ULONG)Path->Content,
                 (ULONG)Path->VidPnTargetColorBasis));
        KdPrint(("AMDBC250: UpdateActivePath offsets TL=(%ld,%ld) BR=(%ld,%ld) cpType=%u\n",
                 (LONG)Path->VisibleFromActiveTLOffset.cx,
                 (LONG)Path->VisibleFromActiveTLOffset.cy,
                 (LONG)Path->VisibleFromActiveBROffset.cx,
                 (LONG)Path->VisibleFromActiveBROffset.cy,
                 (ULONG)Path->CopyProtection.CopyProtectionType));
    }
    Bc250MarkCallbackExit(BC250_CB_UPDATEACTIVEPATH, STATUS_SUCCESS);
    return STATUS_SUCCESS;
#else
    Bc250MarkCallbackExit(BC250_CB_UPDATEACTIVEPATH, STATUS_SUCCESS);
    return STATUS_SUCCESS;
#endif
}

NTSTATUS APIENTRY Bc250DdiRecommendMonitorModes(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_RECOMMENDMONITORMODES *pRecommendMonitorModes)
{
    D3DKMDT_MONITOR_SOURCE_MODE *ModeInfo;
    NTSTATUS Status;

    if (hAdapter == NULL || pRecommendMonitorModes == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    if (pRecommendMonitorModes->VideoPresentTargetId != 0 ||
        pRecommendMonitorModes->pMonitorSourceModeSetInterface == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    Status = pRecommendMonitorModes->pMonitorSourceModeSetInterface->pfnCreateNewModeInfo(
        pRecommendMonitorModes->hMonitorSourceModeSet,
        &ModeInfo);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    RtlZeroMemory(ModeInfo, sizeof(*ModeInfo));
    Bc250FillSafeSignalInfo(&ModeInfo->VideoSignalInfo, (PAMDBC250_DEVICE_EXTENSION)hAdapter);
    ModeInfo->ColorBasis = D3DKMDT_CB_SRGB;
    ModeInfo->ColorCoeffDynamicRanges.FirstChannel = 8;
    ModeInfo->ColorCoeffDynamicRanges.SecondChannel = 8;
    ModeInfo->ColorCoeffDynamicRanges.ThirdChannel = 8;
    ModeInfo->ColorCoeffDynamicRanges.FourthChannel = 8;
    ModeInfo->Origin = D3DKMDT_MCO_DRIVER;
    ModeInfo->Preference = D3DKMDT_MP_PREFERRED;

    Status = pRecommendMonitorModes->pMonitorSourceModeSetInterface->pfnAddMode(
        pRecommendMonitorModes->hMonitorSourceModeSet,
        ModeInfo);
    if (Status == STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET) {
        return STATUS_SUCCESS;
    }
    if (!NT_SUCCESS(Status)) {
        (VOID)pRecommendMonitorModes->pMonitorSourceModeSetInterface->pfnReleaseModeInfo(
            pRecommendMonitorModes->hMonitorSourceModeSet,
            ModeInfo);
        return Status;
    }

    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DdiGetScanLine(
    _In_    CONST HANDLE hAdapter,
    _Inout_ DXGKARG_GETSCANLINE *pGetScanLine)
{
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)hAdapter;
    if (DevExt == NULL || pGetScanLine == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (!DevExt->HardwareInitialized || DevExt->MmioVirtualBase == NULL) {
        pGetScanLine->ScanLine = 0;
        pGetScanLine->InVerticalBlank = FALSE;
        return STATUS_SUCCESS;
    }
    ULONG CrtcStatus = Bc250ReadMmio(DevExt, AMDBC250_REG_CRTC0_STATUS);
    pGetScanLine->ScanLine = CrtcStatus & 0x0000FFFF;
    pGetScanLine->InVerticalBlank = (CrtcStatus & 0x00010000) ? TRUE : FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DdiQueryVidPnHwCapability(
    _In_ CONST HANDLE hAdapter,
    _Inout_ DXGKARG_QUERYVIDPNHWCAPABILITY *pVidPnHWCaps)
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (pVidPnHWCaps == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    RtlZeroMemory(&pVidPnHWCaps->VidPnHWCaps, sizeof(pVidPnHWCaps->VidPnHWCaps));
    pVidPnHWCaps->VidPnHWCaps.DriverRotation      = 1; /* software rotation, like KMDOD */
    pVidPnHWCaps->VidPnHWCaps.DriverScaling       = 0;
    pVidPnHWCaps->VidPnHWCaps.DriverCloning       = 0;
    pVidPnHWCaps->VidPnHWCaps.DriverColorConvert  = 1; /* software color conversion */
    pVidPnHWCaps->VidPnHWCaps.DriverLinkedAdapaterOutput = 0;
    pVidPnHWCaps->VidPnHWCaps.DriverRemoteDisplay = 0;
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DdiControlInterrupt(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGK_INTERRUPT_TYPE InterruptType,
    _In_ BOOLEAN EnableInterrupt)
{
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)hAdapter;
    if (DevExt == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (DevExt->MmioVirtualBase == NULL) {
        UNREFERENCED_PARAMETER(InterruptType);
        UNREFERENCED_PARAMETER(EnableInterrupt);
        return STATUS_SUCCESS;
    }
    ULONG IhCntl = Bc250ReadMmio(DevExt, AMDBC250_REG_IH_CNTL);

    if (EnableInterrupt) {
        IhCntl |= IH_CNTL__ENABLE_INTR_MASK;
    } else {
        IhCntl &= ~IH_CNTL__ENABLE_INTR_MASK;
    }

    Bc250WriteMmio(DevExt, AMDBC250_REG_IH_CNTL, IhCntl);
    UNREFERENCED_PARAMETER(InterruptType);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DdiCreateOverlay(
    _In_    CONST HANDLE hAdapter,
    _Inout_ DXGKARG_CREATEOVERLAY *pCreateOverlay)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pCreateOverlay);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS APIENTRY Bc250DdiDestroyOverlay(
    _In_ CONST HANDLE hOverlay)
{
    UNREFERENCED_PARAMETER(hOverlay);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DdiPresent(
     _In_    CONST HANDLE hContext,
     _Inout_ DXGKARG_PRESENT *pPresent)
{
     PAMDBC250_DEVICE_EXTENSION DevExt = g_Bc250PrimaryDevice;

     if (hContext == NULL || pPresent == NULL) {
         return STATUS_INVALID_PARAMETER;
     }

     if (DevExt == NULL || !DevExt->FbMapped || DevExt->FbVirtualBase == NULL) {
         return STATUS_SUCCESS;
     }

     /*
      * Display-only WDDM adapter: present blits from the source allocation
      * to the POST framebuffer. dxgkrnl passes the source allocation in
      * pAllocationList[0].PhysicalAddress. If the address is the POST FB
      * itself, the BIOS scanout already shows it. If it's a separate
      * allocation, copy it line-by-line.
      */
     if (pPresent->pAllocationList != NULL && pPresent->NumSrcAllocations > 0) {
         PHYSICAL_ADDRESS SrcPA = pPresent->pAllocationList[0].PhysicalAddress;
         PBYTE pDst = (PBYTE)DevExt->FbVirtualBase;
         SIZE_T FbBytes = (SIZE_T)DevExt->FbPitch * (SIZE_T)DevExt->FbHeight;

         if (SrcPA.QuadPart != 0 && DevExt->FbPhysicalBase.QuadPart != 0 &&
             SrcPA.QuadPart != DevExt->FbPhysicalBase.QuadPart) {
             /*
              * Source is a different allocation than the POST FB.
              * Map the source physical memory and copy to POST FB.
              */
              PVOID pSrcMap = MmMapIoSpace(SrcPA, FbBytes, MmNonCached);
              if (pSrcMap != NULL) {
                  RtlCopyMemory(pDst, pSrcMap, FbBytes);
                  MmUnmapIoSpace(pSrcMap, FbBytes);
              }
          }
      }
      return STATUS_SUCCESS;
}

/*===========================================================================
   WDDM 3.2 Required Hardware Queue Callbacks
   dxgkrnl on Win11 26100 requires these for WDDM 3.2 registration.
===========================================================================*/

NTSTATUS
APIENTRY
Bc250DdiCreateHwQueue(
    _In_  IN_CONST_HANDLE               hHwContext,
    _Inout_ INOUT_PDXGKARG_CREATEHWQUEUE  pCreateHwQueue
    )
{
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)hHwContext;
    UNREFERENCED_PARAMETER(pCreateHwQueue);
    KdPrint(("AMDBC250: DxgkDdiCreateHwQueue called\n"));
    pCreateHwQueue->hHwQueue = (HANDLE)0xDEADBEEF;
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Bc250DdiDestroyHwQueue(
    _In_  IN_CONST_HANDLE               hHwQueue
    )
{
    UNREFERENCED_PARAMETER(hHwQueue);
    KdPrint(("AMDBC250: DxgkDdiDestroyHwQueue called\n"));
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Bc250DdiSubmitCommandToHwQueue(
    _In_  PVOID                         MiniportDeviceContext,
    _In_  IN_CONST_PDXGKARG_SUBMITCOMMANDTOHWQUEUE SubmitCommandToHwQueue
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
    _In_  PVOID                         MiniportDeviceContext,
    _In_  IN_CONST_PDXGKARG_SUBMITCOMMANDVIRTUAL  SubmitCommandVirtual
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(SubmitCommandVirtual);
    KdPrint(("AMDBC250: DxgkDdiSubmitCommandVirtual called\n"));
     return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DdiRender(
    _In_    CONST HANDLE hContext,
    _Inout_ DXGKARG_RENDER *pRender)
{
    if (hContext == NULL || pRender == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DdiEscape(
    _In_ CONST HANDLE hAdapter,
    _In_ IN_CONST_PDXGKARG_ESCAPE pEscape)
{
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)hAdapter;

    if (DevExt == NULL || pEscape == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    KdPrint(("AMDBC250: Escape type=%lu pPrivateDriverData=%p privateDriverDataSize=%lu\n",
             (ULONG)pEscape->EscapeType,
             pEscape->pPrivateDriverData,
             pEscape->PrivateDriverDataSize));

    /*
     * Handle custom BC-250 escape types for SMU mailbox, register access,
     * and other driver-specific operations. The UMD passes opaque data
     * through here.
     */
    if (pEscape->pPrivateDriverData != NULL && pEscape->PrivateDriverDataSize > 0) {
        PULONG pInput = (PULONG)pEscape->pPrivateDriverData;

        /* First DWORD is the custom escape code */
        if (pEscape->PrivateDriverDataSize >= sizeof(ULONG)) {
            ULONG EscapeCode = pInput[0];

            switch (EscapeCode) {
            case 0x43425341: /* "ABCA" - BC-250 SMU mailbox test */
                KdPrint(("AMDBC250: Escape SMU test\n"));
                return STATUS_SUCCESS;

            default:
                KdPrint(("AMBC250: Escape unknown code=0x%08X\n", EscapeCode));
                break;
            }
        }
    }

    return STATUS_SUCCESS;
}

/*
 * Bc250DdiPresentDisplayOnly
 * Display-only adapter present path. dxgkrnl passes the desktop image as
 * pSource (a contiguous buffer of BytesPerPixel x Pitch) and asks us to
 * blit it into our POST framebuffer. No 3D/CP engine is required.
 */
NTSTATUS APIENTRY Bc250DdiPresentDisplayOnly(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_PRESENT_DISPLAYONLY *pPresentDisplayOnly)
{
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)hAdapter;
    BYTE *pDst;
    const BYTE *pSrc;
    UINT DstPitch, SrcPitch, Lines, CopyX, Line;

    if (DevExt == NULL || pPresentDisplayOnly == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (pPresentDisplayOnly->VidPnSourceId != 0) {
        return STATUS_NOT_SUPPORTED;
    }
    Bc250DiagWriteDword(L"DiagPresentEntered", 1);
    if (!DevExt->SourceIsVisible) {
        return STATUS_SUCCESS;
    }
    if (!DevExt->FbMapped || DevExt->FbVirtualBase == NULL ||
        pPresentDisplayOnly->pSource == NULL) {
        Bc250DiagWriteDword(L"DiagPresentSkipped", 1);
        return STATUS_SUCCESS;
    }

    /*
     * Safe-boot switch: when FbCopyDisabled=1 in the driver Parameters key,
     * skip the framebuffer copy entirely. This isolates whether the hard hang
     * (Kernel-Power 41, no BugCheck) seen after the EnumVidPn fix is caused by
     * the write into the acquired POST FB (PA 0xC0000000) at commit/present
     * time. dxgkrnl still drives the full VidPn path; only the pixel write is
     * suppressed, so we can bisect the hang without uninstalling.
     */
    if (Bc250DiagReadDword(L"FbCopyDisabled", 0) != 0) {
        Bc250DiagWriteDword(L"DiagPresentCopySkipped", 1);
        return STATUS_SUCCESS;
    }
    Bc250DiagWriteDword(L"DiagPresentCopyEntered", 1);

    pDst = (BYTE *)DevExt->FbVirtualBase;
    pSrc = (const BYTE *)pPresentDisplayOnly->pSource;
    DstPitch = DevExt->FbPitch;
    SrcPitch = (pPresentDisplayOnly->Pitch != 0)
                   ? (UINT)pPresentDisplayOnly->Pitch
                   : (UINT)(DevExt->FbWidth * 4);
    Lines = DevExt->FbHeight;

    if (DstPitch == 0 || SrcPitch == 0 || pDst == NULL || pSrc == NULL) {
        return STATUS_SUCCESS;
    }

    for (Line = 0; Line < Lines; ++Line) {
        CopyX = DevExt->FbWidth;
        if (SrcPitch < CopyX * 4) {
            CopyX = SrcPitch / 4;
        }
        if (CopyX > (DstPitch / 4)) {
            CopyX = DstPitch / 4;
        }
        if (CopyX == 0) {
            break;
        }
        RtlCopyMemory(pDst + ((ULONGLONG)Line * DstPitch),
                      pSrc + ((ULONGLONG)Line * SrcPitch),
                      (SIZE_T)CopyX * 4);
    }

    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DdiStopDeviceAndReleasePostDisplayOwnership(
    _In_ PVOID MiniportDeviceContext,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _Out_ PDXGK_DISPLAY_INFORMATION DisplayInfo)
{
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)MiniportDeviceContext;
    UNREFERENCED_PARAMETER(TargetId);

    if (DevExt == NULL || DisplayInfo == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * Provide current display information so the OS can transition smoothly.
     * If a POST framebuffer was acquired, report its real geometry so the
     * system display / "spinning dots" can be drawn into it.
     */
    if (DevExt->FbMapped && DevExt->FbWidth > 0 && DevExt->FbHeight > 0) {
        DisplayInfo->Width = DevExt->FbWidth;
        DisplayInfo->Height = DevExt->FbHeight;
        DisplayInfo->Pitch = DevExt->FbPitch;
        DisplayInfo->ColorFormat = DevExt->FbColorFormat;
        DisplayInfo->PhysicAddress = DevExt->FbPhysicalBase;
        DisplayInfo->TargetId = 0;
        KdPrint(("AMDBC250: StopDeviceAndRelease POST fb=%ux%u pitch=%u PA=0x%llX\n",
                 DevExt->FbWidth,
                 DevExt->FbHeight,
                 DevExt->FbPitch,
                 DevExt->FbPhysicalBase.QuadPart));
    } else {
        DisplayInfo->Width = AMDBC250_SAFE_MODE_WIDTH;
        DisplayInfo->Height = AMDBC250_SAFE_MODE_HEIGHT;
        DisplayInfo->Pitch = AMDBC250_SAFE_MODE_STRIDE;
        DisplayInfo->ColorFormat = D3DDDIFMT_A8R8G8B8;
        DisplayInfo->TargetId = 0;
    }

    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY Bc250DdiSystemDisplayEnable(
    _In_ PVOID MiniportDeviceContext,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _In_ PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
    _Out_ UINT *Width,
    _Out_ UINT *Height,
    _Out_ D3DDDIFORMAT *ColorFormat)
{
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)MiniportDeviceContext;
    UNREFERENCED_PARAMETER(TargetId);
    UNREFERENCED_PARAMETER(Flags);

    if (DevExt != NULL && DevExt->FbWidth > 0 && DevExt->FbHeight > 0) {
        if (Width != NULL) {
            *Width = DevExt->FbWidth;
        }
        if (Height != NULL) {
            *Height = DevExt->FbHeight;
        }
    } else {
        if (Width != NULL) {
            *Width = AMDBC250_SAFE_MODE_WIDTH;
        }
        if (Height != NULL) {
            *Height = AMDBC250_SAFE_MODE_HEIGHT;
        }
    }
    if (ColorFormat != NULL) {
        *ColorFormat = D3DDDIFMT_X8R8G8B8;
    }
    return STATUS_SUCCESS;
}

VOID APIENTRY Bc250DdiSystemDisplayWrite(
    _In_ PVOID MiniportDeviceContext,
    _In_ PVOID Source,
    _In_ UINT SourceWidth,
    _In_ UINT SourceHeight,
    _In_ UINT SourceStride,
    _In_ UINT DestinationX,
    _In_ UINT DestinationY)
{
    PAMDBC250_DEVICE_EXTENSION DevExt = (PAMDBC250_DEVICE_EXTENSION)MiniportDeviceContext;
    BYTE *pDst;
    const BYTE *pSrc;
    UINT CopyX, CopyY, Line;
    ULONGLONG DstOffset;

    if (DevExt == NULL || Source == NULL || !DevExt->FbMapped ||
        DevExt->FbVirtualBase == NULL) {
        return;
    }

    Bc250DiagWriteDword(L"DiagSysDispWriteEntered", 1);

    /*
     * Same safe-boot switch as PresentDisplayOnly (see FbCopyDisabled note).
     * Suppress the SystemDisplayWrite copy so the boot-time "spinning dots"
     * render path cannot hang the machine during the bisect.
     */
    if (Bc250DiagReadDword(L"FbCopyDisabled", 0) != 0) {
        Bc250DiagWriteDword(L"DiagSysDispWriteSkipped", 1);
        return;
    }

    CopyX = SourceWidth;
    if (DestinationX + CopyX > DevExt->FbWidth) {
        CopyX = DevExt->FbWidth - DestinationX;
    }
    CopyY = SourceHeight;
    if (DestinationY + CopyY > DevExt->FbHeight) {
        CopyY = DevExt->FbHeight - DestinationY;
    }

    pDst = (BYTE *)DevExt->FbVirtualBase;
    pSrc = (const BYTE *)Source;

    for (Line = 0; Line < CopyY; ++Line) {
        DstOffset = ((ULONGLONG)(DestinationY + Line) * DevExt->FbPitch) +
                    ((ULONGLONG)DestinationX * 4);
        if ((DstOffset + ((ULONGLONG)CopyX * 4)) > DevExt->FbSize) {
            break;
        }
        RtlCopyMemory(pDst + DstOffset,
                      pSrc + ((ULONGLONG)Line * SourceStride),
                      (SIZE_T)CopyX * 4);
    }
}





























