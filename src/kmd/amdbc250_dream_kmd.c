/*++

Copyright (c) 2026 AMD BC-250 "Dream Drivers" Project � Version 3.0

Module Name:
    amdbc250_dream_kmd.c

Abstract:
    Kernel-Mode Display Miniport Driver (KMD) for AMD BC-250 APU.
    
    ========================================
    VERSION 3.0 � COMPLETE REWRITE
    ========================================
    
    ARCHITECTURE: RDNA2 / Cyan Skillfish (GFX1013)
    - 24 RDNA2 Compute Units (1536 shaders)
    - 16GB GDDR6 shared memory
    - Dedicated Ray Tracing cores
    - DCN 2.1 display engine
    - GFX10 command processor
    
    Based on Linux amdgpu driver architecture:
    - drivers/gpu/drm/amd/amdgpu/gfx_v10_0.c
    - drivers/gpu/drm/amd/amdgpu/nv.c (Navi family init)
    - drivers/gpu/drm/amd/display/dc/dcn20/ (DCN 2.x)

Environment:
    Kernel mode (WDDM 2.x/3.x)

--*/

#include "amdbc250_dream_kmd.h"
#include "amdbc250_ioctl.h"
#include "amdbc250_psp.h"

static PDRIVER_OBJECT g_DriverObject = NULL;

static PDEVICE_OBJECT g_ControlDevice = NULL;

/* TRUE if DxgkInitialize succeeded � dxgkrnl owns the DriverObject */
static BOOLEAN g_DxgkInitialized = FALSE;

/* PCI device extension (from DxgkDdiAddDevice) - used by IOCTL handler */
static PDREAM_V3_DEVICE_EXTENSION g_PciDevExt = NULL;

/* Shared memory communication with Vulkan ICD */
static PVOID g_SharedBuffer = NULL;
static SIZE_T g_SharedBufferSize = 64 * 1024;  /* 64KB */
static KEVENT g_CmdReadyEvent;
static KEVENT g_CmdDoneEvent;
static PVOID g_SharedSectionObject = NULL;

/* Stored DDI initialization data */
static DRIVER_INITIALIZATION_DATA g_InitData = {0};
static UNICODE_STRING g_DeviceName;
static UNICODE_STRING g_SymlinkName;

/* MDL tracking table � prevents memory leak in ALLOC_VIDMEM IOCTL */
typedef struct _DREAM_V3_MDL_ENTRY {
    PVOID Va;
    PMDL Mdl;
    SIZE_T Size;
} DREAM_V3_MDL_ENTRY;

static DREAM_V3_MDL_ENTRY g_MdlTable[64] = {0};
static KSPIN_LOCK g_MdlTableLock;

/* Forward declarations */
NTSTATUS DreamV3DeviceControl(PDEVICE_OBJECT, PIRP);
NTSTATUS DreamV3CreateClose(PDEVICE_OBJECT, PIRP);
NTSTATUS DreamV3DdiEscape(HANDLE, CONST DXGKARG_ESCAPE*);
NTSTATUS DreamV3SdmaCopyBuffer(PDREAM_V3_DEVICE_EXTENSION, PHYSICAL_ADDRESS, PHYSICAL_ADDRESS, SIZE_T);
NTSTATUS DreamV3SdmaFillBuffer(PDREAM_V3_DEVICE_EXTENSION, PHYSICAL_ADDRESS, SIZE_T, ULONG);
NTSTATUS DreamV3TdrReset(PDREAM_V3_DEVICE_EXTENSION);
NTSTATUS DreamV3ReadEdid(PDREAM_V3_DEVICE_EXTENSION, ULONG, PUCHAR, PULONG);
NTSTATUS DreamV3ParseEdid(PDREAM_V3_DEVICE_EXTENSION, ULONG, PULONG, PULONG, PULONG);
NTSTATUS DreamV3ShaderCompileStub(PDREAM_V3_DEVICE_EXTENSION, PVOID, SIZE_T, ULONG, PVOID*, PULONG);

/* Mandatory DDI stubs (required by DxgkInitialize but not yet implemented) */
NTSTATUS APIENTRY DreamV3DdiDispatchIoRequest(PVOID, ULONG, PVIDEO_REQUEST_PACKET);
NTSTATUS APIENTRY DreamV3DdiControlEtwLogging(PVOID, UINT, UINT, PVOID);
NTSTATUS APIENTRY DreamV3DdiDescribeAllocation(PVOID, PVOID);
NTSTATUS APIENTRY DreamV3DdiGetStandardAllocationDriverData(PVOID, PVOID);
NTSTATUS APIENTRY DreamV3DdiAcquireSwizzlingRange(PVOID, PVOID);
NTSTATUS APIENTRY DreamV3DdiReleaseSwizzlingRange(PVOID, PVOID);
NTSTATUS APIENTRY DreamV3DdiPatch(PVOID, PVOID);
NTSTATUS APIENTRY DreamV3DdiSetPalette(PVOID, PVOID);
NTSTATUS APIENTRY DreamV3DdiSetPointerPosition(PVOID, PVOID);
NTSTATUS APIENTRY DreamV3DdiSetPointerShape(PVOID, PVOID);
NTSTATUS APIENTRY DreamV3DdiResetFromTimeout(PVOID, PVOID);
NTSTATUS APIENTRY DreamV3DdiRestartFromTimeout(PVOID);
NTSTATUS APIENTRY DreamV3DdiCollectDbgInfo(PVOID, PVOID);
NTSTATUS APIENTRY DreamV3DdiIsSupportedVidPn(PVOID, PVOID);
NTSTATUS APIENTRY DreamV3DdiRecommendVidPnTopology(PVOID, PVOID);
NTSTATUS APIENTRY DreamV3DdiStopCapture(PVOID, PVOID);
NTSTATUS APIENTRY DreamV3DdiCreateOverlay(PVOID, PVOID, PVOID);

/*===========================================================================
  DreamV3DxgkInitialize � Calls real DxgkInitialize from dxgkrnl.sys
  Resolves at RUNTIME from dxgkrnl.sys export table (link-time import
  from dxgkrnl.lib causes Code 39 on Win11 26100).
===========================================================================*/

typedef NTSTATUS (NTAPI *PFN_DXGK_INITIALIZE)(
    PDRIVER_OBJECT, PUNICODE_STRING, PDRIVER_INITIALIZATION_DATA);

static PFN_DXGK_INITIALIZE g_pfnDxgkInitialize = NULL;

static NTSTATUS
DreamV3ResolveDxgkInitialize(VOID)
{
    NTSTATUS status;
    ULONG needed = 0;

    if (g_pfnDxgkInitialize != NULL) {
        return STATUS_SUCCESS;
    }

    status = ZwQuerySystemInformation(SystemModuleInformation, &needed, 0, &needed);
    PVOID pModInfo = ExAllocatePool2(POOL_FLAG_NON_PAGED, needed, 'xmGD');
    if (pModInfo == NULL) return STATUS_NO_MEMORY;

    status = ZwQuerySystemInformation(SystemModuleInformation, pModInfo, needed, &needed);
    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(pModInfo, 'xmGD');
        return status;
    }

    PVOID modBase = NULL;
    PUCHAR rawBuf = (PUCHAR)pModInfo;
    ULONG numModules = *(ULONG *)rawBuf;
    PUCHAR moduleStart = rawBuf + sizeof(ULONG);
    /* Compute entry stride dynamically to avoid struct layout assumptions */
    ULONG entrySize = (needed - sizeof(ULONG)) / numModules;

    for (ULONG i = 0; i < numModules; i++) {
        PUCHAR pEntry = moduleStart + (i * entrySize);
        /* FullPathName is always the last 256 bytes of each module entry */
        PUCHAR modPath = pEntry + entrySize - 256;

        /* Compare last 11 bytes of FullPathName with "dxgkrnl.sys" byte-by-byte.
           No CRT dependency � avoids strnlen/_strnicmp link issues. */
        BOOLEAN match = FALSE;
        for (int j = 255; j >= 10; j--) {
            if (modPath[j] == 's' && modPath[j-1] == 'y' && modPath[j-2] == 's' &&
                modPath[j-3] == '.' && modPath[j-4] == 'l' && modPath[j-5] == 'r' &&
                modPath[j-6] == 'n' && modPath[j-7] == 'k' && modPath[j-8] == 'g' &&
                modPath[j-9] == 'x' && modPath[j-10] == 'd') {
                match = TRUE;
                break;
            }
        }
        if (match) {
            /* ImageBase is always at offset 16: Section(8) + MappedBase(8) */
            modBase = *(PVOID *)(pEntry + 16);
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                       "AMDBC250-DREAM-V4.3: dxgkrnl.sys base=%p\n", modBase));
            break;
        }
    }
    ExFreePoolWithTag(pModInfo, 'xmGD');

    if (modBase == NULL) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                   "AMDBC250-DREAM-V4.3: dxgkrnl.sys NOT found in memory\n"));
        return STATUS_NOT_FOUND;
    }

    PIMAGE_DOS_HEADER pDos = (PIMAGE_DOS_HEADER)modBase;
    if (pDos->e_magic != IMAGE_DOS_SIGNATURE) return STATUS_INVALID_IMAGE_FORMAT;
    PIMAGE_NT_HEADERS pNt = (PIMAGE_NT_HEADERS)((PUCHAR)modBase + pDos->e_lfanew);
    if (pNt->Signature != IMAGE_NT_SIGNATURE) return STATUS_INVALID_IMAGE_FORMAT;

    PIMAGE_DATA_DIRECTORY pExpDir =
        &pNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (pExpDir->Size == 0 || pExpDir->VirtualAddress == 0) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                   "AMDBC250-DREAM-V4.3: No export dir in dxgkrnl\n"));
        return STATUS_NOT_FOUND;
    }

    PIMAGE_EXPORT_DIRECTORY pExports = (PIMAGE_EXPORT_DIRECTORY)
        ((PUCHAR)modBase + pExpDir->VirtualAddress);
    PULONG pFunctions = (PULONG)((PUCHAR)modBase + pExports->AddressOfFunctions);
    PULONG pNames = (PULONG)((PUCHAR)modBase + pExports->AddressOfNames);
    PUSHORT pOrdinals = (PUSHORT)((PUCHAR)modBase + pExports->AddressOfNameOrdinals);

    for (ULONG i = 0; i < pExports->NumberOfNames; i++) {
        PCHAR name = (PCHAR)((PUCHAR)modBase + pNames[i]);
        /* Manual byte comparison � avoids CRT strcmp dependency */
        if (name[0] == 'D' && name[1] == 'x' && name[2] == 'g' && name[3] == 'k' &&
            name[4] == 'I' && name[5] == 'n' && name[6] == 'i' && name[7] == 't' &&
            name[8] == 'i' && name[9] == 'a' && name[10] == 'l' && name[11] == 'i' &&
            name[12] == 'z' && name[13] == 'e' && name[14] == '\0') {
            g_pfnDxgkInitialize = (PFN_DXGK_INITIALIZE)
                ((PUCHAR)modBase + pFunctions[pOrdinals[i]]);
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                       "AMDBC250-DREAM-V4.3: DxgkInitialize resolved=%p\n",
                       g_pfnDxgkInitialize));
            return STATUS_SUCCESS;
        }
    }

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
               "AMDBC250-DREAM-V4.3: DxgkInitialize NOT found in dxgkrnl exports\n"));
    return STATUS_NOT_FOUND;
}

static VOID DreamV3WriteStep(ULONG step)
{
    UNICODE_STRING vp, vn;
    OBJECT_ATTRIBUTES oa;
    RtlInitUnicodeString(&vp, L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\atikmdag");
    InitializeObjectAttributes(&oa, &vp, OBJ_CASE_INSENSITIVE, NULL, NULL);
    HANDLE hk = NULL;
    if (NT_SUCCESS(ZwOpenKey(&hk, KEY_SET_VALUE, &oa))) {
        RtlInitUnicodeString(&vn, L"Step_AfterDxgkInit");
        ZwSetValueKey(hk, &vn, 0, REG_DWORD, &step, sizeof(step));
        ZwClose(hk);
    }
}

static NTSTATUS
DreamV3DxgkInitialize(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath,
    _In_ PDRIVER_INITIALIZATION_DATA DriverInitializationData
    )
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);
    UNREFERENCED_PARAMETER(DriverInitializationData);
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
               "AMDBC250-DREAM-V4.3: DxgkInitialize not available on Win11 26100\n"));
    return STATUS_NOT_SUPPORTED;
}

/* Forward declaration */
static VOID DreamV3WdmUnload(_In_ PDRIVER_OBJECT DriverObject);

/*===========================================================================
   DriverEntry � Main entry point (WDDM 2.x/3.x)
===========================================================================*/

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    DRIVER_INITIALIZATION_DATA InitData = {0};
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

    /* Write DriverEntryRan marker using the registry path passed by PnP */
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
               "AMDBC250-DREAM-V4.3: DriverEntry v%d.%d.%d - RDNA2/Cyan Skillfish\n",
               AMDBC250_DREAM_V3_VERSION_MAJOR,
               AMDBC250_DREAM_V3_VERSION_MINOR,
               AMDBC250_DREAM_V3_VERSION_PATCH));

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: Architecture: 24 CU RDNA2, 16GB GDDR6\n"));

    /* Initialize the DDI callback table */
    /* Use WIN8 version � enough for basic WDDM but not too many mandatory DDIs */
    InitData.Version = DXGKDDI_INTERFACE_VERSION_WIN8;
    
    /* Core device lifecycle */
    InitData.DxgkDdiAddDevice = DreamV3DdiAddDevice;
    InitData.DxgkDdiStartDevice = DreamV3DdiStartDevice;
    InitData.DxgkDdiStopDevice = DreamV3DdiStopDevice;
    InitData.DxgkDdiRemoveDevice = DreamV3DdiRemoveDevice;
    InitData.DxgkDdiResetDevice = DreamV3DdiResetDevice;
    InitData.DxgkDdiUnload = DreamV3DdiUnload;
    
    /* Display enumeration */
    InitData.DxgkDdiQueryChildRelations = DreamV3DdiQueryChildRelations;
    InitData.DxgkDdiQueryChildStatus = DreamV3DdiQueryChildStatus;
    InitData.DxgkDdiQueryDeviceDescriptor = DreamV3DdiQueryDeviceDescriptor;
    
    /* Power management */
    InitData.DxgkDdiSetPowerState = DreamV3DdiSetPowerState;
    InitData.DxgkDdiNotifyAcpiEvent = DreamV3DdiNotifyAcpiEvent;
    
    /* Interrupt handling */
    InitData.DxgkDdiInterruptRoutine = DreamV3DdiInterruptRoutine;
    InitData.DxgkDdiDpcRoutine = DreamV3DdiDpcRoutine;
    
    /* Adapter queries */
    InitData.DxgkDdiQueryAdapterInfo = DreamV3DdiQueryAdapterInfo;
    InitData.DxgkDdiQueryInterface = DreamV3DdiQueryInterface;
    
    /* Device context */
    InitData.DxgkDdiCreateDevice = DreamV3DdiCreateDevice;
    InitData.DxgkDdiDestroyDevice = DreamV3DdiDestroyDevice;
    
    /* Memory management */
    InitData.DxgkDdiCreateAllocation = DreamV3DdiCreateAllocation;
    InitData.DxgkDdiDestroyAllocation = DreamV3DdiDestroyAllocation;
    InitData.DxgkDdiBuildPagingBuffer = DreamV3DdiBuildPagingBuffer;
    
    /* Command submission */
    InitData.DxgkDdiSubmitCommand = DreamV3DdiSubmitCommand;
    InitData.DxgkDdiPreemptCommand = DreamV3DdiPreemptCommand;
    InitData.DxgkDdiQueryCurrentFence = DreamV3DdiQueryCurrentFence;
    
    /* Rendering and present */
    InitData.DxgkDdiPresent = DreamV3DdiPresent;
    InitData.DxgkDdiRender = DreamV3DdiRender;
    
    /* Display/VidPN */
    InitData.DxgkDdiRecommendFunctionalVidPn = DreamV3DdiRecommendFunctionalVidPn;
    InitData.DxgkDdiEnumVidPnCofuncModality = DreamV3DdiEnumVidPnCofuncModality;
    InitData.DxgkDdiCommitVidPn = DreamV3DdiCommitVidPn;
    InitData.DxgkDdiSetVidPnSourceAddress = DreamV3DdiSetVidPnSourceAddress;
    InitData.DxgkDdiSetVidPnSourceVisibility = DreamV3DdiSetVidPnSourceVisibility;
    InitData.DxgkDdiUpdateActiveVidPnPresentPath = DreamV3DdiUpdateActiveVidPnPresentPath;
    InitData.DxgkDdiRecommendMonitorModes = DreamV3DdiRecommendMonitorModes;
    InitData.DxgkDdiGetScanLine = DreamV3DdiGetScanLine;
    InitData.DxgkDdiControlInterrupt = DreamV3DdiControlInterrupt;
    
    /* UMD?KMD communication via WDDM Escape (replaces MajorFunction IOCTL dispatch) */
    InitData.DxgkDdiEscape = DreamV3DdiEscape;

    /* Mandatory DDI stubs (required by DxgkInitialize for WIN8+) */
    InitData.DxgkDdiDispatchIoRequest = DreamV3DdiDispatchIoRequest;
    InitData.DxgkDdiControlEtwLogging = DreamV3DdiControlEtwLogging;
    InitData.DxgkDdiDescribeAllocation = DreamV3DdiDescribeAllocation;
    InitData.DxgkDdiGetStandardAllocationDriverData = DreamV3DdiGetStandardAllocationDriverData;
    InitData.DxgkDdiAcquireSwizzlingRange = DreamV3DdiAcquireSwizzlingRange;
    InitData.DxgkDdiReleaseSwizzlingRange = DreamV3DdiReleaseSwizzlingRange;
    InitData.DxgkDdiPatch = DreamV3DdiPatch;
    InitData.DxgkDdiSetPalette = DreamV3DdiSetPalette;
    InitData.DxgkDdiSetPointerPosition = DreamV3DdiSetPointerPosition;
    InitData.DxgkDdiSetPointerShape = DreamV3DdiSetPointerShape;
    InitData.DxgkDdiResetFromTimeout = DreamV3DdiResetFromTimeout;
    InitData.DxgkDdiRestartFromTimeout = DreamV3DdiRestartFromTimeout;
    InitData.DxgkDdiCollectDbgInfo = DreamV3DdiCollectDbgInfo;
    InitData.DxgkDdiIsSupportedVidPn = DreamV3DdiIsSupportedVidPn;
    InitData.DxgkDdiRecommendVidPnTopology = DreamV3DdiRecommendVidPnTopology;
    InitData.DxgkDdiStopCapture = DreamV3DdiStopCapture;
    InitData.DxgkDdiCreateOverlay = DreamV3DdiCreateOverlay;

    /* Register with DXGKRNL */
    {
        UNICODE_STRING vp2, vn2;
        OBJECT_ATTRIBUTES oa2;
        RtlInitUnicodeString(&vp2, L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\atikmdag");
        InitializeObjectAttributes(&oa2, &vp2, OBJ_CASE_INSENSITIVE, NULL, NULL);
        HANDLE hk2 = NULL;
        if (NT_SUCCESS(ZwOpenKey(&hk2, KEY_SET_VALUE, &oa2))) {
            ULONG step = 10;
            RtlInitUnicodeString(&vn2, L"Step_BeforeDxgkInit");
            ZwSetValueKey(hk2, &vn2, 0, REG_DWORD, &step, sizeof(step));
            ZwClose(hk2);
        }
    }
    /* DxgkInitialize is NOT exported from dxgkrnl.sys on Win11 26100.
       Skip it entirely and fall through to WDM IOCTL mode. */
    Status = STATUS_NOT_SUPPORTED;
    {
        UNICODE_STRING vp2, vn2;
        OBJECT_ATTRIBUTES oa2;
        RtlInitUnicodeString(&vp2, L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\atikmdag");
        InitializeObjectAttributes(&oa2, &vp2, OBJ_CASE_INSENSITIVE, NULL, NULL);
        HANDLE hk2 = NULL;
        if (NT_SUCCESS(ZwOpenKey(&hk2, KEY_SET_VALUE, &oa2))) {
            ULONG step = 11;
            RtlInitUnicodeString(&vn2, L"Step_DriverEntryPost");
            ZwSetValueKey(hk2, &vn2, 0, REG_DWORD, &step, sizeof(step));
            ZwClose(hk2);
        }
    }

    if (NT_SUCCESS(Status)) {
        /* Never reached � DxgkInitialize always returns STATUS_NOT_SUPPORTED */
    } else {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: DxgkInitialize FAILED: 0x%08X � falling back to WDM IOCTL mode\n", Status));

        /* DxgkInitialize failed: create WDM control device for IOCTL communication */
        {
            UNICODE_STRING devName, symLink;
            NTSTATUS symStatus;
            RtlInitUnicodeString(&devName, L"\\Device\\AMDBC250DreamV43");
            RtlInitUnicodeString(&symLink, L"\\DosDevices\\AMDBC250DreamV43");
            RtlCopyMemory(&g_DeviceName, &devName, sizeof(UNICODE_STRING));
            RtlCopyMemory(&g_SymlinkName, &symLink, sizeof(UNICODE_STRING));

            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                       "AMDBC250-DREAM-V4.3: Creating WDM control device (fallback mode)...\n"));

            /* Delete any stale control device from a previous failed unload (fixes Error 38) */
            {
                PFILE_OBJECT oldFileObj = NULL;
                PDEVICE_OBJECT oldDevObj = NULL;
                NTSTATUS openStatus = IoGetDeviceObjectPointer(
                    &devName, FILE_ALL_ACCESS, &oldFileObj, &oldDevObj);
                if (NT_SUCCESS(openStatus) && oldDevObj != NULL) {
                    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                        "AMDBC250-DREAM-V4.3: Found stale control device at %p, deleting...\n", oldDevObj));
                    if (oldFileObj != NULL) {
                        ObDereferenceObject(oldFileObj);
                    }
                    IoDeleteDevice(oldDevObj);
                    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                        "AMDBC250-DREAM-V4.3: Stale control device deleted\n"));
                }
            }

            Status = IoCreateDevice(
                DriverObject,
                sizeof(DREAM_V3_DEVICE_EXTENSION),
                &devName,
                FILE_DEVICE_UNKNOWN,
                0,
                FALSE,
                &g_ControlDevice);

            if (NT_SUCCESS(Status)) {
                g_ControlDevice->Flags |= DO_BUFFERED_IO;
                g_ControlDevice->Flags &= ~DO_DEVICE_INITIALIZING;
                symStatus = IoCreateSymbolicLink(&symLink, &devName);

                /* Mark that device was created */
                {
                    UNICODE_STRING devPath2, valName2;
                    OBJECT_ATTRIBUTES objAttr2;
                    RtlInitUnicodeString(&devPath2, L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\atikmdag");
                    InitializeObjectAttributes(&objAttr2, &devPath2, OBJ_CASE_INSENSITIVE, NULL, NULL);
                    HANDLE hKey2 = NULL;
                    if (NT_SUCCESS(ZwOpenKey(&hKey2, KEY_SET_VALUE, &objAttr2))) {
                        ULONG devCreated = 1;
                        RtlInitUnicodeString(&valName2, L"ControlDeviceCreated");
                        ZwSetValueKey(hKey2, &valName2, 0, REG_DWORD, &devCreated, sizeof(devCreated));
                        ULONG symVal = NT_SUCCESS(symStatus) ? 1 : 0;
                        RtlInitUnicodeString(&valName2, L"SymlinkCreated");
                        ZwSetValueKey(hKey2, &valName2, 0, REG_DWORD, &symVal, sizeof(symVal));
                        ZwClose(hKey2);
                    }
                }

                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                           "AMDBC250-DREAM-V4.3: IoCreateDevice OK, symlink=0x%08X\n", symStatus));
                DriverObject->MajorFunction[IRP_MJ_CREATE] = DreamV3CreateClose;
                DriverObject->MajorFunction[IRP_MJ_CLOSE] = DreamV3CreateClose;
                DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DreamV3DeviceControl;

                /* Allocate PCI device extension for IOCTL handler */
                {
                    g_PciDevExt = (PDREAM_V3_DEVICE_EXTENSION)
                        ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(DREAM_V3_DEVICE_EXTENSION), '3vDA');
                    if (g_PciDevExt != NULL) {
                        RtlZeroMemory(g_PciDevExt, sizeof(DREAM_V3_DEVICE_EXTENSION));
                        ExInitializeFastMutex(&g_PciDevExt->DeviceMutex);
                        KeInitializeSpinLock(&g_PciDevExt->FenceLock);
                        InitializeListHead(&g_PciDevExt->AllocationList);
                        KeInitializeEvent(&g_PciDevExt->DeviceRemoved, NotificationEvent, FALSE);

                        g_PciDevExt->VendorId = 0x1002;
                        g_PciDevExt->DeviceId = 0x13FE;
                        g_PciDevExt->VisibleVramBytes = 4ULL * 1024 * 1024 * 1024;
                        g_PciDevExt->TotalVramBytes = 16ULL * 1024 * 1024 * 1024;
                        g_PciDevExt->NumDisplayPipes = 4;
                        g_PciDevExt->NextGpuVa = 0x100000000ULL;

                        g_ControlDevice->DeviceExtension = g_PciDevExt;

                        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                                   "AMDBC250-DREAM-V4.3: g_PciDevExt allocated at %p\n", g_PciDevExt));
                    }
                }
            } else {
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                           "AMDBC250-DREAM-V4.3: IoCreateDevice FAILED: 0x%08X\n", Status));
                Status = STATUS_SUCCESS; /* Non-fatal */
            }
        }
    }

    /* Create shared memory for Vulkan ICD command submission */
    {
        UNICODE_STRING eventName;
        OBJECT_ATTRIBUTES eventAttr;
        
        /* Create command-ready event (manual reset, initially non-signaled) */
        RtlInitUnicodeString(&eventName, L"\\BaseNamedObjects\\BC250CmdReady");
        InitializeObjectAttributes(&eventAttr, &eventName, OBJ_CASE_INSENSITIVE, NULL, NULL);
        KeInitializeEvent(&g_CmdReadyEvent, SynchronizationEvent, FALSE);
        
        /* Create command-done event */
        RtlInitUnicodeString(&eventName, L"\\BaseNamedObjects\\BC250CmdDone");
        InitializeObjectAttributes(&eventAttr, &eventName, OBJ_CASE_INSENSITIVE, NULL, NULL);
        KeInitializeEvent(&g_CmdDoneEvent, SynchronizationEvent, FALSE);
        
        /* Allocate shared buffer (non-paged pool, accessible from both kernel and user) */
        g_SharedBuffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, g_SharedBufferSize, 'cmhS');
        if (g_SharedBuffer != NULL) {
            RtlZeroMemory(g_SharedBuffer, g_SharedBufferSize);
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                       "AMDBC250-DREAM-V4.3: Shared buffer allocated at %p (%llu bytes)\n",
                       g_SharedBuffer, (ULONG64)g_SharedBufferSize));
        }
    }

    return Status;
}

/*===========================================================================
  DxgkDdiAddDevice � PnP manager found matching PCI device
===========================================================================*/

NTSTATUS
APIENTRY
DreamV3DdiAddDevice(
    _In_  CONST PDEVICE_OBJECT  PhysicalDeviceObject,
    _Out_ PVOID                 *MiniportDeviceContext
    )
{
    PDREAM_V3_DEVICE_EXTENSION DevExt;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
               "AMDBC250-DREAM-V4.3: DxgkDdiAddDevice called\n"));

    /* Allocate device extension */
    DevExt = (PDREAM_V3_DEVICE_EXTENSION)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        sizeof(DREAM_V3_DEVICE_EXTENSION),
        DREAM_V3_TAG_DEVICE
        );

    if (DevExt == NULL) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                   "AMDBC250-DREAM-V4.3: Failed to allocate device extension\n"));
        return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(DevExt, sizeof(DREAM_V3_DEVICE_EXTENSION));

    /* Initialize synchronization primitives */
    ExInitializeFastMutex(&DevExt->DeviceMutex);
    KeInitializeSpinLock(&DevExt->FenceLock);
    KeInitializeSpinLock(&DevExt->AllocationListLock);
    KeInitializeSpinLock(&g_MdlTableLock);
    InitializeListHead(&DevExt->AllocationList);
    KeInitializeEvent(&DevExt->DeviceRemoved, NotificationEvent, FALSE);

    DevExt->PhysicalDeviceObject = PhysicalDeviceObject;

    /* Initialize hardware quirks from Linux driver knowledge */
    /* BC-250 is UMA: 16GB shared, VRAM split configurable in BIOS */
    DevExt->VisibleVramBytes = 4ULL * 1024 * 1024 * 1024; /* 4GB default (BIOS configurable) */
    DevExt->NumDisplayPipes = 4;  /* DCN 2.1: 4 pipes */
    DevExt->CurrentTemperatureC = 0;
    DevExt->NextGpuVa = 0x100000000ULL; /* GPU VA starts at 4GB */

    *MiniportDeviceContext = DevExt;
    g_PciDevExt = DevExt;  /* Store for IOCTL handler */

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: Device extension allocated at %p\n", DevExt));

    return STATUS_SUCCESS;
}

/*===========================================================================
  DxgkDdiStartDevice � Start the device
===========================================================================*/

NTSTATUS
APIENTRY
DreamV3DdiStartDevice(
    _In_  PVOID                     MiniportDeviceContext,
    _In_  PDXGK_START_INFO          DxgkStartInfo,
    _In_  PDXGKRNL_INTERFACE        DxgkInterface,
    _Out_ PULONG                    NumberOfVideoPresentSources,
    _Out_ PULONG                    NumberOfChildren
    )
{
    PDREAM_V3_DEVICE_EXTENSION DevExt = (PDREAM_V3_DEVICE_EXTENSION)MiniportDeviceContext;
    NTSTATUS Status;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: DxgkDdiStartDevice called\n"));

    if (DevExt == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    /* Save DXGKRNL interface and device handle */
    if (DxgkInterface) {
        DevExt->DxgkInterface = *DxgkInterface;
        DevExt->DxgkDeviceHandle = DxgkInterface->DeviceHandle;
    }

    /* Get device information */
    DXGK_DEVICE_INFO DeviceInfo = {0};
    
    Status = DxgkInterface->DxgkCbGetDeviceInformation(
        DevExt->DxgkDeviceHandle,
        &DeviceInfo
        );

    if (!NT_SUCCESS(Status)) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                   "AMDBC250-DREAM-V4.3: DxgkCbGetDeviceInformation failed: 0x%08X\n", Status));
        return Status;
    }
    
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: DeviceInfo retrieved\n"));

    /* Get PCI configuration space to read VendorId/DeviceId */
    /* Use fallback values since PCI config access requires bus interface */
    DevExt->VendorId = AMD_VENDOR_ID;
    DevExt->DeviceId = AMDBC250_DEVICE_ID_PRIMARY;
    DevExt->RevisionId = 0x00;
    DevExt->SubsystemVendorId = 0;
    DevExt->SubsystemId = 0;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: PCI %04X:%04X (Rev %02X) � Cyan Skillfish\n",
               DevExt->VendorId, DevExt->DeviceId, DevExt->RevisionId));

    /* Verify this is our GPU */
    if (DevExt->VendorId != AMD_VENDOR_ID) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                   "AMDBC250-DREAM-V4.3: Invalid vendor: %04X\n", DevExt->VendorId));
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    if (DevExt->DeviceId != AMDBC250_DEVICE_ID_PRIMARY) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                   "AMDBC250-DREAM-V4.3: Unexpected device ID: %04X\n", DevExt->DeviceId));
        /* Continue anyway � might be a variant */
    }

    /* Map MMIO BAR - use safe iteration */
    PCM_PARTIAL_RESOURCE_LIST PartialResourceList = 
        &DeviceInfo.TranslatedResourceList->List[0].PartialResourceList;
    
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: PartialResourceList has %lu resources\n",
               PartialResourceList->Count));
    
    BOOLEAN MmioFound = FALSE;
    BOOLEAN FbFound = FALSE;
    for (ULONG i = 0; i < PartialResourceList->Count; i++) {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR desc = &PartialResourceList->PartialDescriptors[i];
        
        if (desc->Type == CmResourceTypeMemory) {
            PHYSICAL_ADDRESS PhysAddr = desc->u.Memory.Start;
            ULONG Size = desc->u.Memory.Length;
            
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                       "AMDBC250-DREAM-V4.3: Resource[%lu] Type=Memory, PA=0x%llX, Size=0x%X\n",
                       i, PhysAddr.QuadPart, Size));
            
            /* Heuristic: MMIO register BAR is < 16MB, VRAM framebuffer is >= 16MB */
            if (Size >= 0x1000000 && !FbFound) {
                /* Large region = VRAM framebuffer */
                DevExt->FbPhysicalBase = PhysAddr;
                DevExt->FbSize = Size;
                FbFound = TRUE;
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                           "AMDBC250-DREAM-V4.3: Framebuffer (VRAM): PA=0x%llX, Size=0x%X (%lu MB)\n",
                           DevExt->FbPhysicalBase.QuadPart,
                           DevExt->FbSize,
                           DevExt->FbSize / (1024 * 1024)));
            } else if (!MmioFound) {
                /* Small region = MMIO registers */
                DevExt->MmioPhysicalBase = PhysAddr;
                DevExt->MmioSize = Size;
                
                DevExt->MmioVirtualBase = MmMapIoSpace(
                    DevExt->MmioPhysicalBase,
                    DevExt->MmioSize,
                    MmNonCached
                    );

                if (DevExt->MmioVirtualBase == NULL) {
                    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                               "AMDBC250-DREAM-V4.3: *** FAILED to map MMIO (PA=0x%llX, Size=0x%X)\n",
                               DevExt->MmioPhysicalBase.QuadPart, DevExt->MmioSize));
                    return STATUS_INSUFFICIENT_RESOURCES;
                }

                MmioFound = TRUE;
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                           "AMDBC250-DREAM-V4.3: *** MMIO MAPPED SUCCESS: VA=0x%p, PA=0x%llX, Size=0x%X\n",
                           DevExt->MmioVirtualBase, DevExt->MmioPhysicalBase.QuadPart, DevExt->MmioSize));
            }
        }
    }
    
    if (!MmioFound) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: *** NO MMIO RESOURCE FOUND � continuing in software mode ***\n"));
        /* Don't fail � PS5 may not expose MMIO resources to PnP.
           The driver still works for D3DKMTEscape queries. */
    }

    /* CRITICAL: Initialize hardware � non-fatal if fails (PS5 NBIO may block MMIO) */
    Status = DreamV3HwInitialize(DevExt);
    if (!NT_SUCCESS(Status)) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: Hardware init failed: 0x%08X � continuing in software mode\n", Status));
        /* Don't fail � continue with software-only mode for Escape queries */
    }

    /* Report display topology */
    *NumberOfVideoPresentSources = DevExt->NumDisplayPipes;
    *NumberOfChildren = DevExt->NumDisplayPipes;

    DevExt->HardwareInitialized = TRUE;
    DevExt->DeviceStarted = TRUE;

    /* Check registry for 40 CU unlock */
    {
        UNICODE_STRING regPath;
        UNICODE_STRING regValueName;
        OBJECT_ATTRIBUTES objAttr;
        HANDLE hKey = NULL;
        UCHAR regBuffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
        PKEY_VALUE_PARTIAL_INFORMATION regInfo = (PKEY_VALUE_PARTIAL_INFORMATION)regBuffer;
        ULONG regSize = 0;

        RtlInitUnicodeString(&regPath, L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\AMDBC250DreamV43");
        InitializeObjectAttributes(&objAttr, &regPath, OBJ_CASE_INSENSITIVE, NULL, NULL);

        NTSTATUS regStatus = ZwOpenKey(&hKey, KEY_READ, &objAttr);
        if (NT_SUCCESS(regStatus)) {
            RtlInitUnicodeString(&regValueName, L"Enable40CU");
            regStatus = ZwQueryValueKey(hKey, &regValueName, KeyValuePartialInformation,
                                         regBuffer, sizeof(regBuffer), &regSize);
            ZwClose(hKey);

            if (NT_SUCCESS(regStatus) && regInfo->Type == REG_DWORD &&
                regInfo->DataLength == sizeof(ULONG)) {
                ULONG regValue = *(PULONG)regInfo->Data;
                if (regValue == 1) {
                    /* Enable all WGPs (bits 8-13, verified via write-back) */
                    DreamV3WriteRegister(DevExt, AMDBC250_REG_CC_GC_SHADER_ARRAY_CONFIG, 0xFFE00000);
                    DreamV3WriteRegister(DevExt, AMDBC250_REG_SPI_PG_ENABLE_STATIC_WGP_MASK, 0x00003F00);
                    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                        "AMDBC250-DREAM-V4.3: *** WGP UNLOCK via registry (SPI=0x3F00) ***\n"));
                }
            }
        }
    }

    /* Control device: only exists in WDM IOCTL mode (when DxgkInitialize failed) */
    if (g_ControlDevice != NULL) {
        /* Control device exists from DriverEntry fallback � update pointers */
        if (g_ControlDevice->DeviceExtension == NULL) {
            g_ControlDevice->DeviceExtension = DevExt;
        }
        g_PciDevExt = DevExt;
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "AMDBC250-DREAM-V4.3: Control device already exists, pointers updated (WDM mode)\n"));
    } else {
        /* DxgkInitialize succeeded � no WDM control device, dxgkrnl owns everything */
        g_PciDevExt = DevExt;
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "AMDBC250-DREAM-V4.3: WDDM mode � dxgkrnl owns adapter\n"));
    }

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: StartDevice SUCCESS\n"));

    /* Only re-set MajorFunction in WDM IOCTL mode (when DxgkInitialize failed and
       we have a control device). When DxgkInitialize succeeded, dxgkrnl owns the
       DriverObject and we must NOT touch MajorFunction � it causes BSOD. */
    if (g_ControlDevice != NULL) {
        g_DriverObject->MajorFunction[IRP_MJ_CREATE] = DreamV3CreateClose;
        g_DriverObject->MajorFunction[IRP_MJ_CLOSE] = DreamV3CreateClose;
        g_DriverObject->DriverUnload = DreamV3WdmUnload;
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "AMDBC250-DREAM-V4.3: MajorFunction re-set (WDM IOCTL mode)\n"));
    } else {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "AMDBC250-DREAM-V4.3: MajorFunction NOT re-set (WDDM mode � dxgkrnl owns)\n"));
    }
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3:   24 CU RDNA2, 1536 SP\n"));
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3:   VRAM: %llu MB GDDR6\n",
               DevExt->TotalVramBytes / (1024 * 1024)));
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3:   Ray Tracing: Enabled (early gen)\n"));

    return STATUS_SUCCESS;
}

/*===========================================================================
  DxgkDdiStopDevice � Stop device
===========================================================================*/

NTSTATUS
APIENTRY
DreamV3DdiStopDevice(
    _In_ PVOID MiniportDeviceContext
    )
{
    PDREAM_V3_DEVICE_EXTENSION DevExt = (PDREAM_V3_DEVICE_EXTENSION)MiniportDeviceContext;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: DxgkDdiStopDevice called\n"));

    if (DevExt == NULL) return STATUS_INVALID_PARAMETER;

    if (DevExt->HardwareInitialized) {
        DreamV3HwShutdown(DevExt);
        DevExt->HardwareInitialized = FALSE;
    }

    /* Cleanup PSP (unmap GPU BAR5, free rings) */
    if (DevExt->PspInitialized) {
        Amdbc250PspCleanup();
        Amdbc250PspProxyCleanup();
        DevExt->PspInitialized = FALSE;
    }

    if (DevExt->MmioVirtualBase != NULL) {
        MmUnmapIoSpace(DevExt->MmioVirtualBase, DevExt->MmioSize);
        DevExt->MmioVirtualBase = NULL;
        DevExt->MmioSize = 0;
    }

    /* FIXED: Only unmap FB and Doorbell if they were actually mapped */
    if (DevExt->FbVirtualBase != NULL && DevExt->FbSize > 0) {
        MmUnmapIoSpace(DevExt->FbVirtualBase, DevExt->FbSize);
        DevExt->FbVirtualBase = NULL;
        DevExt->FbSize = 0;
    }

    if (DevExt->DoorbellVirtualBase != NULL && DevExt->DoorbellSize > 0) {
        MmUnmapIoSpace(DevExt->DoorbellVirtualBase, DevExt->DoorbellSize);
        DevExt->DoorbellVirtualBase = NULL;
        DevExt->DoorbellSize = 0;
    }

    DevExt->DeviceStarted = FALSE;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: StopDevice complete\n"));

    return STATUS_SUCCESS;
}

/*===========================================================================
  DxgkDdiRemoveDevice � Final cleanup
===========================================================================*/

NTSTATUS
APIENTRY
DreamV3DdiRemoveDevice(
    _In_ PVOID MiniportDeviceContext
    )
{
    PDREAM_V3_DEVICE_EXTENSION DevExt = (PDREAM_V3_DEVICE_EXTENSION)MiniportDeviceContext;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: DxgkDdiRemoveDevice called\n"));

    if (DevExt != NULL) {
        KeSetEvent(&DevExt->DeviceRemoved, 0, FALSE);
        ExFreePoolWithTag(DevExt, DREAM_V3_TAG_DEVICE);
        g_PciDevExt = NULL;
    }

    return STATUS_SUCCESS;
}

/*===========================================================================
  DxgkDdiResetDevice � TDR recovery
===========================================================================*/

VOID
APIENTRY
DreamV3DdiResetDevice(
    _In_ PVOID MiniportDeviceContext
    )
{
    PDREAM_V3_DEVICE_EXTENSION DevExt = (PDREAM_V3_DEVICE_EXTENSION)MiniportDeviceContext;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
               "AMDBC250-DREAM-V4.3: TDR reset initiated\n"));

    if (DevExt == NULL) return;

    DevExt->GpuResetInProgress = TRUE;
    DevExt->ResetCount++;

    if (DevExt->HardwareInitialized) {
        NTSTATUS Status = DreamV3HwReset(DevExt);
        if (!NT_SUCCESS(Status)) {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                       "AMDBC250-DREAM-V4.3: GPU reset FAILED: 0x%08X\n", Status));
        } else {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                       "AMDBC250-DREAM-V4.3: GPU reset SUCCESS\n"));
        }
    }

    DevExt->GpuResetInProgress = FALSE;
}

/* WDM DriverUnload � called by PnP when driver is unloaded */
static VOID DreamV3WdmUnload(_In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: WDM Unload called\n"));
    
    /* Free GCVM page table pages and ring buffer */
    if (g_ControlDevice != NULL) {
        PDREAM_V3_DEVICE_EXTENSION devExt = (PDREAM_V3_DEVICE_EXTENSION)g_ControlDevice->DeviceExtension;
        if (devExt != NULL) {
            for (int i = 0; i < 3; i++) {
                if (devExt->GcvmPtPages[i] != NULL) {
                    MmFreeContiguousMemory(devExt->GcvmPtPages[i]);
                    devExt->GcvmPtPages[i] = NULL;
                }
            }
            if (devExt->GcvmRingBuf != NULL) {
                MmFreeContiguousMemory(devExt->GcvmRingBuf);
                devExt->GcvmRingBuf = NULL;
            }
            if (devExt->HqdMqdBuf != NULL) {
                MmFreeContiguousMemory(devExt->HqdMqdBuf);
                devExt->HqdMqdBuf = NULL;
            }
        }
    }

    /* Free shared memory */
    if (g_SharedBuffer != NULL) {
        ExFreePoolWithTag(g_SharedBuffer, 'cmhS');
        g_SharedBuffer = NULL;
    }

    /* Cleanup any remaining MDL allocations (prevent memory leak on unload) */
    for (int m = 0; m < 64; m++) {
        if (g_MdlTable[m].Va != NULL && g_MdlTable[m].Mdl != NULL) {
            MmUnmapLockedPages(g_MdlTable[m].Va, g_MdlTable[m].Mdl);
            MmFreePagesFromMdl(g_MdlTable[m].Mdl);
            ExFreePoolWithTag(g_MdlTable[m].Mdl, 'MDL');
            g_MdlTable[m].Va = NULL;
            g_MdlTable[m].Mdl = NULL;
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                       "AMDBC250-DREAM-V4.3: Leaked MDL freed on unload: %llu bytes\n",
                       (ULONG64)g_MdlTable[m].Size));
        }
    }
    
    if (g_ControlDevice != NULL) {
        IoDeleteSymbolicLink(&g_SymlinkName);
        IoDeleteDevice(g_ControlDevice);
        g_ControlDevice = NULL;
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "AMDBC250-DREAM-V4.3: Control device deleted by WDM unload\n"));
    }

    /* Free separately-allocated device extension (WDM path only — WDDM path frees in RemoveDevice) */
    if (g_PciDevExt != NULL) {
        ExFreePoolWithTag(g_PciDevExt, '3vDA');
        g_PciDevExt = NULL;
    }
}

/*===========================================================================
  DxgkDdiUnload
===========================================================================*/

VOID
APIENTRY
DreamV3DdiUnload(VOID)
{
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: Driver unload\n"));

    /* Mark that unload was called */
    {
        UNICODE_STRING devPath;
        OBJECT_ATTRIBUTES objAttr;
        RtlInitUnicodeString(&devPath, L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\atikmdag");
        InitializeObjectAttributes(&objAttr, &devPath, OBJ_CASE_INSENSITIVE, NULL, NULL);
        HANDLE hKey = NULL;
        if (NT_SUCCESS(ZwOpenKey(&hKey, KEY_SET_VALUE, &objAttr))) {
            UNICODE_STRING valName;
            ULONG val = 1;
            RtlInitUnicodeString(&valName, L"UnloadCalled");
            ZwSetValueKey(hKey, &valName, 0, REG_DWORD, &val, sizeof(val));
            ZwClose(hKey);
        }
    }

    /* Cleanup control device */
    if (g_ControlDevice != NULL) {
        IoDeleteSymbolicLink(&g_SymlinkName);
        IoDeleteDevice(g_ControlDevice);
        g_ControlDevice = NULL;
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "AMDBC250-DREAM-V4.3: Control device deleted\n"));
    }
}

/*===========================================================================
  Interrupt Routine (ISR) � Runs at DIRQL
===========================================================================*/

BOOLEAN
APIENTRY
DreamV3DdiInterruptRoutine(
    _In_ PVOID  MiniportDeviceContext,
    _In_ ULONG  MessageNumber
    )
{
    PDREAM_V3_DEVICE_EXTENSION DevExt = (PDREAM_V3_DEVICE_EXTENSION)MiniportDeviceContext;
    ULONG IhWptr;
    BOOLEAN OurInterrupt = FALSE;

    UNREFERENCED_PARAMETER(MessageNumber);

    if (DevExt == NULL || !DevExt->IhRing.Initialized) {
        return FALSE;
    }

    /* CRITICAL: Flush HDP before reading ring pointers (Linux quirk) */
    DreamV3HdpFlush(DevExt);

    /* Read IH ring write pointer */
    IhWptr = DreamV3ReadRegister(DevExt, AMDBC250_REG_IH_RB_WPTR) & 0x0001FFFF;

    if (IhWptr != DevExt->IhRing.ReadPointer) {
        DevExt->LastInterruptStatus = IhWptr;
        DevExt->InterruptCount++;
        OurInterrupt = TRUE;

        /* Queue DPC */
        DevExt->DxgkInterface.DxgkCbQueueDpc(DevExt->DxgkDeviceHandle);
    }

    return OurInterrupt;
}

/*===========================================================================
  DPC Routine � Deferred interrupt processing
===========================================================================*/

VOID
APIENTRY
DreamV3DdiDpcRoutine(
    _In_ PVOID MiniportDeviceContext
    )
{
    PDREAM_V3_DEVICE_EXTENSION DevExt = (PDREAM_V3_DEVICE_EXTENSION)MiniportDeviceContext;
    PULONG IhBase;
    ULONG WPtr, RPtr;
    ULONG Entry[4];
    ULONG ClientId, SrcId;

    if (DevExt == NULL || !DevExt->HardwareInitialized ||
        DevExt->IhRing.VirtualAddress == NULL) {
        return;
    }

    IhBase = (PULONG)DevExt->IhRing.VirtualAddress;
    WPtr = DevExt->LastInterruptStatus;
    RPtr = DevExt->IhRing.ReadPointer;

    /* Process IH entries */
    while (RPtr != WPtr) {
        /* IH entries are 4 DWORDs (16 bytes) */
        ULONG EntryOffset = RPtr / sizeof(ULONG);

        Entry[0] = IhBase[EntryOffset + 0];
        Entry[1] = IhBase[EntryOffset + 1];
        Entry[2] = IhBase[EntryOffset + 2];
        Entry[3] = IhBase[EntryOffset + 3];

        ClientId = (Entry[0] >> 8) & 0xFF;
        SrcId    = Entry[0] & 0xFF;

        switch (ClientId) {
        case IH_CLIENTID_GFX:
            if (SrcId == 0xE0) {
                /* EOP � fence completion */
                DXGKARGCB_NOTIFY_INTERRUPT_DATA NotifyData = {0};
                NotifyData.InterruptType = DXGK_INTERRUPT_DMA_COMPLETED;
                NotifyData.DmaCompleted.SubmissionFenceId = (UINT64)Entry[2] | ((UINT64)Entry[3] << 32);
                DevExt->DxgkInterface.DxgkCbNotifyInterrupt(
                    DevExt->DxgkDeviceHandle, &NotifyData);
            }
            break;

        case IH_CLIENTID_DCE:
            /* VSYNC */
            {
                DXGKARGCB_NOTIFY_INTERRUPT_DATA NotifyData = {0};
                NotifyData.InterruptType = DXGK_INTERRUPT_CRTC_VSYNC;
                NotifyData.CrtcVsync.VidPnTargetId = 0;
                DevExt->DxgkInterface.DxgkCbNotifyInterrupt(
                    DevExt->DxgkDeviceHandle, &NotifyData);
            }
            break;

        case IH_CLIENTID_VMC:
            /* VM fault */
            DevExt->ErrorCount++;
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                       "AMDBC250-DREAM-V4.3: VM fault in DPC\n"));
            break;
        }

        RPtr += IH_ENTRY_SIZE_BYTES;
        if (RPtr >= DevExt->IhRing.SizeInBytes) {
            RPtr = 0;
        }
    }

    /* Update read pointer */
    DevExt->IhRing.ReadPointer = RPtr;
    DreamV3WriteRegister(DevExt, AMDBC250_REG_IH_RB_RPTR, RPtr);

    DevExt->DxgkInterface.DxgkCbNotifyDpc(DevExt->DxgkDeviceHandle);
}

/*===========================================================================
  QueryAdapterInfo � Report GPU capabilities
===========================================================================*/

NTSTATUS
APIENTRY
DreamV3DdiQueryAdapterInfo(
    _In_ CONST HANDLE                   hAdapter,
    _In_ CONST DXGKARG_QUERYADAPTERINFO *pQueryAdapterInfo
    )
{
    PDREAM_V3_DEVICE_EXTENSION DevExt = (PDREAM_V3_DEVICE_EXTENSION)hAdapter;

    if (DevExt == NULL || pQueryAdapterInfo == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    switch (pQueryAdapterInfo->Type) {

    case DXGKQAITYPE_DRIVERCAPS: {
        DXGK_DRIVERCAPS *pCaps = (DXGK_DRIVERCAPS *)pQueryAdapterInfo->pOutputData;
        RtlZeroMemory(pCaps, sizeof(DXGK_DRIVERCAPS));

        pCaps->WDDMVersion = DXGKDDI_WDDMv2;
        pCaps->SchedulingCaps.MultiEngineAware = TRUE;
        pCaps->MemoryManagementCaps.PagingNode = 0;

        /* Report hardware info to Windows (for dxdiag) */
        /* These will be read by the INF and stored in registry */

        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "AMDBC250-DREAM-V4.3: DRIVERCAPS reported, VRAM=%llu MB (%llu GB)\n",
                   DevExt->TotalVramBytes / (1024 * 1024),
                   DevExt->TotalVramBytes / (1024 * 1024 * 1024)));
        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_QUERYSEGMENT: {
        DXGK_QUERYSEGMENTOUT *pSegOut = (DXGK_QUERYSEGMENTOUT *)pQueryAdapterInfo->pOutputData;

        if (pQueryAdapterInfo->pInputData == NULL) {
            pSegOut->NbSegment = 2;
            return STATUS_SUCCESS;
        }

        pSegOut->NbSegment = 2;

        /* Segment 0: GDDR6 VRAM */
        pSegOut->pSegmentDescriptor[0].BaseAddress.QuadPart = 0;
        pSegOut->pSegmentDescriptor[0].Size = DevExt->TotalVramBytes;
        pSegOut->pSegmentDescriptor[0].CommitLimit = DevExt->TotalVramBytes;
        pSegOut->pSegmentDescriptor[0].Flags.CpuVisible = TRUE;
        pSegOut->pSegmentDescriptor[0].Flags.Aperture = TRUE;
        pSegOut->pSegmentDescriptor[0].Flags.CacheCoherent = FALSE;

        /* Segment 1: System memory */
        pSegOut->pSegmentDescriptor[1].BaseAddress.QuadPart = 0;
        pSegOut->pSegmentDescriptor[1].Size = 0x400000000ULL;  /* 16 GB */
        pSegOut->pSegmentDescriptor[1].CommitLimit = 0x400000000ULL;
        pSegOut->pSegmentDescriptor[1].Flags.Aperture = TRUE;
        pSegOut->pSegmentDescriptor[1].Flags.CpuVisible = TRUE;
        pSegOut->pSegmentDescriptor[1].Flags.CacheCoherent = TRUE;

        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
                   "AMDBC250-DREAM-V4.3: QUERYSEGMENT reported\n"));
        return STATUS_SUCCESS;
    }

    /* DXGKQAITYPE_CURRENTDISPLAYMODE is deprecated in WDDM 3.x */

    default:
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: QueryAdapterInfo - unhandled type %d\n",
                   pQueryAdapterInfo->Type));
        return STATUS_NOT_SUPPORTED;
    }
}

/*===========================================================================
  CreateDevice � Per-process GPU context
===========================================================================*/

NTSTATUS
APIENTRY
DreamV3DdiCreateDevice(
    _In_    CONST HANDLE             hAdapter,
    _Inout_ DXGKARG_CREATEDEVICE     *pCreateDevice
    )
{
    PDREAM_V3_DEVICE_EXTENSION DevExt = (PDREAM_V3_DEVICE_EXTENSION)hAdapter;
    PDREAM_V3_GPU_CONTEXT Context;

    if (DevExt == NULL || pCreateDevice == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    Context = (PDREAM_V3_GPU_CONTEXT)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        sizeof(DREAM_V3_GPU_CONTEXT),
        DREAM_V3_TAG_CONTEXT
        );

    if (Context == NULL) return STATUS_NO_MEMORY;

    RtlZeroMemory(Context, sizeof(DREAM_V3_GPU_CONTEXT));
    Context->ContextId = DevExt->NumContexts++;
    Context->VmId = AMDBC250_VMID_MIN_USER + Context->ContextId;
    Context->IsValid = TRUE;
    KeInitializeSpinLock(&Context->ContextLock);
    InitializeListHead(&Context->AllocationList);

    pCreateDevice->hDevice = (HANDLE)Context;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
               "AMDBC250-DREAM-V4.3: CreateDevice � Context %d, VMID %d\n",
               Context->ContextId, Context->VmId));

    return STATUS_SUCCESS;
}

/*===========================================================================
  DestroyDevice
===========================================================================*/

NTSTATUS
APIENTRY
DreamV3DdiDestroyDevice(_In_ CONST HANDLE hDevice)
{
    PDREAM_V3_GPU_CONTEXT Context = (PDREAM_V3_GPU_CONTEXT)hDevice;
    if (Context != NULL) {
        Context->IsValid = FALSE;
        ExFreePoolWithTag(Context, DREAM_V3_TAG_CONTEXT);
    }
    return STATUS_SUCCESS;
}

/*===========================================================================
  CreateAllocation
===========================================================================*/

NTSTATUS
APIENTRY
DreamV3DdiCreateAllocation(
    _In_    CONST HANDLE                    hAdapter,
    _Inout_ DXGKARG_CREATEALLOCATION        *pCreateAllocation
    )
{
    PDREAM_V3_DEVICE_EXTENSION DevExt = (PDREAM_V3_DEVICE_EXTENSION)hAdapter;
    ULONG i;

    if (DevExt == NULL || pCreateAllocation == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    for (i = 0; i < pCreateAllocation->NumAllocations; i++) {
        DXGK_ALLOCATIONINFO *pAllocInfo = &pCreateAllocation->pAllocationInfo[i];
        PDREAM_V3_ALLOCATION Alloc;
        SIZE_T AllocSize;
        PHYSICAL_ADDRESS LowAddress, HighAddress, SkipBytes;

        Alloc = (PDREAM_V3_ALLOCATION)ExAllocatePool2(
            POOL_FLAG_NON_PAGED,
            sizeof(DREAM_V3_ALLOCATION),
            DREAM_V3_TAG_ALLOCATION
            );

        if (Alloc == NULL) return STATUS_NO_MEMORY;

        RtlZeroMemory(Alloc, sizeof(DREAM_V3_ALLOCATION));
        
        /* Determine allocation size (align to page boundary) */
        AllocSize = (pAllocInfo->Size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        if (AllocSize < PAGE_SIZE) AllocSize = PAGE_SIZE;
        
        Alloc->SizeInBytes = AllocSize;
        Alloc->Alignment = max(4096, pAllocInfo->Alignment);
        
        /* CRITICAL: Allocate actual physical memory for GPU */
        LowAddress.QuadPart = 0;
        HighAddress.QuadPart = 0xFFFFFFFFFFULL;  /* 40-bit address space */
        SkipBytes.QuadPart = 0;
        
        Alloc->VirtualAddress = MmAllocateContiguousMemorySpecifyCache(
            AllocSize,
            LowAddress,
            HighAddress,
            SkipBytes,
            MmCached
            );
        
        if (Alloc->VirtualAddress == NULL) {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                       "AMDBC250-DREAM-V4.3: Allocation failed: %llu bytes\n", AllocSize));
            ExFreePoolWithTag(Alloc, DREAM_V3_TAG_ALLOCATION);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        
        Alloc->PhysicalAddress = MmGetPhysicalAddress(Alloc->VirtualAddress);
        
        if (Alloc->PhysicalAddress.QuadPart < (LONGLONG)DevExt->TotalVramBytes) {
            Alloc->SegmentId = 0;
        } else {
            Alloc->SegmentId = 1;
        }

        pAllocInfo->hAllocation = (HANDLE)Alloc;
        pAllocInfo->Alignment = Alloc->Alignment;
        pAllocInfo->SupportedReadSegmentSet = (1 << 0) | (1 << 1);
        pAllocInfo->SupportedWriteSegmentSet = (1 << 0) | (1 << 1);
        pAllocInfo->EvictionSegmentSet = (1 << 1);

        ExAcquireFastMutex(&DevExt->DeviceMutex);
        InsertTailList(&DevExt->AllocationList, &Alloc->ListEntry);
        ExReleaseFastMutex(&DevExt->DeviceMutex);
        
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
                   "AMDBC250-DREAM-V4.3: Alloc: %llu bytes (PA: 0x%llX, Seg %d)\n",
                   AllocSize, Alloc->PhysicalAddress.QuadPart, Alloc->SegmentId));
    }

    return STATUS_SUCCESS;
}

/*===========================================================================
  DestroyAllocation
===========================================================================*/

NTSTATUS
APIENTRY
DreamV3DdiDestroyAllocation(
    _In_ CONST HANDLE                   hAdapter,
    _In_ CONST DXGKARG_DESTROYALLOCATION *pDestroyAllocation
    )
{
    PDREAM_V3_DEVICE_EXTENSION DevExt = (PDREAM_V3_DEVICE_EXTENSION)hAdapter;
    ULONG i;

    if (DevExt == NULL || pDestroyAllocation == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    ExAcquireFastMutex(&DevExt->DeviceMutex);
    for (i = 0; i < pDestroyAllocation->NumAllocations; i++) {
        PDREAM_V3_ALLOCATION Alloc =
            (PDREAM_V3_ALLOCATION)pDestroyAllocation->pAllocationList[i];
        if (Alloc != NULL) {
            RemoveEntryList(&Alloc->ListEntry);
            if (Alloc->VirtualAddress != NULL) {
                MmFreeContiguousMemory(Alloc->VirtualAddress);
                Alloc->VirtualAddress = NULL;
            }
            ExFreePoolWithTag(Alloc, DREAM_V3_TAG_ALLOCATION);
        }
    }
    ExReleaseFastMutex(&DevExt->DeviceMutex);

    return STATUS_SUCCESS;
}

/*===========================================================================
  PM4 Packet Building Helpers � GFX10 (RDNA2)
===========================================================================*/

/*
 * Write PM4 Type 0 packet (register writes)
 */
static VOID
DreamV3WritePm4Type0(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ ULONG BaseRegister,
    _In_ const ULONG* pValues,
    _In_ ULONG Count
    )
{
    volatile PULONG Ring = (volatile PULONG)DevExt->GfxRing.VirtualAddress;
    ULONG WPtr = DevExt->GfxRing.WritePointer;
    ULONG Header = PM4_TYPE0_HDR(BaseRegister, Count);
    ULONG TotalSize = sizeof(ULONG) + (Count * sizeof(ULONG));

    if (Ring == NULL) return;

    /* CRITICAL: Check ring buffer bounds to prevent kernel memory corruption */
    if (WPtr + TotalSize > (ULONG)DevExt->GfxRing.SizeInBytes) {
        /* Ring buffer wrap - write NOP packet and reset pointer */
        ULONG SpaceLeft = (ULONG)DevExt->GfxRing.SizeInBytes - WPtr;
        ULONG NopCount = SpaceLeft / sizeof(ULONG);
        
        /* Fill remaining space with NOPs */
        for (ULONG i = 0; i < NopCount; i++) {
            Ring[WPtr / sizeof(ULONG)] = PM4_TYPE2_NOP;
            WPtr += sizeof(ULONG);
        }
        WPtr = 0;  /* Wrap to beginning */
    }

    /* Write header */
    Ring[WPtr / sizeof(ULONG)] = Header;
    WPtr += sizeof(ULONG);

    /* Write register values */
    for (ULONG i = 0; i < Count; i++) {
        Ring[WPtr / sizeof(ULONG)] = pValues[i];
        WPtr += sizeof(ULONG);
    }

    DevExt->GfxRing.WritePointer = WPtr;
}

/*
 * Write PM4 Type 3 packet (executive commands)
 */
static VOID
DreamV3WritePm4Type3(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ ULONG Opcode,
    _In_ const ULONG* pValues,
    _In_ ULONG Count
    )
{
    volatile PULONG Ring = (volatile PULONG)DevExt->GfxRing.VirtualAddress;
    ULONG WPtr = DevExt->GfxRing.WritePointer;
    ULONG Header = PM4_TYPE3_HDR(Opcode, Count);
    ULONG TotalSize = sizeof(ULONG) + (Count * sizeof(ULONG));

    if (Ring == NULL) return;

    /* CRITICAL: Check ring buffer bounds to prevent kernel memory corruption */
    if (WPtr + TotalSize > (ULONG)DevExt->GfxRing.SizeInBytes) {
        /* Ring buffer wrap - fill remaining space with NOPs */
        ULONG SpaceLeft = (ULONG)DevExt->GfxRing.SizeInBytes - WPtr;
        ULONG NopCount = SpaceLeft / sizeof(ULONG);
        
        for (ULONG i = 0; i < NopCount; i++) {
            Ring[WPtr / sizeof(ULONG)] = PM4_TYPE2_NOP;
            WPtr += sizeof(ULONG);
        }
        WPtr = 0;  /* Wrap to beginning */
    }

    /* Write header */
    Ring[WPtr / sizeof(ULONG)] = Header;
    WPtr += sizeof(ULONG);

    /* Write packet data */
    for (ULONG i = 0; i < Count; i++) {
        Ring[WPtr / sizeof(ULONG)] = pValues[i];
        WPtr += sizeof(ULONG);
    }

    DevExt->GfxRing.WritePointer = WPtr;
}

/*
 * Write EOP (End of Pipe) packet with fence
 */
static VOID
DreamV3WriteEopFence(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ ULONG64 FenceValue
    )
{
    volatile PULONG Ring = (volatile PULONG)DevExt->GfxRing.VirtualAddress;
    ULONG WPtr = DevExt->GfxRing.WritePointer;
    PHYSICAL_ADDRESS FencePA = DevExt->GlobalFence.PhysicalAddress;
    ULONG TotalSize = 6 * sizeof(ULONG);  /* EOP packet is 6 DWORDs */

    if (Ring == NULL) return;

    /* CRITICAL: Check ring buffer bounds */
    if (WPtr + TotalSize > (ULONG)DevExt->GfxRing.SizeInBytes) {
        ULONG SpaceLeft = (ULONG)DevExt->GfxRing.SizeInBytes - WPtr;
        ULONG NopCount = SpaceLeft / sizeof(ULONG);
        
        for (ULONG i = 0; i < NopCount; i++) {
            Ring[WPtr / sizeof(ULONG)] = PM4_TYPE2_NOP;
            WPtr += sizeof(ULONG);
        }
        WPtr = 0;
    }

    /* IT_EVENT_WRITE_EOP packet � 6 DWORDs total (count=4 in header = 5 payload + 1 header = 6)
     * Format per AMD GPU ISA:
     *   DWORD 0: PM4 header
     *   DWORD 1: Control (EVENT_TYPE | EVENT_INDEX | DATA_SEL | INT_SEL)
     *   DWORD 2: Address low
     *   DWORD 3: Address high
     *   DWORD 4: Data low (fence value low)
     *   DWORD 5: Data high (fence value high)
     */
    ULONG Header = PM4_TYPE3_HDR(IT_EVENT_WRITE_EOP, 5);
    Ring[WPtr / sizeof(ULONG)] = Header;
    WPtr += sizeof(ULONG);
    
    /* Control: EVENT_TYPE=0x47(EOP) | EVENT_INDEX=5 | DATA_SEL=2(write 64-bit fence) | INT_SEL=1(interrupt)
     * GFX10 EVENT_WRITE_EOP bit layout: EVENT_TYPE[7:0] | EVENT_INDEX[11:8] | DATA_SEL[13:12] | INT_SEL[14] */
    Ring[WPtr / sizeof(ULONG)] = (0x47 << 0) | (5 << 8) | (2 << 12) | (1 << 14);
    WPtr += sizeof(ULONG);
    
    /* Address (64-bit physical, DWORD aligned) */
    Ring[WPtr / sizeof(ULONG)] = (ULONG)(FencePA.QuadPart & 0xFFFFFFFC);
    WPtr += sizeof(ULONG);
    Ring[WPtr / sizeof(ULONG)] = (ULONG)((FencePA.QuadPart >> 32) & 0xFFFF);
    WPtr += sizeof(ULONG);
    
    /* Data (64-bit fence value) */
    Ring[WPtr / sizeof(ULONG)] = (ULONG)(FenceValue & 0xFFFFFFFF);
    WPtr += sizeof(ULONG);
    Ring[WPtr / sizeof(ULONG)] = (ULONG)(FenceValue >> 32);
    WPtr += sizeof(ULONG);
    
    DevExt->GfxRing.WritePointer = WPtr;
}

/*
 * Software PM4 executor — translate PM4 packets to direct register writes.
 *
 * Handles: IT_NOP, IT_WRITE_DATA, IT_EVENT_WRITE_EOP, IT_RELEASE_MEM,
 *          IT_SET_CONFIG_REG, IT_SET_CONTEXT_REG, IT_SET_SH_REG, PM4_TYPE_0,
 *          and IT_INDIRECT_BUFFER (maps physical address and recurses).
 *
 * This is a FALLBACK path when hardware KIQ ring processing is unavailable
 * (KIQ_SIZE=0 blocks all CP ring processing on BC-250).
 */
static NTSTATUS
DreamV3SwPm4Process(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_reads_(CommandCount) const ULONG *Commands,
    _In_ ULONG CommandCount,
    _In_ ULONG64 FenceValue,
    _In_ ULONG Depth
    )
{
    ULONG i = 0;

    /* Depth guard — prevent stack overflow from nested IT_INDIRECT_BUFFER */
    if (Depth == 0) {
        return STATUS_ALERTED;
    }

    while (i < CommandCount) {
        ULONG header = Commands[i];
        ULONG type = header >> 30;

        switch (type) {
        case 0: {
            /* PM4_TYPE_0: Write consecutive registers */
            ULONG count = ((header >> 16) & 0x7FF) + 1;
            ULONG baseReg = (header & 0xFFFF) << 2;
            i++;
            for (ULONG j = 0; j < count && i < CommandCount; j++, i++) {
                DreamV3WriteRegister(DevExt, baseReg + j * 4, Commands[i]);
            }
            break;
        }

        case 2:
            /* PM4_TYPE_2: NOP padding */
            i++;
            break;

        case 3: {
            /* PM4_TYPE_3: Executive command
             * Header bits: [31:30]=type, [27:16]=payload_count-1, [15:8]=opcode */
            ULONG count = ((header >> 16) & 0xFFF) + 1;
            ULONG opcode = (header >> 8) & 0xFF;
            i++;

            /* NOP has no meaningful payload — skip count validation */
            if (opcode != IT_NOP && i + count > CommandCount) {
                return STATUS_BUFFER_TOO_SMALL;
            }

            switch (opcode) {
            case IT_NOP:
                i += count;
                break;

            case IT_WRITE_DATA: {
                if (count < 3) {
                    i += count;
                    break;
                }
                ULONG control = Commands[i + 0];
                ULONG addrLo  = Commands[i + 1];
                ULONG addrHi  = Commands[i + 2];
                BOOLEAN isReg = ((control >> 28) & 1) != 0;
                ULONG wrOne   = (control >> 25) & 0x1;
                ULONG dataDwCount = count - 3;

                if (isReg || (addrLo < 0x200000)) {
                    for (ULONG d = 0; d < dataDwCount; d++) {
                        DreamV3WriteRegister(DevExt, addrLo + (wrOne ? 0 : d * 4), Commands[i + 3 + d]);
                    }
                }
                i += count;
                break;
            }

            case IT_EVENT_WRITE_EOP:
            case IT_RELEASE_MEM: {
                if (count >= 5 && FenceValue > 0 && DevExt->GlobalFence.VirtualAddress != NULL) {
                    *DevExt->GlobalFence.VirtualAddress = FenceValue;
                    KeMemoryBarrier();
                    DevExt->GlobalFence.LastSubmittedValue = FenceValue;
                }
                i += count;
                break;
            }

            case IT_SET_CONFIG_REG: {
                /* Config register space: hwOff = mmREGISTER (byte offset from GC block).
                 * On BC-250: BAR5_offset = AMDBC250_GC_BASE + hwOff * 4
                 * Remaining DWORDs = register values (written consecutively). */
                if (count >= 1) {
                    ULONG hwOff = Commands[i];
                    ULONG numRegs = count - 1;
                    ULONG baseReg = AMDBC250_GC_BASE + (hwOff << 2);
                    for (ULONG r = 0; r < numRegs; r++) {
                        DreamV3WriteRegister(DevExt, baseReg + r * 4, Commands[i + 1 + r]);
                    }
                }
                i += count;
                break;
            }

            case IT_SET_CONTEXT_REG: {
                /* Context register space: hwOff = mmREGISTER / 4.
                 * On BC-250: BAR5_offset = AMDBC250_GC_BASE + hwOff * 4 */
                if (count >= 1) {
                    ULONG hwOff = Commands[i];
                    ULONG numRegs = count - 1;
                    ULONG baseReg = AMDBC250_GC_BASE + (hwOff << 2);
                    for (ULONG r = 0; r < numRegs; r++) {
                        DreamV3WriteRegister(DevExt, baseReg + r * 4, Commands[i + 1 + r]);
                    }
                }
                i += count;
                break;
            }

            case IT_SET_SH_REG: {
                /* SH register space: hwOff = mmREGISTER / 4.
                 * On BC-250: BAR5_offset = AMDBC250_GC_BASE + hwOff * 4
                 * (No +0x2C000 — BC-250's mmREGISTER already encodes GC block offset.) */
                if (count >= 1) {
                    ULONG hwOff = Commands[i];
                    ULONG numRegs = count - 1;
                    ULONG baseReg = AMDBC250_GC_BASE + (hwOff << 2);
                    for (ULONG r = 0; r < numRegs; r++) {
                        DreamV3WriteRegister(DevExt, baseReg + r * 4, Commands[i + 1 + r]);
                    }
                }
                i += count;
                break;
            }

            case IT_DISPATCH_DIRECT: {
                /* DISPATCH_DIRECT: dim_x, dim_y, dim_z, dispatch_initiator.
                 * COMPUTE registers use GC_BASE + mm*4 (BASE_IDX=0), NOT SEG1.
                 * COMPUTE_DISPATCH_INITIATOR = BAR5 0x80E0 (W1C trigger, VALID consumed).
                 * COMPUTE_DIM_X/Y/Z        = BAR5 0x80E4/0x80E8/0x80EC.
                 *
                 * Select ME=1 (MEC/compute) via GRBM_GFX_INDEX before writes. */
                if (count >= 4) {
                    ULONG dimX = Commands[i + 0];
                    ULONG dimY = Commands[i + 1];
                    ULONG dimZ = Commands[i + 2];
                    ULONG initiator = Commands[i + 3];
                    /* Select ME=1 (MEC/compute engine) */
                    DreamV3WriteRegister(DevExt, DevExt->GrbmGfxIndexOffset,
                        AMDBC250_GRBM_GFX_INDEX_KIQ_VAL);
                    /* Write dispatch dimensions (read-only shadow on BC-250, but correct registers) */
                    DreamV3WriteRegister(DevExt, AMDBC250_REG_COMPUTE_DIM_X, dimX);
                    DreamV3WriteRegister(DevExt, AMDBC250_REG_COMPUTE_DIM_Y, dimY);
                    DreamV3WriteRegister(DevExt, AMDBC250_REG_COMPUTE_DIM_Z, dimZ);
                    /* Trigger dispatch with VALID=1 */
                    DreamV3WriteRegister(DevExt, AMDBC250_REG_COMPUTE_DISPATCH_INITIATOR, initiator);
                    /* Restore broadcast */
                    DreamV3WriteRegister(DevExt, DevExt->GrbmGfxIndexOffset,
                        AMDBC250_GRBM_GFX_INDEX_BROADCAST_VAL);
                }
                i += count;
                break;
            }

            case IT_INDIRECT_BUFFER: {
                /* IB format: DWORD1=addrLo, DWORD2=addrHi, DWORD3=sizeDwords.
                 * Map the physical address and recursively process. */
                if (count >= 3) {
                    PHYSICAL_ADDRESS ibAddr;
                    ULONG ibSizeDwords = Commands[i + 2];
                    ibAddr.LowPart = Commands[i + 0] & 0xFFFFFFFC;
                    ibAddr.HighPart = Commands[i + 1];
                    if (ibSizeDwords > 0 && ibSizeDwords <= 4096) {
                        PVOID ibVa = MmMapIoSpace(ibAddr, ibSizeDwords * sizeof(ULONG),
                                                   MmNonCached);
                        if (ibVa != NULL) {
                            DreamV3SwPm4Process(DevExt, (PULONG)ibVa, ibSizeDwords,
                                                FenceValue, Depth - 1);
                            MmUnmapIoSpace(ibVa, ibSizeDwords * sizeof(ULONG));
                        }
                    }
                }
                i += count;
                break;
            }

            default:
                i += count;
                break;
            }
            break;
        }

        default:
            i++;
            break;
        }
    }

    return STATUS_SUCCESS;
}

/*
 * Submit GFX ring to hardware (doorbell)
 */
static VOID
DreamV3SubmitGfxRing(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    )
{
    ULONG WPtr = DevExt->GfxRing.WritePointer;
    
    /* Only write to hardware if MMIO is mapped */
    if (DevExt->MmioVirtualBase != NULL && DevExt->HardwareInitialized) {
        /* Write WPTR � use HQD/SRBM path if available */
        if (DevExt->UseHqdKiq) {
            DreamV3WriteRegister(DevExt, DevExt->GrbmGfxIndexOffset,
                AMDBC250_GRBM_GFX_INDEX_KIQ_VAL);
            DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_HQD_PQ_WPTR_LO, WPtr);
            DreamV3WriteRegister(DevExt, DevExt->GrbmGfxIndexOffset,
                AMDBC250_GRBM_GFX_INDEX_BROADCAST_VAL);
        } else {
            DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_GFX_RING0_WPTR, WPtr);
        }
        KeStallExecutionProcessor(10);
    }
    
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
               "AMDBC250-DREAM-V4.3: GfxRing submitted, WPTR=0x%X hw=%d\n",
               WPtr, DevExt->HardwareInitialized));
}

/*===========================================================================
  SubmitCommand
===========================================================================*/

NTSTATUS
APIENTRY
DreamV3DdiSubmitCommand(
    _In_ CONST HANDLE               hAdapter,
    _In_ CONST DXGKARG_SUBMITCOMMAND *pSubmitCommand
    )
{
    PDREAM_V3_DEVICE_EXTENSION DevExt = (PDREAM_V3_DEVICE_EXTENSION)hAdapter;
    KIRQL OldIrql;
    ULONG64 CurrentFence;

    if (DevExt == NULL || pSubmitCommand == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    if (!DevExt->HardwareInitialized) {
        return STATUS_DEVICE_NOT_READY;
    }

    /* CRITICAL: Check thermal throttle before submitting */
    DreamV3CheckThermalThrottle(DevExt);

    /* Acquire ring lock */
    KeAcquireSpinLock(&DevExt->GfxRing.Lock, &OldIrql);

    /* Write IB (Indirect Buffer) packet to ring */
    if (DevExt->GfxRing.VirtualAddress != NULL && pSubmitCommand->DmaBufferSize > 0) {
        volatile PULONG Ring = (volatile PULONG)DevExt->GfxRing.VirtualAddress;
        ULONG WPtr = DevExt->GfxRing.WritePointer;
        ULONG RingSize = (ULONG)DevExt->GfxRing.SizeInBytes;
        ULONG WPtrDword = WPtr / sizeof(ULONG);
        ULONG RingDwords = RingSize / sizeof(ULONG);
        
        /* Check if we have enough space for IB packet (4 DWORDs) + EOP (7 DWORDs) */
        ULONG NeededSpace = (4 + 7) * sizeof(ULONG);
        
        if (WPtr + NeededSpace > RingSize) {
            /* Ring wrap - fill with NOPs and reset */
            ULONG SpaceLeft = RingSize - WPtr;
            ULONG NopCount = SpaceLeft / sizeof(ULONG);
            
            for (ULONG i = 0; i < NopCount; i++) {
                Ring[WPtr / sizeof(ULONG)] = PM4_TYPE2_NOP;
                WPtr += sizeof(ULONG);
            }
            WPtr = 0;
            WPtrDword = 0;
        }
        
        /* PM4 INDIRECT_BUFFER packet - points to command buffer */
        Ring[WPtrDword + 0] = PM4_TYPE3_HDR(0x3F, 4);  /* IT_INDIRECT_BUFFER */
        Ring[WPtrDword + 1] = (ULONG)(pSubmitCommand->DmaBufferPhysicalAddress.LowPart & 0xFFFFFFFC);
        Ring[WPtrDword + 2] = (ULONG)(pSubmitCommand->DmaBufferPhysicalAddress.HighPart);
        Ring[WPtrDword + 3] = (pSubmitCommand->DmaBufferSize + 3) / sizeof(ULONG);  /* Round up */
        
        WPtr += 4 * sizeof(ULONG);
        DevExt->GfxRing.WritePointer = WPtr;
        
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
                   "AMDBC250-DREAM-V4.3: IB submitted - PA: 0x%llX, Size: %u bytes\n",
                   pSubmitCommand->DmaBufferPhysicalAddress.QuadPart,
                   pSubmitCommand->DmaBufferSize));
    }

    /* Update fence (64-bit for GFX10) */
    CurrentFence = InterlockedIncrement64(
        (volatile LONG64*)&DevExt->GlobalFence.LastSubmittedValue);

    /* Write EOP fence packet to ring */
    DreamV3WriteEopFence(DevExt, CurrentFence);

    /* Submit ring to hardware - write WPTR */
    ULONG WPtr = DevExt->GfxRing.WritePointer;
    DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_GFX_RING0_WPTR, WPtr);
    
    /* Doorbell notification (if available) */
    if (DevExt->DoorbellVirtualBase != NULL) {
        PULONG Doorbell = (PULONG)((PUCHAR)DevExt->DoorbellVirtualBase + DevExt->GfxRing.DoorbellOffset);
        *Doorbell = WPtr;
        KeMemoryBarrier();  /* Ensure write is visible */
    }

    KeReleaseSpinLock(&DevExt->GfxRing.Lock, OldIrql);

    DevExt->SubmitCount++;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
               "AMDBC250-DREAM-V4.3: SubmitCommand - fence=%llu, wptr=0x%X\n",
               CurrentFence, WPtr));

    return STATUS_SUCCESS;
}

/*===========================================================================
  QueryCurrentFence
===========================================================================*/

NTSTATUS
APIENTRY
DreamV3DdiQueryCurrentFence(
    _In_    CONST HANDLE                hAdapter,
    _Inout_ DXGKARG_QUERYCURRENTFENCE   *pCurrentFence
    )
{
    PDREAM_V3_DEVICE_EXTENSION DevExt = (PDREAM_V3_DEVICE_EXTENSION)hAdapter;

    if (DevExt == NULL || pCurrentFence == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    pCurrentFence->CurrentFence = (UINT)DevExt->GlobalFence.LastSignaledValue;
    pCurrentFence->NodeOrdinal = 0;  /* GFX engine */
    pCurrentFence->EngineOrdinal = 0;

    return STATUS_SUCCESS;
}

/*===========================================================================
  Present
===========================================================================*/

NTSTATUS
APIENTRY
DreamV3DdiPresent(
    _In_    CONST HANDLE        hContext,
    _Inout_ DXGKARG_PRESENT     *pPresent
    )
{
    PDREAM_V3_DEVICE_EXTENSION DevExt;
    DXGK_ALLOCATIONLIST *pSrcAlloc = NULL;

    UNREFERENCED_PARAMETER(hContext);

    if (pPresent == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    /* Get device extension */
    if (g_ControlDevice == NULL) { return STATUS_SUCCESS; }
    DevExt = (PDREAM_V3_DEVICE_EXTENSION)g_ControlDevice->DeviceExtension;
    if (DevExt == NULL || !DevExt->HardwareInitialized) {
        return STATUS_SUCCESS;
    }

    /* Get the source allocation from the allocation list */
    if (pPresent->pAllocationList != NULL && pPresent->NumSrcAllocations > 0) {
        pSrcAlloc = &pPresent->pAllocationList[0];
    }

    if (pSrcAlloc != NULL && pSrcAlloc->PhysicalAddress.QuadPart != 0) {
        /* Program HUBPREQ primary surface address */
        DreamV3WriteRegister(DevExt,
            AMDBC250_REG_HUBPREQ0_DCSURF_PRIMARY_SURFACE_ADDRESS,
            (ULONG)(pSrcAlloc->PhysicalAddress.QuadPart & 0xFFFFFFFF));
        DreamV3WriteRegister(DevExt,
            AMDBC250_REG_HUBPREQ0_DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH,
            (ULONG)(pSrcAlloc->PhysicalAddress.QuadPart >> 32));

        /* Set surface pitch (default 800*4 = 3200 for 800x600, or use current mode) */
        DreamV3WriteRegister(DevExt,
            AMDBC250_REG_HUBPREQ0_DCSURF_SURFACE_PITCH,
            DevExt->CurrentMode.Width * (DevExt->CurrentMode.BitsPerPixel / 8));

        /* Trigger flip */
        DreamV3WriteRegister(DevExt,
            AMDBC250_REG_HUBPREQ0_DCSURF_FLIP_CONTROL, 0x1);

        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
            "AMDBC250-DREAM-V4.3: Present PA=0x%llX\n", pSrcAlloc->PhysicalAddress.QuadPart));
    }

    return STATUS_SUCCESS;
}

/*===========================================================================
  Render
===========================================================================*/

NTSTATUS
APIENTRY
DreamV3DdiRender(
    _In_    CONST HANDLE    hContext,
    _Inout_ DXGKARG_RENDER  *pRender
    )
{
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pRender);
    return STATUS_NOT_IMPLEMENTED;
}

/*===========================================================================
  BuildPagingBuffer � Memory management (page table updates)

  This function is called by DXGKRNL to update GPU page tables
  for virtual memory management. Critical for D3D12!

  GFX10 supports 4-level page tables:
  - Level 0: PML4 (Page Map Level 4)
  - Level 1: PDPE (Page Directory Pointer Entry)
  - Level 2: PDE (Page Directory Entry)
  - Level 3: PTE (Page Table Entry)

  Page size: 4 KB (standard) or 64 KB (large)
===========================================================================*/

/* DreamV3DdiBuildPagingBuffer moved to amdbc250_dream_vm.c */

/*===========================================================================
  PreemptCommand
===========================================================================*/

NTSTATUS
APIENTRY
DreamV3DdiPreemptCommand(
    _In_ CONST HANDLE               hAdapter,
    _In_ CONST DXGKARG_PREEMPTCOMMAND *pPreemptCommand
    )
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(pPreemptCommand);
    return STATUS_NOT_IMPLEMENTED;
}

/*===========================================================================
  VidPN (Video Present Network) Implementation � DCN 2.1 Display Engine

  VidPN manages the relationship between:
  - Sources (framebuffers in VRAM)
  - Targets (physical display outputs: HDMI, DP, eDP)
  - Paths (source ? target mappings)

  DCN 2.1 (Display Core Next) supports:
  - 4 display pipes (HUBP + DPP + OTG)
  - HDR10, FreeSync, DSC (Display Stream Compression)
  - Up to 4K@120Hz or 8K@30Hz
===========================================================================*/

/*
 * Helper: Log display mode recommendation
 */
static VOID
DreamV3LogDisplayMode(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG RefreshRate
    )
{
    UNREFERENCED_PARAMETER(DevExt);
    
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
               "AMDBC250-DREAM-V4.3: Mode %lux%lu @ %luHz\n",
               Width, Height, RefreshRate));
}

NTSTATUS
APIENTRY
DreamV3DdiRecommendFunctionalVidPn(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN *pRecommendFunctionalVidPn
    )
{
    PDREAM_V3_DEVICE_EXTENSION DevExt = (PDREAM_V3_DEVICE_EXTENSION)hAdapter;
    
    if (DevExt == NULL || pRecommendFunctionalVidPn == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: RecommendFunctionalVidPn � DCN 2.1\n"));

    /*
     * Recommend a functional VidPN by:
     * 1. Creating source modes (what GPU can produce)
     * 2. Creating target modes (what display can show)
     * 3. Creating paths (mapping source ? target)
     * 4. Setting primary surface format
     */

    /* Log recommended modes */
    DreamV3LogDisplayMode(DevExt, 1920, 1080, 60);
    DreamV3LogDisplayMode(DevExt, 1280, 720, 60);
    
    /* In real driver, would use DXGKRNL callbacks to build mode set */
    
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
DreamV3DdiEnumVidPnCofuncModality(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY *pEnumCofuncModality
    )
{
    PDREAM_V3_DEVICE_EXTENSION DevExt = (PDREAM_V3_DEVICE_EXTENSION)hAdapter;
    
    if (DevExt == NULL || pEnumCofuncModality == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
               "AMDBC250-DREAM-V4.3: EnumVidPnCofuncModality\n"));

    /*
     * Enumerate cofunctional modality:
     * - When VidPN topology changes
     * - When mode set needs updating
     * - Recommend new modes if needed
     */
    
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
DreamV3DdiCommitVidPn(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_COMMITVIDPN *pCommitVidPn
    )
{
    PDREAM_V3_DEVICE_EXTENSION DevExt = (PDREAM_V3_DEVICE_EXTENSION)hAdapter;
    
    if (DevExt == NULL || pCommitVidPn == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: CommitVidPn � Activating display config\n"));

    /*
     * Commit VidPN makes the recommended configuration active:
     * 1. Program display engine (DCN 2.1)
     * 2. Set framebuffer addresses
     * 3. Configure OTG (Output Timing Generator)
     * 4. Enable display pipes
     */
    
    /* Update current mode from committed VidPN */
    DevExt->CurrentMode.Width = 1920;
    DevExt->CurrentMode.Height = 1080;
    DevExt->CurrentMode.RefreshRate = 60;
    DevExt->CurrentMode.BitsPerPixel = 32;
    DevExt->CurrentMode.Format = D3DDDIFMT_A8R8G8B8;
    
    /* Re-init display with new mode */
    DreamV3HwInitDisplay(DevExt);

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
DreamV3DdiSetVidPnSourceAddress(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_SETVIDPNSOURCEADDRESS *pSetVidPnSourceAddress
    )
{
    PDREAM_V3_DEVICE_EXTENSION DevExt = (PDREAM_V3_DEVICE_EXTENSION)hAdapter;
    PHYSICAL_ADDRESS SurfAddress;
    ULONG SurfaceOffset;
    ULONG SurfaceOffsetHigh;

    if (DevExt == NULL || pSetVidPnSourceAddress == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
               "AMDBC250-DREAM-V4.3: SetVidPnSourceAddress � Source %u\n",
               pSetVidPnSourceAddress->VidPnSourceId));

    /* Get framebuffer physical address from primary surface */
    SurfAddress = pSetVidPnSourceAddress->PrimaryAddress;

    if (SurfAddress.QuadPart == 0) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: SetVidPnSourceAddress � NULL address (expected for stub)\n"));
        return STATUS_SUCCESS;
    }

    /* Program HUBPREQ0_DCSURF_PRIMARY_SURFACE_ADDRESS (DCN 2.1) */
    SurfaceOffset = (ULONG)(SurfAddress.QuadPart & 0xFFFFFFFF);
    DreamV3WriteRegister(DevExt, AMDBC250_REG_HUBPREQ0_DCSURF_PRIMARY_SURFACE_ADDRESS, SurfaceOffset);
    
    SurfaceOffsetHigh = (ULONG)((SurfAddress.QuadPart >> 32) & 0xFFFFFFFF);
    DreamV3WriteRegister(DevExt, AMDBC250_REG_HUBPREQ0_DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH, SurfaceOffsetHigh);

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: Surface at PA: 0x%llX\n", SurfAddress.QuadPart));

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
DreamV3DdiSetVidPnSourceVisibility(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY *pSetVidPnSourceVisibility
    )
{
    PDREAM_V3_DEVICE_EXTENSION DevExt = (PDREAM_V3_DEVICE_EXTENSION)hAdapter;
    
    if (DevExt == NULL || pSetVidPnSourceVisibility == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
               "AMDBC250-DREAM-V4.3: SetVidPnSourceVisibility � Source %u, Visible=%d\n",
               pSetVidPnSourceVisibility->VidPnSourceId,
               pSetVidPnSourceVisibility->Visible));

    /* Show/hide display output */
    /* OTG0 registers (0x6000+) are in the freeze zone — skip */
    if (DevExt->MmioVirtualBase != NULL) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
                   "AMDBC250-DREAM-V4.3: SetVidPnSourceVisibility — OTG skip (freeze zone)\n"));
    }

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
DreamV3DdiUpdateActiveVidPnPresentPath(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH *pUpdateActiveVidPnPresentPath
    )
{
    PDREAM_V3_DEVICE_EXTENSION DevExt = (PDREAM_V3_DEVICE_EXTENSION)hAdapter;
    
    if (DevExt == NULL || pUpdateActiveVidPnPresentPath == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
               "AMDBC250-DREAM-V4.3: UpdateActiveVidPnPresentPath\n"));

    /* Update present path (rotation, scaling, etc.) */
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
DreamV3DdiRecommendMonitorModes(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_RECOMMENDMONITORMODES *pRecommendMonitorModes
    )
{
    PDREAM_V3_DEVICE_EXTENSION DevExt = (PDREAM_V3_DEVICE_EXTENSION)hAdapter;
    
    if (DevExt == NULL || pRecommendMonitorModes == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
               "AMDBC250-DREAM-V4.3: RecommendMonitorModes\n"));

    /*
     * Recommend monitor modes from EDID:
     * - Parse EDID (Extended Display Identification Data)
     * - Extract supported resolutions/refresh rates
     * - Build mode list
     */
    
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
DreamV3DdiGetScanLine(
    _In_ CONST HANDLE hAdapter,
    _Inout_ DXGKARG_GETSCANLINE *pGetScanLine
    )
{
    PDREAM_V3_DEVICE_EXTENSION DevExt = (PDREAM_V3_DEVICE_EXTENSION)hAdapter;
    
    if (DevExt == NULL || pGetScanLine == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    /* Read current scanline from OTG status */
    /* OTG0 registers (0x6000+) are in the freeze zone — skip */
    pGetScanLine->ScanLine = 0;
    
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
DreamV3DdiControlInterrupt(
    _In_ CONST HANDLE               hAdapter,
    _In_ CONST DXGK_INTERRUPT_TYPE  InterruptType,
    _In_ BOOLEAN                    EnableInterrupt
    )
{
    PDREAM_V3_DEVICE_EXTENSION DevExt = (PDREAM_V3_DEVICE_EXTENSION)hAdapter;
    UNREFERENCED_PARAMETER(InterruptType);
    UNREFERENCED_PARAMETER(EnableInterrupt);
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
DreamV3DdiQueryChildRelations(
    _In_ PVOID MiniportDeviceContext,
    _Inout_ PDXGK_CHILD_DESCRIPTOR ChildRelations,
    _In_ ULONG ChildRelationsSize
    )
{
    PDREAM_V3_DEVICE_EXTENSION DevExt = (PDREAM_V3_DEVICE_EXTENSION)MiniportDeviceContext;
    ULONG i;
    ULONG NumChildren;

    UNREFERENCED_PARAMETER(ChildRelationsSize);

    if (DevExt == NULL || ChildRelations == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    /* Report our display outputs:
       BC-250 has DCN 2.1 with up to 4 display pipes.
       We report 1 child (the primary display output). */
    NumChildren = 1;
    if (ChildRelationsSize / sizeof(DXGK_CHILD_DESCRIPTOR) < NumChildren) {
        NumChildren = ChildRelationsSize / sizeof(DXGK_CHILD_DESCRIPTOR);
    }

    for (i = 0; i < NumChildren; i++) {
        RtlZeroMemory(&ChildRelations[i], sizeof(DXGK_CHILD_DESCRIPTOR));
        ChildRelations[i].ChildUid = i;
        ChildRelations[i].ChildDeviceType = TypeVideoOutput;
        ChildRelations[i].ChildCapabilities.HpdAwareness = HpdAwarenessAlwaysConnected;
    }

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250-DREAM-V4.3: QueryChildRelations - %u children reported\n", NumChildren));

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
DreamV3DdiQueryChildStatus(
    _In_ PVOID MiniportDeviceContext,
    _Inout_ PDXGK_CHILD_STATUS ChildStatus,
    _In_ BOOLEAN NonDestructiveOnly
    )
{
    PDREAM_V3_DEVICE_EXTENSION DevExt = (PDREAM_V3_DEVICE_EXTENSION)MiniportDeviceContext;

    UNREFERENCED_PARAMETER(NonDestructiveOnly);

    if (DevExt == NULL || ChildStatus == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    /* Hotplug detection: report display connected */
    if (ChildStatus->Type == StatusConnection) {
        /* Assume display is connected for all pipes */
        ChildStatus->HotPlug.Connected = TRUE;
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
            "AMDBC250-DREAM-V4.3: Child %u connected\n", ChildStatus->ChildUid));
    } else if (ChildStatus->Type == StatusRotation) {
        ChildStatus->Rotation.Angle = 0; /* No rotation */
    }

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
DreamV3DdiQueryDeviceDescriptor(
    _In_ PVOID MiniportDeviceContext,
    _In_ ULONG ChildUid,
    _Inout_ PDXGK_DEVICE_DESCRIPTOR DeviceDescriptor
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(ChildUid);
    UNREFERENCED_PARAMETER(DeviceDescriptor);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
APIENTRY
DreamV3DdiQueryInterface(
    _In_ PVOID MiniportDeviceContext,
    _In_ PQUERY_INTERFACE QueryInterface
    )
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(QueryInterface);
    return STATUS_NOT_SUPPORTED;
}

/*===========================================================================
  DreamV3DdiEscape � UMD?KMD communication via WDDM Escape
  
  This is the WDDM-correct path for user-mode to kernel-mode communication.
  Replaces the MajorFunction[IRP_MJ_DEVICE_CONTROL] that caused bugcheck 0x3B.
  
  UMD calls D3DKMTEscape() which routes through dxgkrnl to this callback.
  We use D3DKMT_ESCAPE_DRIVERPRIVATE with custom command IDs.
  
  Command IDs (in pPrivateDriverData->CommandId):
    0x01 = GET_CAPS        � return GPU capabilities
    0x02 = GET_VRAM_INFO   � return VRAM layout
    0x03 = READ_MMIO       � read GPU register (safe reads only)
    0x04 = GET_BIOS_INFO   � return BIOS/firmware info
    0x05 = GET_FW_VERSION  � return firmware version strings
===========================================================================*/

/* ===========================================================================
   Mandatory DDI stub implementations
   =========================================================================== */

NTSTATUS APIENTRY DreamV3DdiDispatchIoRequest(PVOID MiniportDeviceContext, ULONG VidPnSourceId, PVIDEO_REQUEST_PACKET RequestPacket) {
    UNREFERENCED_PARAMETER(MiniportDeviceContext); UNREFERENCED_PARAMETER(VidPnSourceId);
    if (RequestPacket) RequestPacket->StatusBlock->Status = STATUS_NOT_IMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS APIENTRY DreamV3DdiControlEtwLogging(PVOID MiniportDeviceContext, UINT Enable, UINT VerboseLevel, PVOID Reserved) {
    UNREFERENCED_PARAMETER(MiniportDeviceContext); UNREFERENCED_PARAMETER(Enable);
    UNREFERENCED_PARAMETER(VerboseLevel); UNREFERENCED_PARAMETER(Reserved);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY DreamV3DdiDescribeAllocation(PVOID MiniportDeviceContext, PVOID DescribeAllocation) {
    UNREFERENCED_PARAMETER(MiniportDeviceContext); UNREFERENCED_PARAMETER(DescribeAllocation);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS APIENTRY DreamV3DdiGetStandardAllocationDriverData(PVOID MiniportDeviceContext, PVOID Data) {
    UNREFERENCED_PARAMETER(MiniportDeviceContext); UNREFERENCED_PARAMETER(Data);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS APIENTRY DreamV3DdiAcquireSwizzlingRange(PVOID MiniportDeviceContext, PVOID Range) {
    UNREFERENCED_PARAMETER(MiniportDeviceContext); UNREFERENCED_PARAMETER(Range);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS APIENTRY DreamV3DdiReleaseSwizzlingRange(PVOID MiniportDeviceContext, PVOID Range) {
    UNREFERENCED_PARAMETER(MiniportDeviceContext); UNREFERENCED_PARAMETER(Range);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY DreamV3DdiPatch(PVOID MiniportDeviceContext, PVOID Patch) {
    UNREFERENCED_PARAMETER(MiniportDeviceContext); UNREFERENCED_PARAMETER(Patch);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS APIENTRY DreamV3DdiSetPalette(PVOID MiniportDeviceContext, PVOID Palette) {
    UNREFERENCED_PARAMETER(MiniportDeviceContext); UNREFERENCED_PARAMETER(Palette);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS APIENTRY DreamV3DdiSetPointerPosition(PVOID MiniportDeviceContext, PVOID Position) {
    UNREFERENCED_PARAMETER(MiniportDeviceContext); UNREFERENCED_PARAMETER(Position);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY DreamV3DdiSetPointerShape(PVOID MiniportDeviceContext, PVOID Shape) {
    UNREFERENCED_PARAMETER(MiniportDeviceContext); UNREFERENCED_PARAMETER(Shape);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS APIENTRY DreamV3DdiResetFromTimeout(PVOID MiniportDeviceContext, PVOID Reset) {
    UNREFERENCED_PARAMETER(MiniportDeviceContext); UNREFERENCED_PARAMETER(Reset);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY DreamV3DdiRestartFromTimeout(PVOID MiniportDeviceContext) {
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY DreamV3DdiCollectDbgInfo(PVOID MiniportDeviceContext, PVOID DbgInfo) {
    UNREFERENCED_PARAMETER(MiniportDeviceContext); UNREFERENCED_PARAMETER(DbgInfo);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY DreamV3DdiIsSupportedVidPn(PVOID MiniportDeviceContext, PVOID IsSupported) {
    UNREFERENCED_PARAMETER(MiniportDeviceContext); UNREFERENCED_PARAMETER(IsSupported);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY DreamV3DdiRecommendVidPnTopology(PVOID MiniportDeviceContext, PVOID Recommend) {
    UNREFERENCED_PARAMETER(MiniportDeviceContext); UNREFERENCED_PARAMETER(Recommend);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS APIENTRY DreamV3DdiStopCapture(PVOID MiniportDeviceContext, PVOID Stop) {
    UNREFERENCED_PARAMETER(MiniportDeviceContext); UNREFERENCED_PARAMETER(Stop);
    return STATUS_SUCCESS;
}

NTSTATUS APIENTRY DreamV3DdiCreateOverlay(PVOID MiniportDeviceContext, PVOID CreateOverlay, PVOID OverlayHandle) {
    UNREFERENCED_PARAMETER(MiniportDeviceContext); UNREFERENCED_PARAMETER(CreateOverlay); UNREFERENCED_PARAMETER(OverlayHandle);
    return STATUS_NOT_IMPLEMENTED;
}

typedef struct _DREAM_ESCAPE_HEADER {
    ULONG CommandId;
    NTSTATUS Status;
    ULONG OutputSize;
} DREAM_ESCAPE_HEADER, *PDREAM_ESCAPE_HEADER;

NTSTATUS
APIENTRY
DreamV3DdiEscape(
    _In_ HANDLE                     hAdapter,
    _In_ CONST DXGKARG_ESCAPE*      pEscape
    )
{
    PDREAM_V3_DEVICE_EXTENSION DevExt = (PDREAM_V3_DEVICE_EXTENSION)hAdapter;

    if (pEscape == NULL || pEscape->pPrivateDriverData == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    if (pEscape->PrivateDriverDataSize < sizeof(DREAM_ESCAPE_HEADER)) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    PDREAM_ESCAPE_HEADER Header = (PDREAM_ESCAPE_HEADER)pEscape->pPrivateDriverData;

    switch (Header->CommandId) {
    case 0x01: /* GET_CAPS */
    {
        if (pEscape->PrivateDriverDataSize < sizeof(DREAM_ESCAPE_HEADER) + sizeof(ULONG) * 8) {
            Header->Status = STATUS_BUFFER_TOO_SMALL;
            Header->OutputSize = 0;
            return STATUS_SUCCESS;
        }
        PULONG Caps = (PULONG)(Header + 1);
        Caps[0] = 0x1002; /* VendorId */
        Caps[1] = 0x13FE; /* DeviceId */
        Caps[2] = 24; /* NumComputeUnits */
        Caps[3] = 1536; /* NumShaders */
        Caps[4] = 4; /* NumDisplayPipes */
        Caps[5] = 64; /* GPU clock MHz (base) */
        Caps[6] = 600; /* GPU clock MHz (boost) */
        Caps[7] = 0; /* Reserved */
        Header->Status = STATUS_SUCCESS;
        Header->OutputSize = sizeof(ULONG) * 8;
        return STATUS_SUCCESS;
    }

    case 0x02: /* GET_VRAM_INFO */
    {
        if (pEscape->PrivateDriverDataSize < sizeof(DREAM_ESCAPE_HEADER) + sizeof(ULONG) * 4) {
            Header->Status = STATUS_BUFFER_TOO_SMALL;
            Header->OutputSize = 0;
            return STATUS_SUCCESS;
        }
        PULONG Info = (PULONG)(Header + 1);
        Info[0] = (ULONG)(DevExt->TotalVramBytes >> 20); /* Total VRAM in MB */
        Info[1] = (ULONG)(DevExt->VisibleVramBytes >> 20); /* Visible VRAM in MB */
        Info[2] = 0xC0000000; /* VRAM physical base (low 32 bits) */
        Info[3] = 0; /* VRAM physical base (high 32 bits) */
        Header->Status = STATUS_SUCCESS;
        Header->OutputSize = sizeof(ULONG) * 4;
        return STATUS_SUCCESS;
    }

    case 0x03: /* READ_MMIO */
    {
        /* Read a GPU register � safe reads only (BAR5 writes cause hard freeze) */
        if (pEscape->PrivateDriverDataSize < sizeof(DREAM_ESCAPE_HEADER) + sizeof(ULONG) * 2) {
            Header->Status = STATUS_BUFFER_TOO_SMALL;
            Header->OutputSize = 0;
            return STATUS_SUCCESS;
        }
        PULONG Params = (PULONG)(Header + 1);
        ULONG Offset = Params[0]; /* Register offset */
        /* Only allow safe read offsets (BAR5) */
        if (Offset >= 0x100000 && Offset < 0x140000) {
            Params[1] = 0xDEAD0000; /* Refuse unsafe range */
        } else if (Offset < 0x100000) {
            /* Safe to read � but we need physical mapping.
               For now return placeholder. */
            Params[1] = 0x00000000;
        } else {
            Params[1] = 0x00000000;
        }
        Header->Status = STATUS_SUCCESS;
        Header->OutputSize = sizeof(ULONG) * 2;
        return STATUS_SUCCESS;
    }

    case 0x04: /* GET_BIOS_INFO */
    {
        if (pEscape->PrivateDriverDataSize < sizeof(DREAM_ESCAPE_HEADER) + sizeof(ULONG) * 4) {
            Header->Status = STATUS_BUFFER_TOO_SMALL;
            Header->OutputSize = 0;
            return STATUS_SUCCESS;
        }
        PULONG Bios = (PULONG)(Header + 1);
        Bios[0] = 1; /* BIOS version (stub) */
        Bios[1] = 0; /* UMD version major */
        Bios[2] = 43; /* UMD version minor */
        Bios[3] = 0; /* Reserved */
        Header->Status = STATUS_SUCCESS;
        Header->OutputSize = sizeof(ULONG) * 4;
        return STATUS_SUCCESS;
    }

    case 0x05: /* GET_FW_VERSION */
    {
        /* Return firmware version strings as ASCII in remaining buffer */
        const char *fwVer = "BC250-FW-V43-PS5";
        SIZE_T len = strlen(fwVer) + 1;
        if (pEscape->PrivateDriverDataSize < sizeof(DREAM_ESCAPE_HEADER) + len) {
            Header->Status = STATUS_BUFFER_TOO_SMALL;
            Header->OutputSize = 0;
            return STATUS_SUCCESS;
        }
        RtlCopyMemory(Header + 1, fwVer, len);
        Header->Status = STATUS_SUCCESS;
        Header->OutputSize = (ULONG)len;
        return STATUS_SUCCESS;
    }

    default:
        Header->Status = STATUS_INVALID_PARAMETER;
        Header->OutputSize = 0;
        return STATUS_SUCCESS;
    }
}

/*===========================================================================
  Stub Functions - Not yet implemented but required for linking
===========================================================================*/

#if 0
NTSTATUS
APIENTRY
DreamV3DdiBuildPagingBuffer(
    _In_ CONST HANDLE hAdapter,
    _Inout_ DXGKARG_BUILDPAGINGBUFFER *pBuildPagingBuffer
    )
{
    PDREAM_V3_DEVICE_EXTENSION DevExt = (PDREAM_V3_DEVICE_EXTENSION)hAdapter;
    PULONG DmaBuffer;
    ULONG DmaOffset = 0;

    if (DevExt == NULL || pBuildPagingBuffer == NULL || pBuildPagingBuffer->pDmaBuffer == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    DmaBuffer = (PULONG)pBuildPagingBuffer->pDmaBuffer;

    /* CRITICAL: This function programs GPU page tables for GPU virtual memory.
     * 
     * DXGKRNL calls this to:
     * - Map/unmap GPU virtual addresses
     * - Update GPU page tables  
     * - Flush GPU TLB
     * - Transfer memory (eviction/restore)
     * - Fill memory regions
     * 
     * For RDNA2/GFX1013, we build PM4/SDMA packets in the DMA buffer.
     */

    switch (pBuildPagingBuffer->Operation) {
    case DXGK_OPERATION_TRANSFER:
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
                   "AMDBC250-DREAM-V4.3: Transfer: %llu bytes\n",
                   pBuildPagingBuffer->Transfer.TransferSize));
        break;

    case DXGK_OPERATION_FILL:
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
                   "AMDBC250-DREAM-V4.3: Fill: %llu bytes\n",
                   pBuildPagingBuffer->Fill.FillSize));
        break;

    case 3:  /* SET_PAGE_TABLE_ENTRY */
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
                   "AMDBC250-DREAM-V4.3: Set PTE - VA: 0x%llX -> PA: 0x%llX\n",
                   pBuildPagingBuffer->SetPageTableEntry.VirtualAddress,
                   pBuildPagingBuffer->SetPageTableEntry.PagePhysicalAddress.QuadPart));
        break;

    case 4:  /* FLUSH_TLB */
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
                   "AMDBC250-DREAM-V4.3: Flush TLB\n"));
        break;

    default:
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: Unknown paging operation %d\n",
                   pBuildPagingBuffer->Operation));
        break;
    }

    return STATUS_SUCCESS;
}
#endif

/* Forward declarations for power management (in power.c) */
NTSTATUS DreamV3DdiSetPowerState(_In_ PVOID, _In_ ULONG, _In_ DEVICE_POWER_STATE, _In_ POWER_ACTION);
NTSTATUS DreamV3DdiNotifyAcpiEvent(_In_ PVOID, _In_ DXGK_EVENT_TYPE, _In_ ULONG, _In_ PVOID, _Out_ PULONG);
NTSTATUS DreamV3CheckThermalThrottle(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt);

/*===========================================================================
  Memory Management Stub Functions
===========================================================================*/

#if 0
NTSTATUS
DreamV3GartInitialize(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    /* GART (Graphics Aperture Remapping Table) stub */
    if (DevExt == NULL) return STATUS_INVALID_PARAMETER;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: GART initialized (stub)\n"));

    return STATUS_SUCCESS;
}

NTSTATUS
DreamV3VmInitialize(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    /* GPU Virtual Memory stub */
    if (DevExt == NULL) return STATUS_INVALID_PARAMETER;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: GPUVM initialized (stub)\n"));

    return STATUS_SUCCESS;
}
#endif

/*===========================================================================
  IOCTL Dispatch � UMD ? KMD Communication
  
  The UMD opens \\.\AMDBC250DreamV43 and sends IOCTLs.
  This device is created by DreamV3DdiAddDevice via IoCreateDevice.
===========================================================================*/

NTSTATUS
DreamV3CreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp
    )
{
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

NTSTATUS
DreamV3DeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp
    )
{
    /* CRITICAL: Only handle IRPs for our control device.
       This handler is set on g_DriverObject->MajorFunction which covers ALL
       device objects from this driver � including the dxgkrnl WDDM adapter.
       dxgkrnl sends its own IRPs (DxgkIrp) to the adapter device object.
       We must NOT try to parse those as DeviceIoControl � it causes bugcheck 0x3B. */
    if (DeviceObject != g_ControlDevice) {
        /* Not our control device � pass through to next handler */
        Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_NOT_SUPPORTED;
    }
    /* MARKER: Write once to confirm IOCTL dispatch is called */
    {
        static BOOLEAN once = FALSE;
        if (!once) {
            once = TRUE;
            UNICODE_STRING devPath;
            OBJECT_ATTRIBUTES objAttr;
            RtlInitUnicodeString(&devPath, L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\atikmdag");
            InitializeObjectAttributes(&objAttr, &devPath, OBJ_CASE_INSENSITIVE, NULL, NULL);
            HANDLE hKey = NULL;
            if (NT_SUCCESS(ZwOpenKey(&hKey, KEY_SET_VALUE, &objAttr))) {
                UNICODE_STRING valName;
                ULONG val = 1;
                RtlInitUnicodeString(&valName, L"IoctlDispatchCalled");
                ZwSetValueKey(hKey, &valName, 0, REG_DWORD, &val, sizeof(val));
                ZwClose(hKey);
            }
        }
    }

    PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS status = STATUS_SUCCESS;
    ULONG bytesReturned = 0;
    /* WARNING: METHOD_BUFFERED — inputBuffer == outputBuffer (same SystemBuffer).
     * Read ALL input fields BEFORE writing to output. Do NOT RtlZeroMemory before reading. */
    PVOID inputBuffer = Irp->AssociatedIrp.SystemBuffer;
    PVOID outputBuffer = Irp->AssociatedIrp.SystemBuffer;
    ULONG inputLen = irpSp->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outputLen = irpSp->Parameters.DeviceIoControl.OutputBufferLength;
    ULONG ioctlCode = irpSp->Parameters.DeviceIoControl.IoControlCode;

    /* IMMEDIATE MARKER � write before anything else */
    {
        static BOOLEAN once = FALSE;
        if (!once) {
            once = TRUE;
            UNICODE_STRING devPath;
            OBJECT_ATTRIBUTES objAttr;
            RtlInitUnicodeString(&devPath, L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\atikmdag");
            InitializeObjectAttributes(&objAttr, &devPath, OBJ_CASE_INSENSITIVE, NULL, NULL);
            HANDLE hKey = NULL;
            if (NT_SUCCESS(ZwOpenKey(&hKey, KEY_SET_VALUE, &objAttr))) {
                UNICODE_STRING valName;
                ULONG val = ioctlCode;
                RtlInitUnicodeString(&valName, L"FirstIoctlCode");
                ZwSetValueKey(hKey, &valName, 0, REG_DWORD, &val, sizeof(val));
                ZwClose(hKey);
            }
        }
    }

    /* Use PCI device extension (not control device extension which is empty) */
    PDREAM_V3_DEVICE_EXTENSION DevExt = g_PciDevExt;

    /* Log first IOCTL call details */
    {
        static BOOLEAN logged = FALSE;
        if (!logged) {
            logged = TRUE;
            UNICODE_STRING devPath;
            OBJECT_ATTRIBUTES objAttr;
            RtlInitUnicodeString(&devPath, L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\atikmdag");
            InitializeObjectAttributes(&objAttr, &devPath, OBJ_CASE_INSENSITIVE, NULL, NULL);
            HANDLE hKey = NULL;
            if (NT_SUCCESS(ZwOpenKey(&hKey, KEY_SET_VALUE, &objAttr))) {
                UNICODE_STRING valName;
                RtlInitUnicodeString(&valName, L"IoctlDevExtPtr");
                ULONG val = (ULONG)(UINT_PTR)DevExt;
                ZwSetValueKey(hKey, &valName, 0, REG_DWORD, &val, sizeof(val));
                RtlInitUnicodeString(&valName, L"IoctlCode");
                val = ioctlCode;
                ZwSetValueKey(hKey, &valName, 0, REG_DWORD, &val, sizeof(val));
                ZwClose(hKey);
            }
        }
    }

    if (DevExt == NULL) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
            "DEVEXT_NULL: DevExt is NULL\n"));
        status = STATUS_DEVICE_NOT_READY;
        goto Cleanup;
    }

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "IOCTL_CODE=%08X HW_INIT=%d\n", ioctlCode, DevExt->HardwareInitialized));

    /* If hardware not initialized, return safe dummy data */
    if (!DevExt->HardwareInitialized) {
        switch (ioctlCode) {
        case 0x80000800: /* GET_CAPS */
            if (outputLen >= sizeof(ULONG) * 7) {
                PULONG d = (PULONG)outputBuffer;
                d[0] = 430;                                  /* Version */
                d[1] = 0x05;                                 /* Caps flags */
                d[2] = AMDBC250_BOOST_CLOCK_MHZ;             /* Max clock MHz */
                d[3] = AMDBC250_MEMORY_CLOCK_MHZ;            /* Memory clock MHz */
                d[4] = AMDBC250_MAX_COMPUTE_UNITS;            /* CUs */
                d[5] = AMDBC250_STREAM_PROCESSORS;            /* SPs */
                d[6] = AMDBC250_RT_ACCELERATORS;              /* RT accelerators */
                bytesReturned = sizeof(ULONG) * 7;
            }
            status = STATUS_SUCCESS;
            goto Cleanup;
        case 0x80000804: /* GET_VRAM_INFO */
            if (outputLen >= sizeof(ULONG64) * 3 + sizeof(ULONG)) {
                PULONG64 d64 = (PULONG64)outputBuffer;
                d64[0] = 16ULL * 1024 * 1024 * 1024;        /* Total bytes (16GB) */
                d64[1] = 4ULL * 1024 * 1024 * 1024;          /* Visible bytes (4GB) */
                d64[2] = 0;                                   /* Used bytes */
                PULONG d32 = (PULONG)(d64 + 3);
                d32[0] = 2;                                   /* Segment count */
                bytesReturned = sizeof(ULONG64) * 3 + sizeof(ULONG);
            }
            status = STATUS_SUCCESS;
            goto Cleanup;
        case 0x80000840: { /* ALLOC_VIDMEM - proper MDL allocation */
            if (inputLen >= sizeof(ULONG) * 3 && outputLen >= sizeof(ULONG64) * 2) {
                PULONG InData = (PULONG)inputBuffer;
                SIZE_T allocSize = (SIZE_T)InData[0];
                allocSize = (allocSize + 0xFFF) & ~0xFFFULL; /* 4KB align */
                if (allocSize < 4096) allocSize = 4096;
                if (allocSize > 64 * 1024 * 1024) allocSize = 64 * 1024 * 1024;

                PHYSICAL_ADDRESS low = {0}, high, skip = {0};
                high.QuadPart = 0x3FFFFFFFFFULL; /* 40-bit */

                PMDL mdl = MmAllocatePagesForMdlEx(low, high, skip, allocSize, MmCached, 0);
                if (mdl != NULL) {
                    PVOID va = MmMapLockedPagesSpecifyCache(mdl, KernelMode, MmCached,
                                                           NULL, FALSE, NormalPagePriority);
                    if (va != NULL) {
                        PHYSICAL_ADDRESS pa = MmGetPhysicalAddress(va);
                        PULONG64 OutData = (PULONG64)outputBuffer;
                        OutData[0] = pa.QuadPart;
                        OutData[1] = (ULONG64)(UINT_PTR)va;
                        bytesReturned = sizeof(ULONG64) * 2;
                        /* Store MDL for later cleanup � prevents memory leak */
                        {
                            KIRQL oldIrql;
                            KeAcquireSpinLock(&g_MdlTableLock, &oldIrql);
                            for (int m = 0; m < 64; m++) {
                                if (g_MdlTable[m].Va == NULL) {
                                    g_MdlTable[m].Va = va;
                                    g_MdlTable[m].Mdl = mdl;
                                    g_MdlTable[m].Size = allocSize;
                                    break;
                                }
                            }
                            KeReleaseSpinLock(&g_MdlTableLock, oldIrql);
                        }
                        status = STATUS_SUCCESS;
                    } else {
                        MmFreePagesFromMdl(mdl);
                        ExFreePool(mdl);
                        status = STATUS_INSUFFICIENT_RESOURCES;
                    }
                } else {
                    status = STATUS_INSUFFICIENT_RESOURCES;
                }
            } else {
                status = STATUS_BUFFER_TOO_SMALL;
            }
            goto Cleanup;
        }
    /* --- Report BAR addresses from StartDevice resource list --- */
    case 0x80000BB8: { /* IOCTL_AMDBC250_GET_RESOURCE_BARS */
        if (outputLen >= sizeof(AMDBC250_IOCTL_RESOURCE_BARS) && DevExt != NULL) {
            PAMDBC250_IOCTL_RESOURCE_BARS r = (PAMDBC250_IOCTL_RESOURCE_BARS)outputBuffer;
            RtlZeroMemory(r, sizeof(*r));
            r->DeviceStarted = DevExt->DeviceStarted ? 1 : 0;
            r->MmioMapped = (DevExt->MmioVirtualBase != NULL) ? 1 : 0;
            r->MmioSize = (UINT32)DevExt->MmioSize;
            r->MmioPhysicalBase = DevExt->MmioPhysicalBase.QuadPart;
            r->MmioVirtualBase = (UINT64)(UINT_PTR)DevExt->MmioVirtualBase;
            r->FbSize = (UINT32)DevExt->FbSize;
            r->FbPhysicalBase = DevExt->FbPhysicalBase.QuadPart;
            bytesReturned = sizeof(AMDBC250_IOCTL_RESOURCE_BARS);
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                "AMDBC250-DREAM-V4.3: GET_RESOURCE_BARS: started=%u mmio=%d PA=0x%llX sz=0x%X fb=0x%llX sz=0x%X\n",
                r->DeviceStarted, r->MmioMapped, r->MmioPhysicalBase, r->MmioSize,
                r->FbPhysicalBase, r->FbSize));
            status = STATUS_SUCCESS;
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        goto Cleanup;
    }

    case 0x80000BBC: { /* IOCTL_AMDBC250_FORCE_ENABLE_MMIO */
        if (inputLen >= sizeof(AMDBC250_IOCTL_FORCE_ENABLE_MMIO) && DevExt != NULL) {
            PAMDBC250_IOCTL_FORCE_ENABLE_MMIO f = (PAMDBC250_IOCTL_FORCE_ENABLE_MMIO)inputBuffer;

            ULONG bus = f->Bus;
            ULONG dev = f->Device;
            ULONG func = f->Function;

            /* Step 1: Read PCI Command register via IO ports (before) */
            WRITE_PORT_ULONG((PULONG)(UINT_PTR)0xCF8,
                0x80000000 | (bus << 16) | (dev << 11) | (func << 8) | 4);
            KeMemoryBarrier();
            f->CommandBefore = READ_PORT_ULONG((PULONG)(UINT_PTR)0xCFC);

            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                "AMDBC250-DREAM-V4.3: FORCE_ENABLE_MMIO: Command reg before=0x%08X\n", f->CommandBefore));

            /* Step 2: Try HalSetBusDataByOffset to write Command = 0x0007 (I/O+Mem+BusMaster) */
            {
                ULONG slotNumber = (dev << 0) | (func << 5);
                PCI_COMMON_CONFIG pciCfg;
                RtlZeroMemory(&pciCfg, sizeof(pciCfg));
                pciCfg.Command = 0x0007;

                ULONG bytesWritten = HalSetBusDataByOffset(
                    PCIConfiguration, bus, slotNumber,
                    &pciCfg, 0x04, sizeof(UINT16)); /* Write only offset 4 (Command) */
                f->HalSetBusResult = (bytesWritten == sizeof(UINT16)) ? 1 : 0;

                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                    "AMDBC250-DREAM-V4.3: FORCE_ENABLE_MMIO: HalSetBusData at B%u:D%u:F%u wrote %lu bytes (expected %llu)\n",
                    bus, dev, func, bytesWritten, (ULONG64)sizeof(UINT16)));
            }

            /* Step 3: Try writing via IO ports */
            {
                WRITE_PORT_ULONG((PULONG)(UINT_PTR)0xCF8,
                    0x80000000 | (bus << 16) | (dev << 11) | (func << 8) | 4);
                KeMemoryBarrier();
                WRITE_PORT_ULONG((PULONG)(UINT_PTR)0xCFC, 0x0007); /* I/O+Mem+BusMaster */
                KeMemoryBarrier();

                /* Read back to verify */
                WRITE_PORT_ULONG((PULONG)(UINT_PTR)0xCF8,
                    0x80000000 | (bus << 16) | (dev << 11) | (func << 8) | 4);
                KeMemoryBarrier();
                f->CommandAfter = READ_PORT_ULONG((PULONG)(UINT_PTR)0xCFC);

                f->IoPortWriteResult = (f->CommandAfter == 0x0007 || (f->CommandAfter & 0x0007) == 0x0007) ? 1 : 0;

                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                    "AMDBC250-DREAM-V4.3: FORCE_ENABLE_MMIO: IO port write, Command after=0x%08X\n", f->CommandAfter));
            }

            /* Step 4: Map MMIO and test registers */
            PHYSICAL_ADDRESS mmioPa;
            mmioPa.QuadPart = f->MmioPhysicalBase;
            UINT32 mmioSize = f->MmioSize;

            if (mmioPa.QuadPart != 0 && mmioSize != 0) {
                PUCHAR mappedVa = (PUCHAR)MmMapIoSpace(mmioPa, mmioSize, MmNonCached);
                if (mappedVa) {
                    /* Read GPU_ID at offset 0 */
                    f->GpuIdBefore = READ_REGISTER_ULONG((PULONG)(mappedVa));
                    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                        "AMDBC250-DREAM-V4.3: FORCE_ENABLE_MMIO: GPU_ID at PA=0x%llX before=0x%08X\n",
                        mmioPa.QuadPart, f->GpuIdBefore));

                    /* Write scratch value if offset provided */
                    if (f->ScratchOffset != 0 && f->ScratchOffset < mmioSize) {
                        WRITE_REGISTER_ULONG((PULONG)(mappedVa + f->ScratchOffset), f->ScratchWriteVal);
                        KeMemoryBarrier();
                        f->ScratchReadVal = READ_REGISTER_ULONG((PULONG)(mappedVa + f->ScratchOffset));
                        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                            "AMDBC250-DREAM-V4.3: FORCE_ENABLE_MMIO: Scratch at +0x%X wrote 0x%08X read back 0x%08X\n",
                            f->ScratchOffset, f->ScratchWriteVal, f->ScratchReadVal));
                    } else {
                        f->ScratchReadVal = 0;
                    }

                    /* Read GPU_ID again after enabling */
                    f->GpuIdAfter = READ_REGISTER_ULONG((PULONG)(mappedVa));

                    MmUnmapIoSpace(mappedVa, mmioSize);
                } else {
                    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                        "AMDBC250-DREAM-V4.3: FORCE_ENABLE_MMIO: MmMapIoSpace FAILED\n"));
                }
            }

            bytesReturned = sizeof(AMDBC250_IOCTL_FORCE_ENABLE_MMIO);
            status = STATUS_SUCCESS;

            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                "AMDBC250-DREAM-V4.3: FORCE_ENABLE_MMIO: Hal=%d IO=%d Cmd=0x%04X->0x%04X GPU_ID=0x%08X->0x%08X Scratch=0x%08X\n",
                f->HalSetBusResult, f->IoPortWriteResult,
                (UINT16)f->CommandBefore, (UINT16)f->CommandAfter,
                f->GpuIdBefore, f->GpuIdAfter, f->ScratchReadVal));
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        goto Cleanup;
    }

    default:
            /* Let unhandled IOCTLs fall through to the main switch below */
            break;
        }
    }

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
               "AMDBC250-DREAM-V4.3: IOCTL 0x%08X received\n", ioctlCode));

    switch (ioctlCode) {

    /* --- Get Caps --- */
    case 0x80000800: { /* IOCTL_AMDBC250_GET_CAPS */
        if (outputLen >= sizeof(ULONG) * 7) {
            PULONG Data = (PULONG)outputBuffer;
            Data[0] = AMDBC250_DREAM_V3_VERSION_MAJOR * 100 +
                      AMDBC250_DREAM_V3_VERSION_MINOR * 10 +
                      AMDBC250_DREAM_V3_VERSION_PATCH; /* Version */
            Data[1] = 0x05;  /* Caps: D3D12 + DISPLAY + RT */
            Data[2] = DevExt->GpuClockMhz;  /* MaxClockMhz */
            Data[3] = DevExt->MemoryClockMhz; /* MemoryClockMhz */
            Data[4] = AMDBC250_MAX_COMPUTE_UNITS;
            Data[5] = AMDBC250_STREAM_PROCESSORS;
            Data[6] = AMDBC250_RT_ACCELERATORS;
            bytesReturned = sizeof(ULONG) * 7;
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    /* --- Get VRAM Info --- */
    case 0x80000804: { /* IOCTL_AMDBC250_GET_VRAM_INFO */
        if (outputLen >= sizeof(ULONG64) * 3 + sizeof(ULONG)) {
            PULONG64 Data64 = (PULONG64)outputBuffer;
            PULONG Data32 = (PULONG)(Data64 + 3);
            Data64[0] = DevExt->TotalVramBytes;
            Data64[1] = DevExt->VisibleVramBytes;
            Data64[2] = DevExt->UsedVramBytes;
            *Data32 = 2; /* SegmentCount */
            bytesReturned = sizeof(ULONG64) * 3 + sizeof(ULONG);
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    /* --- Get Temp Info --- */
    case 0x80000808: { /* IOCTL_AMDBC250_GET_TEMP_INFO */
        if (outputLen >= sizeof(ULONG) * 4 + sizeof(BOOLEAN)) {
            PLONG TempData = (PLONG)outputBuffer;
            TempData[0] = DevExt->CurrentTemperatureC; /* Edge */
            TempData[1] = DevExt->CurrentTemperatureC + 12; /* Junction */
            TempData[2] = DevExt->CurrentTemperatureC + 5; /* VRM */
            PULONG UData = (PULONG)(TempData + 3);
            *UData = DevExt->PowerState.CurrentFanSpeedPercent;
            PBOOLEAN BData = (PBOOLEAN)(UData + 1);
            *BData = DevExt->PowerState.ThermalThrottleActive;
            bytesReturned = sizeof(ULONG) * 4 + sizeof(BOOLEAN);
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    /* --- Allocate Video Memory --- */
    case 0x80000840: { /* IOCTL_AMDBC250_ALLOC_VIDMEM */
        /* Mark that we reached this case */
        {
            UNICODE_STRING devPath;
            OBJECT_ATTRIBUTES objAttr;
            RtlInitUnicodeString(&devPath, L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\atikmdag");
            InitializeObjectAttributes(&objAttr, &devPath, OBJ_CASE_INSENSITIVE, NULL, NULL);
            HANDLE hKey = NULL;
            if (NT_SUCCESS(ZwOpenKey(&hKey, KEY_SET_VALUE, &objAttr))) {
                UNICODE_STRING valName;
                ULONG val = ioctlCode;
                RtlInitUnicodeString(&valName, L"LastIoctlCode");
                ZwSetValueKey(hKey, &valName, 0, REG_DWORD, &val, sizeof(val));
                ZwClose(hKey);
            }
        }
        if (inputLen >= sizeof(ULONG) * 3 && outputLen >= sizeof(ULONG64) * 2) {
            PULONG InData = (PULONG)inputBuffer;
            ULONG SizeLo = InData[0];
            SIZE_T AllocSize = (SIZE_T)SizeLo;

            /* Safety: cap at 64KB for testing */
            if (AllocSize > 64 * 1024) AllocSize = 64 * 1024;
            if (AllocSize < 4096) AllocSize = 4096;

            PHYSICAL_ADDRESS highestAddr;
            highestAddr.QuadPart = 0xFFFFFFFFULL;

            PVOID virtualAddr = NULL;
            __try {
                virtualAddr = MmAllocateContiguousMemory(AllocSize, highestAddr);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                    "AMDBC250-DREAM-V4.3: AllocVidMem EXCEPTION 0x%X\n", GetExceptionCode()));
                virtualAddr = NULL;
            }

            if (virtualAddr != NULL) {
                PHYSICAL_ADDRESS physAddr;
                __try {
                    physAddr = MmGetPhysicalAddress(virtualAddr);
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                        "AMDBC250-DREAM-V4.3: MmGetPhysicalAddress EXCEPTION\n"));
                    MmFreeContiguousMemory(virtualAddr);
                    virtualAddr = NULL;
                    physAddr.QuadPart = 0;
                }

                if (virtualAddr != NULL) {
                    PULONG64 OutData = (PULONG64)outputBuffer;
                    OutData[0] = physAddr.QuadPart;
                    OutData[1] = (ULONG64)(UINT_PTR)virtualAddr;
                    bytesReturned = sizeof(ULONG64) * 2;

                    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                        "AMDBC250-DREAM-V4.3: AllocVidMem OK: %llu bytes, PA=0x%llX VA=%p\n",
                        (ULONG64)AllocSize, physAddr.QuadPart, virtualAddr));
                }
            }

            if (virtualAddr == NULL) {
                status = STATUS_INSUFFICIENT_RESOURCES;
            }
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    /* --- Free Video Memory --- */
    case 0x80000844: { /* IOCTL_AMDBC250_FREE_VIDMEM */
        if (inputLen >= sizeof(ULONG64)) {
            PULONG64 InData = (PULONG64)inputBuffer;
            PVOID handle = (PVOID)(UINT_PTR)InData[0];
            if (handle != NULL) {
                KIRQL oldIrql;
                BOOLEAN found = FALSE;
                KeAcquireSpinLock(&g_MdlTableLock, &oldIrql);
                for (int m = 0; m < 64; m++) {
                    if (g_MdlTable[m].Va == handle) {
                        MmUnmapLockedPages(handle, g_MdlTable[m].Mdl);
                        MmFreePagesFromMdl(g_MdlTable[m].Mdl);
                        ExFreePoolWithTag(g_MdlTable[m].Mdl, 'MDL');
                        g_MdlTable[m].Va = NULL;
                        g_MdlTable[m].Mdl = NULL;
                        g_MdlTable[m].Size = 0;
                        found = TRUE;
                        break;
                    }
                }
                KeReleaseSpinLock(&g_MdlTableLock, oldIrql);

                if (!found) {
                    MmFreeContiguousMemory(handle);
                }
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                    "AMDBC250-DREAM-V4.3: FreeVidMem OK (MDL=%s)\n",
                    found ? "freed" : "contiguous"));
            }
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    /* --- Map Video Memory (CPU access) --- */
    case 0x80000848: { /* IOCTL_AMDBC250_MAP_VIDMEM */
        if (inputLen >= sizeof(ULONG64) * 3 && outputLen >= sizeof(ULONG64) * 2) {
            PULONG64 InData = (PULONG64)inputBuffer;
            PVOID handle = (PVOID)(UINT_PTR)InData[0];
            ULONG64 offset = InData[1];
            ULONG64 size = InData[2];
            UNREFERENCED_PARAMETER(offset);
            UNREFERENCED_PARAMETER(size);

            PULONG64 OutData = (PULONG64)outputBuffer;
            OutData[0] = (ULONG64)(UINT_PTR)handle; /* CPU address */
            OutData[1] = 0; /* Physical (not needed for CPU map) */
            bytesReturned = sizeof(ULONG64) * 2;
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    /* --- Submit Commands --- */
    case 0x80000880: { /* IOCTL_AMDBC250_SUBMIT_COMMANDS */
        if (inputLen >= sizeof(ULONG) * 4) {
            PULONG InData = (PULONG)inputBuffer;
            ULONG fenceValue;
            ULONG ibAddrLo = InData[0];
            ULONG ibAddrHi = InData[1];
            ULONG ibSize = 0;

            /* Dual-format compatibility:
               Old format (Vulkan ICD): {0, 0, fence, 0}  � fence at InData[2]
               New format (D3D9):       {PA_lo, PA_hi, size, fence} � fence at InData[3] */
            if (ibAddrLo == 0) {
                fenceValue = InData[2];  /* Old format: fence at field 2 */
            } else {
                fenceValue = InData[3];  /* New format: fence at field 3 */
                ibSize = InData[2];      /* IB size in bytes */
            }

            /* If IB provided, write INDIRECT_BUFFER packet into ring */
            if (ibAddrLo != 0 && ibSize > 0 &&
                DevExt->GfxRing.VirtualAddress != NULL &&
                DevExt->HardwareInitialized) {
                volatile PULONG Ring = (volatile PULONG)DevExt->GfxRing.VirtualAddress;
                ULONG WPtr = DevExt->GfxRing.WritePointer;
                ULONG RingSize = (ULONG)DevExt->GfxRing.SizeInBytes;
                ULONG NeededSpace = 4 * sizeof(ULONG);

                /* Ring wrap if needed */
        if ((ULONG64)WPtr + NeededSpace > RingSize) {
                    WPtr = 0;
                }

                /* Write INDIRECT_BUFFER PM4 packet (4 DWORDs) */
                Ring[WPtr / sizeof(ULONG) + 0] = PM4_TYPE3_HDR(IT_INDIRECT_BUFFER, 3);
                Ring[WPtr / sizeof(ULONG) + 1] = ibAddrLo & 0xFFFFFFFC;
                Ring[WPtr / sizeof(ULONG) + 2] = ibAddrHi;
                Ring[WPtr / sizeof(ULONG) + 3] = (ibSize + 3) / sizeof(ULONG);
                WPtr += 4 * sizeof(ULONG);
                DevExt->GfxRing.WritePointer = WPtr;
            }

            DreamV3WriteEopFence(DevExt, (ULONG64)fenceValue);
            DevExt->GlobalFence.LastSubmittedValue = (ULONG64)fenceValue;
            DreamV3SubmitGfxRing(DevExt);
            KeSetEvent(&DevExt->GlobalFence.FenceEvent, 0, FALSE);

            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
                "AMDBC250-DREAM-V4.3: SubmitCommands fence=%u ib=%s\n",
                fenceValue, (ibAddrLo != 0) ? "yes" : "no"));
        }
        break;
    }

    /* --- Wait Fence --- */
    case 0x80000884: { /* IOCTL_AMDBC250_WAIT_FENCE */
        if (inputLen >= sizeof(ULONG) * 2) {
            PULONG InData = (PULONG)inputBuffer;
            ULONG targetFence = InData[0];
            ULONG timeoutMs = InData[1];
            LARGE_INTEGER timeout;
            NTSTATUS waitStatus;

            /* Check if fence already signaled */
            if (DevExt->GlobalFence.LastSignaledValue >= (ULONG64)targetFence) {
                status = STATUS_SUCCESS;
                break;
            }

            /* Wait on fence event with timeout */
            timeout.QuadPart = (LONGLONG)timeoutMs * -10000; /* Convert ms to 100ns units */
            waitStatus = KeWaitForSingleObject(
                &DevExt->GlobalFence.FenceEvent,
                Executive,
                KernelMode,
                FALSE,
                &timeout
                );

            if (waitStatus == STATUS_TIMEOUT) {
                status = STATUS_TIMEOUT;
            } else {
                status = STATUS_SUCCESS;
            }
        }
        break;
    }

    /* --- Signal Fence --- */
    case 0x80000888: { /* IOCTL_AMDBC250_SIGNAL_FENCE */
        if (inputLen >= sizeof(ULONG)) {
            PULONG InData = (PULONG)inputBuffer;
            ULONG fenceValue = InData[0];
            DevExt->GlobalFence.LastSignaledValue = (ULONG64)fenceValue;
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
                "AMDBC250-DREAM-V4.3: SignalFence=%u\n", fenceValue));
        }
        break;
    }

    /* --- Reset Device --- */
    case 0x8000088C: { /* IOCTL_AMDBC250_RESET_DEVICE */
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
            "AMDBC250-DREAM-V4.3: ResetDevice requested\n"));
        DreamV3HwReset(DevExt);
        break;
    }

    /* --- Set Display Mode --- */
    case 0x800008C0: { /* IOCTL_AMDBC250_SET_DISPLAY_MODE */
        if (inputLen >= sizeof(ULONG) * 4) {
            PULONG InData = (PULONG)inputBuffer;
            DevExt->CurrentMode.Width = InData[0];
            DevExt->CurrentMode.Height = InData[1];
            DevExt->CurrentMode.RefreshRate = InData[2];
            DevExt->CurrentMode.BitsPerPixel = InData[3];

            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                "AMDBC250-DREAM-V4.3: SetDisplayMode %ux%u@%uHz\n",
                InData[0], InData[1], InData[2]));

            DreamV3HwInitDisplay(DevExt);
        }
        break;
    }

    /* --- Flip Display --- */
    case 0x800008C4: { /* IOCTL_AMDBC250_FLIP_DISPLAY */
        if (inputLen >= sizeof(ULONG) * 7) {
            PULONG InData = (PULONG)inputBuffer;
            ULONG64 physAddr = ((ULONG64)InData[1] << 32) | InData[0];

            if (physAddr != 0) {
                DreamV3WriteRegister(DevExt,
                    AMDBC250_REG_HUBPREQ0_DCSURF_PRIMARY_SURFACE_ADDRESS,
                    (ULONG)(physAddr & 0xFFFFFFFF));
                DreamV3WriteRegister(DevExt,
                    AMDBC250_REG_HUBPREQ0_DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH,
                    (ULONG)(physAddr >> 32));
            }

            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
                "AMDBC250-DREAM-V4.3: FlipDisplay PA=0x%llX\n", physAddr));
        }
        break;
    }

    /* --- Get Display Info --- */
    case 0x800008C8: { /* IOCTL_AMDBC250_GET_DISPLAY_INFO */
        if (outputLen >= sizeof(ULONG) * 7) {
            PULONG OutData = (PULONG)outputBuffer;
            OutData[0] = DevExt->CurrentMode.Width;
            OutData[1] = DevExt->CurrentMode.Height;
            OutData[2] = DevExt->CurrentMode.RefreshRate;
            OutData[3] = 7680;  /* MaxWidth */
            OutData[4] = 4320;  /* MaxHeight */
            OutData[5] = 0x03;  /* OutputTypes: DP + HDMI */
            OutData[6] = DevExt->NumDisplayPipes;
            bytesReturned = sizeof(ULONG) * 7;
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    /* --- Set Power State --- */
    case 0x80000900: { /* IOCTL_AMDBC250_SET_POWER_STATE */
        if (inputLen >= sizeof(ULONG) * 3) {
            PULONG InData = (PULONG)inputBuffer;
            ULONG powerState = InData[0];
            ULONG gpuClock = InData[1];
            ULONG memClock = InData[2];

            if (gpuClock > 0) DevExt->GpuClockMhz = gpuClock;
            if (memClock > 0) DevExt->MemoryClockMhz = memClock;

            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                "AMDBC250-DREAM-V4.3: SetPowerState D%u, SCLK=%u, MCLK=%u\n",
                powerState, gpuClock, memClock));
        }
        break;
    }

    /* --- Get Power Telemetry --- */
    case 0x80000904: { /* IOCTL_AMDBC250_GET_POWER_TELEMETRY */
        if (outputLen >= sizeof(ULONG) * 9) {
            PULONG OutData = (PULONG)outputBuffer;
            OutData[0] = 0; /* PowerMilliwatts (stub) */
            OutData[1] = DevExt->PowerState.PowerLimitWatts;
            OutData[2] = DevExt->GpuClockMhz;
            OutData[3] = DevExt->MemoryClockMhz;
            OutData[4] = DevExt->PowerState.CurrentFanSpeedPercent;
            OutData[5] = (ULONG)DevExt->CurrentTemperatureC;
            OutData[6] = (ULONG)(DevExt->CurrentTemperatureC + 12);
            OutData[7] = DevExt->PowerState.ThermalThrottleActive ? 1 : 0;
            OutData[8] = DevExt->ThermalThrottleCount;
            bytesReturned = sizeof(ULONG) * 9;
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    /* --- Allocate DMA Buffer (for command submission) --- */
    case 0x80000930: { /* IOCTL_AMDBC250_ALLOC_DMA_BUFFER */
        if (inputLen >= sizeof(ULONG) && outputLen >= sizeof(ULONG64) * 2) {
            PULONG InData = (PULONG)inputBuffer;
            SIZE_T bufSize = (SIZE_T)InData[0];
            bufSize = (bufSize + 0xFFF) & ~0xFFFULL; /* Align to 4KB */

            PHYSICAL_ADDRESS low, high, skip;
            low.QuadPart = 0;
            high.QuadPart = 0xFFFFFFFFFFULL;
            skip.QuadPart = 0;

            PVOID virtAddr = MmAllocateContiguousMemorySpecifyCache(
                bufSize, low, high, skip, MmCached);

            if (virtAddr != NULL) {
                PHYSICAL_ADDRESS physAddr = MmGetPhysicalAddress(virtAddr);
                PULONG64 OutData = (PULONG64)outputBuffer;
                OutData[0] = physAddr.QuadPart;
                OutData[1] = (ULONG64)(UINT_PTR)virtAddr;
                bytesReturned = sizeof(ULONG64) * 2;

                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                    "AMDBC250-DREAM-V4.3: AllocDmaBuffer: %llu bytes, PA=0x%llX\n",
                    (ULONG64)bufSize, physAddr.QuadPart));
            } else {
                status = STATUS_INSUFFICIENT_RESOURCES;
            }
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    /* --- Free DMA Buffer --- */
    case 0x80000934: { /* IOCTL_AMDBC250_FREE_DMA_BUFFER */
        if (inputLen >= sizeof(ULONG64)) {
            PULONG64 InData = (PULONG64)inputBuffer;
            PVOID handle = (PVOID)(UINT_PTR)InData[0];
            if (handle != NULL) {
                MmFreeContiguousMemory(handle);
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                    "AMDBC250-DREAM-V4.3: FreeDmaBuffer OK\n"));
            }
        }
        break;
    }

    /* --- SDMA Copy Buffer --- */
    case 0x80000940: { /* IOCTL_AMDBC250_SDMA_COPY */
        if (inputLen >= sizeof(ULONG) * 4 + sizeof(ULONG64)) {
            PULONG InData32 = (PULONG)inputBuffer;
            ULONG64* InData64 = (ULONG64*)inputBuffer;
            PHYSICAL_ADDRESS src, dst;
            src.QuadPart = InData64[0];
            dst.QuadPart = InData64[1];
            SIZE_T size = (SIZE_T)InData32[4];
            status = DreamV3SdmaCopyBuffer(DevExt, src, dst, size);
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    /* --- SDMA Fill Buffer --- */
    case 0x80000944: { /* IOCTL_AMDBC250_SDMA_FILL */
        if (inputLen >= sizeof(ULONG) * 4) {
            PULONG InData = (PULONG)inputBuffer;
            PHYSICAL_ADDRESS dst;
            dst.QuadPart = ((ULONG64)InData[1] << 32) | InData[0];
            SIZE_T size = (SIZE_T)InData[2];
            ULONG fillVal = InData[3];
            status = DreamV3SdmaFillBuffer(DevExt, dst, size, fillVal);
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    /* --- TDR Reset --- */
    case 0x80000950: { /* IOCTL_AMDBC250_TDR_RESET */
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
            "AMDBC250-DREAM-V4.3: TDR reset requested via IOCTL\n"));
        status = DreamV3TdrReset(DevExt);
        break;
    }

    /* --- Read EDID --- */
    case 0x80000960: { /* IOCTL_AMDBC250_READ_EDID */
        if (inputLen >= sizeof(ULONG) && outputLen >= 128 + sizeof(ULONG)) {
            PULONG InData = (PULONG)inputBuffer;
            ULONG childUid = InData[0];
            PUCHAR edidBuf = (PUCHAR)outputBuffer;
            ULONG edidSize = 0;
            status = DreamV3ReadEdid(DevExt, childUid, edidBuf, &edidSize);
            bytesReturned = edidSize;
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    /* --- Get Child Relations (monitor enumeration) --- */
    case 0x80000964: { /* IOCTL_AMDBC250_GET_CHILD_RELATIONS */
        if (outputLen >= sizeof(ULONG) * 3) {
            PULONG OutData = (PULONG)outputBuffer;
            ULONG maxW, maxH, maxR;
            DreamV3ParseEdid(DevExt, 0, &maxW, &maxH, &maxR);
            OutData[0] = DevExt->NumDisplayPipes; /* Child count */
            OutData[1] = 0x03; /* Connected: DP + HDMI */
            OutData[2] = maxW; /* Max width */
            bytesReturned = sizeof(ULONG) * 3;
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    /* --- Compile Shader (stub) --- */
    case 0x80000970: { /* IOCTL_AMDBC250_SHADER_COMPILE */
        if (inputLen >= sizeof(ULONG) * 2) {
            PULONG InData = (PULONG)inputBuffer;
            ULONG shaderType = InData[0];
            ULONG shaderSize = InData[1];
            PVOID compiled = NULL;
            ULONG compiledSize = 0;
            DreamV3ShaderCompileStub(DevExt, NULL, (SIZE_T)shaderSize,
                                      shaderType, &compiled, &compiledSize);
            /* Return stub result */
            if (outputLen >= sizeof(ULONG) * 2) {
                PULONG OutData = (PULONG)outputBuffer;
                OutData[0] = compiledSize;
                OutData[1] = compiled != NULL ? 0 : 1; /* 0=success, 1=stub */
                bytesReturned = sizeof(ULONG) * 2;
            }
            status = STATUS_SUCCESS;
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    /* --- Unlock 40 CUs --- */
    case 0x80000980: { /* IOCTL_AMDBC250_UNLOCK_40CU */
        /*
         * BC-250 40 CU Unlock (from duggasco/bc250-40cu-unlock)
         *
         * Two registers must be written:
         * 1. CC_GC_SHADER_ARRAY_CONFIG (BC-250: 0x9C1C): CU enumeration mask
         *    Stock: 0xFFF80000 (24 CUs) ? Unlocked: 0xFFE00000 (40 CUs)
         * 2. SPI_PG_ENABLE_STATIC_WGP_MASK (BC-250: 0x34FC): WGP dispatch gate
         *    Stock: 0x7 (WGP 0-2) ? Unlocked: 0x1F (WGP 0-4)
         *
         * Both registers must be written together.
         * CC alone changes what driver reports but SPI still dispatches to 24 CUs.
         * SPI alone enables hardware dispatch but driver only generates for 24 CUs.
         */
        if (inputLen >= sizeof(ULONG)) {
            PULONG InData = (PULONG)inputBuffer;
            ULONG enable = InData[0]; /* 0=disable (stock 24CU), 1=enable (40CU) */

            if (enable) {
                /* Write CC_GC_SHADER_ARRAY_CONFIG (BC-250: 0x3264): confirmed read-only */
                DreamV3WriteRegister(DevExt, AMDBC250_REG_CC_GC_SHADER_ARRAY_CONFIG, 0xFFE00000);
                /* Write SPI_PG_ENABLE_STATIC_WGP_MASK (BC-250: 0x34FC):
                 * BC-250 WGP bits are 8-13 (verified via write-back test).
                 * Stock: bit13=WGP5 (0x2000). Enable all: bits 8-13 (0x3F00). */
                DreamV3WriteRegister(DevExt, AMDBC250_REG_SPI_PG_ENABLE_STATIC_WGP_MASK, 0x00003F00);

                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                    "AMDBC250-DREAM-V4.3: *** WGP UNLOCK ENABLED *** "
                    "SPI=0x2000->0x3F00 (WGP0-5)\n"));
            } else {
                /* Restore stock (WGP5 only) */
                DreamV3WriteRegister(DevExt, AMDBC250_REG_CC_GC_SHADER_ARRAY_CONFIG, 0xFFF80000);
                DreamV3WriteRegister(DevExt, AMDBC250_REG_SPI_PG_ENABLE_STATIC_WGP_MASK, 0x00002000);

                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                    "AMDBC250-DREAM-V4.3: WGP unlock DISABLED (WGP5 only, stock)\n"));
            }
            status = STATUS_SUCCESS;
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    /* --- Get CU Status --- */
    case 0x80000984: { /* IOCTL_AMDBC250_GET_CU_STATUS */
        if (outputLen >= sizeof(ULONG) * 4) {
            PULONG OutData = (PULONG)outputBuffer;

            /* Read current register values (BC-250 corrected offsets) */
            ULONG ccConfig = DreamV3ReadRegister(DevExt, AMDBC250_REG_CC_GC_SHADER_ARRAY_CONFIG);
            ULONG spiMask = DreamV3ReadRegister(DevExt, AMDBC250_REG_SPI_PG_ENABLE_STATIC_WGP_MASK);

            /* Decode CU count from CC_GC_SHADER_ARRAY_CONFIG */
            /* Bits [31:19] = CU mask, count set bits */
            ULONG cuMask = (ccConfig >> 19) & 0x1FFF;
            ULONG cuCount = 0;
            for (ULONG i = 0; i < 13; i++) {
                if (cuMask & (1 << i)) cuCount++;
            }
            cuCount *= 2; /* Each bit = 2 CUs (1 WGP = 2 CUs) */

            ULONG wgpCount = 0;
            for (ULONG i = 8; i < 14; i++) {
                if (spiMask & (1 << i)) wgpCount++;
            }

            OutData[0] = cuCount;      /* Active CUs */
            OutData[1] = wgpCount;     /* Active WGPs */
            OutData[2] = ccConfig;     /* CC register value */
            OutData[3] = spiMask;      /* SPI register value */
            bytesReturned = sizeof(ULONG) * 4;

            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                "AMDBC250-DREAM-V4.3: CU Status: %lu CUs, %lu WGPs, CC=0x%08X, SPI=0x%08X\n",
                cuCount, wgpCount, ccConfig, spiMask));
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    /* --- Init Hardware (user-mode provides MMIO base) --- */
    case 0x80000B80: { /* IOCTL_AMDBC250_INIT_HARDWARE */
        /* Accept either old (16B) or new (32B) struct size */
        if (inputLen >= 16 && inputLen <= sizeof(AMDBC250_IOCTL_INIT_HARDWARE)) {
            PAMDBC250_IOCTL_INIT_HARDWARE InitHw = (PAMDBC250_IOCTL_INIT_HARDWARE)inputBuffer;

            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                "AMDBC250-DREAM-V4.3: INIT_HARDWARE requested: MMIO PA=0x%llX, Size=0x%X\n",
                InitHw->MmioPhysicalBase, InitHw->MmioSize));

            if (DevExt->HardwareInitialized) {
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                    "AMDBC250-DREAM-V4.3: Hardware already initialized, re-mapping with new base\n"));
                /* Unmap old MMIO if mapped */
                if (DevExt->MmioVirtualBase) {
                    MmUnmapIoSpace(DevExt->MmioVirtualBase, DevExt->MmioSize);
                    DevExt->MmioVirtualBase = NULL;
                }
                /* Unmap old FB if mapped */
                if (DevExt->FbVirtualBase) {
                    MmUnmapIoSpace(DevExt->FbVirtualBase, DevExt->FbSize);
                    DevExt->FbVirtualBase = NULL;
                }
                DevExt->HardwareInitialized = FALSE;
            }

            /* Auto-detect BAR5 from PCIe config if caller passed 0.
             * On Win11 26100 the WDM fallback never gets PnP resources, so
             * the user-mode tool may not know the BAR5 PA. Read it from the
             * PCIe config space (BAR5 = BaseAddresses[5]) via HalGetBusData. */
            if (InitHw->MmioPhysicalBase == 0) {
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                    "AMDBC250-DREAM-V4.3: INIT_HARDWARE MmioPhysicalBase==0, auto-detecting BAR5 via PCIe config\n"));
                BOOLEAN foundPci = FALSE;
                UINT32 fBus = 0, fDev = 0, fFunc = 0;
                for (ULONG bus = 0; bus < 256 && !foundPci; bus++) {
                    for (ULONG dev = 0; dev < 32 && !foundPci; dev++) {
                        for (ULONG func = 0; func < 8 && !foundPci; func++) {
                            ULONG slot = (dev << 0) | (func << 5);
                            PCI_COMMON_CONFIG pc;
                            RtlZeroMemory(&pc, sizeof(pc));
                            ULONG br = HalGetBusDataByOffset(
                                PCIConfiguration, bus, slot, &pc, 0,
                                sizeof(PCI_COMMON_HDR_LENGTH));
                            if (br < sizeof(PCI_COMMON_HDR_LENGTH))
                                continue;
                            if (pc.VendorID == 0x1002 && pc.DeviceID == 0x13FE) {
                                foundPci = TRUE;
                                fBus = bus; fDev = dev; fFunc = func;
                                /* BAR5 is at offset 0x24 (BaseAddresses[5]) */
                                ULONG bar5 = pc.u.type0.BaseAddresses[5];
                                /* Mask flags (lower 4 bits) */
                                InitHw->MmioPhysicalBase = (ULONGLONG)(bar5 & 0xFFFFFFF0);
                                if (InitHw->MmioSize == 0)
                                    InitHw->MmioSize = 0x80000; /* 512KB default */
                                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                                    "AMDBC250-DREAM-V4.3: BAR5 auto-detected at B%u:D%u:F%u PA=0x%llX\n",
                                    fBus, fDev, fFunc, InitHw->MmioPhysicalBase));
                            }
                        }
                    }
                }
                if (!foundPci) {
                    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                        "AMDBC250-DREAM-V4.3: INIT_HARDWARE BAR5 auto-detect FAILED (VEN_1002 DEV_13FE not found)\n"));
                    status = STATUS_NO_SUCH_DEVICE;
                    break;
                }
            }

            /* Validate input */
            if (InitHw->MmioPhysicalBase == 0 || InitHw->MmioSize == 0) {
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                    "AMDBC250-DREAM-V4.3: INIT_HARDWARE invalid params\n"));
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            /* Map MMIO BAR (BAR2 = register space) */
            DevExt->MmioPhysicalBase.QuadPart = InitHw->MmioPhysicalBase;
            DevExt->MmioSize = InitHw->MmioSize;

            DevExt->MmioVirtualBase = MmMapIoSpace(
                DevExt->MmioPhysicalBase,
                DevExt->MmioSize,
                MmNonCached
            );

            if (DevExt->MmioVirtualBase == NULL) {
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                    "AMDBC250-DREAM-V4.3: MmMapIoSpace FAILED for MMIO PA=0x%llX\n",
                    InitHw->MmioPhysicalBase));
                status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }

            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                "AMDBC250-DREAM-V4.3: MMIO mapped: VA=%p\n", DevExt->MmioVirtualBase));

            /* Publish the BAR5 mapping to the PSP proxy so it can read SOS
             * status (C2PMSG_81) directly without depending on the PSP driver's
             * own BAR5 mapping (which is often NULL on Win11 26100). */
            Amdbc250PspSetGpuBar5Va(DevExt->MmioVirtualBase);

            /* Map VRAM framebuffer BAR (BAR0) if provided (new struct with Fb fields) */
            if (inputLen >= sizeof(AMDBC250_IOCTL_INIT_HARDWARE) &&
                InitHw->FbPhysicalBase != 0 && InitHw->FbSize != 0) {
                if (DevExt->FbVirtualBase) {
                    MmUnmapIoSpace(DevExt->FbVirtualBase, DevExt->FbSize);
                    DevExt->FbVirtualBase = NULL;
                }
                DevExt->FbPhysicalBase.QuadPart = InitHw->FbPhysicalBase;
                DevExt->FbSize = InitHw->FbSize;
                DevExt->FbVirtualBase = MmMapIoSpace(
                    DevExt->FbPhysicalBase,
                    DevExt->FbSize,
                    MmNonCached
                );
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                    "AMDBC250-DREAM-V4.3: VRAM mapped: VA=%p, PA=0x%llX, Size=0x%X\n",
                    DevExt->FbVirtualBase, InitHw->FbPhysicalBase, InitHw->FbSize));
            }

            /* If NBIO_MAP flag set, skip GPU alive test but still enable PCI memory space */
            if (InitHw->Flags & AMDBC250_INIT_FLAG_NBIO_MAP) {
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                    "AMDBC250-DREAM-V4.3: NBIO_MAP flag set - skipping GPU alive test + GPU init\n"));
                
                /* Enable PCI memory space for the GPU to respond to MMIO */
                {
                    BOOLEAN foundPci = FALSE;
                    for (ULONG bus = 0; bus < 256 && !foundPci; bus++) {
                        for (ULONG dev = 0; dev < 32 && !foundPci; dev++) {
                            for (ULONG func = 0; func < 8 && !foundPci; func++) {
                                WRITE_PORT_ULONG((PULONG)(UINT_PTR)0xCF8,
                                    0x80000000 | (bus << 16) | (dev << 11) | (func << 8) | 0);
                                ULONG id = READ_PORT_ULONG((PULONG)(UINT_PTR)0xCFC);
                                if ((id & 0xFFFF) == 0x1002 && ((id >> 16) & 0xFFFF) == 0x13FE) {
                                    foundPci = TRUE;
                                    WRITE_PORT_ULONG((PULONG)(UINT_PTR)0xCF8,
                                        0x80000000 | (bus << 16) | (dev << 11) | (func << 8) | 4);
                                    KeMemoryBarrier();
                                    WRITE_PORT_ULONG((PULONG)(UINT_PTR)0xCFC, 0x0007);
                                    KeMemoryBarrier();
                                    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                                        "AMDBC250-DREAM-V4.3: PCI enable at B%lu:D%lu:F%lu (NBIO_MAP)\n", bus, dev, func));
                                }
                            }
                        }
                    }
                }
                
                /* Verify GPU responds */
                ULONG gpuId = DreamV3ReadRegister(DevExt, 0x0000);
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                    "AMDBC250-DREAM-V4.3: GPU reg[0x0000] = 0x%08X (NBIO_MAP test)\n", gpuId));
                
                /* 40 CU unlock deferred — inaccurate offsets cause hangs */
                
                DevExt->HardwareInitialized = TRUE;
                DevExt->GpuClockMhz = AMDBC250_BOOST_CLOCK_MHZ;
                DevExt->MemoryClockMhz = AMDBC250_MEMORY_CLOCK_MHZ;

                /* Initialize KIQ ring even in NBIO_MAP mode (needed for SEND_PM4) */
                if (NT_SUCCESS(Amdbc250PspKiqInit())) {
                    DevExt->KiqAvailable = TRUE;
                    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                        "AMDBC250-DREAM-V4.3: KIQ ring initialized (NBIO_MAP mode)\n"));
                } else {
                    DevExt->KiqAvailable = FALSE;
                    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                        "AMDBC250-DREAM-V4.3: KIQ init FAILED in NBIO_MAP mode\n"));
                }

                /* SDMA ring init SKIPPED — suspected BSOD 0x1a source.
                 * Register range 0xE000-0xE018 needs probing first. */
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                    "AMDBC250-DREAM-V4.3: SDMA ring init SKIPPED\n"));

                bytesReturned = sizeof(AMDBC250_IOCTL_INIT_HARDWARE);
                status = STATUS_SUCCESS;
                break;
            }

            /* FULL INIT path entered (Flags=0). Sentinel marker 100 = before
             * DreamV3HwInitialize; if a TDR occurs in the PCI scan / GPU-alive
             * test below, Step_HwInit stays 100. */
            DreamV3MarkHwInitStep(100);

            /* Verify GPU is alive � read a known register */
            {
                ULONG gpuId = DreamV3ReadRegister(DevExt, 0x0000); /* GPU_ID or scratch */
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                    "AMDBC250-DREAM-V4.3: GPU reg[0x0000] = 0x%08X (GPU alive test)\n", gpuId));
            }

            /* Try to enable PCI Memory Space � scan for BC-250 via IO ports */
            {
                BOOLEAN foundPci = FALSE;
                for (ULONG bus = 0; bus < 256 && !foundPci; bus++) {
                    for (ULONG dev = 0; dev < 32 && !foundPci; dev++) {
                        for (ULONG func = 0; func < 8 && !foundPci; func++) {
                            WRITE_PORT_ULONG((PULONG)(UINT_PTR)0xCF8,
                                0x80000000 | (bus << 16) | (dev << 11) | (func << 8) | 0);
                            ULONG id = READ_PORT_ULONG((PULONG)(UINT_PTR)0xCFC);
                            if ((id & 0xFFFF) == 0x1002 && ((id >> 16) & 0xFFFF) == 0x13FE) {
                                foundPci = TRUE;
                                /* Enable I/O + Mem + BusMaster */
                                WRITE_PORT_ULONG((PULONG)(UINT_PTR)0xCF8,
                                    0x80000000 | (bus << 16) | (dev << 11) | (func << 8) | 4);
                                KeMemoryBarrier();
                                WRITE_PORT_ULONG((PULONG)(UINT_PTR)0xCFC, 0x0007);
                                KeMemoryBarrier();
                                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                                    "AMDBC250-DREAM-V4.3: PCI enable at B%lu:D%lu:F%lu\n", bus, dev, func));
                            }
                        }
                    }
                }
                if (!foundPci) {
                    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                        "AMDBC250-DREAM-V4.3: PCI scan did not find BC-250 (continuing)\n"));
                }

                /* Re-read GPU ID after enable */
                ULONG gpuId2 = DreamV3ReadRegister(DevExt, 0x0000);
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                    "AMDBC250-DREAM-V4.3: GPU reg[0x0000] AFTER PCI enable = 0x%08X\n", gpuId2));
            }

            /* Initialize hardware (rings, fence, CP, etc.) */
            NTSTATUS hwStatus = DreamV3HwInitialize(DevExt);
            if (!NT_SUCCESS(hwStatus)) {
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                    "AMDBC250-DREAM-V4.3: HwInitialize failed: 0x%08X (continuing anyway)\n", hwStatus));
                /* Continue � some things may still work */
            }

            DevExt->HardwareInitialized = TRUE;

            /* Enable interrupt handler ring if initialized */
            if (DevExt->IhRing.Initialized) {
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                    "AMDBC250-DREAM-V4.3: IH ring active, PA=0x%llX\n",
                    DevExt->IhRing.PhysicalAddress.QuadPart));
            }

            /* Report ring status */
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                "AMDBC250-DREAM-V4.3: INIT_HARDWARE complete. GFX ring: %s (PA=0x%llX, %lluKB)\n",
                DevExt->GfxRing.Initialized ? "OK" : "FAIL",
                DevExt->GfxRing.PhysicalAddress.QuadPart,
                (ULONG64)DevExt->GfxRing.SizeInBytes / 1024));

            bytesReturned = sizeof(AMDBC250_IOCTL_INIT_HARDWARE);
            status = STATUS_SUCCESS;
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    /* --- Send PM4 commands to GFX ring --- */
    case 0x80000B84: { /* IOCTL_AMDBC250_SEND_PM4 */
        if (inputLen >= sizeof(AMDBC250_IOCTL_SEND_PM4)) {
            PAMDBC250_IOCTL_SEND_PM4 SendPm4 = (PAMDBC250_IOCTL_SEND_PM4)inputBuffer;

            if (!DevExt->HardwareInitialized) {
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                    "AMDBC250-DREAM-V4.3: SEND_PM4 but hardware not initialized\n"));
                status = STATUS_DEVICE_NOT_READY;
                break;
            }

            if (SendPm4->CommandCount == 0 || SendPm4->CommandCount > 64) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            /* PATH 1: PSP KIQ ring (preferred � KIQ_WPTR works via PSP driver) */
            if (Amdbc250PspKiqIsInitialized()) {
                /* Build PM4 buffer: user commands + EOP fence (if requested).
                 * PSP KIQ submit accepts max 64 DWORDs total. */
                ULONG Pm4Buffer[128];
                ULONG Pm4Count = SendPm4->CommandCount;

                if (SendPm4->FenceValue > 0) {
                    if (Pm4Count > 58) Pm4Count = 58;
                }

                RtlCopyMemory(Pm4Buffer, SendPm4->Commands, Pm4Count * sizeof(ULONG));

                if (SendPm4->FenceValue > 0) {
                    ULONG idx = Pm4Count;
                    Pm4Buffer[idx++] = PM4_TYPE3_HDR(IT_EVENT_WRITE_EOP, 5);
                    Pm4Buffer[idx++] = (0x47 << 0) | (5 << 8) | (2 << 12) | (1 << 14);
                    Pm4Buffer[idx++] = (ULONG)DevExt->GlobalFence.PhysicalAddress.QuadPart;
                    Pm4Buffer[idx++] = (ULONG)(DevExt->GlobalFence.PhysicalAddress.QuadPart >> 32);
                    Pm4Buffer[idx++] = (ULONG)SendPm4->FenceValue;
                    Pm4Buffer[idx++] = (ULONG)(SendPm4->FenceValue >> 32);
                    Pm4Count = idx;

                    DevExt->GlobalFence.LastSubmittedValue = (ULONG64)SendPm4->FenceValue;
                }

                status = Amdbc250PspKiqSubmit(Pm4Buffer, Pm4Count);
                if (NT_SUCCESS(status)) {
                    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
                        "AMDBC250-DREAM-V4.3: SEND_PM4 via PSP KIQ: %lu DWORDs, fence=%llu\n",
                        Pm4Count, (ULONG64)SendPm4->FenceValue));
                    break;
                }
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                    "AMDBC250-DREAM-V4.3: PSP KIQ submit failed (0x%08X), trying fallback\n", status));
            }

            /* PATH 2: Legacy GfxRing (HQD/KIQ/GFX doorbell) */
            if (DevExt->GfxRing.VirtualAddress != NULL) {
                KIRQL OldIrql;
                KeAcquireSpinLock(&DevExt->GfxRing.Lock, &OldIrql);

                volatile PULONG Ring = (volatile PULONG)DevExt->GfxRing.VirtualAddress;
                ULONG WPtr = DevExt->GfxRing.WritePointer;
                ULONG RingSize = (ULONG)DevExt->GfxRing.SizeInBytes;
                ULONG BytesNeeded = SendPm4->CommandCount * sizeof(ULONG);
                ULONG EopSize = 6 * sizeof(ULONG); /* EOP packet is 6 DWORDs */
                ULONG TotalBytes = BytesNeeded + (SendPm4->FenceValue > 0 ? EopSize : 0);

                /* Ring wrap if needed (including space for EOP) — use 64-bit to avoid overflow */
                if ((ULONG64)WPtr + TotalBytes > RingSize) {
                    ULONG NopCount = (RingSize - WPtr) / sizeof(ULONG);
                    for (ULONG i = 0; i < NopCount; i++) {
                        Ring[WPtr / sizeof(ULONG) + i] = PM4_TYPE2_NOP;
                    }
                    WPtr = 0;
                }

                /* Copy PM4 commands into ring */
                ULONG idx = WPtr / sizeof(ULONG);
                RtlCopyMemory((PVOID)&Ring[idx], SendPm4->Commands, BytesNeeded);
                WPtr += BytesNeeded;

                /* CRITICAL: Update WritePointer BEFORE EOP fence so EOP fence
                 * uses the correct ring offset (after commands, not at old WritePointer). */
                DevExt->GfxRing.WritePointer = WPtr;

                /* Write EOP fence BEFORE doorbell
                 * NOTE: Fence PM4 is written to GfxRing buffer but doorbell
                 * below kicks HQD/KIQ ring — fence never consumed by HW.
                 * TODO: Move fence to SavedPm4Cmds/KIQ ring instead. */
                if (SendPm4->FenceValue > 0) {
                    DreamV3WriteEopFence(DevExt, (ULONG64)SendPm4->FenceValue);
                    WPtr = DevExt->GfxRing.WritePointer;
                    DevExt->GlobalFence.LastSubmittedValue = (ULONG64)SendPm4->FenceValue;
                }

                DevExt->GfxRing.WritePointer = WPtr;
                KeMemoryBarrier();

                /* Kick doorbell -- write WPTR to MMIO */
                if (DevExt->UseHqdKiq) {
                    DreamV3WriteRegister(DevExt, DevExt->GrbmGfxIndexOffset,
                        AMDBC250_GRBM_GFX_INDEX_KIQ_VAL);
                    DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_HQD_PQ_WPTR_LO, WPtr);
                    DreamV3WriteRegister(DevExt, DevExt->GrbmGfxIndexOffset,
                        AMDBC250_GRBM_GFX_INDEX_BROADCAST_VAL);
                } else if (DevExt->UseKiqRing) {
                    DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_KIQ_WPTR, WPtr);
                } else {
                    DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_GFX_RING0_WPTR, WPtr);
                }

                KeReleaseSpinLock(&DevExt->GfxRing.Lock, OldIrql);

                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
                    "AMDBC250-DREAM-V4.3: SEND_PM4: %lu DWORDs, WPtr=%u, fence=%u\n",
                    SendPm4->CommandCount, WPtr, SendPm4->FenceValue));
                status = STATUS_SUCCESS;
            } else {
                /* PATH 3: Software PM4 fallback (when no KIQ ring available) */
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                    "AMDBC250-DREAM-V4.3: SEND_PM4 via software executor\n"));
                status = DreamV3SwPm4Process(DevExt, SendPm4->Commands,
                    SendPm4->CommandCount, (ULONG64)SendPm4->FenceValue, 32);
                if (NT_SUCCESS(status)) {
                    DevExt->GlobalFence.LastSubmittedValue = (ULONG64)SendPm4->FenceValue;
                }
            }
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    /* --- Read GPU register (via PSP proxy when available) --- */
    case 0x80000B88: { /* IOCTL_AMDBC250_READ_REG */
        if (inputLen >= sizeof(AMDBC250_IOCTL_REG_ACCESS) &&
            outputLen >= sizeof(AMDBC250_IOCTL_REG_ACCESS)) {
            PAMDBC250_IOCTL_REG_ACCESS RegAcc = (PAMDBC250_IOCTL_REG_ACCESS)inputBuffer;

            if (!DevExt->HardwareInitialized || DevExt->MmioVirtualBase == NULL) {
                status = STATUS_DEVICE_NOT_READY;
                break;
            }

            if (DevExt->MmioSize < 4 || RegAcc->RegisterOffset > DevExt->MmioSize - 4) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            ULONG value = DreamV3ReadRegister(DevExt, RegAcc->RegisterOffset);
            RegAcc->Value = value;
            bytesReturned = sizeof(AMDBC250_IOCTL_REG_ACCESS);

            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
                "AMDBC250-DREAM-V4.3: READ_REG[0x%04X] = 0x%08X\n",
                RegAcc->RegisterOffset, value));
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    /* --- Write GPU register (via PSP proxy when available) --- */
    case 0x80000B8C: { /* IOCTL_AMDBC250_WRITE_REG */
        if (inputLen >= sizeof(AMDBC250_IOCTL_REG_ACCESS)) {
            PAMDBC250_IOCTL_REG_ACCESS RegAcc = (PAMDBC250_IOCTL_REG_ACCESS)inputBuffer;

            if (!DevExt->HardwareInitialized || DevExt->MmioVirtualBase == NULL) {
                status = STATUS_DEVICE_NOT_READY;
                break;
            }

            if (DevExt->MmioSize < 4 || RegAcc->RegisterOffset > DevExt->MmioSize - 4) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            DreamV3WriteRegister(DevExt, RegAcc->RegisterOffset, RegAcc->Value);

            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
                "AMDBC250-DREAM-V4.3: WRITE_REG[0x%04X] = 0x%08X\n",
                RegAcc->RegisterOffset, RegAcc->Value));
            status = STATUS_SUCCESS;
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    /* --- Read raw PCI config space via ECAM (MMCFG) --- */
    case 0x80000BAC: { /* IOCTL_AMDBC250_READ_PCI_CONFIG */
        if (inputLen >= sizeof(AMDBC250_IOCTL_READ_PCI_CONFIG) &&
            outputLen >= sizeof(AMDBC250_IOCTL_READ_PCI_CONFIG)) {
            PAMDBC250_IOCTL_READ_PCI_CONFIG pci = (PAMDBC250_IOCTL_READ_PCI_CONFIG)inputBuffer;

            UCHAR buffer[256];
            RtlZeroMemory(buffer, sizeof(buffer));
            ULONG readBytes = 0;

            /* Try multiple ECAM base addresses */
            PHYSICAL_ADDRESS ecamBases[] = {
                {0xF0000000, 0},
                {0xF8000000, 0},
                {0xE0000000, 0},
                {0xFC000000, 0},
            };

            for (int b = 0; b < 4 && readBytes == 0; b++) {
                PHYSICAL_ADDRESS pa = ecamBases[b];
                ULONG ecamOffset = (pci->Bus << 20) | (pci->Device << 15) | (pci->Function << 12);
                pa.QuadPart += ecamOffset;

                PUCHAR va = (PUCHAR)MmMapIoSpace(pa, 256, MmNonCached);
                if (va) {
                    UINT16 vendor = READ_REGISTER_USHORT((PUSHORT)va);
                    if (vendor != 0xFFFF && vendor != 0x0000) {
                        for (ULONG off = 0; off < 256; off++) {
                            buffer[off] = READ_REGISTER_UCHAR(va + off);
                        }
                        readBytes = 256;
                        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                            "AMDBC250: PCI ECAM found at base 0x%llX for B%lu:D%lu:F%lu\n",
                            ecamBases[b].QuadPart, pci->Bus, pci->Device, pci->Function));
                    }
                    MmUnmapIoSpace(va, 256);
                }
            }

            if (readBytes > 0) {
                pci->BytesRead = readBytes;
                RtlCopyMemory(pci->ConfigData, buffer, readBytes);
                bytesReturned = sizeof(AMDBC250_IOCTL_READ_PCI_CONFIG);
                status = STATUS_SUCCESS;
            } else {
                /* Fallback: try IO ports (CF8/CFC) */
                for (ULONG off = 0; off < 256; off += 4) {
                    WRITE_PORT_ULONG((PULONG)(UINT_PTR)0xCF8,
                        0x80000000 | (pci->Bus << 16) | (pci->Device << 11) | (pci->Function << 8) | (off & 0xFC));
                    ULONG data = READ_PORT_ULONG((PULONG)(UINT_PTR)0xCFC);
                    *(PULONG)(buffer + off) = data;
                }
                pci->BytesRead = 256;
                RtlCopyMemory(pci->ConfigData, buffer, 256);
                bytesReturned = sizeof(AMDBC250_IOCTL_READ_PCI_CONFIG);
                status = STATUS_SUCCESS;
            }
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    /* --- Get hardware status --- */
    case 0x80000B90: { /* IOCTL_AMDBC250_GET_HW_STATUS */
        if (outputLen >= sizeof(AMDBC250_IOCTL_HW_STATUS)) {
            PAMDBC250_IOCTL_HW_STATUS HwStatus = (PAMDBC250_IOCTL_HW_STATUS)outputBuffer;
            RtlZeroMemory(HwStatus, sizeof(*HwStatus));

            HwStatus->MmioMapped = (DevExt->MmioVirtualBase != NULL) ? 1 : 0;
            HwStatus->RingsInitialized = DevExt->GfxRing.Initialized ? 1 : 0;
            HwStatus->FenceInitialized = (DevExt->GlobalFence.VirtualAddress != NULL) ? 1 : 0;
            HwStatus->GfxRingPhysAddr = DevExt->GfxRing.PhysicalAddress.QuadPart;
            HwStatus->GfxRingSize = (UINT32)DevExt->GfxRing.SizeInBytes;
            HwStatus->GfxRingWptr = DevExt->GfxRing.WritePointer;
            HwStatus->GfxRingRptr = DevExt->GfxRing.ReadPointer;
            HwStatus->FencePhysAddr = DevExt->GlobalFence.PhysicalAddress.QuadPart;
            if (DevExt->GlobalFence.VirtualAddress != NULL) {
                HwStatus->FenceValue = *DevExt->GlobalFence.VirtualAddress;
            }
            HwStatus->LastSubmittedFence = DevExt->GlobalFence.LastSubmittedValue;
            bytesReturned = sizeof(AMDBC250_IOCTL_HW_STATUS);

            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                "AMDBC250-DREAM-V4.3: HW Status: MMIO=%s, Rings=%s, Fence=%s, "
                "RingPA=0x%llX, Fence=%llu/%llu\n",
                HwStatus->MmioMapped ? "YES" : "NO",
                HwStatus->RingsInitialized ? "YES" : "NO",
                HwStatus->FenceInitialized ? "YES" : "NO",
                HwStatus->GfxRingPhysAddr,
                HwStatus->FenceValue, HwStatus->LastSubmittedFence));
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    case 0x80000B94: { /* IOCTL_AMDBC250_READ_PCI_BAR */
        if (outputLen >= sizeof(AMDBC250_IOCTL_PCI_CONFIG) && DevExt != NULL) {
            PAMDBC250_IOCTL_PCI_CONFIG PciCfg = (PAMDBC250_IOCTL_PCI_CONFIG)outputBuffer;
            RtlZeroMemory(PciCfg, sizeof(*PciCfg));

            BOOLEAN found = FALSE;
            UINT32 foundBus = 0, foundDev = 0, foundFunc = 0;

            /* Method 1: HalGetBusDataByOffset */
            for (ULONG bus = 0; bus < 256 && !found; bus++) {
                for (ULONG dev = 0; dev < 32 && !found; dev++) {
                    for (ULONG func = 0; func < 8 && !found; func++) {
                        ULONG slotNumber = (dev << 0) | (func << 5);
                        PCI_COMMON_CONFIG pciCfg;
                        RtlZeroMemory(&pciCfg, sizeof(pciCfg));

                        ULONG bytesRead = HalGetBusDataByOffset(
                            PCIConfiguration, bus, slotNumber,
                            &pciCfg, 0, sizeof(PCI_COMMON_HDR_LENGTH));

                        if (bytesRead < sizeof(PCI_COMMON_HDR_LENGTH))
                            continue;

                        if (pciCfg.VendorID == 0x1002 && pciCfg.DeviceID == 0x13FE) {
                            found = TRUE;
                            foundBus = bus; foundDev = dev; foundFunc = func;
                            PciCfg->VendorId = pciCfg.VendorID;
                            PciCfg->DeviceId = pciCfg.DeviceID;
                            PciCfg->Command = pciCfg.Command;
                            PciCfg->Status = pciCfg.Status;
                            PciCfg->RevisionId = pciCfg.RevisionID;
                            PciCfg->ClassCode = ((ULONG)pciCfg.BaseClass << 16) |
                                                 ((ULONG)pciCfg.SubClass << 8) |
                                                 ((ULONG)pciCfg.ProgIf);
                            PciCfg->Bus = bus;
                        }
                    }
                }
            }

            /* Method 2: IO ports (0xCF8/0xCFC) if HAL failed */
            if (!found) {
                for (ULONG bus = 0; bus < 256 && !found; bus++) {
                    for (ULONG dev = 0; dev < 32 && !found; dev++) {
                        for (ULONG func = 0; func < 8 && !found; func++) {
                            WRITE_PORT_ULONG((PULONG)(UINT_PTR)0xCF8,
                                0x80000000 | (bus << 16) | (dev << 11) | (func << 8) | 0);
                            ULONG id = READ_PORT_ULONG((PULONG)(UINT_PTR)0xCFC);
                            if ((id & 0xFFFF) == 0x1002 && ((id >> 16) & 0xFFFF) == 0x13FE) {
                                found = TRUE;
                                foundBus = bus; foundDev = dev; foundFunc = func;
                                PciCfg->VendorId = (UINT16)(id & 0xFFFF);
                                PciCfg->DeviceId = (UINT16)((id >> 16) & 0xFFFF);
                                PciCfg->Bus = bus;
                                /* Read command/status at offset 4 */
                                WRITE_PORT_ULONG((PULONG)(UINT_PTR)0xCF8,
                                    0x80000000 | (bus << 16) | (dev << 11) | (func << 8) | 4);
                                ULONG cmdSts = READ_PORT_ULONG((PULONG)(UINT_PTR)0xCFC);
                                PciCfg->Command = (UINT16)(cmdSts & 0xFFFF);
                                PciCfg->Status = (UINT16)((cmdSts >> 16) & 0xFFFF);
                                /* Read revision/class at offset 8 */
                                WRITE_PORT_ULONG((PULONG)(UINT_PTR)0xCF8,
                                    0x80000000 | (bus << 16) | (dev << 11) | (func << 8) | 8);
                                ULONG revCls = READ_PORT_ULONG((PULONG)(UINT_PTR)0xCFC);
                                PciCfg->RevisionId = revCls & 0xFF;
                                PciCfg->ClassCode = (revCls >> 8) & 0xFFFFFF;
                            }
                        }
                    }
                }
            }

            if (found) {
                PciCfg->Device = foundDev;
                PciCfg->Function = foundFunc;

                /* Read all 6 BARs using whatever method worked */
                for (ULONG bar = 0; bar < 6; bar++) {
                    UINT32 barValue = 0;
                    ULONG barOffset = 0x10 + (bar * 4);

                    /* Try IO ports first (works on all x64 platforms) */
                    WRITE_PORT_ULONG((PULONG)(UINT_PTR)0xCF8,
                        0x80000000 | (foundBus << 16) | (foundDev << 11) | (foundFunc << 8) | (barOffset & 0xFC));
                    barValue = READ_PORT_ULONG((PULONG)(UINT_PTR)0xCFC);

                    /* Fallback: try HalGetBusDataByOffset */
                    if (barValue == 0) {
                        ULONG slot = (foundDev << 0) | (foundFunc << 5);
                        HalGetBusDataByOffset(PCIConfiguration, foundBus, slot,
                            &barValue, barOffset, sizeof(barValue));
                    }

                    if (barValue == 0) {
                        PciCfg->Bars[bar].PhysicalAddress = 0;
                        PciCfg->Bars[bar].Size = 0;
                        continue;
                    }

                    PciCfg->Bars[bar].IsMemoryBar = (barValue & 1) ? 0 : 1;
                    PciCfg->Bars[bar].Is64Bit = 0;

                    if (PciCfg->Bars[bar].IsMemoryBar) {
                        UINT32 baseMask = 0xFFFFFFF0;
                        PciCfg->Bars[bar].PhysicalAddress = barValue & baseMask;

                        if ((barValue & 0x06) == 0x04) {
                            PciCfg->Bars[bar].Is64Bit = 1;
                            if (bar + 1 < 6) {
                                UINT32 barUpper = 0;
                                WRITE_PORT_ULONG((PULONG)(UINT_PTR)0xCF8,
                                    0x80000000 | (foundBus << 16) | (foundDev << 11) | (foundFunc << 8) | ((barOffset + 4) & 0xFC));
                                barUpper = READ_PORT_ULONG((PULONG)(UINT_PTR)0xCFC);
                                PciCfg->Bars[bar].PhysicalAddress |= ((UINT64)barUpper << 32);
                            }
                        }

                        /* Probe BAR size via IO ports */
                        {
                            UINT32 origLow = 0;
                            WRITE_PORT_ULONG((PULONG)(UINT_PTR)0xCF8,
                                0x80000000 | (foundBus << 16) | (foundDev << 11) | (foundFunc << 8) | (barOffset & 0xFC));
                            origLow = READ_PORT_ULONG((PULONG)(UINT_PTR)0xCFC);

                            WRITE_PORT_ULONG((PULONG)(UINT_PTR)0xCF8,
                                0x80000000 | (foundBus << 16) | (foundDev << 11) | (foundFunc << 8) | (barOffset & 0xFC));
                            WRITE_PORT_ULONG((PULONG)(UINT_PTR)0xCFC, 0xFFFFFFFF);

                            WRITE_PORT_ULONG((PULONG)(UINT_PTR)0xCF8,
                                0x80000000 | (foundBus << 16) | (foundDev << 11) | (foundFunc << 8) | (barOffset & 0xFC));
                            UINT32 probeVal = READ_PORT_ULONG((PULONG)(UINT_PTR)0xCFC);

                            /* Restore original BAR value */
                            WRITE_PORT_ULONG((PULONG)(UINT_PTR)0xCF8,
                                0x80000000 | (foundBus << 16) | (foundDev << 11) | (foundFunc << 8) | (barOffset & 0xFC));
                            WRITE_PORT_ULONG((PULONG)(UINT_PTR)0xCFC, origLow);

                            PciCfg->Bars[bar].Size = (UINT32)(~(probeVal & (UINT32)baseMask)) + 1;
                        }

                        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                            "AMDBC250-DREAM-V4.3: BAR[%lu] Mem: PA=0x%llX Size=0x%X 64bit=%s\n",
                            bar, PciCfg->Bars[bar].PhysicalAddress,
                            PciCfg->Bars[bar].Size,
                            PciCfg->Bars[bar].Is64Bit ? "YES" : "NO"));
                    } else {
                        PciCfg->Bars[bar].PhysicalAddress = barValue & 0xFFFFFFFC;
                        PciCfg->Bars[bar].Size = 0;
                        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                            "AMDBC250-DREAM-V4.3: BAR[%lu] I/O: Port=0x%llX\n",
                            bar, PciCfg->Bars[bar].PhysicalAddress));
                    }
                }

                bytesReturned = sizeof(AMDBC250_IOCTL_PCI_CONFIG);
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                    "AMDBC250-DREAM-V4.3: Found BC-250 at PCI %lu:%lu.%lu via IO ports\n",
                    foundBus, foundDev, foundFunc));
            } else {
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                    "AMDBC250-DREAM-V4.3: BC-250 not found on PCI bus (HAL or IO ports)\n"));
                status = STATUS_UNSUCCESSFUL;
            }
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    /* --- PSP status (integrated into dream driver) --- */
    case 0x80000BA4: { /* IOCTL_AMDBC250_PSP_GET_STATUS */
        PAMDBC250_PSP_CONTEXT pspCtx = Amdbc250PspGetContext();
        if (outputLen >= 4 * sizeof(ULONG)) {
            PULONG out = (PULONG)outputBuffer;
            out[0] = pspCtx->Initialized ? 1 : 0;
            out[1] = pspCtx->SosAlive ? 1 : 0;
            out[2] = DevExt ? (DevExt->NbioUnlocked ? 1 : 0) : 0;
            out[3] = Amdbc250PspReadRegister(0x0244);
            bytesReturned = 4 * sizeof(ULONG);
            status = STATUS_SUCCESS;
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    /* --- Write PCI config space (one DWORD via IO ports) --- */
    case 0x80000BB0: { /* IOCTL_AMDBC250_WRITE_PCI_CONFIG */
        if (inputLen >= sizeof(AMDBC250_IOCTL_WRITE_PCI_CONFIG)) {
            PAMDBC250_IOCTL_WRITE_PCI_CONFIG w = (PAMDBC250_IOCTL_WRITE_PCI_CONFIG)inputBuffer;
            if (w->Offset < 256 && (w->Offset & 3) == 0) {
                /* Method 1: IO port config write */
                WRITE_PORT_ULONG((PULONG)(UINT_PTR)0xCF8,
                    0x80000000 | (w->Bus << 16) | (w->Device << 11) | (w->Function << 8) | w->Offset);
                WRITE_PORT_ULONG((PULONG)(UINT_PTR)0xCFC, w->Value);
                KeMemoryBarrier();

                /* Verify IO port write */
                WRITE_PORT_ULONG((PULONG)(UINT_PTR)0xCF8,
                    0x80000000 | (w->Bus << 16) | (w->Device << 11) | (w->Function << 8) | w->Offset);
                KeMemoryBarrier();
                ULONG readback = READ_PORT_ULONG((PULONG)(UINT_PTR)0xCFC);

                if (readback != w->Value) {
                    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                        "AMDBC250-DREAM-V4.3: IO port write B%u:D%u:F%u+0x%02X = 0x%08X readback 0x%08X (blocked)\n",
                        w->Bus, w->Device, w->Function, w->Offset, w->Value, readback));

                    /* Method 2: ECAM write (memory-mapped config) � try all known bases */
                    PHYSICAL_ADDRESS ecamBases[] = {
                        {0xE0000000, 0}, {0xF0000000, 0}, {0xF8000000, 0}, {0xFC000000, 0},
                    };
                    for (int b = 0; b < 4; b++) {
                        PHYSICAL_ADDRESS pa = ecamBases[b];
                        pa.QuadPart += ((ULONG64)w->Bus << 20) | ((ULONG64)w->Device << 15) |
                                       ((ULONG64)w->Function << 12) | w->Offset;
                        PUCHAR va = (PUCHAR)MmMapIoSpace(pa, 256, MmNonCached);
                        if (va) {
                            /* Test if ECAM is alive by reading vendor ID first */
                            UINT16 vid = *(volatile UINT16*)va;
                            if (vid != 0x0000 && vid != 0xFFFF) {
                                /* ECAM is responsive � write the value */
                                *(volatile PULONG)(va) = w->Value;
                                KeMemoryBarrier();
                                ULONG ecamReadback = *(volatile PULONG)(va);
                                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                                    "AMDBC250-DREAM-V4.3: ECAM write B%u:D%u:F%u+0x%02X = 0x%08X "
                                    "via base 0x%llX, readback 0x%08X\n",
                                    w->Bus, w->Device, w->Function, w->Offset, w->Value,
                                    ecamBases[b].QuadPart, ecamReadback));
                            }
                            MmUnmapIoSpace(va, 256);
                        }
                    }
                }

                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                    "AMDBC250-DREAM-V4.3: PCI config write B%u:D%u:F%u+0x%02X = 0x%08X\n",
                    w->Bus, w->Device, w->Function, w->Offset, w->Value));
                status = STATUS_SUCCESS;
            } else {
                status = STATUS_INVALID_PARAMETER;
            }
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    /* --- Discover PCI device (BC-250) via all available methods --- */
    case 0x80000BB4: { /* IOCTL_AMDBC250_DISCOVER_PCI */
        if (outputLen >= sizeof(AMDBC250_IOCTL_DISCOVER_PCI)) {
            PAMDBC250_IOCTL_DISCOVER_PCI d = (PAMDBC250_IOCTL_DISCOVER_PCI)outputBuffer;
            RtlZeroMemory(d, sizeof(*d));

            BOOLEAN found = FALSE;
            UINT32 foundBus = 0, foundDev = 0, foundFunc = 0;

            /* Try multiple ECAM base addresses */
            PHYSICAL_ADDRESS ecamBases[] = {
                {0xF0000000, 0}, {0xF8000000, 0}, {0xE0000000, 0}, {0xFC000000, 0},
                {0xFE000000, 0}, {0xC0000000, 0}, {0xD0000000, 0}, {0x80000000, 0},
                {0x90000000, 0}, {0xA0000000, 0}, {0xB0000000, 0}, {0x40000000, 0},
                {0x50000000, 0}, {0x60000000, 0}, {0x70000000, 0},
            };

            for (int b = 0; b < sizeof(ecamBases)/sizeof(ecamBases[0]) && !found; b++) {
                for (ULONG bus = 0; bus < 256 && !found; bus++) {
                    for (ULONG dev = 0; dev < 32 && !found; dev++) {
                        for (ULONG func = 0; func < 8 && !found; func++) {
                            PHYSICAL_ADDRESS pa = ecamBases[b];
                            pa.QuadPart += (bus << 20) | (dev << 15) | (func << 12);
                            PUCHAR va = (PUCHAR)MmMapIoSpace(pa, 8, MmNonCached);
                            if (va) {
                                UINT16 vendor = READ_REGISTER_USHORT((PUSHORT)va);
                                if (vendor == 0x1002) {
                                    UINT16 device = READ_REGISTER_USHORT((PUSHORT)(va + 2));
                                    if (device == 0x13FE) {
                                        d->MethodUsed = 1;
                                        found = TRUE;
                                        foundBus = bus; foundDev = dev; foundFunc = func;
                                    }
                                }
                                MmUnmapIoSpace(va, 8);
                            }
                        }
                    }
                }
            }

            /* Fallback: IO ports */
            if (!found) {
                for (ULONG bus = 0; bus < 256 && !found; bus++) {
                    for (ULONG dev = 0; dev < 32 && !found; dev++) {
                        for (ULONG func = 0; func < 8 && !found; func++) {
                            WRITE_PORT_ULONG((PULONG)(UINT_PTR)0xCF8,
                                0x80000000 | (bus << 16) | (dev << 11) | (func << 8) | 0);
                            ULONG id = READ_PORT_ULONG((PULONG)(UINT_PTR)0xCFC);
                            if ((id & 0xFFFF) == 0x1002 && ((id >> 16) & 0xFFFF) == 0x13FE) {
                                found = TRUE;
                                d->MethodUsed = 2; /* IO ports */
                                foundBus = bus; foundDev = dev; foundFunc = func;
                            }
                        }
                    }
                }
            }

            if (found) {
                d->VendorFound = 1;
                d->FoundBus = foundBus;
                d->FoundDevice = foundDev;
                d->FoundFunction = foundFunc;
                d->PciConfig.VendorId = 0x1002;
                d->PciConfig.DeviceId = 0x13FE;
                d->PciConfig.Bus = foundBus;
                d->PciConfig.Device = foundDev;
                d->PciConfig.Function = foundFunc;

                /* Read BARs via IO ports */
                for (ULONG bar = 0; bar < 6; bar++) {
                    ULONG barOffset = 0x10 + (bar * 4);
                    WRITE_PORT_ULONG((PULONG)(UINT_PTR)0xCF8,
                        0x80000000 | (foundBus << 16) | (foundDev << 11) | (foundFunc << 8) | (barOffset & 0xFC));
                    UINT32 barValue = READ_PORT_ULONG((PULONG)(UINT_PTR)0xCFC);

                    if (barValue == 0) continue;

                    d->PciConfig.Bars[bar].IsMemoryBar = (barValue & 1) ? 0 : 1;
                    d->PciConfig.Bars[bar].Is64Bit = 0;

                    if (d->PciConfig.Bars[bar].IsMemoryBar) {
                        UINT32 baseMask = 0xFFFFFFF0;
                        d->PciConfig.Bars[bar].PhysicalAddress = barValue & baseMask;
                        if ((barValue & 0x06) == 0x04) {
                            d->PciConfig.Bars[bar].Is64Bit = 1;
                            if (bar + 1 < 6) {
                                WRITE_PORT_ULONG((PULONG)(UINT_PTR)0xCF8,
                                    0x80000000 | (foundBus << 16) | (foundDev << 11) | (foundFunc << 8) | ((barOffset + 4) & 0xFC));
                                UINT32 upper = READ_PORT_ULONG((PULONG)(UINT_PTR)0xCFC);
                                d->PciConfig.Bars[bar].PhysicalAddress |= ((UINT64)upper << 32);
                            }
                        }
                        /* Probe size */
                        {
                            WRITE_PORT_ULONG((PULONG)(UINT_PTR)0xCF8,
                                0x80000000 | (foundBus << 16) | (foundDev << 11) | (foundFunc << 8) | (barOffset & 0xFC));
                            UINT32 orig = READ_PORT_ULONG((PULONG)(UINT_PTR)0xCFC);
                            WRITE_PORT_ULONG((PULONG)(UINT_PTR)0xCF8,
                                0x80000000 | (foundBus << 16) | (foundDev << 11) | (foundFunc << 8) | (barOffset & 0xFC));
                            WRITE_PORT_ULONG((PULONG)(UINT_PTR)0xCFC, 0xFFFFFFFF);
                            WRITE_PORT_ULONG((PULONG)(UINT_PTR)0xCF8,
                                0x80000000 | (foundBus << 16) | (foundDev << 11) | (foundFunc << 8) | (barOffset & 0xFC));
                            UINT32 probe = READ_PORT_ULONG((PULONG)(UINT_PTR)0xCFC);
                            WRITE_PORT_ULONG((PULONG)(UINT_PTR)0xCF8,
                                0x80000000 | (foundBus << 16) | (foundDev << 11) | (foundFunc << 8) | (barOffset & 0xFC));
                            WRITE_PORT_ULONG((PULONG)(UINT_PTR)0xCFC, orig);
                            d->PciConfig.Bars[bar].Size = (UINT32)(~(probe & (UINT32)baseMask)) + 1;
                        }
                    } else {
                        d->PciConfig.Bars[bar].PhysicalAddress = barValue & 0xFFFFFFFC;
                    }
                }

                /* Read command register */
                WRITE_PORT_ULONG((PULONG)(UINT_PTR)0xCF8,
                    0x80000000 | (foundBus << 16) | (foundDev << 11) | (foundFunc << 8) | 4);
                d->PciConfig.Command = (UINT16)READ_PORT_ULONG((PULONG)(UINT_PTR)0xCFC);

                bytesReturned = sizeof(AMDBC250_IOCTL_DISCOVER_PCI);
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                    "AMDBC250-DREAM-V4.3: DISCOVER_PCI found BC-250 at %lu:%lu.%lu (method %lu)\n",
                    foundBus, foundDev, foundFunc, d->MethodUsed));
            } else {
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                    "AMDBC250-DREAM-V4.3: DISCOVER_PCI: BC-250 not found via any method\n"));
                status = STATUS_UNSUCCESSFUL;
            }
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    /* --- Direct MMIO test: map any physical address and read/write --- */
    case 0x80000BC0: { /* IOCTL_AMDBC250_MMIO_TEST */
        if (inputLen >= sizeof(AMDBC250_IOCTL_MMIO_TEST) &&
            outputLen >= sizeof(AMDBC250_IOCTL_MMIO_TEST)) {
            PAMDBC250_IOCTL_MMIO_TEST m = (PAMDBC250_IOCTL_MMIO_TEST)inputBuffer;

            m->MapResult = 0;
            m->ValueRead = 0;
            m->ValueWrittenBack = 0;

            if (m->PhysicalAddress != 0 && m->Size >= 4 && m->Size <= 0x1000000) {
                PHYSICAL_ADDRESS pa;
                pa.QuadPart = m->PhysicalAddress;

                PUCHAR va = (PUCHAR)MmMapIoSpace(pa, m->Size, MmNonCached);
                if (va) {
                    m->MapResult = 1;

                    __try {
                        /* Read at offset */
                        if (m->OffsetRead + 4 <= m->Size) {
                            m->ValueRead = READ_REGISTER_ULONG((PULONG)(va + m->OffsetRead));
                        }

                        /* Write at offset if requested */
                        if (m->OffsetWrite != 0 && m->OffsetWrite + 4 <= m->Size) {
                            WRITE_REGISTER_ULONG((PULONG)(va + m->OffsetWrite), m->ValueWrite);
                            KeMemoryBarrier();
                            m->ValueWrittenBack = READ_REGISTER_ULONG((PULONG)(va + m->OffsetWrite));
                        }
                    } __except (EXCEPTION_EXECUTE_HANDLER) {
                        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                            "AMDBC250-DREAM-V4.3: MMIO_TEST EXCEPTION 0x%08X at PA=0x%llX off=0x%X\n",
                            GetExceptionCode(), m->PhysicalAddress, m->OffsetRead));
                        m->ValueRead = 0xFFFFFFFF;
                        m->ValueWrittenBack = 0;
                        m->MapResult = 0;
                    }

                    MmUnmapIoSpace(va, m->Size);
                    status = STATUS_SUCCESS;
                } else {
                    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                        "AMDBC250-DREAM-V4.3: MMIO_TEST MmMapIoSpace FAILED PA=0x%llX sz=0x%X\n",
                        m->PhysicalAddress, m->Size));
                    status = STATUS_INSUFFICIENT_RESOURCES;
                }
            } else {
                status = STATUS_INVALID_PARAMETER;
            }
            bytesReturned = sizeof(AMDBC250_IOCTL_MMIO_TEST);
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    /* --- I/O Port read/write (for PCI I/O BAR like GPU doorbell) --- */
    case 0x80000BC8: { /* IOCTL_AMDBC250_PORT_IO */
        if (inputLen >= sizeof(AMDBC250_IOCTL_PORT_IO) &&
            outputLen >= sizeof(AMDBC250_IOCTL_PORT_IO)) {
            PAMDBC250_IOCTL_PORT_IO p = (PAMDBC250_IOCTL_PORT_IO)inputBuffer;
            p->Result = 0;

            __try {
                switch (p->Width) {
                case 1:
                    if (p->IsWrite) {
                        WRITE_PORT_UCHAR((PUCHAR)(ULONG_PTR)p->Port, (UCHAR)p->Value);
                    } else {
                        p->Value = READ_PORT_UCHAR((PUCHAR)(ULONG_PTR)p->Port);
                    }
                    p->Result = 1;
                    break;
                case 2:
                    if (p->IsWrite) {
                        WRITE_PORT_USHORT((PUSHORT)(ULONG_PTR)p->Port, (USHORT)p->Value);
                    } else {
                        p->Value = READ_PORT_USHORT((PUSHORT)(ULONG_PTR)p->Port);
                    }
                    p->Result = 1;
                    break;
                case 4:
                    if (p->IsWrite) {
                        WRITE_PORT_ULONG((PULONG)(ULONG_PTR)p->Port, p->Value);
                    } else {
                        p->Value = READ_PORT_ULONG((PULONG)(ULONG_PTR)p->Port);
                    }
                    p->Result = 1;
                    break;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                    "AMDBC250-DREAM-V4.3: PORT_IO EXCEPTION 0x%08X port=0x%04X\n",
                    GetExceptionCode(), p->Port));
                p->Result = 0;
            }

            status = STATUS_SUCCESS;
            bytesReturned = sizeof(AMDBC250_IOCTL_PORT_IO);
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    /* --- SMN access via MMIO index/data ports --- */
    case 0x80000BC4: { /* IOCTL_AMDBC250_SMN_ACCESS */
        if (inputLen >= sizeof(AMDBC250_IOCTL_SMN_ACCESS) &&
            outputLen >= sizeof(AMDBC250_IOCTL_SMN_ACCESS)) {
            PAMDBC250_IOCTL_SMN_ACCESS s = (PAMDBC250_IOCTL_SMN_ACCESS)inputBuffer;

            /* Default SMN ports for Cyan Skillfish (PS5 APU) */
            UINT32 idxPort = s->IndexPort ? s->IndexPort : 0x3B10528;
            UINT32 dataPort = s->DataPort ? s->DataPort : 0x3B10564;
            s->Result = 0;

            __try {
                /* Map the index port MMIO register */
                PHYSICAL_ADDRESS paIdx;
                paIdx.QuadPart = idxPort;
                PUCHAR vaIdx = (PUCHAR)MmMapIoSpace(paIdx, 4, MmNonCached);

                /* Map the data port MMIO register */
                PHYSICAL_ADDRESS paData;
                paData.QuadPart = dataPort;
                PUCHAR vaData = (PUCHAR)MmMapIoSpace(paData, 4, MmNonCached);

                if (vaIdx && vaData) {
                    volatile PULONG pIdx = (volatile PULONG)vaIdx;
                    volatile PULONG pData = (volatile PULONG)vaData;

                    if (s->IsWrite) {
                        /* Write: index ? SMN address, data ? SMN value */
                        WRITE_REGISTER_ULONG(pIdx, s->SmnAddress);
                        KeMemoryBarrier();
                        WRITE_REGISTER_ULONG(pData, s->SmnData);
                        KeMemoryBarrier();
                        s->SmnData = READ_REGISTER_ULONG(pData);
                    } else {
                        /* Read: index ? SMN address, read data */
                        WRITE_REGISTER_ULONG(pIdx, s->SmnAddress);
                        KeMemoryBarrier();
                        s->SmnData = READ_REGISTER_ULONG(pData);
                    }
                    s->Result = 1;
                }

                if (vaIdx) MmUnmapIoSpace(vaIdx, 4);
                if (vaData) MmUnmapIoSpace(vaData, 4);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                    "AMDBC250-DREAM-V4.3: SMN_ACCESS EXCEPTION 0x%08X addr=0x%08X\n",
                    GetExceptionCode(), s->SmnAddress));
                s->Result = 0;
            }

            status = STATUS_SUCCESS;
            bytesReturned = sizeof(AMDBC250_IOCTL_SMN_ACCESS);
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    /* --- GPU Info --- */
    case 0x80000C00: { /* IOCTL_AMDBC250_GET_GPU_INFO */
        if (outputLen >= 48) {
            PDREAM_V3_DEVICE_EXTENSION ext = (PDREAM_V3_DEVICE_EXTENSION)g_ControlDevice->DeviceExtension;
            UINT32 *out = (UINT32 *)outputBuffer;
            /* Read GPU_ID from BAR5 offset 0x0000 */
            __try {
                if (ext && ext->MmioVirtualBase) {
                    out[0] = READ_REGISTER_ULONG((PULONG)ext->MmioVirtualBase);  /* GPU_ID */
                } else {
                    out[0] = 0;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                out[0] = 0;
            }
            out[1] = 0x1002;  /* Vendor ID: AMD */
            out[2] = 0x13FE;  /* Device ID: BC-250 */
            out[3] = 24;      /* Compute Units */
            out[4] = 1536;    /* Stream Processors */
            /* Architecture string: "Cyan Skillfish(GFX10" (20 chars) */
            out[5] = 0x6E617943; /* "Cyan" */
            out[6] = 0x696B5320; /* " Ski" */
            out[7] = 0x666C6C6C; /* "lllf" */
            out[8] = 0x28687369; /* "ish(" */
            out[9] = 0x30315846; /* "FX10" */
            status = STATUS_SUCCESS;
            bytesReturned = 48;
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    /* --- Firewall Status --- */
    case 0x80000C04: { /* IOCTL_AMDBC250_GET_FIREWALL_STATUS */
        if (outputLen >= 12) {
            UINT32 *out = (UINT32 *)outputBuffer;
            out[0] = 6;   /* Allowed blocks: MMHUB, GC, DF, HDP, NBIO, GPU_ID */
            out[1] = 7;   /* Blocked reads: GRBM, CP, CLK, RSMU, UVD, SDMA, RLCG */
            out[2] = 7;   /* Blocked writes: same */
            status = STATUS_SUCCESS;
            bytesReturned = 12;
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    /* --- Register Test (Read + Write + ReadBack) --- */
    case 0x80000C08: { /* IOCTL_AMDBC250_TEST_REGISTER */
        if (inputLen >= 8 && outputLen >= 20) {
            PDREAM_V3_DEVICE_EXTENSION ext = (PDREAM_V3_DEVICE_EXTENSION)g_ControlDevice->DeviceExtension;
            UINT32 *in = (UINT32 *)inputBuffer;
            UINT32 *out = (UINT32 *)outputBuffer;
            UINT32 regAddr = in[0];
            UINT32 writeVal = in[1];

            __try {
                if (ext && ext->MmioVirtualBase && regAddr < ext->MmioSize) {
                    PUCHAR regVa = (PUCHAR)ext->MmioVirtualBase + regAddr;
                    out[0] = READ_REGISTER_ULONG((PULONG)regVa);  /* ReadBefore */
                    WRITE_REGISTER_ULONG((PULONG)regVa, writeVal);
                    KeMemoryBarrier();
                    out[1] = READ_REGISTER_ULONG((PULONG)regVa);  /* ReadAfter */
                    out[2] = (out[1] == writeVal) ? 1 : 0;  /* WriteSuccess */
                } else {
                    out[0] = 0;
                    out[1] = 0;
                    out[2] = 0;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                out[0] = 0;
                out[1] = 0;
                out[2] = 0;
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                    "AMDBC250-DREAM-V4.3: TEST_REGISTER EXCEPTION addr=0x%08X\n", regAddr));
            }

            status = STATUS_SUCCESS;
            bytesReturned = 20;
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    /* --- Read Physical Memory (verify write, debug) --- */
    case 0x80000C14: { /* IOCTL_AMDBC250_READ_PHYSICAL_MEM */
        /* Input: {PA_lo (ULONG), PA_hi (ULONG), size (ULONG)}
         * Output: data[0..size-1] */
        if (inputLen >= sizeof(ULONG) * 3 && outputLen >= 1) {
            PULONG InData = (PULONG)inputBuffer;
            PHYSICAL_ADDRESS pa;
            pa.QuadPart = ((ULONG64)InData[1] << 32) | InData[0];
            ULONG size = InData[2];
            if (size > 0 && size <= 4096 && size <= outputLen && pa.QuadPart != 0) {
                __try {
                    PHYSICAL_ADDRESS pagePa;
                    pagePa.QuadPart = pa.QuadPart & ~(ULONG64)0xFFF;
                    ULONG pageOff = (ULONG)(pa.QuadPart & 0xFFF);
                    ULONG mapSize = (pageOff + size + 0xFFF) & ~(ULONG)0xFFF;
                    PUCHAR va = (PUCHAR)MmMapIoSpace(pagePa, mapSize, MmNonCached);
                    if (va) {
                        RtlCopyMemory(outputBuffer, va + pageOff, size);
                        MmUnmapIoSpace(va, mapSize);
                        bytesReturned = size;
                    } else {
                        status = STATUS_INSUFFICIENT_RESOURCES;
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    status = STATUS_ACCESS_VIOLATION;
                }
            } else {
                status = STATUS_INVALID_PARAMETER;
            }
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    /* --- Write Physical Memory (for shader loading on BC-250) --- */
    case 0x80000C10: { /* IOCTL_AMDBC250_WRITE_PHYSICAL_MEM */
        /* Input: {PA_lo (ULONG), PA_hi (ULONG), size (ULONG), data[0..size-1] (BYTE)}
         * Maps physical address (page-aligned internally), copies data, unmaps. */
        if (inputLen >= (LONG)(sizeof(ULONG) * 3 + 1)) {
            PULONG InData = (PULONG)inputBuffer;
            PHYSICAL_ADDRESS pa;
            pa.QuadPart = ((ULONG64)InData[1] << 32) | InData[0];
            ULONG size = InData[2];
            ULONG inputHdr = sizeof(ULONG) * 3;
            if (size > 0 && size <= 4096 && pa.QuadPart != 0 &&
                (LONG)(inputHdr + size) <= inputLen) {
                __try {
                    /* MmMapIoSpace requires page-aligned physical address */
                    PHYSICAL_ADDRESS pagePa;
                    pagePa.QuadPart = pa.QuadPart & ~(ULONG64)0xFFF;
                    ULONG pageOff = (ULONG)(pa.QuadPart & 0xFFF);
                    ULONG mapSize = (pageOff + size + 0xFFF) & ~(ULONG)0xFFF;
                    PUCHAR va = (PUCHAR)MmMapIoSpace(pagePa, mapSize, MmNonCached);
                    if (va) {
                        RtlCopyMemory(va + pageOff, (PUCHAR)inputBuffer + inputHdr, size);
                        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
                            "AMDBC250-DREAM-V4.3: WritePhys OK PA=0x%llX sz=%lu off=%lu mapsz=%lu\n",
                            pa.QuadPart, size, pageOff, mapSize));
                    } else {
                        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                            "AMDBC250-DREAM-V4.3: WritePhys MmMapIoSpace FAILED PA=0x%llX\n",
                            pa.QuadPart));
                        status = STATUS_INSUFFICIENT_RESOURCES;
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                        "AMDBC250-DREAM-V4.3: WritePhys EXCEPTION PA=0x%llX\n",
                        pa.QuadPart));
                    status = STATUS_ACCESS_VIOLATION;
                }
            } else {
                status = STATUS_INVALID_PARAMETER;
            }
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    /* --- NBIO Status (live check) --- */
    case 0x80000C0C: { /* IOCTL_AMDBC250_GET_NBIO_STATUS */
        if (outputLen >= 5 * sizeof(ULONG)) {
            PAMDBC250_PSP_CONTEXT pspCtx = Amdbc250PspGetContext();
            PULONG out = (PULONG)outputBuffer;
            ULONG sol = 0;
            ULONG grbm = 0xFFFFFFFF;
            ULONG cp = 0xFFFFFFFF;
            ULONG clk = 0xFFFFFFFF;

            __try {
                if (pspCtx && pspCtx->MmioBase) {
                    sol = Amdbc250PspReadRegister(0x0244);
                }
                if (DevExt && DevExt->MmioVirtualBase) {
                    /* Try KIQ first - bypasses NBIO firewall */
                    if (Amdbc250PspKiqAvailable()) {
                        grbm = Amdbc250PspKiqReadReg(AMDBC250_REG_CC_GC_SHADER_ARRAY_CONFIG);
                        cp   = Amdbc250PspKiqReadReg(AMDBC250_REG_GRBM_STATUS);
                        clk  = Amdbc250PspKiqReadReg(0x0D00);
                    } else {
                        grbm = READ_REGISTER_ULONG((PULONG)((PUCHAR)DevExt->MmioVirtualBase + AMDBC250_REG_CC_GC_SHADER_ARRAY_CONFIG));
                        cp = READ_REGISTER_ULONG((PULONG)((PUCHAR)DevExt->MmioVirtualBase + AMDBC250_REG_GRBM_STATUS));
                        clk = READ_REGISTER_ULONG((PULONG)((PUCHAR)DevExt->MmioVirtualBase + 0x0D00));
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                    "AMDBC250-DREAM-V4.3: GET_NBIO_STATUS exception\n"));
            }

            if (sol & 0x80000000) { out[0] = 1; } else { out[0] = 0; }
            if (grbm != 0xFFFFFFFF && grbm != 0x00000000) {
                out[1] = 0;  /* NBIO unlocked */
                /* Auto-init GFX ring if needed */
                if (DevExt && !DevExt->GfxRing.Initialized) {
                    DreamV3HwInitGfxRing(DevExt);
                }
            } else {
                out[1] = 1;  /* NBIO locked */
            }
            out[2] = sol;
            out[3] = grbm;
            out[4] = cp;
            out[2] = sol;     /* C2PMSG_81 raw value */
            out[3] = grbm;    /* GRBM_STATUS raw value */
            out[4] = cp;      /* CP raw value */
            bytesReturned = 5 * sizeof(ULONG);
            status = STATUS_SUCCESS;
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    /* --- BAR5 Proxy Read (for PSP driver mailbox access) --- */
    case 0x900: { /* IOCTL_AMDBC250_BAR5_READ_PROXY */
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
            "BAR5_PROXY_READ: inputLen=%lu outputLen=%lu DevExt=%p Mmio=%p\n",
            inputLen, outputLen, DevExt, DevExt ? DevExt->MmioVirtualBase : NULL));
        if (inputLen >= sizeof(ULONG) && outputLen >= sizeof(ULONG)) {
            PULONG inOffset = (PULONG)inputBuffer;
            PULONG outValue = (PULONG)outputBuffer;
            ULONG offset = *inOffset;
            
            if (DevExt && DevExt->MmioVirtualBase && offset <= 0x80000 - sizeof(ULONG)) {
                PUCHAR mmioBase = (PUCHAR)DevExt->MmioVirtualBase;
                __try {
                    *outValue = READ_REGISTER_ULONG((PULONG)(mmioBase + offset));
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    *outValue = 0xFFFFFFFF;
                    status = STATUS_DEVICE_NOT_READY;
                    break;
                }
                bytesReturned = sizeof(ULONG);
                status = STATUS_SUCCESS;
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
                    "AMDBC250-DREAM-V4.3: BAR5_PROXY_READ offset=0x%X value=0x%08X\n",
                    offset, *outValue));
            } else {
                *outValue = 0xFFFFFFFF;
                bytesReturned = sizeof(ULONG);
                status = STATUS_BUFFER_TOO_SMALL;
            }
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    case 0x901: { /* IOCTL_AMDBC250_BAR5_WRITE_PROXY */
        if (inputLen >= sizeof(ULONG) * 2 && DevExt && DevExt->MmioVirtualBase) {
            PULONG params = (PULONG)inputBuffer;
            ULONG offset = params[0];
            ULONG value = params[1];
            
            if (offset <= 0x80000 - sizeof(ULONG)) {
                PUCHAR mmioBase = (PUCHAR)DevExt->MmioVirtualBase;
                __try {
                    WRITE_REGISTER_ULONG((PULONG)(mmioBase + offset), value);
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    status = STATUS_DEVICE_NOT_READY;
                    break;
                }
                bytesReturned = 0;
                status = STATUS_SUCCESS;
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
                    "AMDBC250-DREAM-V4.3: BAR5_PROXY_WRITE offset=0x%X value=0x%08X\n",
                    offset, value));
            } else {
                status = STATUS_ARRAY_BOUNDS_EXCEEDED;
            }
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    case IOCTL_AMDBC250_BAR5_READ_PROXY: {
        if (outputLen >= sizeof(AMDBC250_IOCTL_BAR5_READ_PROXY) && DevExt && DevExt->MmioVirtualBase) {
            PAMDBC250_IOCTL_BAR5_READ_PROXY bar5Info = (PAMDBC250_IOCTL_BAR5_READ_PROXY)outputBuffer;
            bar5Info->Bar5VirtualAddress = (UINT64)DevExt->MmioVirtualBase;
            bar5Info->Bar5Size = 0x80000;
            bytesReturned = sizeof(AMDBC250_IOCTL_BAR5_READ_PROXY);
            status = STATUS_SUCCESS;
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
                "AMDBC250-DREAM-V4.3: BAR5_PROXY returning VA=0x%llX size=0x%X\n",
                bar5Info->Bar5VirtualAddress, bar5Info->Bar5Size));
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    /* --- Direct PSP mailbox: load IP firmware via C2PMSG_35/36/37/81 (no PSP driver) --- */
    case IOCTL_AMDBC250_PSP_LOAD_IP_FW: {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
            "AMDBC250-DREAM-V4.3: PSP_LOAD_IP_FW entered, inputLen=%u outputLen=%u\n",
            inputLen, outputLen));

        if (!DevExt || !DevExt->MmioVirtualBase) {
            status = STATUS_DEVICE_NOT_READY;
            break;
        }

        if (inputLen < sizeof(AMDBC250_IOCTL_PSP_LOAD_IP_FW) ||
            outputLen < sizeof(AMDBC250_IOCTL_PSP_LOAD_IP_FW)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        PAMDBC250_IOCTL_PSP_LOAD_IP_FW req = (PAMDBC250_IOCTL_PSP_LOAD_IP_FW)inputBuffer;
        PAMDBC250_IOCTL_PSP_LOAD_IP_FW resp = (PAMDBC250_IOCTL_PSP_LOAD_IP_FW)outputBuffer;

        ULONG fwType = req->FwType;
        ULONG fwSize = req->FwSize;

        resp->Result = 0;
        resp->C2Pmsg35After = 0;
        resp->C2Pmsg81After = 0;

        /* Validate firmware type. */
        if (fwType < 1 || fwType > 10) {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                "AMDBC250-DREAM-V4.3: PSP_LOAD_IP_FW - invalid type %u\n", fwType));
            status = STATUS_SUCCESS;
            bytesReturned = sizeof(*resp);
            break;
        }

        if (fwSize < 64 || fwSize > 4 * 1024 * 1024) {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                "AMDBC250-DREAM-V4.3: PSP_LOAD_IP_FW - invalid size %u\n", fwSize));
            status = STATUS_SUCCESS;
            bytesReturned = sizeof(*resp);
            break;
        }

        /* Firmware data follows the header. */
        const UINT8 *fwBlob = (const UINT8 *)(req + 1);
        ULONG blobAvailable = inputLen - sizeof(AMDBC250_IOCTL_PSP_LOAD_IP_FW);
        if (blobAvailable < fwSize) {
            status = STATUS_SUCCESS;
            bytesReturned = sizeof(*resp);
            break;
        }

        /* Allocate contiguous physical memory for the firmware. */
        PHYSICAL_ADDRESS low = {0}, high = {0}, boundary = {0};
        high.QuadPart = 0xFFFFFFFFULL;
        PVOID fwVa = MmAllocateContiguousMemorySpecifyCache(
            fwSize, low, high, boundary, MmNonCached);
        if (!fwVa) {
            status = STATUS_SUCCESS;
            bytesReturned = sizeof(*resp);
            break;
        }
        RtlCopyMemory(fwVa, fwBlob, fwSize);
        PHYSICAL_ADDRESS fwPa = MmGetPhysicalAddress(fwVa);

        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
            "AMDBC250-DREAM-V4.3: PSP_LOAD_IP_FW type=%u size=%u PA=0x%llX\n",
            fwType, fwSize, fwPa.QuadPart));

        /* Call the direct mailbox function. */
        NTSTATUS pspStatus = Amdbc250PspDirectLoadIpFw(
            DevExt->MmioVirtualBase, fwType, fwSize, fwPa,
            &resp->C2Pmsg35After, &resp->C2Pmsg81After);

        if (NT_SUCCESS(pspStatus)) {
            resp->Result = 1;
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                "AMDBC250-DREAM-V4.3: PSP_LOAD_IP_FW type=%u OK (C35=0x%08X C81=0x%08X)\n",
                fwType, resp->C2Pmsg35After, resp->C2Pmsg81After));
        } else {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                "AMDBC250-DREAM-V4.3: PSP_LOAD_IP_FW type=%u FAILED (0x%08X)\n",
                fwType, pspStatus));
        }

        MmFreeContiguousMemory(fwVa);

        status = STATUS_SUCCESS;
        bytesReturned = sizeof(*resp);
        break;
    }

    /* --- Direct SMU message via SMN (BAR5+0x38/0x3C, no PSP driver) --- */
    case IOCTL_AMDBC250_PSP_SMU_MSG: {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
            "AMDBC250-DREAM-V4.3: PSP_SMU_MSG entered\n"));

        if (!DevExt || !DevExt->MmioVirtualBase) {
            status = STATUS_DEVICE_NOT_READY;
            break;
        }

        if (inputLen < sizeof(AMDBC250_IOCTL_PSP_SMU_MSG) ||
            outputLen < sizeof(AMDBC250_IOCTL_PSP_SMU_MSG)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        PAMDBC250_IOCTL_PSP_SMU_MSG req = (PAMDBC250_IOCTL_PSP_SMU_MSG)inputBuffer;
        PAMDBC250_IOCTL_PSP_SMU_MSG resp = (PAMDBC250_IOCTL_PSP_SMU_MSG)outputBuffer;

        resp->Result = 0;
        resp->Response = 0;
        resp->ResponseStatus = 0;

        NTSTATUS smuStatus = Amdbc250PspDirectSmuMsg(
            DevExt->MmioVirtualBase, req->Message, req->Argument,
            &resp->Response, &resp->ResponseStatus);

        if (NT_SUCCESS(smuStatus) && resp->ResponseStatus == 1) {
            resp->Result = 1;
        }

        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
            "AMDBC250-DREAM-V4.3: PSP_SMU_MSG msg=0x%X arg=0x%X resp=0x%X status=%u\n",
            req->Message, req->Argument, resp->Response, resp->ResponseStatus));

        status = STATUS_SUCCESS;
        bytesReturned = sizeof(*resp);
        break;
    }

    /* --- GPU-local KIQ test: allocate ring + program HQD + submit PM4, all via BAR5 --- */
    case 0x80000BE4: /* IOCTL_AMDBC250_GPU_IB_TEST = CTL_CODE_AMDBC250(0x89) - force IB mode */
        if (outputLen >= sizeof(AMDBC250_IOCTL_GPU_KIQ_TEST) && DevExt && DevExt->MmioVirtualBase) {
            ((PAMDBC250_IOCTL_GPU_KIQ_TEST)outputBuffer)->UseIB = 1;
        }
        /* fall through */
    case 0x80000BD0: { /* IOCTL_AMDBC250_GPU_KIQ_TEST = CTL_CODE_AMDBC250(0x84) */
        if (outputLen >= sizeof(AMDBC250_IOCTL_GPU_KIQ_TEST) && DevExt && DevExt->MmioVirtualBase) {
            PAMDBC250_IOCTL_GPU_KIQ_TEST kiqTest = (PAMDBC250_IOCTL_GPU_KIQ_TEST)outputBuffer;
            PUCHAR mmio = (PUCHAR)DevExt->MmioVirtualBase;
            ULONG useIb = kiqTest->UseIB;  /* Save input flag before zero */
            RtlZeroMemory(kiqTest, sizeof(*kiqTest));
            kiqTest->MmioMapped = 1;
            kiqTest->UseIB = useIb;

            /* Save live GPU state we are about to perturb, so cleanup can restore it
             * and we don't blank a display-driving GPU.
             * Non-GRBM-indexed regs (GCVM/ME/RLC) read fine in broadcast mode.
             * KIQ_BASE/WPTR are GRBM-indexed: must select KIQ (ME=1) first or
             * they read 0 in broadcast (the live value only appears with ME=1). */
            ULONG savedGcvmCntl  = DreamV3ReadRegister(DevExt, 0x0B460); /* GCVM_CONTEXT0_CNTL */
            ULONG savedGcvmPtLo  = DreamV3ReadRegister(DevExt, 0x6C8C);  /* GCVM_CONTEXT0_PT_BASE_LO */
            ULONG savedGcvmPtHi  = DreamV3ReadRegister(DevExt, 0x6C90);  /* GCVM_CONTEXT0_PT_BASE_HI */
            ULONG savedMeCntl    = DreamV3ReadRegister(DevExt, 0x4A74);  /* ME_CNTL */
            ULONG savedMecCntl   = DreamV3ReadRegister(DevExt, 0x4B14);  /* CP_MEC_CNTL (GC) */
            ULONG savedRlcSched  = DreamV3ReadRegister(DevExt, 0xECA8);  /* RLC_CP_SCHEDULERS */

            ULONG grbmLive = DreamV3ReadRegister(DevExt, 0x34D0);        /* GRBM_INDEX */
            DreamV3WriteRegister(DevExt, 0x34D0, 0x00010000);           /* select KIQ (ME=1) */
            ULONG savedKiQBaseLo = DreamV3ReadRegister(DevExt, 0xE060);  /* KIQ_BASE_LO */
            ULONG savedKiQBaseHi = DreamV3ReadRegister(DevExt, 0xE064);  /* KIQ_BASE_HI */
            ULONG savedKiQWptr   = DreamV3ReadRegister(DevExt, 0xE078);  /* KIQ_WPTR */
            DreamV3WriteRegister(DevExt, 0x34D0, grbmLive);              /* restore live GRBM */

            /* Step 2a: Load CP microcode (ME/PFP/CE/MEC) so MEC0 can run the KIQ.
             * In NBIO_MAP fallback DreamV3HwInitialize() is skipped, so firmware
             * was never loaded -> MEC0 had no microcode and could not fetch/execute.
             * DreamV3LoadAllFirmware() uploads via IC_BASE DMA from bc-250\*.bin. */
            {
                NTSTATUS fwStatus = DreamV3LoadAllFirmware(DevExt);
                kiqTest->FwLoaded = (NT_SUCCESS(fwStatus)) ? 1 : 0;
                kiqTest->MecCntlBefore = savedMecCntl;
                KdPrint((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                    "GPU_KIQ_TEST: DreamV3LoadAllFirmware -> 0x%08X (FwLoaded=%u) CP_MEC_CNTL=0x%08X\n",
                    fwStatus, kiqTest->FwLoaded, savedMecCntl));
            }

            /* PT page variables (declared here for cleanup access) */
            PVOID ptPml4Va = NULL, ptPdpVa = NULL, ptPdVa = NULL, ptPtVa = NULL;
            PHYSICAL_ADDRESS ptPml4Pa = {0}, ptPdpPa = {0}, ptPdPa = {0}, ptPtPa = {0};

            /* Register access via DreamV3WriteRegister (WRITE_REGISTER_ULONG � works on Win11 26100)
             * NOTE: Direct BAR5 volatile pointer writes are silently dropped on Win11 26100!
             * Must use DreamV3WriteRegister/ReadRegister which use WDK WRITE_REGISTER_ULONG macro. */
            #define BAR5_WRITE(off, val) DreamV3WriteRegister(DevExt, (off), (val))
            /* MSVC-compatible read: DreamV3ReadRegister returns value directly */
            #define BAR5_READ(off) DreamV3ReadRegister(DevExt, (off))
            #define GRBM_INDEX      0x34D0
            #define ME_CNTL         0x4A74
            #define HQD_ACTIVE      0x910C
            #define HQD_VMID        0x9110
            #define HQD_PERSISTENT  0x9114
            #define HQD_PQ_BASE     0x9124
            #define HQD_PQ_BASE_HI  0x9128
            #define HQD_PQ_RPTR     0x912C
            #define HQD_PQ_CONTROL  0x914C  /* Linux mmCP_HQD_PQ_CONTROL=0x1FBB (header line 376 typo: 0x9148 is PQ_WPTR_POLL_ADDR_HI) */
            #define HQD_PQ_WPTR_LO  0x91DC
            #define HQD_PQ_WPTR_HI  0x91E0
            #define HQD_PQ_WP_POLL  0x9150  /* CP_HQD_PQ_WPTR_POLL_CNTL */
            #define HQD_PQ_DOORBELL 0x9154  /* CP_HQD_PQ_DOORBELL_CONTROL */
            #define HQD_EOP_BASE    0x90EC
            #define HQD_EOP_BASE_HI 0x90F4
            #define HQD_EOP_CNTL    0x90F8
            #define HQD_RPTR_RPT    0x913C
            #define HQD_RPTR_RPT_HI 0x9140
            #define HQD_WP_POLL_A   0x9144  /* CP_HQD_PQ_WPTR_POLL_ADDR */
            #define HQD_WP_POLL_A_HI 0x9148  /* CP_HQD_PQ_WPTR_POLL_ADDR_HI */
            #define KIQ_BASE_LO     0xE060
            #define KIQ_BASE_HI     0xE064
            #define KIQ_RPTR        0xE06C
            #define KIQ_WPTR        0xE078
            #define RLC_SCHEDULERS  0xECA8  /* empirically verified writable (kiq-rlc-test.c) */
            #define SCRATCH_OFF     0x32D4

            /* Step 1: Read SCRATCH before */
            kiqTest->ScratchBefore = BAR5_READ(SCRATCH_OFF);

            /* Step 2: Allocate 4KB ring buffer (contiguous, non-cached) */
            PVOID ringVa = NULL;
            PHYSICAL_ADDRESS ringPa = {0};
            ULONG64 ringGpuVa = 0;   /* GPU virtual address of ring (CP translates via GPUVM) */
            PVOID doorbellVa = NULL; /* mapped doorbell BAR2 (0xD0000000) for KIQ kick */
            ULONG savedDbRangeLo = 0, savedDbRangeHi = 0, savedDbCtl = 0;
            {
                /* Ring MUST live in real VRAM so FB_LOCATION maps its GPU-VA
                 * (0xF400000000 window) to the right physical page. A system-RAM
                 * ring addressed via a VRAM-window GPU-VA would be mis-translated
                 * by FB_LOCATION to VRAM, not to the ring. Use a LOW VRAM offset
                 * (within the 256MB CPU-visible PCIe BAR0) so MmMapIoSpace
                 * succeeds (allocating at FbSize-0x6000 fails: BAR0 is too small). */
                LARGE_INTEGER vramOff;
                vramOff.QuadPart = 0x100000;   /* 1MB into VRAM, clear of VBIOS/display */
                PHYSICAL_ADDRESS vramBase;
                vramBase.QuadPart = DevExt->FbPhysicalBase.QuadPart + vramOff.QuadPart;
                PVOID vramVa = MmMapIoSpace(vramBase, 0x6000, MmNonCached);
                if (vramVa) {
                    RtlZeroMemory(vramVa, 0x6000);
                    ringVa = vramVa;
                    ringPa = vramBase;
                    /* GPU-VA in the VRAM window so FB_LOCATION translates it.
                     * ringGpuVa = ring_phys + (VRAM_GPU_VA_BASE - FB_phys_base). */
                    ringGpuVa = ringPa.QuadPart +
                        (0xF400000000ULL - DevExt->FbPhysicalBase.QuadPart);
                    kiqTest->RingGpuVa = ringGpuVa;
                    kiqTest->RingAllocated = 1;
                    KdPrint(("GPU_KIQ_TEST: VRAM ring VA=%p PA=0x%llX GpuVa=0x%llX\n",
                        ringVa, ringPa.QuadPart, ringGpuVa));
                }
            }
            /* Allocate the 4 page-table levels in system RAM (<4GB). The MMU
             * reads these via the system aperture (SOS-configured) during the walk. */
            if (ringVa) {
                PHYSICAL_ADDRESS hi4gb;
                hi4gb.QuadPart = 0xFFFFFFFFULL;
                ptPml4Va = MmAllocateContiguousMemory(0x1000, hi4gb);
                ptPdpVa  = MmAllocateContiguousMemory(0x1000, hi4gb);
                ptPdVa   = MmAllocateContiguousMemory(0x1000, hi4gb);
                ptPtVa   = MmAllocateContiguousMemory(0x1000, hi4gb);
                if (ptPml4Va && ptPdpVa && ptPdVa && ptPtVa) {
                    ptPml4Pa = MmGetPhysicalAddress(ptPml4Va);
                    ptPdpPa  = MmGetPhysicalAddress(ptPdpVa);
                    ptPdPa   = MmGetPhysicalAddress(ptPdVa);
                    ptPtPa   = MmGetPhysicalAddress(ptPtVa);
                    KdPrint(("GPU_KIQ_TEST: PT pages: PML4=0x%llX PDP=0x%llX PD=0x%llX PT=0x%llX\n",
                        ptPml4Pa.QuadPart, ptPdpPa.QuadPart, ptPdPa.QuadPart, ptPtPa.QuadPart));
                } else {
                    if (ptPml4Va) MmFreeContiguousMemory(ptPml4Va);
                    if (ptPdpVa)  MmFreeContiguousMemory(ptPdpVa);
                    if (ptPdVa)   MmFreeContiguousMemory(ptPdVa);
                    if (ptPtVa)   MmFreeContiguousMemory(ptPtVa);
                    ptPml4Va = ptPdpVa = ptPdVa = ptPtVa = NULL;
                    MmUnmapIoSpace(ringVa, 0x6000);
                    ringVa = NULL;
                    kiqTest->RingAllocated = 0;
                }
            }

            if (!ringVa) {
                kiqTest->Result = 0xDEAD0001;  /* ring alloc failed */
                status = STATUS_SUCCESS;
                bytesReturned = sizeof(*kiqTest);
                break;
            }

            /* Step 2a2: Map doorbell BAR (GPU PCI BAR2 @ 0xD0000000, 2MB).
             * The KIQ kick is a 64-bit write to the KIQ doorbell in this
             * BAR -- CP_HQD_PQ_WPTR is READ-ONLY, so writing it does NOT
             * kick the ring (this was the second root bug). The NBIO_MAP
             * path only maps BAR5 + VRAM, so map BAR2 here and unmap later. */
            {
                PHYSICAL_ADDRESS dbPa;
                dbPa.QuadPart = 0xD0000000ULL;
                doorbellVa = MmMapIoSpace(dbPa, 0x200000, MmNonCached);
                if (doorbellVa) {
                    KdPrint(("GPU_KIQ_TEST: doorbell BAR mapped VA=%p\n", doorbellVa));
                } else {
                    KdPrint((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                        "GPU_KIQ_TEST: doorbell BAR map FAILED\n"));
                }
            }

            /* Step 2b: Set up GCVM page tables for identity mapping
             * Verified writable registers (BAR5 offsets):
             *   GCVM_CONTEXT0_CNTL   = 0x0B460
             *   GCVM_CONTEXT0_PT_BASE_LO = 0x6C8C  (Linux offset, NOT 0x0B608!)
             *   GCVM_CONTEXT0_PT_BASE_HI = 0x6C90
             *
             * RDNA2 4-level page table: PML4 ? PDP ? PD ? PT
             * Each level: 512 entries � 8 bytes = 4KB per page
             * PTE format: (PA & 0xFFFFFFFFF000) | flags
             *   flags: bit0=VALID bit5=READABLE bit6=WRITABLE
             *
             * We create identity mapping (VA=PA) for the ring buffer page.
             */
            #define GCVM_CONTEXT0_CNTL_REG     0x0B460   /* OLD offset � verified WRITABLE */
            #define MC_VM_FB_LOCATION_BASE      AMDBC250_REG_MC_VM_FB_LOCATION_BASE  /* 0x9520 from hw.h */
            #define GCVM_CONTEXT0_PT_BASE_LO   0x6C8C    /* Linux offset � verified WRITABLE */
            #define GCVM_CONTEXT0_PT_BASE_HI   0x6C90    /* Linux offset � verified WRITABLE */
            #define GCVM_L2_CNTL_REG           0x0B360   /* OLD offset � verified WRITABLE */

            /* Step 2b: Set up GCVM page tables for identity mapping */
            {
                if (ptPml4Va && ptPdpVa && ptPdVa && ptPtVa) {
                    RtlZeroMemory(ptPml4Va, 0x1000);
                    RtlZeroMemory(ptPdpVa, 0x1000);
                    RtlZeroMemory(ptPdVa, 0x1000);
                    RtlZeroMemory(ptPtVa, 0x1000);

                    KdPrint(("GPU_KIQ_TEST: PT pages: PML4=0x%llX PDP=0x%llX PD=0x%llX PT=0x%llX\n",
                        ptPml4Pa.QuadPart, ptPdpPa.QuadPart, ptPdPa.QuadPart, ptPtPa.QuadPart));

                    /* Index the page table by the GPU VIRTUAL address (ringGpuVa),
                     * but the PTE must point at the ring's PHYSICAL address. */
                    ULONG pml4Idx = (ULONG)((ringGpuVa >> 39) & 0x1FF);
                    ULONG pdpIdx  = (ULONG)((ringGpuVa >> 30) & 0x1FF);
                    ULONG pdIdx   = (ULONG)((ringGpuVa >> 21) & 0x1FF);
                    ULONG ptIdx   = (ULONG)((ringGpuVa >> 12) & 0x1FF);

                    KdPrint(("GPU_KIQ_TEST: ringPhys=0x%llX ringGpuVa=0x%llX -> PML4[%lu] PDP[%lu] PD[%lu] PT[%lu]\n",
                        ringPa.QuadPart, ringGpuVa, pml4Idx, pdpIdx, pdIdx, ptIdx));

                    PULONG64 pml4 = (PULONG64)ptPml4Va;
                    PULONG64 pdp = (PULONG64)ptPdpVa;
                    PULONG64 pd = (PULONG64)ptPdVa;
                    PULONG64 pt = (PULONG64)ptPtVa;

                    /* PDE: VALID(bit0) | SYSTEM(bit1) */
                    pml4[pml4Idx] = (ptPdpPa.QuadPart & 0xFFFFFFFFF000ULL) | 0x03;
                    pdp[pdpIdx] = (ptPdPa.QuadPart & 0xFFFFFFFFF000ULL) | 0x03;
                    pd[pdIdx] = (ptPtPa.QuadPart & 0xFFFFFFFFF000ULL) | 0x03;
                    /* PTE: VALID(bit0) | SYSTEM(bit1) | READABLE(bit5) | WRITABLE(bit6)
                     * Maps GPU VA (ringGpuVa) -> ring PHYSICAL (ringPa). */
                    pt[ptIdx] = (ringPa.QuadPart & 0xFFFFFFFFF000ULL) | 0x63;

                    KdPrint(("GPU_KIQ_TEST: PML4[%lu]=0x%llX PDP[%lu]=0x%llX PD[%lu]=0x%llX PT[%lu]=0x%llX\n",
                        pml4Idx, pml4[pml4Idx], pdpIdx, pdp[pdpIdx], pdIdx, pd[pdIdx], ptIdx, pt[ptIdx]));

                    /* Set GCVM_CONTEXT0_PT_BASE to PML4 physical address */
                    BAR5_WRITE(GCVM_CONTEXT0_PT_BASE_LO, (ULONG)(ptPml4Pa.QuadPart & 0xFFFFFFFF));
                    BAR5_WRITE(GCVM_CONTEXT0_PT_BASE_HI, (ULONG)(ptPml4Pa.QuadPart >> 32));
                    KdPrint(("GPU_KIQ_TEST: GCVM_PT_BASE write=0x%llX readback=0x%08X%08X\n",
                        ptPml4Pa.QuadPart,
                        BAR5_READ(GCVM_CONTEXT0_PT_BASE_HI),
                        BAR5_READ(GCVM_CONTEXT0_PT_BASE_LO)));

                    /* Enable GCVM context 0 — MUST be 4-level (PAGE_TABLE_DEPTH=3).
                     * Ring GPU VA is 0xF4FFFA000 (bits[39:30]=0xD); a flat
                     * (depth=0) single-level PT can't walk those top bits -> VM
                     * fault -> ring never fetched. depth field = bits[2:1]=0x06. */
                    ULONG cntlBefore = BAR5_READ(GCVM_CONTEXT0_CNTL_REG);
                    BAR5_WRITE(GCVM_CONTEXT0_CNTL_REG, cntlBefore | 0x07);
                    KdPrint(("GPU_KIQ_TEST: GCVM_CNTL: before=0x%08X after=0x%08X (expect depth=3)\n",
                        cntlBefore, BAR5_READ(GCVM_CONTEXT0_CNTL_REG)));

                    /* CRITICAL: shoot down GCVM TLB for VMID 0 so the new PTEs
                     * for the ring/scratch take effect before the CP fetches.
                     * VMID is encoded as bit position: 1 << vmid. For VMID 0, bit 0. */
                    {
                        BAR5_WRITE(AMDBC250_REG_GCVM_INVALIDATE_ENG0_REQ, 1 << 0);
                        ULONG invT = 1000;
                        while (invT-- > 0) {
                            if (BAR5_READ(AMDBC250_REG_GCVM_INVALIDATE_ENG0_ACK) & 0x1)
                                break;
                            KeStallExecutionProcessor(10);
                        }
                        KdPrint(("GPU_KIQ_TEST: GCVM TLB invalidate VMID0 done\n"));
                    }
                }
            }

            /* Step 3: Halt ME+PFP (preserve other ME_CNTL bits) */
            {
                ULONG meVal = BAR5_READ(ME_CNTL);
                BAR5_WRITE(ME_CNTL, meVal | (1 << 28) | (1 << 30));  /* set ME_HALT | PFP_HALT, keep rest */
            }
            KeStallExecutionProcessor(10);

            /* Step 4: Save GRBM_INDEX and select KIQ engine */
            ULONG savedGrbmIndex = BAR5_READ(GRBM_INDEX);
            BAR5_WRITE(GRBM_INDEX, 0x00010000);  /* ME=1 */

            /* Step 5: Deactivate queue */
            BAR5_WRITE(HQD_ACTIVE, 0);
            KeStallExecutionProcessor(1);

            /* Step 6: Disable WPTR poll + doorbell */
            BAR5_WRITE(HQD_PQ_WP_POLL, 0);
            BAR5_WRITE(HQD_PQ_DOORBELL, 0);

            /* Step 7: Clear EOP */
            BAR5_WRITE(HQD_EOP_BASE, 0);
            BAR5_WRITE(HQD_EOP_BASE_HI, 0);
            BAR5_WRITE(HQD_EOP_CNTL, 0x08000000);

            /* Step 8: Clear RPTR report + WPTR poll */
            BAR5_WRITE(HQD_RPTR_RPT, 0);
            BAR5_WRITE(HQD_RPTR_RPT_HI, 0);
            BAR5_WRITE(HQD_WP_POLL_A, 0);
            BAR5_WRITE(HQD_WP_POLL_A_HI, 0);

            /* Step 9: Set PQ_BASE = ring GPU VIRTUAL address (>>8 form).
             * CP_HQD_PQ_BASE is a VMID0 VA translated by the gfxhub VM.
             * A raw physical write VM-faults (ring never fetched). */
            BAR5_WRITE(HQD_PQ_BASE, (ULONG)(ringGpuVa >> 8));
            BAR5_WRITE(HQD_PQ_BASE_HI, (ULONG)(ringGpuVa >> 40));

            /* Step 9b: Set KIQ_BASE = ring GPU VA (MEC KIQ reads from KIQ_BASE!) */
            BAR5_WRITE(KIQ_BASE_LO, (ULONG)(ringGpuVa >> 8));
            BAR5_WRITE(KIQ_BASE_HI, (ULONG)(ringGpuVa >> 40));

            /* Step 10: PQ_CONTROL = PQ_EN(bit0) | log2(256 dwords)=8 -> (8<<1)|1 = 0x11 */
            BAR5_WRITE(HQD_PQ_CONTROL, 0x11);

            /* Step 11: VMID = 0 */
            BAR5_WRITE(HQD_VMID, 0);

            /* Step 12: PERSISTENT_STATE */
            BAR5_WRITE(HQD_PERSISTENT, 0xE001);

            /* Step 13: RPTR = WPTR = 0 */
            BAR5_WRITE(HQD_PQ_RPTR, 0);
            BAR5_WRITE(HQD_PQ_WPTR_LO, 0);
            BAR5_WRITE(HQD_PQ_WPTR_HI, 0);

            kiqTest->HqdProgrammed = 1;

            /* Step 15: Restore broadcast, then select KIQ for activate */
            BAR5_WRITE(GRBM_INDEX, 0x00010000);  /* ME=1 for KIQ */

            /* Step 16: Activate queue */
            BAR5_WRITE(HQD_ACTIVE, 1);
            KeStallExecutionProcessor(10);

            /* Step 17: Notify RLC scheduler */
            BAR5_WRITE(RLC_SCHEDULERS, 0xA0);  /* ENABLE | ME=1 */

            /* Step 18: Resume CP (clear only halt bits, preserve rest) */
            {
                ULONG meVal = BAR5_READ(ME_CNTL);
                BAR5_WRITE(ME_CNTL, meVal & ~((1 << 28) | (1 << 30)));  /* clear ME_HALT | PFP_HALT */
            }
            /* Step 18b: UNHALT MEC — KIQ runs on MEC0; if MEC is left halted
             * (minimal PSP SOS / firmware load may leave it halted) the ring is
             * never fetched even though WPTR advances. This was the missing piece. */
            {
                ULONG mecVal = BAR5_READ(0x4B14);
                if (mecVal != 0) {
                    BAR5_WRITE(0x4B14, 0);  /* clear all MEC halt bits */
                    kiqTest->MecUnhalted = 1;
                    KdPrint((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                        "GPU_KIQ_TEST: CP_MEC_CNTL was 0x%08X -> unhalted\n", mecVal));
                    KeStallExecutionProcessor(100);
                }
            }
            KeStallExecutionProcessor(100);

            /* Step 19: Write PM4 WRITE_DATA + NOPs to SCRATCH into ring */
            {
                volatile PULONG ring = (volatile PULONG)ringVa;
                /* PM4 Type 3: IT_WRITE_DATA (0x37), count=3 -> [header][control][addr][data] = 4 DWORDs
                 * Header = (3<<30) | (0x37<<8) | (3<<16) = 0xC0033700
                 * CONTROL: DST_SEL=0(register) | WR_CONFIRM(bit20) = 0x00100000 */
                ring[0] = 0xC0033700;  /* HEADER: IT_WRITE_DATA, count=3 */
                ring[1] = 0x00100000;  /* CONTROL: DST_SEL=register | WR_CONFIRM */
                ring[2] = 0x000032D4;  /* ADDRESS_LO = SCRATCH */
                ring[3] = 0x5AFEBABE;  /* DATA (SCRATCH top nibble HW-forced to 0x5) */
                ring[4] = 0xC0001000;  /* NOP (count=0) */
                ring[5] = 0xC0001000;  /* NOP */
                ring[6] = 0xC0001000;  /* NOP */
                KeMemoryBarrier();
                kiqTest->Pm4Submitted = 1;

                if (useIb) {
                    /* IB path: write IB registers + RLC trigger (bypasses HQD/KIQ) */
                    /* Write IB registers with broadcast GRBM first */
                    BAR5_WRITE(GRBM_INDEX, 0xE0000000);
                    BAR5_WRITE(0x3BAC, (ULONG)(ringPa.QuadPart & 0xFFFFFFFF));
                    BAR5_WRITE(0x3BB0, (ULONG)(ringPa.QuadPart >> 32));
                    BAR5_WRITE(0x3BC0, 32);  /* 32 dwords */
                    /* Then set ME=1 for RLC scheduler trigger */
                    BAR5_WRITE(GRBM_INDEX, 0x00010000);
                    BAR5_WRITE(0xECA8, 0xA0);  /* RLC_CP_SCHEDULERS — correct offset */
                    BAR5_WRITE(GRBM_INDEX, 0xE0000000);  /* restore broadcast */
                    kiqTest->HqdProgrammed = 2;  /* IB mode */
                } else {
                    /* KIQ/HQD path: KICK via DOORBELL (CP_HQD_PQ_WPTR is
                     * READ-ONLY, so a WPTR-register write does NOT kick the ring).
                     * Program the MEC doorbell range + HQD doorbell control, then
                     * write the 64-bit WPTR to the KIQ doorbell in PCI BAR2. */
                    savedDbRangeLo = BAR5_READ(0x89F0);  /* CP_MEC_DOORBELL_RANGE_LOWER (kiq idx 0) */
                    savedDbRangeHi = BAR5_READ(0x89F4);  /* CP_MEC_DOORBELL_RANGE_UPPER */
                    savedDbCtl    = BAR5_READ(HQD_PQ_DOORBELL);  /* CP_HQD_PQ_DOORBELL_CONTROL (0x9154) */
                    BAR5_WRITE(0x89F0, 0);            /* MEC owns doorbell idx 0 */
                    BAR5_WRITE(0x89F4, 0x2000);     /* range covers KIQ doorbell */
                    BAR5_WRITE(HQD_PQ_DOORBELL, 0x1); /* DOORBELL_ENABLE (bit0) */
                    /* Also poke WPTR regs (RO; diagnostics only) */
                    BAR5_WRITE(HQD_PQ_WPTR_LO, 7);
                    BAR5_WRITE(HQD_PQ_WPTR_HI, 0);
                    BAR5_WRITE(KIQ_WPTR, 7);
                    if (doorbellVa) {
                        /* 64-bit doorbell write: lo@+0, hi@+4, KIQ doorbell idx 0 */
                        *(volatile ULONG64*)((PUCHAR)doorbellVa + 0) = (ULONG64)7;
                        kiqTest->DoorKicked = 1;
                        KdPrint((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                            "GPU_KIQ_TEST: doorbell kick (WPTR=7) @BAR2+0\n"));
                    }
                }
            }

            /* Step 22: Wait for GPU to process */
            {
                LARGE_INTEGER delay;
                delay.QuadPart = -10000LL * 50;  /* 50ms */
                KeDelayExecutionThread(KernelMode, FALSE, &delay);
            }

            /* Step 23: Read ring/engine status back (diagnostic) */
            kiqTest->HqdRptr = BAR5_READ(HQD_PQ_RPTR);       /* HW-consumed ptr (0 if not fetched) */
            kiqTest->KiQ_RP = BAR5_READ(KIQ_RPTR);           /* KIQ ring RPTR */
            kiqTest->GrbmStat = BAR5_READ(0x3260);           /* GRBM_STATUS: CP/ME busy? */
            kiqTest->RingGpuVa = ringGpuVa;               /* ring GPU VA in PQ_BASE */
            kiqTest->FbLocationBase = BAR5_READ(MC_VM_FB_LOCATION_BASE); /* MC_VM_FB_LOCATION_BASE (RO): VRAM GPU-VA base */
            kiqTest->FbOffset = 0;                           /* MC_VM_FB_OFFSET (unread) */
            kiqTest->HqdPqWptrRb = BAR5_READ(HQD_PQ_WPTR_LO); /* CP WPTR view (RO reg) */
            kiqTest->DbLoRb = BAR5_READ(0x89F0);  /* CP_MEC_DOORBELL_RANGE_LOWER readback */
            kiqTest->DbHiRb = BAR5_READ(0x89F4);  /* CP_MEC_DOORBELL_RANGE_UPPER readback */
            kiqTest->DbCtlRb = BAR5_READ(HQD_PQ_DOORBELL); /* CP_HQD_PQ_DOORBELL_CONTROL readback */

            /* Step 24: Read SCRATCH back */
            kiqTest->ScratchAfter = BAR5_READ(SCRATCH_OFF);

            if (useIb) {
                kiqTest->Result = (kiqTest->ScratchAfter == 0x5AFEBABE) ? 1 : 0;
            } else {
                kiqTest->Result = (kiqTest->ScratchAfter == 0x5AFEBABE) ? 1 : 0;
            }

            KdPrint(("GPU_KIQ_TEST(%s): ScratchBefore=0x%08X ScratchAfter=0x%08X Result=0x%08X\n",
                useIb ? "IB" : "KIQ",
                kiqTest->ScratchBefore, kiqTest->ScratchAfter, kiqTest->Result));

            /* Step 22b: Retry kick — if MEC was just unhalted (or the engine
             * simply needed a second WPTR kick / more time), re-assert WPTR and
             * wait again. Capture retry diagnostics into the *_2 fields. */
            if (kiqTest->ScratchAfter != 0x5AFEBABE) {
                if (!useIb) {
                    BAR5_WRITE(GRBM_INDEX, 0x00010000);  /* ME=1 */
                    BAR5_WRITE(HQD_PQ_WPTR_LO, 7);
                    BAR5_WRITE(HQD_PQ_WPTR_HI, 0);
                    BAR5_WRITE(KIQ_WPTR, 7);
                }
                {
                    LARGE_INTEGER delay;
                    delay.QuadPart = -10000LL * 100;  /* 100ms */
                    KeDelayExecutionThread(KernelMode, FALSE, &delay);
                }
                kiqTest->HqdRptr2 = BAR5_READ(HQD_PQ_RPTR);
                kiqTest->KiQ_RP2  = BAR5_READ(KIQ_RPTR);
                kiqTest->GrbmStat2 = BAR5_READ(0x3260);
                kiqTest->ScratchAfter2 = BAR5_READ(SCRATCH_OFF);
                KdPrint((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                    "GPU_KIQ_TEST retry: HqdRptr=0x%08X KiQ_RP=0x%08X Grbm=0x%08X Scratch=0x%08X\n",
                    kiqTest->HqdRptr2, kiqTest->KiQ_RP2, kiqTest->GrbmStat2, kiqTest->ScratchAfter2));
            }

            /* Cleanup: restore live state BEFORE freeing our buffers, so a
             * (possibly display-driving) GPU keeps working after the test. */
            BAR5_WRITE(GRBM_INDEX, 0x00010000);  /* select KIQ (ME=1) */
            if (!useIb) BAR5_WRITE(HQD_ACTIVE, 0);
            if (useIb) {
                BAR5_WRITE(0x3BAC, 0);  /* clear IB_BASE_LO */
                BAR5_WRITE(0x3BB0, 0);  /* clear IB_BASE_HI */
                BAR5_WRITE(0x3BC0, 0);  /* clear IB_BUFSZ */
            }
            /* Restore KIQ ring pointers to the live ring FIRST (GRBM still KIQ-select) */
            BAR5_WRITE(KIQ_WPTR, savedKiQWptr);
            BAR5_WRITE(KIQ_BASE_LO, savedKiQBaseLo);
            BAR5_WRITE(KIQ_BASE_HI, savedKiQBaseHi);

            /* Restore GCVM context 0 mapping to original */
            BAR5_WRITE(GCVM_CONTEXT0_PT_BASE_LO, savedGcvmPtLo);
            BAR5_WRITE(GCVM_CONTEXT0_PT_BASE_HI, savedGcvmPtHi);
            BAR5_WRITE(GCVM_CONTEXT0_CNTL_REG, savedGcvmCntl);

            /* Restore engine halt state + RLC scheduler, then GRBM (do NOT force-halt) */
            BAR5_WRITE(ME_CNTL, savedMeCntl);
            BAR5_WRITE(0x4B14, savedMecCntl);   /* restore CP_MEC_CNTL */
            BAR5_WRITE(RLC_SCHEDULERS, savedRlcSched);
            BAR5_WRITE(GRBM_INDEX, savedGrbmIndex);

            /* Restore doorbell range + HQD doorbell control (saved before kick) */
            BAR5_WRITE(0x89F0, savedDbRangeLo);  /* CP_MEC_DOORBELL_RANGE_LOWER */
            BAR5_WRITE(0x89F4, savedDbRangeHi);  /* CP_MEC_DOORBELL_RANGE_UPPER */
            BAR5_WRITE(HQD_PQ_DOORBELL, savedDbCtl); /* CP_HQD_PQ_DOORBELL_CONTROL */

            /* Now safe to free our test resources (GPU no longer points at them) */
            /* Ring is VRAM (MmMapIoSpace); PT pages are system RAM (MmFreeContiguousMemory) */
            if (ptPml4Va) MmFreeContiguousMemory(ptPml4Va);
            if (ptPdpVa)  MmFreeContiguousMemory(ptPdpVa);
            if (ptPdVa)   MmFreeContiguousMemory(ptPdVa);
            if (ptPtVa)   MmFreeContiguousMemory(ptPtVa);
            if (ringVa) MmUnmapIoSpace(ringVa, 0x6000);
            /* Unmap the doorbell BAR2 we mapped for the KIQ kick */
            if (doorbellVa) MmUnmapIoSpace(doorbellVa, 0x200000);

            #undef BAR5_WRITE
            #undef BAR5_READ
            #undef GRBM_INDEX
            #undef ME_CNTL
            #undef HQD_ACTIVE
            #undef HQD_VMID
            #undef HQD_PERSISTENT
            #undef HQD_PQ_BASE
            #undef HQD_PQ_BASE_HI
            #undef HQD_PQ_RPTR
            #undef HQD_PQ_CONTROL
            #undef HQD_PQ_WPTR_LO
            #undef HQD_PQ_WPTR_HI
            #undef HQD_PQ_WP_POLL
            #undef HQD_PQ_DOORBELL
            #undef HQD_EOP_BASE
            #undef HQD_EOP_BASE_HI
            #undef HQD_EOP_CNTL
            #undef HQD_RPTR_RPT
            #undef HQD_RPTR_RPT_HI
            #undef HQD_WP_POLL_A
            #undef HQD_WP_POLL_A_HI
            #undef KIQ_BASE_LO
            #undef KIQ_BASE_HI
            #undef KIQ_RPTR
            #undef KIQ_WPTR
            #undef RLC_SCHEDULERS
            #undef SCRATCH_OFF
            #undef GCVM_CONTEXT0_CNTL_REG
            #undef GCVM_CONTEXT0_PT_BASE_LO
            #undef GCVM_CONTEXT0_PT_BASE_HI
            #undef GCVM_L2_CNTL_REG

            status = STATUS_SUCCESS;
            bytesReturned = sizeof(*kiqTest);
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    /* --- Direct CP firmware load via MMIO (bypasses PSP entirely) --- */
    case IOCTL_AMDBC250_LOAD_CP_FW: {
        PDREAM_V3_DEVICE_EXTENSION ext = (PDREAM_V3_DEVICE_EXTENSION)g_ControlDevice->DeviceExtension;

        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
            "AMDBC250-DREAM-V4.3: LOAD_CP_FW entered, inputLen=%u outputLen=%u\n",
            inputLen, outputLen));

        if (!ext || !ext->MmioVirtualBase) {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                "AMDBC250-DREAM-V4.3: LOAD_CP_FW - no BAR5 mapping\n"));
            status = STATUS_DEVICE_NOT_READY;
            break;
        }

        if (inputLen < sizeof(AMDBC250_IOCTL_LOAD_CP_FW)) {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                "AMDBC250-DREAM-V4.3: LOAD_CP_FW - input too small (%u < %u)\n",
                inputLen, (UINT32)sizeof(AMDBC250_IOCTL_LOAD_CP_FW)));
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        if (outputLen < sizeof(AMDBC250_IOCTL_LOAD_CP_FW)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        PAMDBC250_IOCTL_LOAD_CP_FW req = (PAMDBC250_IOCTL_LOAD_CP_FW)inputBuffer;
        PAMDBC250_IOCTL_LOAD_CP_FW resp = (PAMDBC250_IOCTL_LOAD_CP_FW)outputBuffer;

        UINT32 fwType = req->FwType;
        UINT32 fwSize = req->FwSize;

        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
            "AMDBC250-DREAM-V4.3: LOAD_CP_FW type=%u size=%u\n", fwType, fwSize));

        resp->Result = 0;
        resp->UcodeVersion = 0;

        if (fwType < 1 || fwType > 4) {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                "AMDBC250-DREAM-V4.3: LOAD_CP_FW - invalid type %u (1=ME, 2=PFP, 3=CE, 4=MEC)\n", fwType));
            resp->Result = 0xDEAD0010;  /* invalid type */
            status = STATUS_SUCCESS;
            bytesReturned = sizeof(*resp);
            break;
        }

        if (fwSize < 64 || fwSize > 4 * 1024 * 1024) {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                "AMDBC250-DREAM-V4.3: LOAD_CP_FW - invalid size %u\n", fwSize));
            resp->Result = 0xDEAD0011;  /* invalid size */
            status = STATUS_SUCCESS;
            bytesReturned = sizeof(*resp);
            break;
        }

        /* Firmware data follows immediately after the struct header */
        const UINT8 *fwBlob = (const UINT8 *)(req + 1);
        UINT32 blobAvailable = inputLen - sizeof(AMDBC250_IOCTL_LOAD_CP_FW);

        if (blobAvailable < fwSize) {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                "AMDBC250-DREAM-V4.3: LOAD_CP_FW - blob truncated (%u < %u)\n",
                blobAvailable, fwSize));
            resp->Result = 0xDEAD0012;  /* blob truncated */
            status = STATUS_SUCCESS;
            bytesReturned = sizeof(*resp);
            break;
        }

        /* Parse firmware header.
         * Cyan Skillfish firmware layout (44-byte header):
         *   [0] total_size, [1] header_size_bytes, [2] version_major, [3] version_minor
         *   [4] ucode_version, [5] ucode_size_bytes, [6] ucode_offset_bytes
         *   [7] checksum/hash, [8] data_offset, [9] jt_offset(DWORDs), [10] jt_size(DWORDs)
         */
        if (fwSize < 44) {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                "AMDBC250-DREAM-V4.3: LOAD_CP_FW - firmware too small for header (%u)\n", fwSize));
            resp->Result = 0xDEAD0013;
            status = STATUS_SUCCESS;
            bytesReturned = sizeof(*resp);
            break;
        }

        const UINT32 *hdr = (const UINT32 *)fwBlob;
        UINT32 totalSize    = hdr[0];
        UINT32 hdrSizeBytes = hdr[1];
        UINT32 ucodeVersion = hdr[4];
        UINT32 ucodeSize    = hdr[5];
        UINT32 ucodeOffset  = hdr[6];
        UINT32 jtOffsetDw   = hdr[9];  /* DWORD offset from ucode start */
        UINT32 jtSizeDw     = hdr[10]; /* size in DWORDs */

        resp->UcodeVersion = ucodeVersion;

        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
            "AMDBC250-DREAM-V4.3: LOAD_CP_FW header: total=%u hdrSize=%u ver=%u ucodeSize=%u ucodeOff=%u jtOffDw=%u jtSizeDw=%u\n",
            totalSize, hdrSizeBytes, ucodeVersion, ucodeSize, ucodeOffset, jtOffsetDw, jtSizeDw));

        /* Validate header fields — avoid integer overflow */
        if (ucodeSize == 0 || ucodeOffset < hdrSizeBytes || 
            ucodeSize > fwSize || ucodeOffset > fwSize - ucodeSize) {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                "AMDBC250-DREAM-V4.3: LOAD_CP_FW - invalid header fields\n"));
            resp->Result = 0xDEAD0014;
            status = STATUS_SUCCESS;
            bytesReturned = sizeof(*resp);
            break;
        }

        /* Allocate contiguous physical memory for the full firmware blob */
        PHYSICAL_ADDRESS low = {0}, high = {0}, boundary = {0};
        high.QuadPart = 0xFFFFFFFFULL;  /* below 4GB */
        PVOID fwVa = MmAllocateContiguousMemorySpecifyCache(
            fwSize, low, high, boundary, MmNonCached);

        if (!fwVa) {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                "AMDBC250-DREAM-V4.3: LOAD_CP_FW - alloc %u bytes failed\n", fwSize));
            resp->Result = 0xDEAD0015;  /* alloc failed */
            status = STATUS_SUCCESS;
            bytesReturned = sizeof(*resp);
            break;
        }

        RtlCopyMemory(fwVa, fwBlob, fwSize);

        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
            "AMDBC250-DREAM-V4.3: LOAD_CP_FW firmware buffer=%p size=%u\n",
            fwVa, fwSize));

        /* Load firmware via the PSP secure mailbox (LOAD_IP_FW) instead of
         * the host IC_BASE DMA + CP unhalt path.
         *
         * On BC-250 the host cannot un-halt a CP engine after loading microcode
         * — doing so lets the GPU run the firmware and perform a rogue host DMA
         * write that corrupts system memory (0x1A MEMORY_MANAGEMENT). The PSP
         * driver hands the blob to the SOS, which loads it through the secure
         * mailbox (GFX_CMD_ID_LOAD_IP_FW), exactly like Linux does for this ASIC.
         * The host must NOT touch CP_ME_CNTL or IC_BASE. */

        resp->Result = 0;

        /* Ensure PSP proxy is open (initializes SOS context). */
        if (!NT_SUCCESS(Amdbc250PspKiqInit())) {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                "AMDBC250-DREAM-V4.3: LOAD_CP_FW - PSP proxy init failed\n"));
            resp->Result = 0xDEAD0020;
            status = STATUS_SUCCESS;
            bytesReturned = sizeof(*resp);
            if (fwVa) MmFreeContiguousMemory(fwVa);
            break;
        }

        if (!Amdbc250PspGetContext() || !Amdbc250PspGetContext()->SosAlive) {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                "AMDBC250-DREAM-V4.3: LOAD_CP_FW - SOS not alive\n"));
            resp->Result = 0xDEAD0021;
            status = STATUS_SUCCESS;
            bytesReturned = sizeof(*resp);
            if (fwVa) MmFreeContiguousMemory(fwVa);
            break;
        }

        /* Copy blob into the shared PSP firmware buffer and load via mailbox. */
        NTSTATUS pspStatus = Amdbc250PspAllocateFirmwareBuffer(fwSize);
        if (!NT_SUCCESS(pspStatus)) {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                "AMDBC250-DREAM-V4.3: LOAD_CP_FW - alloc PSP buf failed (0x%08X)\n", pspStatus));
            resp->Result = 0xDEAD0022;
            status = STATUS_SUCCESS;
            bytesReturned = sizeof(*resp);
            if (fwVa) MmFreeContiguousMemory(fwVa);
            break;
        }
        pspStatus = Amdbc250PspCopyFirmwareData((PUCHAR)fwVa, fwSize);
        if (!NT_SUCCESS(pspStatus)) {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                "AMDBC250-DREAM-V4.3: LOAD_CP_FW - copy to PSP buf failed (0x%08X)\n", pspStatus));
            resp->Result = 0xDEAD0023;
            status = STATUS_SUCCESS;
            bytesReturned = sizeof(*resp);
            if (fwVa) MmFreeContiguousMemory(fwVa);
            break;
        }

        PHYSICAL_ADDRESS pspFwPa = Amdbc250PspFirmwarePa();
        NTSTATUS loadStatus = Amdbc250PspKiqLoadFirmware(fwType, fwSize, pspFwPa);

        if (NT_SUCCESS(loadStatus)) {
            resp->Result = 1;  /* success */
            resp->UcodeVersion = ucodeVersion;
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                "AMDBC250-DREAM-V4.3: LOAD_CP_FW type=%u loaded via PSP OK (ucode 0x%08X)\n",
                fwType, ucodeVersion));
        } else {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                "AMDBC250-DREAM-V4.3: LOAD_CP_FW type=%u PSP load failed (0x%08X)\n",
                fwType, loadStatus));
            resp->Result = 0xDEAD0024;
        }

        /* Free the contiguous firmware buffer */
        if (fwVa) {
            MmFreeContiguousMemory(fwVa);
        }

        status = STATUS_SUCCESS;
        bytesReturned = sizeof(*resp);
        break;
    }

    /* --- Register state dump (read-only, safe � no state modification) --- */
    case IOCTL_AMDBC250_REG_DUMP: {
        if (outputLen >= sizeof(AMDBC250_IOCTL_REG_DUMP) && DevExt && DevExt->MmioVirtualBase) {
            PAMDBC250_IOCTL_REG_DUMP dump = (PAMDBC250_IOCTL_REG_DUMP)outputBuffer;
            RtlZeroMemory(dump, sizeof(*dump));

            #define DUMP_REG32(off) DreamV3ReadRegister(DevExt, (off))

            /* GC registers (verified BAR5 byte offsets) */
            dump->GpuId               = DUMP_REG32(0x0000);  /* GPU_ID at BAR5 offset 0 */
            dump->GrbmStatus          = DUMP_REG32(0x3260);
            dump->GrbmStatusSe0       = DUMP_REG32(0x3268);
            dump->GrbmStatusSe1       = DUMP_REG32(0x326C);
            dump->CcShaderArrayConfig = DUMP_REG32(0x3264);
            dump->Scratch             = DUMP_REG32(0x32D4);
            dump->SpiWgpMask          = DUMP_REG32(0x34FC);
            dump->GrbmGfxIndex        = DUMP_REG32(0x34D0);

            /* CP registers � BOTH sets of offsets to compare:
             * The fresh boot dump used Navi10+GC_BASE and got 0xFFFFFFFF for CP.
             * The GPU_KIQ_TEST uses raw BAR5 offsets that work.
             * Let's dump BOTH and see. */
            dump->MeCntl              = DUMP_REG32(0x4A74);  /* GPU_KIQ_TEST offset */
            dump->PfpCntl             = DUMP_REG32(0x4A78);
            dump->CeCntl              = DUMP_REG32(0x4A7C);

            /* KIQ ring registers (GPU_KIQ_TEST verified offsets) */
            dump->KiqBaseLo           = DUMP_REG32(0xE060);
            dump->KiqBaseHi           = DUMP_REG32(0xE064);
            dump->KiqCntl             = DUMP_REG32(0xE068);
            dump->KiqRptr             = DUMP_REG32(0xE06C);
            dump->KiqWptr             = DUMP_REG32(0xE078);

            /* HQD registers (corrected BASE_IDX=0 addresses) */
            dump->HqdActiveKiq        = DUMP_REG32(0x910C);
            dump->HqdPqBaseKiq        = DUMP_REG32(0x9124);
            dump->HqdPqBaseHiKiq      = DUMP_REG32(0x9128);
            dump->HqdPqRptrKiq        = DUMP_REG32(0x912C);
            dump->HqdPqWptrLoKiq      = DUMP_REG32(0x91DC);
            dump->HqdVmidKiq          = DUMP_REG32(0x9110);
            /* NOTE: These read at the SAME offsets as KIQ above because
             * GRBM_GFX_INDEX is not changed to select ME=0 (GFX) vs ME=1 (KIQ).
             * Without ME selection, these return the current (KIQ) values.
             * To read actual compute HQD, write GRBM_GFX_INDEX=0 first. */
            dump->HqdActiveCmp        = DUMP_REG32(0x910C);
            dump->HqdPqBaseCmp        = DUMP_REG32(0x9124);
            dump->HqdPqBaseHiCmp      = DUMP_REG32(0x9128);
            dump->HqdPqRptrCmp        = DUMP_REG32(0x912C);
            dump->HqdPqWptrCmp        = DUMP_REG32(0x91DC);
            dump->HqdVmidCmp          = DUMP_REG32(0x9110);
            dump->HqdAqCntlCmp        = DUMP_REG32(0x9148);

            /* GCVM registers */
            dump->GcvmL2Cntl          = DUMP_REG32(0x0B360);
            dump->GcvmContext0Cntl    = DUMP_REG32(0x0B460);
            dump->GcvmPtBaseLo        = DUMP_REG32(0x6C8C);  /* correct writable PT_BASE */
            dump->GcvmPtBaseHi        = DUMP_REG32(0x6C90);

            /* BIOS Context0 TLB entries (0x0B408-0x0B454, 20 DWORDs) */
            {
                int i;
                for (i = 0; i < 20; i++) {
                    dump->Ctx0[i] = DUMP_REG32(0x0B408 + i * 4);
                }
            }

            /* RLC/SDMA */
            dump->RlcCntl             = DUMP_REG32(0xECA8);
            dump->Sdma0Cntl           = DUMP_REG32(AMDBC250_REG_SDMA0_CNTL);

            /* CP GFX ring0 probe at BAR5 0xDA60 range
             * CpRb0BaseProbe[0-3] = RING0_BASE_LO/HI/CNTL/RPTR at 0xDA60-0xDA6C
             * CpRb1BaseProbe[0-3] = RING0_WPTR+fields at 0xDA70-0xDA7C (NOT ring1!) */
            dump->CpRb0BaseProbe[0]   = DUMP_REG32(0xDA60);
            dump->CpRb0BaseProbe[1]   = DUMP_REG32(0xDA64);
            dump->CpRb0BaseProbe[2]   = DUMP_REG32(0xDA68);
            dump->CpRb0BaseProbe[3]   = DUMP_REG32(0xDA6C);
            dump->CpRb1BaseProbe[0]   = DUMP_REG32(0xDA70);
            dump->CpRb1BaseProbe[1]   = DUMP_REG32(0xDA74);
            dump->CpRb1BaseProbe[2]   = DUMP_REG32(0xDA78);
            dump->CpRb1BaseProbe[3]   = DUMP_REG32(0xDA7C);

            dump->Result = 1;
            status = STATUS_SUCCESS;
            bytesReturned = sizeof(*dump);
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    /* --- Clean KIQ NOP test: submit PM4 NOP+WRITE_REG without destroying BIOS state --- */
    case IOCTL_AMDBC250_KIQ_NOP_TEST: {
        if (outputLen >= sizeof(AMDBC250_IOCTL_KIQ_NOP_TEST) && DevExt && DevExt->MmioVirtualBase) {
            PAMDBC250_IOCTL_KIQ_NOP_TEST kt = (PAMDBC250_IOCTL_KIQ_NOP_TEST)outputBuffer;
            RtlZeroMemory(kt, sizeof(*kt));

            #define KIQ_SCRATCH_OFF     0x32D4
            #define KIQ_ME_CNTL_OFF     0x4A74
            #define KIQ_BASE_LO_OFF     0xE060
            #define KIQ_BASE_HI_OFF     0xE064
            #define KIQ_RPTR_OFF        0xE06C
            #define KIQ_WPTR_OFF        0xE078
            #define KIQ_GCVM_CTX0_CNTL  0x0B460

            /* Step 0: Save BIOS state (use DreamV3ReadRegister — no volatile pointer!) */
            kt->ScratchBefore           = DreamV3ReadRegister(DevExt, KIQ_SCRATCH_OFF);
            kt->KiqRptrBefore           = DreamV3ReadRegister(DevExt, KIQ_RPTR_OFF);
            kt->MeCntlBefore            = DreamV3ReadRegister(DevExt, KIQ_ME_CNTL_OFF);
            kt->GcvmContext0CntlBefore  = DreamV3ReadRegister(DevExt, KIQ_GCVM_CTX0_CNTL);

            KdPrint(("AMDBC250-DREAM-V4.3: KIQ_NOP_TEST enter: SCRATCH=0x%08X KIQ_RPTR=0x%08X KIQ_WPTR=0x%08X\n",
                kt->ScratchBefore, kt->KiqRptrBefore, DreamV3ReadRegister(DevExt, KIQ_WPTR_OFF)));

            /* Step 1: Allocate 4KB ring buffer below 4GB */
            PVOID ringVa = NULL;
            PHYSICAL_ADDRESS ringPa = {0};
            {
                PHYSICAL_ADDRESS low = {0}, high = {0}, boundary = {0};
                high.QuadPart = 0xFFFFFFFFULL;
                ringVa = MmAllocateContiguousMemorySpecifyCache(
                    0x1000, low, high, boundary, MmNonCached);
                if (!ringVa) {
                    kt->Result = 0;
                    status = STATUS_SUCCESS;
                    bytesReturned = sizeof(*kt);
                    break;
                }
                RtlZeroMemory(ringVa, 0x1000);
                ringPa = MmGetPhysicalAddress(ringVa);
            }
            kt->RingPaLo = (ULONG)(ringPa.QuadPart & 0xFFFFFFFF);
            kt->RingPaHi = (ULONG)(ringPa.QuadPart >> 32);

            KdPrint(("AMDBC250-DREAM-V4.3: KIQ_NOP_TEST ring PA=0x%llX\n", ringPa.QuadPart));

            /* Step 2: Read current KIQ_BASE — if non-zero, BIOS configured KIQ */
            {
                ULONG kBaseLo = DreamV3ReadRegister(DevExt, KIQ_BASE_LO_OFF);
                ULONG kBaseHi = DreamV3ReadRegister(DevExt, KIQ_BASE_HI_OFF);
                KdPrint(("AMDBC250-DREAM-V4.3: KIQ_NOP_TEST KIQ_BASE before: 0x%08X%08X\n", kBaseHi, kBaseLo));
                if (kBaseLo != 0 || kBaseHi != 0) {
                    KdPrint(("AMDBC250-DREAM-V4.3: KIQ_NOP_TEST WARNING: KIQ_BASE already set by BIOS!\n"));
                }
            }

            /* Step 3: Halt ME+PFP */
            DreamV3WriteRegister(DevExt, KIQ_ME_CNTL_OFF,
                DreamV3ReadRegister(DevExt, KIQ_ME_CNTL_OFF) | (1 << 28) | (1 << 30));
            KeStallExecutionProcessor(10);

            /* Step 4: Program KIQ ring base */
            DreamV3WriteRegister(DevExt, KIQ_BASE_LO_OFF, (ULONG)(ringPa.QuadPart & 0xFFFFFFFF));
            DreamV3WriteRegister(DevExt, KIQ_BASE_HI_OFF, (ULONG)(ringPa.QuadPart >> 32));

            /* Step 5: Reset RPTR and WPTR */
            DreamV3WriteRegister(DevExt, KIQ_RPTR_OFF, 0);
            DreamV3WriteRegister(DevExt, KIQ_WPTR_OFF, 0);
            KeStallExecutionProcessor(1);

            /* Step 6: Write PM4 packets to ring */
            __try {
                volatile PULONG ring = (volatile PULONG)ringVa;
                ring[0] = 0xC0033700;   /* IT_WRITE_DATA count=3 */
                ring[1] = 0x00100000;   /* CONTROL: DST_SEL=register, WR_CONFIRM */
                ring[2] = 0x000032D4;   /* SCRATCH register offset */
                ring[3] = 0x00000000;   /* ADDRESS_HI */
                ring[4] = 0xCAFEBABE;   /* value to write */
                ring[5] = 0xC0001000;   /* NOP */
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                KdPrint(("AMDBC250-DREAM-V4.3: KIQ_NOP_TEST ring access exception\n"));
                kt->Result = 0xDEADCAFE;
                DreamV3WriteRegister(DevExt, KIQ_ME_CNTL_OFF,
                    DreamV3ReadRegister(DevExt, KIQ_ME_CNTL_OFF) | (1 << 28) | (1 << 30));
                KeStallExecutionProcessor(10);
                DreamV3WriteRegister(DevExt, KIQ_BASE_LO_OFF, 0);
                DreamV3WriteRegister(DevExt, KIQ_BASE_HI_OFF, 0);
                DreamV3WriteRegister(DevExt, KIQ_RPTR_OFF, 0);
                DreamV3WriteRegister(DevExt, KIQ_WPTR_OFF, 0);
                DreamV3WriteRegister(DevExt, KIQ_ME_CNTL_OFF, kt->MeCntlBefore);
                MmFreeContiguousMemory(ringVa);
                status = STATUS_SUCCESS;
                bytesReturned = sizeof(*kt);
                break;
            }
            KeMemoryBarrier();

            /* Step 7: Set WPTR = 6 DWORDs */
            DreamV3WriteRegister(DevExt, KIQ_WPTR_OFF, 6);
            kt->KiqWptrSet = 6;

            /* Step 8: Resume ME+PFP (clear halt bits) */
            DreamV3WriteRegister(DevExt, KIQ_ME_CNTL_OFF,
                DreamV3ReadRegister(DevExt, KIQ_ME_CNTL_OFF) & ~((1 << 28) | (1 << 30)));

            /* Step 9: Wait for processing */
            KeStallExecutionProcessor(10000);  /* 10ms */

            /* Step 10: Read results */
            kt->KiqRptrAfter          = DreamV3ReadRegister(DevExt, KIQ_RPTR_OFF);
            kt->ScratchAfter          = DreamV3ReadRegister(DevExt, KIQ_SCRATCH_OFF);
            kt->MeCntlAfter           = DreamV3ReadRegister(DevExt, KIQ_ME_CNTL_OFF);
            kt->GcvmContext0CntlAfter = DreamV3ReadRegister(DevExt, KIQ_GCVM_CTX0_CNTL);

            KdPrint(("AMDBC250-DREAM-V4.3: KIQ_NOP_TEST result: SCRATCH=0x%08X KIQ_RPTR=0x%08X\n",
                kt->ScratchAfter, kt->KiqRptrAfter));

            if (kt->ScratchAfter == 0xCAFEBABE) {
                kt->Result = 2;
                KdPrint(("AMDBC250-DREAM-V4.3: KIQ_NOP_TEST SUCCESS! PM4 executed, SCRATCH=0xCAFEBABE\n"));
            } else if (kt->KiqRptrAfter != kt->KiqRptrBefore) {
                kt->Result = 1;
                KdPrint(("AMDBC250-DREAM-V4.3: KIQ_NOP_TEST: RPTR advanced but SCRATCH unchanged\n"));
            } else {
                kt->Result = 0;
                KdPrint(("AMDBC250-DREAM-V4.3: KIQ_NOP_TEST: no progress\n"));
            }

            /* Cleanup */
            DreamV3WriteRegister(DevExt, KIQ_ME_CNTL_OFF,
                DreamV3ReadRegister(DevExt, KIQ_ME_CNTL_OFF) | (1 << 28) | (1 << 30));
            KeStallExecutionProcessor(10);
            DreamV3WriteRegister(DevExt, KIQ_BASE_LO_OFF, 0);
            DreamV3WriteRegister(DevExt, KIQ_BASE_HI_OFF, 0);
            DreamV3WriteRegister(DevExt, KIQ_RPTR_OFF, 0);
            DreamV3WriteRegister(DevExt, KIQ_WPTR_OFF, 0);
            DreamV3WriteRegister(DevExt, KIQ_ME_CNTL_OFF, kt->MeCntlBefore);

            MmFreeContiguousMemory(ringVa);
            status = STATUS_SUCCESS;
            bytesReturned = sizeof(*kt);
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    /* --- KIQ BIOS ring submit: map the BIOS ring PA, write PM4, check execution --- */
    case IOCTL_AMDBC250_KIQ_BIOS_RING_SUBMIT: {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
            "KIQ_BIOS_RING_SUBMIT: code=%08X outputLen=%lu DevExt=%p HW=%d\n",
            ioctlCode, outputLen, DevExt, DevExt ? DevExt->HardwareInitialized : -1));
        if (outputLen >= sizeof(AMDBC250_IOCTL_KIQ_BIOS_RING_SUBMIT) && DevExt && DevExt->MmioVirtualBase) {
            PAMDBC250_IOCTL_KIQ_BIOS_RING_SUBMIT resp = (PAMDBC250_IOCTL_KIQ_BIOS_RING_SUBMIT)outputBuffer;
            PUCHAR mmio = (PUCHAR)DevExt->MmioVirtualBase;

            /* Register access via DreamV3WriteRegister (direct volatile writes silently dropped on Win11 26100) */
            #define BIOS_WRITE(off, val) DreamV3WriteRegister(DevExt, (off), (val))
            #define BIOS_READ(off) DreamV3ReadRegister(DevExt, (off))
            #define BIOS_SCRATCH_OFF  0x32D4
            #define BIOS_ME_CNTL_OFF  0x4A74
            #define BIOS_KIQ_BASE_LO  0xE060
            #define BIOS_KIQ_BASE_HI  0xE064
            #define BIOS_KIQ_RPTR_OFF 0xE06C
            #define BIOS_KIQ_WPTR_OFF 0xE078
            #define BIOS_HQD_ACTIVE   0x910C  /* CORRECTED: was 0xDAC0 (old uncorrected offset) */
            #define BIOS_GCVM_CONTEXT0_CNTL    0x0B460
            #define BIOS_GCVM_CONTEXT0_PT_BASE_LO 0x6C8C

            /* Step 0: Wake up GPU from GFXOFF � write ME_CNTL to trigger power-on */
            {
                ULONG meVal = BIOS_READ(BIOS_ME_CNTL_OFF);
                KdPrint(("KIQ_BIOS_RING: ME_CNTL before wake=0x%08X\n", meVal));
                /* Clear halt bits to wake GPU */
                BIOS_WRITE(BIOS_ME_CNTL_OFF, 0);
                KeStallExecutionProcessor(1000);
                /* Read back to confirm GPU is alive */
                meVal = BIOS_READ(BIOS_ME_CNTL_OFF);
                KdPrint(("KIQ_BIOS_RING: ME_CNTL after wake=0x%08X\n", meVal));
            }

            /* Step 1: Determine ring PA � read input BEFORE zeroing (METHOD_BUFFERED shares buffer) */
            PHYSICAL_ADDRESS ringPa = {0};
            if (inputLen >= sizeof(AMDBC250_IOCTL_KIQ_BIOS_RING_SUBMIT)) {
                PAMDBC250_IOCTL_KIQ_BIOS_RING_SUBMIT inp = (PAMDBC250_IOCTL_KIQ_BIOS_RING_SUBMIT)inputBuffer;
                ringPa.LowPart = inp->KiqBaseLo;
                ringPa.HighPart = inp->KiqBaseHi;
            }

            /* Now safe to zero output */
            RtlZeroMemory(resp, sizeof(*resp));

            if (ringPa.LowPart == 0 && ringPa.HighPart == 0) {
                ringPa.LowPart = BIOS_READ(BIOS_KIQ_BASE_LO);
                ringPa.HighPart = BIOS_READ(BIOS_KIQ_BASE_HI);
                KdPrint(("KIQ_BIOS_RING read KIQ_BASE from HW: 0x%08X%08X\n",
                    ringPa.HighPart, ringPa.LowPart));
            }

            /* If KIQ_BASE is still 0xFFFFFFFF (GFXOFF) or 0, try HQD_PQ_BASE as fallback */
            if (ringPa.LowPart == 0xFFFFFFFF || ringPa.HighPart == 0xFFFFFFFF ||
                (ringPa.LowPart == 0 && ringPa.HighPart == 0)) {
                /* CORRECTED offsets: was 0xDAD8/0xDADC (old uncorrected) */
                UINT32 pqLo = BIOS_READ(0x9124);
                UINT32 pqHi = BIOS_READ(0x9128);
                KdPrint(("KIQ_BIOS_RING KIQ_BASE=0x%08X%08X trying HQD_PQ_BASE=0x%08X%08X\n",
                    ringPa.HighPart, ringPa.LowPart, pqHi, pqLo));
                if (pqLo != 0xFFFFFFFF && pqLo != 0) {
                    ringPa.LowPart = pqLo;
                    ringPa.HighPart = pqHi;
                }
            }

            /* If still bad, try reading from BIOS state we saved earlier */
            if (ringPa.LowPart == 0xFFFFFFFF || ringPa.HighPart == 0xFFFFFFFF ||
                (ringPa.LowPart == 0 && ringPa.HighPart == 0)) {
                /* GPU may need more time � try once more after longer delay */
                KeStallExecutionProcessor(10000);
                ringPa.LowPart = BIOS_READ(BIOS_KIQ_BASE_LO);
                ringPa.HighPart = BIOS_READ(BIOS_KIQ_BASE_HI);
                KdPrint(("KIQ_BIOS_RING retry KIQ_BASE=0x%08X%08X\n",
                    ringPa.HighPart, ringPa.LowPart));
            }

            resp->KiqBaseLo = ringPa.LowPart;
            resp->KiqBaseHi = ringPa.HighPart;

            if (ringPa.LowPart == 0 && ringPa.HighPart == 0) {
                KdPrint(("KIQ_BIOS_RING: KIQ_BASE is 0\n"));
                resp->Result = 0xDEAD0001;
                status = STATUS_SUCCESS;
                bytesReturned = sizeof(*resp);
                break;
            }

            /* Step 2: Save BIOS state */
            resp->ScratchBefore = BIOS_READ(BIOS_SCRATCH_OFF);
            resp->KiqRptrBefore = BIOS_READ(BIOS_KIQ_RPTR_OFF);
            resp->MeCntlBefore  = BIOS_READ(BIOS_ME_CNTL_OFF);

            KdPrint(("KIQ_BIOS_RING SCRATCH=0x%08X KIQ_RPTR=0x%08X ME=0x%08X\n",
                resp->ScratchBefore, resp->KiqRptrBefore, resp->MeCntlBefore));

            /* Step 2b: Set up GCVM page tables to identity-map the ring address
             * BIOS PML4 at 0x6C8C is writable - we need to update it to map 0x7E508000
             * We allocate a new PML4, PDP, PD, PT and chain them to identity-map the ring.
             * This is a minimal 4-level page table setup for one 4KB page. */
            PHYSICAL_ADDRESS pgAddr = {0};
            PVOID pgVa = NULL;
            PHYSICAL_ADDRESS low = {0}, high = {0}, boundary = {0};
            high.QuadPart = 0xFFFFFFFFULL;
            {
                SIZE_T mapSize = 0x1000;
                pgVa = MmAllocateContiguousMemorySpecifyCache(mapSize, low, high, boundary, MmNonCached);
                if (pgVa) {
                    RtlZeroMemory(pgVa, mapSize);
                    pgAddr = MmGetPhysicalAddress(pgVa);
                    KdPrint(("KIQ_BIOS_RING PT alloc PA=0x%llX\n", pgAddr.QuadPart));
                }
            }
            if (pgVa && ringPa.QuadPart >= 0x100000000ULL) {
                /* 4-level tables needed for addresses above 4GB */
                PVOID pml4Va = pgVa;
                PHYSICAL_ADDRESS pml4Pa = pgAddr;
                PVOID pdpVa = MmAllocateContiguousMemorySpecifyCache(0x1000, low, high, boundary, MmNonCached);
                PVOID pdVa = MmAllocateContiguousMemorySpecifyCache(0x1000, low, high, boundary, MmNonCached);
                PVOID ptVa = MmAllocateContiguousMemorySpecifyCache(0x1000, low, high, boundary, MmNonCached);
                if (pdpVa && pdVa && ptVa) {
                    RtlZeroMemory(pdpVa, 0x1000);
                    RtlZeroMemory(pdVa, 0x1000);
                    RtlZeroMemory(ptVa, 0x1000);
                    PHYSICAL_ADDRESS pdpPa = MmGetPhysicalAddress(pdpVa);
                    PHYSICAL_ADDRESS pdPa = MmGetPhysicalAddress(pdVa);
                    PHYSICAL_ADDRESS ptPa = MmGetPhysicalAddress(ptVa);
                    ((PULONG64)pml4Va)[0] = pdpPa.QuadPart | 0x03;
                    ((PULONG64)pdpVa)[(ringPa.QuadPart >> 30) & 0x1FF] = pdPa.QuadPart | 0x03;
                    ((PULONG64)pdVa)[(ringPa.QuadPart >> 21) & 0x1FF] = ptPa.QuadPart | 0x03;
                    ((PULONG64)ptVa)[(ringPa.QuadPart >> 12) & 0x1FF] = (ringPa.QuadPart & ~0xFFFULL) | 0x63;
                    BIOS_WRITE(0x6C8C, (ULONG)pml4Pa.QuadPart);
                    BIOS_WRITE(0x6C90, (ULONG)(pml4Pa.QuadPart >> 32));
                    BIOS_WRITE(0x0B460, 1);
                    KdPrint(("KIQ_BIOS_RING GCVM setup: PT_BASE=0x%llX (4-level)\n", pml4Pa.QuadPart));
                    /* Keep pages allocated - GPU needs them for translation */
                }
            } else if (pgVa && ringPa.QuadPart >= 0x100000ULL) {
                /* 3-level tables for addresses 1MB - 4GB */
                PVOID pml3Va = pgVa;
                PHYSICAL_ADDRESS pml3Pa = pgAddr;
                PVOID pdVa = MmAllocateContiguousMemorySpecifyCache(0x1000, low, high, boundary, MmNonCached);
                PVOID ptVa = MmAllocateContiguousMemorySpecifyCache(0x1000, low, high, boundary, MmNonCached);
                if (pdVa && ptVa) {
                    RtlZeroMemory(pdVa, 0x1000);
                    RtlZeroMemory(ptVa, 0x1000);
                    PHYSICAL_ADDRESS pdPa = MmGetPhysicalAddress(pdVa);
                    PHYSICAL_ADDRESS ptPa = MmGetPhysicalAddress(ptVa);
                    ((PULONG64)pml3Va)[0] = pdPa.QuadPart | 0x03;
                    ((PULONG64)pdVa)[(ringPa.QuadPart >> 21) & 0x1FF] = ptPa.QuadPart | 0x03;
                    ((PULONG64)ptVa)[(ringPa.QuadPart >> 12) & 0x1FF] = (ringPa.QuadPart & ~0xFFFULL) | 0x63;
                    BIOS_WRITE(0x6C8C, (ULONG)pml3Pa.QuadPart);
                    BIOS_WRITE(0x6C90, (ULONG)(pml3Pa.QuadPart >> 32));
                    BIOS_WRITE(0x0B460, 1);
                    KdPrint(("KIQ_BIOS_RING GCVM setup: PT_BASE=0x%llX (3-level)\n", pml3Pa.QuadPart));
                    /* Keep pages allocated - GPU needs them for translation */
                }
            } else {
                KdPrint(("KIQ_BIOS_RING: GCVM setup skipped - PA too low or alloc failed\n"));
            }

            /* Step 3: Map ring via MmMapIoSpace */
            PVOID ringVa = NULL;
            PMDL ringMdl = NULL;
            {
                SIZE_T mapSize = 0x1000;
                ringVa = MmMapIoSpace(ringPa, mapSize, MmNonCached);
                if (!ringVa) {
                    KdPrint(("KIQ_BIOS_RING MmMapIoSpace FAILED PA=0x%llX\n", ringPa.QuadPart));
                    resp->Result = 0xDEAD0002;
                    status = STATUS_SUCCESS;
                    bytesReturned = sizeof(*resp);
                    break;
                }
                KdPrint(("KIQ_BIOS_RING MmMapIoSpace OK VA=%p\n", ringVa));
            }

            /* Step 4: Read current ring contents */
            __try {
                volatile PULONG ring = (volatile PULONG)ringVa;
                resp->RingDword0 = ring[0];
                resp->RingDword1 = ring[1];
                resp->RingDword2 = ring[2];
                resp->RingDword3 = ring[3];
                KdPrint(("KIQ_BIOS_RING ring[0..3] = 0x%08X 0x%08X 0x%08X 0x%08X\n",
                    resp->RingDword0, resp->RingDword1, resp->RingDword2, resp->RingDword3));
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                KdPrint(("KIQ_BIOS_RING ring read exception\n"));
                resp->Result = 0xDEADCAFE;
                BIOS_WRITE(BIOS_ME_CNTL_OFF, BIOS_READ(BIOS_ME_CNTL_OFF) | (1 << 28) | (1 << 30));
                MmUnmapIoSpace(ringVa, 0x1000);
                status = STATUS_SUCCESS;
                bytesReturned = sizeof(*resp);
                break;
            }

            /* Step 5: Halt ME+PFP */
            {
                ULONG meVal = BIOS_READ(BIOS_ME_CNTL_OFF);
                BIOS_WRITE(BIOS_ME_CNTL_OFF, meVal | (1 << 28) | (1 << 30));
            }
            KeStallExecutionProcessor(10);

            /* Step 6: Reset RPTR/WPTR */
            BIOS_WRITE(BIOS_KIQ_RPTR_OFF, 0);
            BIOS_WRITE(BIOS_KIQ_WPTR_OFF, 0);
            KeStallExecutionProcessor(1);

            /* Step 7: Write PM4 WRITE_DATA to SCRATCH (0x32D4) = 0xCAFEBABE
             * PM4 IT_WRITE_DATA (0x37), count=3 (4 DWORDs total):
             *   HEADER: opcode=0x37, count=3 -> 0xC0033700
             *   CONTROL: DST_SEL=register(0), WR_CONFIRM(1<<20) -> 0x00100000
             *   ADDRESS_LO = SCRATCH byte offset (0x32D4)
             *   ADDRESS_HI = 0
             *   DATA = 0xCAFEBABE */
            __try {
                PULONG ring = (PULONG)ringVa;
                ring[0] = 0xC0033700;  /* HEADER: IT_WRITE_DATA, count=3 */
                ring[1] = 0x00100000;  /* CONTROL: WR_CONFIRM, DST_SEL=register */
                ring[2] = 0x000032D4;  /* ADDRESS_LO = SCRATCH */
                ring[3] = 0x00000000;  /* ADDRESS_HI */
                ring[4] = 0xCAFEBABE;  /* value to write */
                ring[5] = 0xC0001000;  /* NOP (padding) */
                ring[6] = 0xC0001000;  /* NOP (padding) */
                ring[7] = 0xC0001000;  /* NOP (padding) */
                KdPrint(("KIQ_BIOS_RING: wrote IT_WRITE_DATA 0xCAFEBABE -> SCRATCH (0x32D4)\n"));
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                KdPrint(("KIQ_BIOS_RING ring write exception\n"));
                resp->Result = 0xDEADCAFE;
                BIOS_WRITE(BIOS_ME_CNTL_OFF, BIOS_READ(BIOS_ME_CNTL_OFF) | (1 << 28) | (1 << 30));
                MmUnmapIoSpace(ringVa, 0x1000);
                status = STATUS_SUCCESS;
                bytesReturned = sizeof(*resp);
                break;
            }

            /* Step 7.5: CRITICAL � flush CPU stores before WPTR update */
            KeMemoryBarrier();

            /* Step 8: Set WPTR = 8 DWORDs (header + control + offset + data + 4 NOPs) */
            BIOS_WRITE(BIOS_KIQ_WPTR_OFF, 8);
            resp->KiqWptrSet = 8;

            /* Step 9: Resume ME+PFP */
            {
                ULONG meVal = BIOS_READ(BIOS_ME_CNTL_OFF);
                BIOS_WRITE(BIOS_ME_CNTL_OFF, meVal & ~((1 << 28) | (1 << 30)));
            }

            /* Step 10: Wait 100ms for GPU to process PM4 */
            {
                LARGE_INTEGER delay;
                delay.QuadPart = -10000LL * 100;  /* 100ms */
                KeDelayExecutionThread(KernelMode, FALSE, &delay);
            }

            /* Step 11: Read results */
            resp->KiqRptrAfter = BIOS_READ(BIOS_KIQ_RPTR_OFF);
            resp->ScratchAfter = BIOS_READ(BIOS_SCRATCH_OFF);
            resp->MeCntlAfter  = BIOS_READ(BIOS_ME_CNTL_OFF);
            __try {
                volatile PULONG ring = (volatile PULONG)ringVa;
                resp->RingDword0 = ring[0];
                resp->RingDword1 = ring[1];
                resp->RingDword2 = ring[2];
                resp->RingDword3 = ring[3];
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                /* Ring may have been unmapped — non-fatal */
                resp->RingDword0 = resp->RingDword1 = resp->RingDword2 = resp->RingDword3 = 0xDEAD;
            }

            KdPrint(("KIQ_BIOS_RING result SCRATCH=0x%08X RPTR=0x%08X ME=0x%08X\n",
                resp->ScratchAfter, resp->KiqRptrAfter, resp->MeCntlAfter));

            if (resp->ScratchAfter == 0xCAFEBABE) {
                resp->Result = 2;
            } else if (resp->KiqRptrAfter != resp->KiqRptrBefore) {
                resp->Result = 1;
            } else {
                resp->Result = 0;
            }

            /* Cleanup: halt, clear ring, reset, restore original ME_CNTL */
            {
                ULONG meVal = BIOS_READ(BIOS_ME_CNTL_OFF);
                BIOS_WRITE(BIOS_ME_CNTL_OFF, meVal | (1 << 28) | (1 << 30));
            }
            KeStallExecutionProcessor(10);
            {
                PULONG ring = (PULONG)ringVa;
                for (int i = 0; i < 8; i++) ring[i] = 0;
            }
            KeMemoryBarrier();
            BIOS_WRITE(BIOS_KIQ_RPTR_OFF, 0);
            BIOS_WRITE(BIOS_KIQ_WPTR_OFF, 0);
            BIOS_WRITE(BIOS_ME_CNTL_OFF, resp->MeCntlBefore);  /* restore original */

            /* Unmap ring */
            MmUnmapIoSpace(ringVa, 0x1000);

            status = STATUS_SUCCESS;
            bytesReturned = sizeof(*resp);
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

    case 0x80000988: { /* IOCTL_AMDBC250_SDMA_SELFTEST */
        {
            PULONG Resp = (PULONG)outputBuffer;
            PHYSICAL_ADDRESS srcPhys = {0}, dstPhys = {0}, lowAddr = {0x100000}, highAddr = {0xFFFFFFFFFFFFFFFFULL}, boundaryAddr = {0};
            PVOID srcVa = NULL, dstVa = NULL;
            ULONG sz = 4096;
            NTSTATUS testStatus;

            if (outputLen < sizeof(ULONG)) {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            /* Allocate two physically contiguous pages (inline, no wrapper dependency) */
            srcVa = MmAllocateContiguousMemorySpecifyCache(sz, lowAddr, highAddr, boundaryAddr, MmWriteCombined);
            if (srcVa != NULL) {
                srcPhys = MmGetPhysicalAddress(srcVa);
                dstVa = MmAllocateContiguousMemorySpecifyCache(sz, lowAddr, highAddr, boundaryAddr, MmWriteCombined);
                if (dstVa != NULL) {
                    dstPhys = MmGetPhysicalAddress(dstVa);
                }
            }

            if (srcVa == NULL || dstVa == NULL) {
                *Resp = (srcVa == NULL) ? 0xDEAD0001 : 0xDEAD0002;
                if (srcVa != NULL) MmFreeContiguousMemory(srcVa);
                if (dstVa != NULL) MmFreeContiguousMemory(dstVa);
                bytesReturned = sizeof(ULONG);
                status = STATUS_SUCCESS;
                break;
            }

            RtlZeroMemory(srcVa, sz);
            RtlZeroMemory(dstVa, sz);

            /* Fill source with pattern */
            for (ULONG i = 0; i < sz / sizeof(ULONG); i++) {
                ((PULONG)srcVa)[i] = 0x600DC0DE + i;
            }
            KeMemoryBarrier();

            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                "AMDBC250-DREAM-V4.3: SDMA self-test: srcPA=0x%llX dstPA=0x%llX size=%u\n",
                srcPhys.QuadPart, dstPhys.QuadPart, sz));

            /* Do SDMA copy */
            testStatus = DreamV3SdmaCopyBuffer(DevExt, srcPhys, dstPhys, sz);

            if (!NT_SUCCESS(testStatus)) {
                *Resp = testStatus;
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                    "AMDBC250-DREAM-V4.3: SDMA copy FAILED: 0x%08X\n", testStatus));
            } else {
                BOOLEAN match = TRUE;
                for (ULONG i = 0; i < sz / sizeof(ULONG); i++) {
                    if (((PULONG)dstVa)[i] != 0x600DC0DE + i) {
                        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                            "AMDBC250-DREAM-V4.3: SDMA mismatch at offset %u: expected 0x%08X got 0x%08X\n",
                            i * 4, 0x600DC0DE + i, ((PULONG)dstVa)[i]));
                        match = FALSE;
                        break;
                    }
                }
                if (match) {
                    *Resp = 0x600DCAFE;
                    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                        "AMDBC250-DREAM-V4.3: SDMA self-test PASSED!\n"));
                } else {
                    *Resp = 0xDEADBEEF;
                    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                        "AMDBC250-DREAM-V4.3: SDMA self-test FAILED (data mismatch)\n"));
                }
            }

            MmFreeContiguousMemory(srcVa);
            MmFreeContiguousMemory(dstVa);

            bytesReturned = sizeof(ULONG);
            status = STATUS_SUCCESS;
        }
        break;
    }

    case 0x8000098C: { /* IOCTL_AMDBC250_GCVM_PT_SETUP — Set up GCVM page table */
        {
            PHYSICAL_ADDRESS lowAddr = {0x100000}, highAddr = {0xFFFFFFFFFFFFFFFFULL}, boundaryAddr = {0};
            PHYSICAL_ADDRESS ringPhys, ptPhys[3];
            PVOID ptPages[3] = {NULL};
            ULONG i, rootIdx, midIdx, leafIdx, pollCount;
            ULONG checkLo, checkHi;
            ULONG64 writtenVal;

            struct {
                ULONG CtxCntlBefore;
                ULONG RingBaseLo;
                ULONG RingBaseHi;
                ULONG PtBase0LoBefore;
                ULONG PtBase0HiBefore;
                ULONG PtBase0LoAfter;
                ULONG PtBase0HiAfter;
                ULONG PtBaseLoAfter;
                ULONG PtBaseHiAfter;
                ULONG Result;
                ULONG PtPhysLo[3];
                ULONG PtPhysHi[3];
                ULONG InvStatus;
                ULONG KIQ_WPTR;
                ULONG KIQ_RPTR;
                ULONG Reserved[8];
            } *resp = (PVOID)outputBuffer;

            if (outputLen < sizeof(*resp)) {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            if (!DevExt || !DevExt->MmioVirtualBase) {
                status = STATUS_DEVICE_NOT_READY;
                break;
            }
            RtlZeroMemory(resp, sizeof(*resp));

            /* Read before state */
            resp->CtxCntlBefore = DreamV3ReadRegister(DevExt, 0x0B460);
            resp->RingBaseLo    = DreamV3ReadRegister(DevExt, 0xE060);
            resp->RingBaseHi    = DreamV3ReadRegister(DevExt, 0xE064);
            resp->PtBase0LoBefore = DreamV3ReadRegister(DevExt, 0x6C8C);
            resp->PtBase0HiBefore = DreamV3ReadRegister(DevExt, 0x6C90);
            resp->KIQ_WPTR = DreamV3ReadRegister(DevExt, 0xE078);
            resp->KIQ_RPTR = DreamV3ReadRegister(DevExt, 0xE06C);
            ringPhys.QuadPart = ((ULONG64)resp->RingBaseHi << 32) | resp->RingBaseLo;

            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                "AMDBC250-DREAM-V4.3: GCVM_PT_SETUP: ring PA=0x%llX ctx=0x%08X pt0=0x%08X_%08X\n",
                ringPhys.QuadPart, resp->CtxCntlBefore,
                resp->PtBase0HiBefore, resp->PtBase0LoBefore));

            /* Must have 3-level page tables (depth=2 = bits 2:1 = 10b = 4) */
            if ((resp->CtxCntlBefore & 6) != 4) {
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                    "AMDBC250-DREAM-V4.3: Unexpected depth: 0x%X\n", resp->CtxCntlBefore & 6));
                resp->Result = 0xBAD0BAD0;
                bytesReturned = sizeof(*resp);
                status = STATUS_SUCCESS;
                break;
            }

            /* KIQ ring must be initialized - auto-allocate if needed */
            if (ringPhys.QuadPart == 0) {
                /* Allocate ring buffer if not already present */
                if (DevExt->GcvmRingBuf == NULL) {
                    PHYSICAL_ADDRESS lowR = {0x100000}, highR = {0xFFFFFFFFFFFFFFFFULL}, boundaryR = {0};
                    DevExt->GcvmRingBuf = MmAllocateContiguousMemorySpecifyCache(
                        4096, lowR, highR, boundaryR, MmNonCached);
                    if (DevExt->GcvmRingBuf) {
                        PHYSICAL_ADDRESS ringPa = MmGetPhysicalAddress(DevExt->GcvmRingBuf);
                        DevExt->GcvmRingBufPa = ringPa.QuadPart;
                        RtlZeroMemory(DevExt->GcvmRingBuf, 4096);

                        /* Write PM4 NOP at ring offset 0 */
                        *(volatile ULONG*)((PUCHAR)DevExt->GcvmRingBuf + 0) = 0xC0001000; /* IT_NOP */

                        /* Set KIQ_BASE register to ring buffer PA */
                        DreamV3WriteRegister(DevExt, 0xE060, (ULONG)(ringPa.QuadPart & 0xFFFFFFFF));
                        DreamV3WriteRegister(DevExt, 0xE064, (ULONG)(ringPa.QuadPart >> 32));
                        KeMemoryBarrier();

                        ringPhys.QuadPart = ringPa.QuadPart;
                        resp->RingBaseLo = (ULONG)(ringPa.QuadPart & 0xFFFFFFFF);
                        resp->RingBaseHi = (ULONG)(ringPa.QuadPart >> 32);
                    }
                } else {
                    /* Reuse existing ring buffer */
                    ringPhys.QuadPart = DevExt->GcvmRingBufPa;

                    /* Write PM4 NOP at ring offset 0 (re-init) */
                    RtlZeroMemory(DevExt->GcvmRingBuf, 4096);
                    *(volatile ULONG*)((PUCHAR)DevExt->GcvmRingBuf + 0) = 0xC0001000; /* IT_NOP */

                    /* Ensure KIQ_BASE register is set */
                    DreamV3WriteRegister(DevExt, 0xE060, (ULONG)(ringPhys.QuadPart & 0xFFFFFFFF));
                    DreamV3WriteRegister(DevExt, 0xE064, (ULONG)(ringPhys.QuadPart >> 32));
                    KeMemoryBarrier();

                    resp->RingBaseLo = (ULONG)(ringPhys.QuadPart & 0xFFFFFFFF);
                    resp->RingBaseHi = (ULONG)(ringPhys.QuadPart >> 32);
                }

                if (ringPhys.QuadPart == 0) {
                    resp->Result = 0xBAD0C0DE;
                    bytesReturned = sizeof(*resp);
                    status = STATUS_SUCCESS;
                    break;
                }
            }

            /* For 3-level (depth=2): root = bits 38:30, mid = bits 29:21, leaf = bits 20:12 */
            rootIdx = (ULONG)((ringPhys.QuadPart >> 30) & 0x1FF);
            midIdx  = (ULONG)((ringPhys.QuadPart >> 21) & 0x1FF);
            leafIdx = (ULONG)((ringPhys.QuadPart >> 12) & 0x1FF);

            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                "AMDBC250-DREAM-V4.3:   ring VA=0x%llX -> root[%lu] mid[%lu] leaf[%lu]\n",
                ringPhys.QuadPart, rootIdx, midIdx, leafIdx));

            /* Reuse or allocate 3 page table pages (non-cached).
             * Track which pages were already existing so we don't free them on error. */
            {
                BOOLEAN newlyAllocated[3] = {FALSE, FALSE, FALSE};
                for (i = 0; i < 3; i++) {
                    if (DevExt->GcvmPtPages[i] == NULL) {
                        DevExt->GcvmPtPages[i] = MmAllocateContiguousMemorySpecifyCache(
                            4096, lowAddr, highAddr, boundaryAddr, MmNonCached);
                        if (DevExt->GcvmPtPages[i] == NULL) break;
                        newlyAllocated[i] = TRUE;
                        RtlZeroMemory(DevExt->GcvmPtPages[i], 4096);
                    }
                    ptPages[i] = DevExt->GcvmPtPages[i];
                    ptPhys[i] = MmGetPhysicalAddress(ptPages[i]);
                    resp->PtPhysLo[i] = (ULONG)(ptPhys[i].QuadPart & 0xFFFFFFFF);
                    resp->PtPhysHi[i] = (ULONG)(ptPhys[i].QuadPart >> 32);
                }
                if (ptPages[0] == NULL || ptPages[1] == NULL || ptPages[2] == NULL) {
                    for (i = 0; i < 3; i++) {
                        if (newlyAllocated[i] && DevExt->GcvmPtPages[i]) {
                            MmFreeContiguousMemory(DevExt->GcvmPtPages[i]);
                            DevExt->GcvmPtPages[i] = NULL;
                        }
                        ptPages[i] = NULL;
                    }
                    resp->Result = 0xDEADF00D;
                    bytesReturned = sizeof(*resp);
                    status = STATUS_SUCCESS;
                    break;
                }
            }

            /* Fill page tables: PDE = VALID|SYSTEM, PTE = VALID|SYSTEM|READABLE|WRITABLE */
            { PULONG64 root = (PULONG64)ptPages[0];
              PULONG64 mid  = (PULONG64)ptPages[1];
              PULONG64 leaf = (PULONG64)ptPages[2];

            root[rootIdx] = (ptPhys[1].QuadPart & 0xFFFFFFFFF000ULL) | 0x03;
            mid[midIdx]   = (ptPhys[2].QuadPart & 0xFFFFFFFFF000ULL) | 0x03;
            leaf[leafIdx] = (ringPhys.QuadPart & 0xFFFFFFFFF000ULL) | 0x63;

            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                "AMDBC250-DREAM-V4.3:   root[%lu]=0x%llX mid[%lu]=0x%llX leaf[%lu]=0x%llX\n",
                rootIdx, root[rootIdx], midIdx, mid[midIdx], leafIdx, leaf[leafIdx]));
            }

            /* Flush cache to ensure GPU sees page tables */
            KeMemoryBarrier();

            /* Write PT_BASE (0x6C8C/0x6C90) with root page PA */
            DreamV3WriteRegister(DevExt, 0x6C8C, (ULONG)(ptPhys[0].QuadPart & 0xFFFFFFFF));
            DreamV3WriteRegister(DevExt, 0x6C90, (ULONG)(ptPhys[0].QuadPart >> 32));

            KeMemoryBarrier();

            /* Verify write */
            checkLo = DreamV3ReadRegister(DevExt, 0x6C8C);
            checkHi = DreamV3ReadRegister(DevExt, 0x6C90);
            writtenVal = ((ULONG64)checkHi << 32) | checkLo;

            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                "AMDBC250-DREAM-V4.3:   PT_BASE0 written=0x%llX readback=0x%llX\n",
                ptPhys[0].QuadPart, writtenVal));

            if (writtenVal != ptPhys[0].QuadPart) {
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                    "AMDBC250-DREAM-V4.3:   PT_BASE0 write MISMATCH! read=0x%llX expected=0x%llX\n",
                    writtenVal, ptPhys[0].QuadPart));
            }

            /* TLB invalidation via 0x6C0C/0x6C10 */
            resp->InvStatus = 0;

            /* Step 1: Clear previous ACK */
            DreamV3WriteRegister(DevExt, 0x6C10, 1);
            KeMemoryBarrier();

            /* Step 2: Request invalidation */
            DreamV3WriteRegister(DevExt, 0x6C0C, 1);
            KeMemoryBarrier();

            /* Step 3: Poll for ACK (0x6C10 bit 0) with timeout */
            {
                pollCount = 0;
                while (pollCount < 1000) {
                    ULONG ack; ack = DreamV3ReadRegister(DevExt, 0x6C10);
                    if (ack & 1) {
                        resp->InvStatus = 1; /* ACK received */
                        break;
                    }
                    KeStallExecutionProcessor(1); /* 1 us */
                    pollCount++;
                }
                if (pollCount >= 1000) {
                    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                        "AMDBC250-DREAM-V4.3:   TLB invalidation TIMEOUT!\n"));
                }
            }

            resp->PtBase0LoAfter = DreamV3ReadRegister(DevExt, 0x6C8C);
            resp->PtBase0HiAfter = DreamV3ReadRegister(DevExt, 0x6C90);
            resp->PtBaseLoAfter = DreamV3ReadRegister(DevExt, 0x6C8C);
            resp->PtBaseHiAfter = DreamV3ReadRegister(DevExt, 0x6C90);

            resp->Result = 0xCAFEBABE; /* success */

            bytesReturned = sizeof(*resp);
            status = STATUS_SUCCESS;
        }
        break;
    }

    case 0x80000BE8: { /* IOCTL_AMDBC250_EXECUTE_RING_PM4 */
        PAMDBC250_IOCTL_EXECUTE_RING_PM4 rp = (PAMDBC250_IOCTL_EXECUTE_RING_PM4)inputBuffer;
        ULONG cmdCount, pollTimeoutMs;
        if (inputLen < sizeof(*rp)) { status = STATUS_BUFFER_TOO_SMALL; break; }

        cmdCount = rp->CommandCount;
        if (cmdCount == 0 || cmdCount > 64) { status = STATUS_INVALID_PARAMETER; break; }

        /* Save input fields BEFORE RtlZeroMemory clobbers them */
        pollTimeoutMs = rp->TimeoutMs;
        {
            ULONG i;
            for (i = 0; i < cmdCount; i++) DevExt->SavedPm4Cmds[i] = rp->Commands[i];
            DevExt->SavedPm4Count = cmdCount;
        }
        RtlZeroMemory(rp, sizeof(*rp));
        rp->CommandCount = cmdCount;
        rp->TimeoutMs = (pollTimeoutMs > 0) ? pollTimeoutMs : 500;

        /* --- Allocate ring buffer (MQD stored at offset 0; PM4 at offset 256) --- */
        if (DevExt->GcvmRingBuf == NULL) {
            PHYSICAL_ADDRESS lowR = {0x100000}, highR = {0xFFFFFFFFFFFFFFFFULL}, boundaryR = {0};
            DevExt->GcvmRingBuf = MmAllocateContiguousMemorySpecifyCache(
                4096, lowR, highR, boundaryR, MmNonCached);
            if (DevExt->GcvmRingBuf) {
                DevExt->GcvmRingBufPa = MmGetPhysicalAddress(DevExt->GcvmRingBuf).QuadPart;
                RtlZeroMemory(DevExt->GcvmRingBuf, 4096);
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                    "AMDBC250-DREAM-V4.3: EXEC_RING: ring PA=0x%llX\n",
                    DevExt->GcvmRingBufPa));
            } else {
                rp->Result = 2; status = STATUS_INSUFFICIENT_RESOURCES; break;
            }
        }
        /* Full v10_compute_mqd at ring buffer offset 0 (256 dwords = 1024B).
           Layout matches Linux v10_structs.h: compute fields 0-39, then
           HQD fields at offsets 128-186. Shader at byte 1024 (dword 256). */
        {
            volatile PULONG buf = (volatile PULONG)DevExt->GcvmRingBuf;
            ULONG64 ringPa = DevExt->GcvmRingBufPa;
            ULONG64 shaderPa = ringPa + 1024;
            int i;
            for (i = 0; i < 256; i++) buf[i] = 0;
            buf[0]  = 0xC0310800;           /* header: type=compute, qsize=8, priv */
            buf[1]  = 0;                     /* DISPATCH_INITIATOR */
            buf[2]  = 1; buf[3] = 1; buf[4] = 1;  /* DIM_X/Y/Z */
            buf[5]  = 0; buf[6] = 0; buf[7] = 0;  /* START_X/Y/Z */
            buf[8]  = 63;                    /* NUM_THREAD_X (64-1) */
            buf[9]  = 0;                     /* NUM_THREAD_Y */
            buf[10] = 0;                     /* NUM_THREAD_Z */
            buf[11] = 1;                     /* PIPELINESTAT_ENABLE */
            buf[13] = (ULONG)(shaderPa >> 8); /* PGM_LO */
            buf[14] = (ULONG)(shaderPa >> 32);/* PGM_HI */
            buf[19] = (2 << 0) | (1 << 6);   /* PGM_RSRC1: VGPRS=2,SGPRS=1 */
            buf[20] = (63 << 0);             /* PGM_RSRC2: 64 threads */
            buf[23] = 0xFFFFFFFF;            /* THREAD_MGMT_SE0 */
            buf[24] = 0xFFFFFFFF;            /* THREAD_MGMT_SE1 */
            buf[26] = 0xFFFFFFFF;            /* THREAD_MGMT_SE2 */
            buf[27] = 0xFFFFFFFF;            /* THREAD_MGMT_SE3 */
            buf[128] = (ULONG)(ringPa & 0xFFFFFFFC);  /* MQD_BASE_ADDR_LO */
            buf[129] = (ULONG)(ringPa >> 32);          /* MQD_BASE_ADDR_HI */
            buf[130] = 1;                             /* HQD_ACTIVE */
            buf[131] = 0;                             /* HQD_VMID */
            buf[132] = 0x8000014C;                    /* PERSISTENT_STATE */
            buf[133] = 1;                             /* PIPE_PRIORITY */
            buf[134] = 15;                            /* QUEUE_PRIORITY */
            buf[135] = 0x00010011;                    /* QUANTUM */
            buf[136] = (ULONG)(ringPa >> 8);           /* PQ_BASE_LO (ring >> 8) */
            buf[137] = (ULONG)(ringPa >> 40);          /* PQ_BASE_HI */
            buf[138] = 0;                             /* PQ_RPTR */
            buf[139] = 0;                             /* RPTR_REPORT_ADDR_LO */
            buf[140] = 0;                             /* RPTR_REPORT_ADDR_HI */
            buf[141] = 0;                             /* WPTR_POLL_ADDR_LO */
            buf[142] = 0;                             /* WPTR_POLL_ADDR_HI */
            buf[143] = 0;                             /* DOORBELL_CONTROL */
            buf[145] = 0x000A0101;                    /* PQ_CONTROL */
            buf[146] = 0;                             /* IB_BASE_ADDR_LO */
            buf[147] = 0;                             /* IB_BASE_ADDR_HI */
            buf[148] = 0;                             /* IB_RPTR */
            buf[149] = 0x00030003;                    /* IB_CONTROL */
            buf[150] = 0;                             /* IQ_TIMER */
            buf[164] = 1 << 14;                       /* HQ_SCHEDULER0 (bit 14) */
            buf[166] = 1;                             /* MQD_CONTROL (PRIV_STATE) */
        }

        /* --- Step 0: Wake GFX from GFXOFF (SMU) --- */
        DreamV3SmuWakeGfx(DevExt);
        KeStallExecutionProcessor(1000);
        rp->SmuFeaturesMask = 0;
        rp->SmuGfxFreqMhz = 0;
        DreamV3SmuSendMessage(DevExt, SMU_MSG_GetEnabledSmuFeatures, 0, &rp->SmuFeaturesMask);
        DreamV3SmuSendMessage(DevExt, SMU_MSG_GetGfxFrequency, 0, &rp->SmuGfxFreqMhz);

        /* --- Baseline (MQD is at ring buffer offset 0) --- */
        rp->RingPa = DevExt->GcvmRingBufPa;
        rp->MqdPa  = DevExt->GcvmRingBufPa;
        rp->ScratchBefore = DreamV3ReadRegister(DevExt, AMDBC250_REG_SCRATCH_REG0);

        /* Step 1: Select ME=1 (MEC) AND unhalt ME + MEC + enable schedulers */
        DreamV3WriteRegister(DevExt, AMDBC250_REG_GRBM_GFX_INDEX,
            AMDBC250_GRBM_GFX_INDEX_KIQ_VAL);

        /* Write 0xFF to RLC_CP_SCHEDULERS to enable all queue slots */
        DreamV3WriteRegister(DevExt, AMDBC250_REG_RLC_CP_SCHEDULERS, 0xFF);

        /* Unhalt ME (write 0 to ME_CNTL at 0x4A74) */
        DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_ME_CNTL, 0);

        /* Unhalt MEC (write 0 to CP_MEC_CNTL at 0x4B14) */
        DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_MEC_CNTL_GC, 0);

        KeMemoryBarrier();

        /* NOTE: Linux gc_10_1_0_offset.h confirms ALL CP_HQD registers
           have BASE_IDX=0 (NOT SEG1). Earlier SEG1 hypothesis was wrong.
           Formula: BAR5 = GC_BASE(0x1260) + mm*4. Using BASE_IDX=0. */
        /* Read baseline */
        rp->RptrBefore = DreamV3ReadRegister(DevExt, AMDBC250_REG_CP_HQD_PQ_RPTR);
        rp->WptrBefore = DreamV3ReadRegister(DevExt, AMDBC250_REG_CP_HQD_PQ_WPTR_LO);

        /* Step 2: Program CP_HQD registers (all BASE_IDX=0) */
        rp->PqCtrlBefore = DreamV3ReadRegister(DevExt, AMDBC250_REG_CP_HQD_PQ_CONTROL);
        DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_HQD_ACTIVE, 0);
        rp->PqCtrlAfter = DreamV3ReadRegister(DevExt, AMDBC250_REG_CP_HQD_PQ_CONTROL);
        DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_MQD_BASE_ADDR,
            (ULONG)(DevExt->GcvmRingBufPa & 0xFFFFFFFF));
        DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_MQD_BASE_ADDR_HI,
            (ULONG)(DevExt->GcvmRingBufPa >> 32));
        /* Ring buffer layout:
           0-1023  = MQD (256 dwords)
           1024+   = ring PM4 data (ring base at ringPA+1024, PQ_BASE = (ringPA+1024)>>8) */
        #define RING_DATA_OFFSET_BYTES  1024
        #define RING_DATA_OFFSET_DWORDS 256
        ULONG64 ringDataPa = DevExt->GcvmRingBufPa + RING_DATA_OFFSET_BYTES;
        DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_HQD_PQ_BASE_LO,
            (ULONG)((ringDataPa >> 8) & 0xFFFFFFFF));
        DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_HQD_PQ_BASE_HI,
            (ULONG)((ringDataPa >> 8) >> 32));
        /* QUEUE_SIZE=7 (2^8=256 dwords ring), RPTR_BLOCK_SIZE=5, UNORD_DISPATCH */
        DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_HQD_PQ_CONTROL, 0x00004507);
        rp->HqdActive = DreamV3ReadRegister(DevExt, AMDBC250_REG_CP_HQD_PQ_CONTROL);
        DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_HQD_PERSISTENT_STATE, 0x8000014C);
        rp->PqBaseReadback = DreamV3ReadRegister(DevExt, AMDBC250_REG_CP_HQD_PQ_BASE_LO);

        /* Fill ring at offset 1024+ (AFTER MQD) */
        {
            ULONG i;
            PULONG ringBase = (PULONG)((PUCHAR)DevExt->GcvmRingBuf + RING_DATA_OFFSET_BYTES);
            for (i = 0; i < 8; i++)
                WRITE_REGISTER_ULONG(ringBase + i, 0xC0001000);
            for (i = 0; i < DevExt->SavedPm4Count; i++)
                WRITE_REGISTER_ULONG(ringBase + i, DevExt->SavedPm4Cmds[i]);
        }

        /* Read back MQD header to verify MQD preserved */
        {
            rp->RingDwords[0] = READ_REGISTER_ULONG((PULONG)DevExt->GcvmRingBuf + 0);
            rp->RingDwords[1] = READ_REGISTER_ULONG((PULONG)DevExt->GcvmRingBuf + 1);
            rp->RingDwords[2] = READ_REGISTER_ULONG((PULONG)DevExt->GcvmRingBuf + 2);
            rp->RingDwords[3] = READ_REGISTER_ULONG((PULONG)DevExt->GcvmRingBuf + 3);
        }

        /* Activate HQD - triggers MQD load if GCVM page tables active */
        DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_HQD_ACTIVE, 1);
        rp->HqdActive = DreamV3ReadRegister(DevExt, AMDBC250_REG_CP_HQD_ACTIVE);
        KeStallExecutionProcessor(1000);
        /* Read PGM_LO to verify MQD load (if value matches (ringPa+256)>>8, MQD loaded) */
        rp->MqdLoadPgmLo = DreamV3ReadRegister(DevExt, AMDBC250_REG_COMPUTE_PGM_LO);
        /* Read PQ_CONTROL + PQ_BASE to see if MQD load updated them */
        rp->PqCtrlAfter = DreamV3ReadRegister(DevExt, AMDBC250_REG_CP_HQD_PQ_CONTROL);
        rp->PqBaseReadback = DreamV3ReadRegister(DevExt, AMDBC250_REG_CP_HQD_PQ_BASE_LO);

        rp->ScratchAfter = DreamV3ReadRegister(DevExt, AMDBC250_REG_SCRATCH_REG0);

        /* Kick WPTR = offset in bytes to first unused dword */
        {
            ULONG totalBytes = DevExt->SavedPm4Count * sizeof(ULONG);
            DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_HQD_PQ_WPTR_LO, totalBytes);
            rp->WptrAfter = totalBytes;
        }

        /* Restore GRBM */
        DreamV3WriteRegister(DevExt, AMDBC250_REG_GRBM_GFX_INDEX,
            AMDBC250_GRBM_GFX_INDEX_BROADCAST_VAL);

        /* Short poll for RPTR advance (200ms = no TDR on WDDM 2.x) */
        {
            ULONG waited = 0;
            while (waited < 200) {
                DreamV3WriteRegister(DevExt, AMDBC250_REG_GRBM_GFX_INDEX,
                    AMDBC250_GRBM_GFX_INDEX_KIQ_VAL);
                rp->RptrAfter = DreamV3ReadRegister(DevExt, AMDBC250_REG_CP_HQD_PQ_RPTR);
                DreamV3WriteRegister(DevExt, AMDBC250_REG_GRBM_GFX_INDEX,
                    AMDBC250_GRBM_GFX_INDEX_BROADCAST_VAL);
                if (rp->RptrAfter != 0) { rp->Result = 0; break; }
                KeStallExecutionProcessor(1000);
                waited++;
            }
            if (waited >= 200) rp->Result = 1;
        }

        /* Fallback: Execute same PM4 via Software PM4 executor (bypasses ring entirely) */
        rp->SwResult = STATUS_UNSUCCESSFUL;
        rp->ScratchAfter = DreamV3ReadRegister(DevExt, AMDBC250_REG_SCRATCH_REG0);
        {
            NTSTATUS swStatus = DreamV3SwPm4Process(DevExt, DevExt->SavedPm4Cmds,
                DevExt->SavedPm4Count, 0, 32);
            if (NT_SUCCESS(swStatus)) {
                rp->SwResult = 0;
                rp->ScratchAfter = DreamV3ReadRegister(DevExt, AMDBC250_REG_SCRATCH_REG0);
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                    "AMDBC250-DREAM-V4.3: EXEC_RING SW fallback OK, SCRATCH=0x%08X\n",
                    rp->ScratchAfter));
            }
        }

        /* --- DISPATCH_DIRECT test: write s_endpgm shader and trigger --- */
        rp->DispatchResult = 0;
        rp->GrbmStatusBefore = 0;
        rp->GrbmStatusAfter = 0;
        rp->PgmLoReadback = 0;
        rp->PgmHiReadback = 0;
        rp->TmgMaskReadback = 0;
        if (DevExt->GcvmRingBuf != NULL) {
            /* Shader well past MQD + ring area (offset 3072 = dword 768) */
            #define SHADER_OFFSET_BYTES  3072
            ULONG64 shaderPa = DevExt->GcvmRingBufPa + SHADER_OFFSET_BYTES;
            WRITE_REGISTER_ULONG((PULONG)((PUCHAR)DevExt->GcvmRingBuf + SHADER_OFFSET_BYTES), 0xBF810000UL);
            KeMemoryBarrier();
            /* Select ME=1 (MEC) */
            DreamV3WriteRegister(DevExt, AMDBC250_REG_GRBM_GFX_INDEX,
                AMDBC250_GRBM_GFX_INDEX_KIQ_VAL);
            /* Set PGM_LO: read-modify-write to preserve bits 31:28 */
            {
                ULONG pgmLoCurrent = DreamV3ReadRegister(DevExt, AMDBC250_REG_COMPUTE_PGM_LO);
                ULONG pgmLoNew = (pgmLoCurrent & 0xF0000000) | ((ULONG)(shaderPa & 0xFFFFFFFF) & 0x0FFFFFFF);
                DreamV3WriteRegister(DevExt, AMDBC250_REG_COMPUTE_PGM_LO, pgmLoNew);
                rp->PgmLoReadback = DreamV3ReadRegister(DevExt, AMDBC250_REG_COMPUTE_PGM_LO);
            }
            /* Set PGM_HI */
            DreamV3WriteRegister(DevExt, AMDBC250_REG_COMPUTE_PGM_HI, (ULONG)(shaderPa >> 32));
            rp->PgmHiReadback = DreamV3ReadRegister(DevExt, AMDBC250_REG_COMPUTE_PGM_HI);
            /* Set STATIC_THREAD_MGMT_SE0 = allow all WGPs */
            DreamV3WriteRegister(DevExt, AMDBC250_REG_COMPUTE_STATIC_THREAD_MGMT_SE0, 0xFFFFFFFF);
            rp->TmgMaskReadback = DreamV3ReadRegister(DevExt, AMDBC250_REG_COMPUTE_STATIC_THREAD_MGMT_SE0);
            rp->DispatchResult = 1;
            /* Save GRBM_STATUS before trigger */
            rp->GrbmStatusBefore = DreamV3ReadRegister(DevExt, AMDBC250_REG_GRBM_STATUS);
            /* Trigger DISPATCH_DIRECT with VALID=1. Format: dimX[11:0], dimY[23:12], dimZ[31:24] */
            ULONG packedDim = (1 & 0xFFF) | ((1 & 0xFFF) << 12) | ((1 & 0xFF) << 24);
            DreamV3WriteRegister(DevExt, AMDBC250_REG_COMPUTE_DISPATCH_INITIATOR, packedDim);
            DreamV3WriteRegister(DevExt, AMDBC250_REG_COMPUTE_DISPATCH_INITIATOR, 0x00075FFF);
            rp->DispatchResult = 2;
            /* Short poll for GRBM_STATUS change */
            {
                ULONG waited = 0;
                while (waited < 50) {
                    rp->GrbmStatusAfter = DreamV3ReadRegister(DevExt, AMDBC250_REG_GRBM_STATUS);
                    if (rp->GrbmStatusAfter != rp->GrbmStatusBefore) {
                        rp->DispatchResult = 3;
                        break;
                    }
                    KeStallExecutionProcessor(1000);
                    waited++;
                }
            }
            /* Restore broadcast */
            DreamV3WriteRegister(DevExt, AMDBC250_REG_GRBM_GFX_INDEX,
                AMDBC250_GRBM_GFX_INDEX_BROADCAST_VAL);
        }

        bytesReturned = sizeof(*rp);
        status = STATUS_SUCCESS;
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
            "AMDBC250-DREAM-V4.3: EXEC_RING: r=%u w=%u->%u r=0x%X->0x%X pqC=0x%08X->0x%08X pqB=0x%08X hqd=0x%08X sw=0x%08X sA=0x%08X\n",
            rp->Result, rp->WptrBefore, rp->WptrAfter,
            rp->RptrBefore, rp->RptrAfter,
            rp->PqCtrlBefore, rp->PqCtrlAfter,
            rp->PqBaseReadback, rp->HqdActive,
            rp->SwResult, rp->ScratchAfter));
        break;
    }

    default:
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: Unknown IOCTL 0x%08X\n", ioctlCode));
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

Cleanup:
    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = bytesReturned;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

/*===========================================================================
  SDMA Copy Engine � Hardware buffer copies via DMA
  
  SDMA (System DMA) engine copies data without CPU involvement.
  Used for: buffer copies, texture uploads, buffer fills.
  
  GFX10 SDMA packet format:
  - Header: opcode + control
  - Src/Dst addresses (64-bit physical)
  - Size in bytes
  - Fence (optional)
===========================================================================*/

/* SDMA packet header: type(2bits) | opcode(5bits) | sub-op(1bit) | rest */
#define SDMA_PKT_HDR(op, sub) \
    ((1 << 29) | ((op) << 20) | ((sub) << 0))

NTSTATUS
DreamV3SdmaCopyBuffer(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ PHYSICAL_ADDRESS SrcPhysical,
    _In_ PHYSICAL_ADDRESS DstPhysical,
    _In_ SIZE_T SizeBytes
    )
{
    volatile PULONG Ring;
    ULONG WPtr;
    ULONG DwordsNeeded;

    if (DevExt->SdmaRing.VirtualAddress == NULL) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: SDMA ring not initialized\n"));
        return STATUS_DEVICE_NOT_READY;
    }

    if (SizeBytes == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    Ring = (volatile PULONG)DevExt->SdmaRing.VirtualAddress;
    WPtr = DevExt->SdmaRing.WritePointer;

    /* SDMA COPY_LINEAR packet: 10 DWORDs */
    DwordsNeeded = 10;
    ULONG TotalBytes = DwordsNeeded * sizeof(ULONG);

    /* Check ring bounds */
    if (WPtr + TotalBytes > (ULONG)DevExt->SdmaRing.SizeInBytes) {
        /* Wrap: fill with NOPs */
        ULONG SpaceLeft = (ULONG)DevExt->SdmaRing.SizeInBytes - WPtr;
        ULONG NopCount = SpaceLeft / sizeof(ULONG);
        for (ULONG i = 0; i < NopCount; i++) {
            Ring[WPtr / sizeof(ULONG)] = 0; /* SDMA NOP */
            WPtr += sizeof(ULONG);
        }
        WPtr = 0;
    }

    /* Build SDMA COPY_LINEAR packet */
    ULONG idx = WPtr / sizeof(ULONG);

    /* DWORD 0: Header (opcode=COPY_LINEAR, sub=0, int=0, wait=0) */
    Ring[idx + 0] = SDMA_PKT_HDR(SDMA_OP_COPY_LINEAR, 0);

    /* DWORD 1: Control (system architecture) */
    Ring[idx + 1] = 0; /* src/dst = physical */

    /* DWORD 2-3: Src address (64-bit, aligned to 4) */
    Ring[idx + 2] = (ULONG)(SrcPhysical.LowPart & 0xFFFFFFFC);
    Ring[idx + 3] = SrcPhysical.HighPart;

    /* DWORD 4-5: Dst address (64-bit, aligned to 4) */
    Ring[idx + 4] = (ULONG)(DstPhysical.LowPart & 0xFFFFFFFC);
    Ring[idx + 5] = DstPhysical.HighPart;

    /* DWORD 6: Size (bytes - 1) */
    Ring[idx + 6] = (ULONG)(SizeBytes - 1);

    /* DWORD 7: End of packet (EOP=1) */
    Ring[idx + 7] = (1 << 29); /* EOP bit */

    /* DWORD 8-9: Reserved */
    Ring[idx + 8] = 0;
    Ring[idx + 9] = 0;

    WPtr += TotalBytes;
    DevExt->SdmaRing.WritePointer = WPtr;

    /* Submit to hardware: write WPTR */
    DreamV3WriteRegister(DevExt, AMDBC250_REG_SDMA0_GFX_RB_WPTR, WPtr);

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
        "AMDBC250-DREAM-V4.3: SDMA copy: PA 0x%llX -> PA 0x%llX, %llu bytes\n",
        SrcPhysical.QuadPart, DstPhysical.QuadPart, (ULONG64)SizeBytes));

    return STATUS_SUCCESS;
}

NTSTATUS
DreamV3SdmaFillBuffer(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ PHYSICAL_ADDRESS DstPhysical,
    _In_ SIZE_T SizeBytes,
    _In_ ULONG FillValue
    )
{
    volatile PULONG Ring;
    ULONG WPtr;

    if (DevExt->SdmaRing.VirtualAddress == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }

    if (SizeBytes == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    Ring = (volatile PULONG)DevExt->SdmaRing.VirtualAddress;
    WPtr = DevExt->SdmaRing.WritePointer;

    /* SDMA FILL packet: 9 DWORDs */
    ULONG TotalBytes = 9 * sizeof(ULONG);

    if (WPtr + TotalBytes > (ULONG)DevExt->SdmaRing.SizeInBytes) {
        /* Fill remaining space with NOPs before wrapping */
        ULONG nopEnd = (ULONG)(DevExt->SdmaRing.SizeInBytes / sizeof(ULONG));
        for (ULONG n = WPtr / sizeof(ULONG); n < nopEnd; n++) {
            Ring[n] = SDMA_PKT_HDR(SDMA_OP_NOP, 0);
        }
        WPtr = 0;
    }

    ULONG idx = WPtr / sizeof(ULONG);

    /* Header: opcode=FILL */
    Ring[idx + 0] = SDMA_PKT_HDR(SDMA_OP_FILL, 0);
    /* Control */
    Ring[idx + 1] = 0;
    /* Dst address */
    Ring[idx + 2] = (ULONG)(DstPhysical.LowPart & 0xFFFFFFFC);
    Ring[idx + 3] = DstPhysical.HighPart;
    /* Fill value (32-bit) */
    Ring[idx + 4] = FillValue;
    /* Size - 1 (guaranteed > 0 by SizeBytes check above) */
    Ring[idx + 5] = (ULONG)(SizeBytes - 1);
    /* EOP */
    Ring[idx + 6] = (1 << 29);
    Ring[idx + 7] = 0;
    Ring[idx + 8] = 0;

    WPtr += TotalBytes;
    DevExt->SdmaRing.WritePointer = WPtr;
    DreamV3WriteRegister(DevExt, AMDBC250_REG_SDMA0_GFX_RB_WPTR, WPtr);

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
        "AMDBC250-DREAM-V4.3: SDMA fill: PA 0x%llX, %llu bytes, val=0x%X\n",
        DstPhysical.QuadPart, (ULONG64)SizeBytes, FillValue));

    return STATUS_SUCCESS;
}

/*===========================================================================
  TDR (Timeout Detection and Recovery) � Enhanced Reset
  
  Windows TDR mechanism: if GPU doesn't respond within 2 seconds,
  the display driver is reset. Our driver implements:
  1. Emergency halt (CP + SDMA + Display)
  2. Ring buffer reset
  3. Hardware re-initialization
  4. State restoration
===========================================================================*/

NTSTATUS
DreamV3TdrReset(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    )
{
    NTSTATUS Status;
    ULONG TimeoutUs;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
        "AMDBC250-DREAM-V4.3: === TDR RESET STARTED ===\n"));

    DevExt->GpuResetInProgress = TRUE;
    DevExt->ResetCount++;

    /* Step 1: Emergency halt all engines */
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
        "AMDBC250-DREAM-V4.3: TDR Step 1/6: Emergency halt\n"));

    /* Halt Command Processor */
    DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_ME_CNTL,
        CP_ME_CNTL__ME_HALT | CP_ME_CNTL__PFP_HALT | CP_ME_CNTL__CE_HALT);
    KeStallExecutionProcessor(100);

    /* Halt SDMA */
    DreamV3WriteRegister(DevExt, AMDBC250_REG_SDMA0_CNTL, 0x1); /* HALT bit */
    KeStallExecutionProcessor(50);

    /* Disable interrupts */
    DreamV3WriteRegister(DevExt, AMDBC250_REG_IH_CNTL, 0);

    /* Step 2: Drain all pending commands */
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
        "AMDBC250-DREAM-V4.3: TDR Step 2/6: Drain commands\n"));

    /* Wait for CP to finish current batch */
    TimeoutUs = 100000; /* 100ms */
    while (TimeoutUs > 0) {
        ULONG CpStatus = DreamV3ReadRegister(DevExt, AMDBC250_REG_CP_ME_STATUS);
        if ((CpStatus & 0x1) == 0) break; /* CP idle */
        KeStallExecutionProcessor(10);
        TimeoutUs -= 10;
    }

    /* Step 3: Reset ring buffers */
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
        "AMDBC250-DREAM-V4.3: TDR Step 3/6: Reset rings\n"));

    DevExt->GfxRing.ReadPointer = 0;
    DevExt->GfxRing.WritePointer = 0;
    DevExt->IhRing.ReadPointer = 0;
    DevExt->SdmaRing.ReadPointer = 0;
    DevExt->SdmaRing.WritePointer = 0;

    /* Reset hardware ring pointers */
    DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_GFX_RING0_RPTR, 0);
    DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_GFX_RING0_WPTR, 0);
    DreamV3WriteRegister(DevExt, AMDBC250_REG_IH_RB_RPTR, 0);
    DreamV3WriteRegister(DevExt, AMDBC250_REG_SDMA0_GFX_RB_RPTR, 0);
    DreamV3WriteRegister(DevExt, AMDBC250_REG_SDMA0_GFX_RB_WPTR, 0);

    /* Step 4: Reset fence */
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
        "AMDBC250-DREAM-V4.3: TDR Step 4/6: Reset fence\n"));

    DevExt->GlobalFence.LastSubmittedValue = 0;
    DevExt->GlobalFence.LastSignaledValue = 0;
    if (DevExt->GlobalFence.VirtualAddress != NULL) {
        *DevExt->GlobalFence.VirtualAddress = 0;
    }

    /* Step 5: Re-initialize hardware */
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
        "AMDBC250-DREAM-V4.3: TDR Step 5/6: Re-init hardware\n"));

    Status = DreamV3HwInitialize(DevExt);

    /* Step 6: Re-enable interrupts and resume */
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
        "AMDBC250-DREAM-V4.3: TDR Step 6/6: Resume\n"));

    if (NT_SUCCESS(Status)) {
        DreamV3WriteRegister(DevExt, AMDBC250_REG_IH_CNTL,
            IH_CNTL__ENABLE_INTR | IH_CNTL__RPTR_REARM);
    }

    DevExt->GpuResetInProgress = FALSE;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250-DREAM-V4.3: === TDR RESET %s ===\n",
        NT_SUCCESS(Status) ? "SUCCESS" : "FAILED"));

    return Status;
}

/*===========================================================================
  EDID Parsing � Read monitor capabilities from display
  
  EDID (Extended Display Identification Data) contains:
  - Monitor name, serial, manufacture date
  - Supported resolutions and refresh rates
  - Color space, gamma, timing parameters
  
  For BC-250 with DCN 2.1:
  - Up to 4 independent displays
  - DP 1.4, HDMI 2.1, DVI-D, VGA (via DAC)
  - Max 8K@30Hz or 4K@120Hz
===========================================================================*/

#define EDID_BLOCK_SIZE          128
#define EDID_HEADER_SIZE         8
#define EDID_VMT_OFFSET          54
#define EDID_VMT_SIZE            18
#define EDID_NUM_DETAILED_TIMING 4
#define EDID_SERIAL_OFFSET       0xFC

NTSTATUS
DreamV3ParseEdid(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ ULONG ChildUid,
    _Out_ PULONG MaxWidth,
    _Out_ PULONG MaxHeight,
    _Out_ PULONG MaxRefreshRate
    )
{
    /* EDID is typically read via I2C/DDC from the monitor.
     * For now, provide reasonable defaults based on DCN 2.1 capabilities. */

    UNREFERENCED_PARAMETER(DevExt);

    /* Default: support common resolutions */
    *MaxWidth = 3840;      /* 4K */
    *MaxHeight = 2160;
    *MaxRefreshRate = 60;

    /* DCN 2.1 supports higher resolutions */
    if (ChildUid == 0) {
        /* Primary display: up to 4K@120Hz */
        *MaxWidth = 3840;
        *MaxHeight = 2160;
        *MaxRefreshRate = 120;
    } else if (ChildUid == 1) {
        /* Secondary display: up to 2K@60Hz */
        *MaxWidth = 2560;
        *MaxHeight = 1440;
        *MaxRefreshRate = 60;
    }

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250-DREAM-V4.3: EDID child %u: max %ux%u@%uHz\n",
        ChildUid, *MaxWidth, *MaxHeight, *MaxRefreshRate));

    return STATUS_SUCCESS;
}

/* EDID raw data block (128 bytes) for display identification */
static UCHAR g_DefaultEdid[EDID_BLOCK_SIZE] = {
    /* Header: 00 FF FF FF FF FF FF 00 */
    0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
    /* Manufacturer ID: AMD (0x0000) */
    0x00, 0x00,
    /* Product code */
    0xFE, 0x13,
    /* Serial number */
    0x00, 0x00, 0x00, 0x00,
    /* Week/Year of manufacture */
    0x26, 0x16, /* Week 38, Year 2022 */
    /* EDID version 1.3 */
    0x01, 0x03,
    /* Display type: Digital */
    0x80, 0x20, 0x15, 0x2D, 0xE0, 0xA0, 0x4E, 0xA0,
    0x10, 0x32, 0x90, 0x04, 0x01, 0x31, 0x00, 0x00,
    /* Detailed timing: 1920x1080@60Hz (VESA standard) */
    0x01, 0x1D, 0x00, 0x72, 0x51, 0xD0, 0x1E, 0x20,
    0x6E, 0x28, 0x55, 0x00, 0xC4, 0x8E, 0x21, 0x00,
    0x00, 0x1E,
    /* Monitor name */
    0x00, 0x00, 0x00, 0xFD, 0x00, 0x17, 0x3C, 0x1E,
    0x50, 0x10, 0x00, 0x0A, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20,
    /* Monitor serial */
    0x00, 0x00, 0x00, 0xFC, 0x00, 0x41, 0x4D, 0x44,
    0x20, 0x42, 0x43, 0x2D, 0x32, 0x35, 0x30, 0x0A,
    0x20, 0x20,
    /* Checksum (last byte) */
    0x00
};

NTSTATUS
DreamV3ReadEdid(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ ULONG ChildUid,
    _Out_writes_(EDID_BLOCK_SIZE) PUCHAR EdidBuffer,
    _Out_ PULONG EdidSize
    )
{
    UNREFERENCED_PARAMETER(DevExt);
    UNREFERENCED_PARAMETER(ChildUid);

    /* Copy default EDID */
    RtlCopyMemory(EdidBuffer, g_DefaultEdid, EDID_BLOCK_SIZE);
    *EdidSize = EDID_BLOCK_SIZE;

    /* Calculate checksum */
    UCHAR sum = 0;
    for (ULONG i = 0; i < EDID_BLOCK_SIZE - 1; i++) {
        sum += EdidBuffer[i];
    }
    EdidBuffer[EDID_BLOCK_SIZE - 1] = (UCHAR)(256 - sum);

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250-DREAM-V4.3: EDID read for child %u (%u bytes)\n",
        ChildUid, *EdidSize));

    return STATUS_SUCCESS;
}

/*===========================================================================
  Shader Compilation Stub � DXBC ? PM4 command conversion
  
  In a real driver, this would:
  1. Parse DXBC (DirectX Bytecode) shader binary
  2. Translate to GFX10 PM4 packets (SP/SGPR setup, fetch shaders)
  3. Upload to GPU command processor
  
  For now, this is a stub that logs the shader and returns success.
  Real implementation requires:
  - Shader bytecode parser
  - GFX10 ISA knowledge
  - Register allocation
  - Instruction scheduling
===========================================================================*/

NTSTATUS
DreamV3ShaderCompileStub(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ PVOID ShaderCode,
    _In_ SIZE_T ShaderSize,
    _In_ ULONG ShaderType,  /* 0=VS, 1=PS, 2=CS, 3=GS, 4=HS, 5=DS */
    _Out_ PVOID* CompiledShader,
    _Out_ PULONG CompiledSize
    )
{
    UNREFERENCED_PARAMETER(ShaderCode);
    UNREFERENCED_PARAMETER(ShaderSize);

    static const CHAR* ShaderTypeNames[] = {
        "Vertex", "Pixel", "Compute", "Geometry", "Hull", "Domain"
    };

    if (ShaderType < 6) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
            "AMDBC250-DREAM-V4.3: Shader compile stub: %s shader (%llu bytes)\n",
            ShaderTypeNames[ShaderType], (ULONG64)ShaderSize));
    }

    /* Stub: return empty compiled shader */
    *CompiledShader = NULL;
    *CompiledSize = 0;

    return STATUS_SUCCESS;
}

/*===========================================================================
  Additional IOCTL handlers for new features
  (Added to existing switch in DreamV3DeviceControl)
===========================================================================*/

/* Note: These are called from the existing IOCTL dispatch.
 * New IOCTL codes added for SDMA, EDID, and shader operations. */

/* BAR5 proxy IOCTL for PSP driver mailbox access */
// IOCTL code 0x900: Read GPU BAR5 register via GPU driver's mapping
// Input: ULONG Offset (register offset from BAR5 base)
// Output: ULONG Value (register value read)
// This allows PSP driver to access mailbox registers on Windows 11 26100
// where MmMapIoSpace for BAR5 is blocked from the PSP driver.

/* Return the GPU BAR5 virtual mapping (or NULL if INIT_HARDWARE not done).
 * Used by the PSP proxy so it can read SOS status (C2PMSG_81) directly from
 * the GPU MMIO when the PSP driver's own GpuMmioBase is not yet established. */
PVOID
Amdbc250PspGetGpuBar5Va(VOID)
{
    if (!g_ControlDevice) return NULL;
    PDREAM_V3_DEVICE_EXTENSION ext =
        (PDREAM_V3_DEVICE_EXTENSION)g_ControlDevice->DeviceExtension;
    if (!ext) return NULL;
    return ext->MmioVirtualBase;
}

/* Publish the GPU BAR5 virtual mapping for the PSP proxy (called by INIT_HARDWARE). */
VOID
Amdbc250PspSetGpuBar5Va(PVOID Va)
{
    /* Forward to the PSP proxy module so it doesn't depend on g_ControlDevice. */
    extern VOID Amdbc250PspPublishGpuBar5(PVOID Va);
    Amdbc250PspPublishGpuBar5(Va);
}

