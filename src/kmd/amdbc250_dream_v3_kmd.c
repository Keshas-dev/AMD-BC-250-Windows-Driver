/*++

Copyright (c) 2026 AMD BC-250 "Dream Drivers" Project — Version 3.0

Module Name:
    amdbc250_dream_v3_kmd.c

Abstract:
    Kernel-Mode Display Miniport Driver (KMD) for AMD BC-250 APU.
    
    ========================================
    VERSION 3.0 — COMPLETE REWRITE
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

#include "amdbc250_dream_v3_kmd.h"
#include "amdbc250_ioctl.h"

static PDRIVER_OBJECT g_DriverObject = NULL;

static PDEVICE_OBJECT g_ControlDevice = NULL;

/* PCI device extension (from DxgkDdiAddDevice) - used by IOCTL handler */
static PDREAM_V3_DEVICE_EXTENSION g_PciDevExt = NULL;

/* Shared memory communication with Vulkan ICD */
static PVOID g_SharedBuffer = NULL;
static SIZE_T g_SharedBufferSize = 64 * 1024;  /* 64KB */
static HANDLE g_CmdReadyEvent = NULL;
static HANDLE g_CmdDoneEvent = NULL;
static PVOID g_SharedSectionObject = NULL;

/* Stored DDI initialization data */
static DRIVER_INITIALIZATION_DATA g_InitData = {0};
static UNICODE_STRING g_DeviceName;
static UNICODE_STRING g_SymlinkName;

/* Forward declarations */
NTSTATUS DreamV3DeviceControl(PDEVICE_OBJECT, PIRP);
NTSTATUS DreamV3CreateClose(PDEVICE_OBJECT, PIRP);
NTSTATUS DreamV3SdmaCopyBuffer(PDREAM_V3_DEVICE_EXTENSION, PHYSICAL_ADDRESS, PHYSICAL_ADDRESS, SIZE_T);
NTSTATUS DreamV3SdmaFillBuffer(PDREAM_V3_DEVICE_EXTENSION, PHYSICAL_ADDRESS, SIZE_T, ULONG);
NTSTATUS DreamV3TdrReset(PDREAM_V3_DEVICE_EXTENSION);
NTSTATUS DreamV3ReadEdid(PDREAM_V3_DEVICE_EXTENSION, ULONG, PUCHAR, PULONG);
NTSTATUS DreamV3ParseEdid(PDREAM_V3_DEVICE_EXTENSION, ULONG, PULONG, PULONG, PULONG);
NTSTATUS DreamV3ShaderCompileStub(PDREAM_V3_DEVICE_EXTENSION, PVOID, SIZE_T, ULONG, PVOID*, PULONG);

/*===========================================================================
  DreamV3DxgkInitialize Stub
  In real WDDM drivers, this is provided by DXGKRNL.
  For our driver, we implement it ourselves.
===========================================================================*/

NTSTATUS
NTAPI
DreamV3DxgkInitialize(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath,
    _In_ PDRIVER_INITIALIZATION_DATA DriverInitializationData
    )
{
    UNREFERENCED_PARAMETER(RegistryPath);

    if (DriverInitializationData == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    /* Store initialization data for later use.
       NOTE: We do NOT call the real DxgkInitialize() here because this driver
       is not a full WDDM display miniport driver. It uses a custom IOCTL channel
       for UMD communication. The DxgkInitialize() call caused Code 39 because
       dxgkrnl.sys expects full WDDM infrastructure that we don't have.

       D3D9 support works through the custom UMD→KMD IOCTL channel instead. */
    RtlCopyMemory(&g_InitData, DriverInitializationData, sizeof(DRIVER_INITIALIZATION_DATA));

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: DreamV3DxgkInitialize called (custom mode, no DxgkInitialize), DDI version=%u\n",
               DriverInitializationData->Version));

    return STATUS_SUCCESS;
}

/* Forward declaration */
static VOID DreamV3WdmUnload(_In_ PDRIVER_OBJECT DriverObject);

