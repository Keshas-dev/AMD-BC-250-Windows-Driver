//
// AMD BC-250 Dream Drivers - Display-Only Driver Entry
// Uses DxgkInitializeDisplayOnlyDriver (KMDOD) for early boot load
//

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    KMDDOD_INITIALIZATION_DATA InitData = {0};
    NTSTATUS Status;

    /* CRITICAL: Write DriverBuildId FIRST to confirm new binary is loaded */
    if (RegistryPath != NULL && RegistryPath->Buffer != NULL) {
        OBJECT_ATTRIBUTES objAttr;
        UNICODE_STRING valName;
        ULONG buildId = 0x00000002;

        InitializeObjectAttributes(&objAttr, RegistryPath, OBJ_CASE_INSENSITIVE, NULL, NULL);

        HANDLE hKey = NULL;
        if (NT_SUCCESS(ZwOpenKey(&hKey, KEY_SET_VALUE, &objAttr))) {
            RtlInitUnicodeString(&valName, L"DriverBuildId");
            ZwSetValueKey(hKey, &valName, 0, REG_DWORD, &buildId, sizeof(buildId));
            ZwClose(hKey);
        }
    }

    /* Write DriverEntryRan marker */
    if (RegistryPath != NULL && RegistryPath->Buffer != NULL) {
        OBJECT_ATTRIBUTES objAttr;
        UNICODE_STRING valName;
        ULONG val = 1;

        InitializeObjectAttributes(&objAttr, RegistryPath, OBJ_CASE_INSENSITIVE, NULL, NULL);

        HANDLE hKey = NULL;
        if (NT_SUCCESS(ZwOpenKey(&hKey, KEY_SET_VALUE, &objAttr))) {
            RtlInitUnicodeString(&valName, L"DriverEntryRan");
            ZwSetValueKey(hKey, &valName, 0, REG_DWORD, &val, sizeof(val));
            ZwClose(hKey);
        }
    }

    g_DriverObject = DriverObject;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: DriverEntry - RDNA2/Cyan Skillfish KMDDOD mode\n"));

    /* Initialize the DDI callback table for DISPLAY-ONLY driver */
    InitData.Version = DXGKDDI_INTERFACE_VERSION;

    /* Core lifecycle */
    InitData.DxgkDdiAddDevice = DreamV3DdiAddDevice;
    InitData.DxgkDdiStartDevice = DreamV3DdiStartDevice;
    InitData.DxgkDdiStopDevice = DreamV3DdiStopDevice;
    InitData.DxgkDdiRemoveDevice = DreamV3DdiRemoveDevice;
    InitData.DxgkDdiUnload = DreamV3DdiUnload;
    InitData.DxgkDdiSetPowerState = DreamV3DdiSetPowerState;

    /* Display enumeration */
    InitData.DxgkDdiQueryChildRelations = DreamV3DdiQueryChildRelations;
    InitData.DxgkDdiQueryChildStatus = DreamV3DdiQueryChildStatus;
    InitData.DxgkDdiQueryDeviceDescriptor = DreamV3DdiQueryDeviceDescriptor;
    InitData.DxgkDdiQueryAdapterInfo = DreamV3DdiQueryAdapterInfo;

    /* VidPN management */
    InitData.DxgkDdiIsSupportedVidPn = DreamV3DdiIsSupportedVidPn;
    InitData.DxgkDdiRecommendFunctionalVidPn = DreamV3DdiRecommendFunctionalVidPn;
    InitData.DxgkDdiEnumVidPnCofuncModality = DreamV3DdiEnumVidPnCofuncModality;
    InitData.DxgkDdiSetVidPnSourceVisibility = DreamV3DdiSetVidPnSourceVisibility;
    InitData.DxgkDdiCommitVidPn = DreamV3DdiCommitVidPn;
    InitData.DxgkDdiUpdateActiveVidPnPresentPath = DreamV3DdiUpdateActiveVidPnPresentPath;
    InitData.DxgkDdiRecommendMonitorModes = DreamV3DdiRecommendMonitorModes;
    InitData.DxgkDdiQueryVidPnHWCapability = DreamV3DdiQueryVidPnHwCapability;

    /* Display-only present + system display callbacks (WDDM 1.2+) */
    InitData.DxgkDdiPresentDisplayOnly = DreamV3DdiPresentDisplayOnly;
    InitData.DxgkDdiStopDeviceAndReleasePostDisplayOwnership = DreamV3DdiStopDeviceAndReleasePostDisplayOwnership;
    InitData.DxgkDdiSystemDisplayEnable = DreamV3DdiSystemDisplayEnable;
    InitData.DxgkDdiSystemDisplayWrite = DreamV3DdiSystemDisplayWrite;

    /* Register with DXGKRNL as a DISPLAY-ONLY driver */
    Status = DxgkInitializeDisplayOnlyDriver(DriverObject, RegistryPath, &InitData);

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: DxgkInitializeDisplayOnlyDriver status=0x%08X\n", (ULONG)Status));

    if (NT_SUCCESS(Status)) {
        InterlockedIncrement(&g_DeDxgkInitializeSuccess);
    }

    if (!NT_SUCCESS(Status)) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                   "AMDBC250-DREAM-V4.3: DxgkInitializeDisplayOnlyDriver failed: 0x%08X\n", (ULONG)Status));
    } else {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "AMDBC250-DREAM-V4.3: DriverEntry successful\n"));
    }

    return Status;
}
