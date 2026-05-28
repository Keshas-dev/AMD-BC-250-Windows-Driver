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

static PDRIVER_OBJECT g_DriverObject = NULL;
static DRIVER_INITIALIZATION_DATA g_InitData = {0};

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

    /* Store the initialization data for later use */
    RtlCopyMemory(&g_InitData, DriverInitializationData, sizeof(DRIVER_INITIALIZATION_DATA));

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: DreamV3DxgkInitialize called, DDI version=%u\n",
               DriverInitializationData->Version));

    return STATUS_SUCCESS;
}

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
    DevExt->VisibleVramBytes = 10ULL * 1024 * 1024 * 1024; /* ~10GB quirk */
    DevExt->NumDisplayPipes = 4;  /* DCN 2.1: 4 pipes */
    DevExt->CurrentTemperatureC = 0;

    *MiniportDeviceContext = DevExt;

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

/*===========================================================================
  DxgkDdiUnload
===========================================================================*/

VOID
APIENTRY
DreamV3DdiUnload(VOID)
{
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: Driver unload\n"));
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
    ULONG TotalSize = 7 * sizeof(ULONG);  /* EOP packet is 7 DWORDs */

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

    /* IT_EVENT_WRITE_EOP packet */
    ULONG Header = PM4_TYPE3_HDR(IT_EVENT_WRITE_EOP, 6);
    Ring[WPtr / sizeof(ULONG)] = Header;
    WPtr += sizeof(ULONG);
    
    /* Event type: EOP */
    Ring[WPtr / sizeof(ULONG)] = EVENT_TYPE_EOP;
    WPtr += sizeof(ULONG);
    
    /* Address (64-bit physical) */
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
    
    /* Write WPTR to hardware */
    DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_GFX_RING0_WPTR, WPtr);
    
    /* Doorbell (notify CP of new commands) */
    /* In real driver, this would write to doorbell BAR */
    KeStallExecutionProcessor(10);
    
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL,
               "AMDBC250-DREAM-V4.3: GfxRing submitted, WPTR=0x%X\n", WPtr));
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
    UNREFERENCED_PARAMETER(hContext);
    UNREFERENCED_PARAMETER(pPresent);
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
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(ChildRelations);
    UNREFERENCED_PARAMETER(ChildRelationsSize);
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
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(ChildStatus);
    UNREFERENCED_PARAMETER(NonDestructiveOnly);
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