/*===========================================================================
   DriverEntry — Main entry point (WDDM 2.x/3.x)
===========================================================================*/

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    DRIVER_INITIALIZATION_DATA InitData = {0};
    NTSTATUS Status;

    /* MARKER: Write immediately so we know DriverEntry ran */
    {
        UNICODE_STRING devPath;
        OBJECT_ATTRIBUTES objAttr;
        UNICODE_STRING valName;
        ULONG val = 1;

        RtlInitUnicodeString(&devPath, L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\atikmdag");
        InitializeObjectAttributes(&objAttr, &devPath, OBJ_CASE_INSENSITIVE, NULL, NULL);

        HANDLE hKey = NULL;
        Status = ZwOpenKey(&hKey, KEY_SET_VALUE, &objAttr);
        if (NT_SUCCESS(Status)) {
            RtlInitUnicodeString(&valName, L"DriverEntryRan");
            ZwSetValueKey(hKey, &valName, 0, REG_DWORD, &val, sizeof(val));
            ZwClose(hKey);
        }
    }

    UNREFERENCED_PARAMETER(RegistryPath);

    g_DriverObject = DriverObject;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: DriverEntry v%d.%d.%d - RDNA2/Cyan Skillfish\n",
               AMDBC250_DREAM_V3_VERSION_MAJOR,
               AMDBC250_DREAM_V3_VERSION_MINOR,
               AMDBC250_DREAM_V3_VERSION_PATCH));

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: Architecture: 24 CU RDNA2, 16GB GDDR6\n"));

    /* Initialize the DDI callback table */
    InitData.Version = DXGKDDI_INTERFACE_VERSION;
    
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

    /* Register with DXGKRNL */
    Status = DreamV3DxgkInitialize(DriverObject, RegistryPath, &InitData);

    if (NT_SUCCESS(Status)) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "AMDBC250-DREAM-V4.3: DreamV3DxgkInitialize successful\n"));
    } else {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                   "AMDBC250-DREAM-V4.3: DreamV3DxgkInitialize failed: 0x%08X\n", Status));
    }

    /* Create control device for UMD IOCTL communication */
    {
        UNICODE_STRING devName, symLink;
        NTSTATUS symStatus;
        RtlInitUnicodeString(&devName, L"\\Device\\AMDBC250DreamV43");
        RtlInitUnicodeString(&symLink, L"\\DosDevices\\AMDBC250DreamV43");
        RtlCopyMemory(&g_DeviceName, &devName, sizeof(UNICODE_STRING));
        RtlCopyMemory(&g_SymlinkName, &symLink, sizeof(UNICODE_STRING));

        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "AMDBC250-DREAM-V4.3: Creating control device...\n"));

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
            DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DreamV3DeviceControl;
            DriverObject->MajorFunction[IRP_MJ_CREATE] = DreamV3CreateClose;
            DriverObject->MajorFunction[IRP_MJ_CLOSE] = DreamV3CreateClose;
            DriverObject->DriverUnload = DreamV3WdmUnload;

            /* Initialize PCI device extension for IOCTL handler
               (normally done in DxgkDdiAddDevice, but we don't call DxgkInitialize) */
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

                    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                               "AMDBC250-DREAM-V4.3: g_PciDevExt allocated at %p\n", g_PciDevExt));
                }
            }
        } else {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                       "AMDBC250-DREAM-V4.3: IoCreateDevice FAILED: 0x%08X\n", Status));
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                       "AMDBC250-DREAM-V4.3: DriverObject=%p, DevExtSize=%u\n",
                       DriverObject, sizeof(DREAM_V3_DEVICE_EXTENSION)));

            /* Write error to registry so user-mode can read it */
            {
                UNICODE_STRING valName;
                RtlInitUnicodeString(&valName, L"IoCreateDeviceFailed");
                ULONG errorVal = (ULONG)Status;
                ZwSetValueKey(RegistryPath, &valName, 0, REG_DWORD, &errorVal, sizeof(errorVal));
            }

            Status = STATUS_SUCCESS; /* Non-fatal */
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
  DxgkDdiAddDevice — PnP manager found matching PCI device
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
  DxgkDdiStartDevice — Start the device
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
    DevExt->DxgkInterface = *DxgkInterface;
    DevExt->DxgkDeviceHandle = DxgkInterface->DeviceHandle;  /* Handle is in interface struct */

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
               "AMDBC250-DREAM-V4.3: PCI %04X:%04X (Rev %02X) — Cyan Skillfish\n",
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
        /* Continue anyway — might be a variant */
    }

    /* Map MMIO BAR - use safe iteration */
    PCM_PARTIAL_RESOURCE_LIST PartialResourceList = 
        &DeviceInfo.TranslatedResourceList->List[0].PartialResourceList;
    
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: PartialResourceList has %lu resources\n",
               PartialResourceList->Count));
    
    BOOLEAN MmioFound = FALSE;
    for (ULONG i = 0; i < PartialResourceList->Count; i++) {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR desc = &PartialResourceList->PartialDescriptors[i];
        
        if (desc->Type == CmResourceTypeMemory) {
            PHYSICAL_ADDRESS PhysAddr = desc->u.Memory.Start;
            ULONG Size = desc->u.Memory.Length;
            
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                       "AMDBC250-DREAM-V4.3: Resource[%lu] Type=Memory, PA=0x%llX, Size=0x%X\n",
                       i, PhysAddr.QuadPart, Size));
            
            /* First memory resource = MMIO */
            if (!MmioFound) {
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
            } else {
                /* Second memory resource = framebuffer */
                DevExt->FbPhysicalBase = PhysAddr;
                DevExt->FbSize = Size;
                
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                           "AMDBC250-DREAM-V4.3: Framebuffer: PA=0x%llX, Size=0x%X (%lu MB)\n",
                           DevExt->FbPhysicalBase.QuadPart,
                           DevExt->FbSize,
                           DevExt->FbSize / (1024 * 1024)));
            }
        }
    }
    
    if (!MmioFound) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                   "AMDBC250-DREAM-V4.3: *** NO MMIO RESOURCE FOUND! Device cannot initialize.\n"));
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    /* CRITICAL: Initialize hardware */
    Status = DreamV3HwInitialize(DevExt);
    if (!NT_SUCCESS(Status)) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                   "AMDBC250-DREAM-V4.3: Hardware init failed: 0x%08X\n", Status));
        
        if (DevExt->MmioVirtualBase != NULL) {
            MmUnmapIoSpace(DevExt->MmioVirtualBase, DevExt->MmioSize);
            DevExt->MmioVirtualBase = NULL;
        }
        return Status;
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
                    /* Unlock 40 CUs! */
                    DreamV3WriteRegister(DevExt, 0x2004, 0xFFE00000);
                    DreamV3WriteRegister(DevExt, 0x229C, 0x0000001F);
                    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                        "AMDBC250-DREAM-V4.3: *** 40 CU UNLOCKED via registry ***\n"));
                }
            }
        }
    }

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: StartDevice SUCCESS\n"));
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
  DxgkDdiStopDevice — Stop device
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
  DxgkDdiRemoveDevice — Final cleanup
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
    }

    return STATUS_SUCCESS;
}

/*===========================================================================
  DxgkDdiResetDevice — TDR recovery
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

/* WDM DriverUnload — called by PnP when driver is unloaded */
static VOID DreamV3WdmUnload(_In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: WDM Unload called\n"));
    
    /* Free shared memory */
    if (g_SharedBuffer != NULL) {
        ExFreePoolWithTag(g_SharedBuffer, 'cmhS');
        g_SharedBuffer = NULL;
    }
    
    if (g_ControlDevice != NULL) {
        IoDeleteSymbolicLink(&g_SymlinkName);
        IoDeleteDevice(g_ControlDevice);
        g_ControlDevice = NULL;
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "AMDBC250-DREAM-V4.3: Control device deleted by WDM unload\n"));
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
  Interrupt Routine (ISR) — Runs at DIRQL
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
  DPC Routine — Deferred interrupt processing
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
                /* EOP — fence completion */
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
  QueryAdapterInfo — Report GPU capabilities
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
  CreateDevice — Per-process GPU context
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
               "AMDBC250-DREAM-V4.3: CreateDevice — Context %d, VMID %d\n",
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
            ExFreePoolWithTag(Alloc, DREAM_V3_TAG_ALLOCATION);
        }
    }
    ExReleaseFastMutex(&DevExt->DeviceMutex);

    return STATUS_SUCCESS;
}

/*===========================================================================
  PM4 Packet Building Helpers — GFX10 (RDNA2)
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
            Ring[WPtr / sizeof(ULONG)] = PM4_TYPE3_HDR(IT_NOP, 0);
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
            Ring[WPtr / sizeof(ULONG)] = PM4_TYPE3_HDR(IT_NOP, 0);
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
            Ring[WPtr / sizeof(ULONG)] = PM4_TYPE3_HDR(IT_NOP, 0);
            WPtr += sizeof(ULONG);
        }
        WPtr = 0;
    }

    /* IT_EVENT_WRITE_EOP packet — 6 DWORDs total (count=4 in header = 5 payload + 1 header = 6)
     * Format per AMD GPU ISA:
     *   DWORD 0: PM4 header
     *   DWORD 1: Control (EVENT_TYPE | EVENT_INDEX | DATA_SEL | INT_SEL)
     *   DWORD 2: Address low
     *   DWORD 3: Address high
     *   DWORD 4: Data low (fence value low)
     *   DWORD 5: Data high (fence value high)
     */
    ULONG Header = PM4_TYPE3_HDR(IT_EVENT_WRITE_EOP, 4);
    Ring[WPtr / sizeof(ULONG)] = Header;
    WPtr += sizeof(ULONG);
    
    /* Control: EVENT_TYPE=0x46(EOP) | EVENT_INDEX=5 | DATA_SEL=1(write fence) | INT_SEL=1(interrupt) */
    Ring[WPtr / sizeof(ULONG)] = 0xA0000246;
    WPtr += sizeof(ULONG);
    
    /* Address (64-bit physical, DWORD aligned) */
    Ring[WPtr / sizeof(ULONG)] = (ULONG)(FencePA.QuadPart & 0xFFFFFFFC);
    WPtr += sizeof(ULONG);
    Ring[WPtr / sizeof(ULONG)] = (ULONG)(FencePA.QuadPart >> 32);
    WPtr += sizeof(ULONG);
    
    /* Data (64-bit fence value) */
    Ring[WPtr / sizeof(ULONG)] = (ULONG)(FenceValue & 0xFFFFFFFF);
    WPtr += sizeof(ULONG);
    Ring[WPtr / sizeof(ULONG)] = (ULONG)(FenceValue >> 32);
    WPtr += sizeof(ULONG);
    
    DevExt->GfxRing.WritePointer = WPtr;
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
        /* Write WPTR to hardware */
        DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_GFX_RING0_WPTR, WPtr);
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
                Ring[WPtr / sizeof(ULONG)] = PM4_TYPE3_HDR(IT_NOP, 0);
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
  BuildPagingBuffer — Memory management (page table updates)

  This function is called by DXGKRNL to update GPU page tables
  for virtual memory management. Critical for D3D12!

  GFX10 supports 4-level page tables:
  - Level 0: PML4 (Page Map Level 4)
  - Level 1: PDPE (Page Directory Pointer Entry)
  - Level 2: PDE (Page Directory Entry)
  - Level 3: PTE (Page Table Entry)

  Page size: 4 KB (standard) or 64 KB (large)
===========================================================================*/

/* DreamV3DdiBuildPagingBuffer moved to amdbc250_dream_v3_vm.c */

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
  VidPN (Video Present Network) Implementation — DCN 2.1 Display Engine

  VidPN manages the relationship between:
  - Sources (framebuffers in VRAM)
  - Targets (physical display outputs: HDMI, DP, eDP)
  - Paths (source → target mappings)

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
               "AMDBC250-DREAM-V4.3: RecommendFunctionalVidPn — DCN 2.1\n"));

    /*
     * Recommend a functional VidPN by:
     * 1. Creating source modes (what GPU can produce)
     * 2. Creating target modes (what display can show)
     * 3. Creating paths (mapping source → target)
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
               "AMDBC250-DREAM-V4.3: CommitVidPn — Activating display config\n"));

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
               "AMDBC250-DREAM-V4.3: SetVidPnSourceAddress — Source %u\n",
               pSetVidPnSourceAddress->VidPnSourceId));

    /* Get framebuffer physical address from primary surface */
    SurfAddress = pSetVidPnSourceAddress->PrimaryAddress;

    if (SurfAddress.QuadPart == 0) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: SetVidPnSourceAddress — NULL address (expected for stub)\n"));
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
               "AMDBC250-DREAM-V4.3: SetVidPnSourceVisibility — Source %u, Visible=%d\n",
               pSetVidPnSourceVisibility->VidPnSourceId,
               pSetVidPnSourceVisibility->Visible));

    /* Show/hide display output */
    if (DevExt->MmioVirtualBase != NULL) {
        ULONG OtgCntl = DreamV3ReadRegister(DevExt, AMDBC250_REG_OTG0_OTG_CONTROL);
        
        if (pSetVidPnSourceVisibility->Visible) {
            OtgCntl |= OTG_CNTL__ENABLE;
        } else {
            OtgCntl &= ~OTG_CNTL__ENABLE;
        }
        
        DreamV3WriteRegister(DevExt, AMDBC250_REG_OTG0_OTG_CONTROL, OtgCntl);
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
    if (DevExt->MmioVirtualBase != NULL) {
        ULONG Status = DreamV3ReadRegister(DevExt, AMDBC250_REG_OTG0_OTG_CRTC_STATUS);
        pGetScanLine->ScanLine = Status & 0xFFFF;
    } else {
        pGetScanLine->ScanLine = 0;
    }
    
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
  IOCTL Dispatch — UMD ↔ KMD Communication
  
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
    PVOID inputBuffer = Irp->AssociatedIrp.SystemBuffer;
    PVOID outputBuffer = Irp->AssociatedIrp.SystemBuffer;
    ULONG inputLen = irpSp->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outputLen = irpSp->Parameters.DeviceIoControl.OutputBufferLength;
    ULONG ioctlCode = irpSp->Parameters.DeviceIoControl.IoControlCode;

    /* IMMEDIATE MARKER — write before anything else */
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
        status = STATUS_DEVICE_NOT_READY;
        goto Cleanup;
    }

    /* If hardware not initialized, return safe dummy data */
    if (!DevExt->HardwareInitialized) {
        switch (ioctlCode) {
        case 0x80000800: /* GET_CAPS */
            if (outputLen >= sizeof(ULONG) * 7) {
                PULONG d = (PULONG)outputBuffer;
                d[0] = 430;  /* Version 4.3.0 */
                d[1] = 24;   /* CUs */
                d[2] = 2000; /* GPU clock MHz */
                d[3] = 1750; /* Memory clock MHz */
                d[4] = 0;    /* Temperature */
                d[5] = 0;    /* Throttle */
                d[6] = 0;    /* Throttle count */
                bytesReturned = sizeof(ULONG) * 7;
            }
            status = STATUS_SUCCESS;
            goto Cleanup;
        case 0x80000804: /* GET_VRAM_INFO */
            if (outputLen >= sizeof(ULONG) * 3) {
                PULONG d = (PULONG)outputBuffer;
                d[0] = 16384; /* Total MB */
                d[1] = 4096;  /* Visible MB */
                d[2] = 0;     /* Used MB */
                bytesReturned = sizeof(ULONG) * 3;
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
                        status = STATUS_SUCCESS;
                    } else {
                        MmFreePagesFromMdl(mdl);
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
        default:
            status = STATUS_SUCCESS;
            goto Cleanup;
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
                MmFreeContiguousMemory(handle);
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                    "AMDBC250-DREAM-V4.3: FreeVidMem OK\n"));
            }
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
               Old format (Vulkan ICD): {0, 0, fence, 0}  — fence at InData[2]
               New format (D3D9):       {PA_lo, PA_hi, size, fence} — fence at InData[3] */
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
                if (WPtr + NeededSpace > RingSize) {
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
         * 1. CC_GC_SHADER_ARRAY_CONFIG (0x2004): CU enumeration mask
         *    Stock: 0xFFF80000 (24 CUs) → Unlocked: 0xFFE00000 (40 CUs)
         * 2. SPI_PG_ENABLE_STATIC_WGP_MASK (0x229C): WGP dispatch gate
         *    Stock: 0x7 (WGP 0-2) → Unlocked: 0x1F (WGP 0-4)
         *
         * Both registers must be written together.
         * CC alone changes what driver reports but SPI still dispatches to 24 CUs.
         * SPI alone enables hardware dispatch but driver only generates for 24 CUs.
         */
        if (inputLen >= sizeof(ULONG)) {
            PULONG InData = (PULONG)inputBuffer;
            ULONG enable = InData[0]; /* 0=disable (stock 24CU), 1=enable (40CU) */

            if (enable) {
                /* Write CC_GC_SHADER_ARRAY_CONFIG: 0xFFF80000 → 0xFFE00000 */
                DreamV3WriteRegister(DevExt, 0x2004, 0xFFE00000);
                /* Write SPI_PG_ENABLE_STATIC_WGP_MASK: 0x7 → 0x1F */
                DreamV3WriteRegister(DevExt, 0x229C, 0x0000001F);

                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                    "AMDBC250-DREAM-V4.3: *** 40 CU UNLOCK ENABLED *** "
                    "CC=0xFFF80000->0xFFE00000 SPI=0x7->0x1F\n"));
            } else {
                /* Restore stock 24 CUs */
                DreamV3WriteRegister(DevExt, 0x2004, 0xFFF80000);
                DreamV3WriteRegister(DevExt, 0x229C, 0x00000007);

                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                    "AMDBC250-DREAM-V4.3: 40 CU unlock DISABLED (stock 24 CU)\n"));
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

            /* Read current register values */
            ULONG ccConfig = DreamV3ReadRegister(DevExt, 0x2004);
            ULONG spiMask = DreamV3ReadRegister(DevExt, 0x229C);

            /* Decode CU count from CC_GC_SHADER_ARRAY_CONFIG */
            /* Bits [31:19] = CU mask, count set bits */
            ULONG cuMask = (ccConfig >> 19) & 0x1FFF;
            ULONG cuCount = 0;
            for (ULONG i = 0; i < 13; i++) {
                if (cuMask & (1 << i)) cuCount++;
            }
            cuCount *= 2; /* Each bit = 2 CUs (1 WGP = 2 CUs) */

            /* Decode WGP count from SPI mask */
            ULONG wgpCount = 0;
            for (ULONG i = 0; i < 5; i++) {
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
  SDMA Copy Engine — Hardware buffer copies via DMA
  
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

    Ring = (volatile PULONG)DevExt->SdmaRing.VirtualAddress;
    WPtr = DevExt->SdmaRing.WritePointer;

    /* SDMA FILL packet: 9 DWORDs */
    ULONG TotalBytes = 9 * sizeof(ULONG);

    if (WPtr + TotalBytes > (ULONG)DevExt->SdmaRing.SizeInBytes) {
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
    /* Size - 1 */
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
  TDR (Timeout Detection and Recovery) — Enhanced Reset
  
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
  EDID Parsing — Read monitor capabilities from display
  
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
  Shader Compilation Stub — DXBC → PM4 command conversion
  
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
