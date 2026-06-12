/*++

Copyright (c) 2026 AMD BC-250 "Dream Drivers" Project — Version 3.0

Module Name:
    amdbc250_dream_kmd.h

Abstract:
    Kernel-Mode Display Miniport Driver interface for AMD BC-250 v3.0.
    Architecture: RDNA2 / Cyan Skillfish (GFX1013)

Environment:
    Kernel mode only

--*/

#pragma once

#ifndef _AMDBC250_DREAM_V3_KMD_H_
#define _AMDBC250_DREAM_V3_KMD_H_

#include <ntddk.h>
#include <wdm.h>
#include <dispmprt.h>
#include <d3dkmddi.h>
#include "amdbc250_dream_hw.h"

/* Resource type definitions (normally from miniport.h) */
#ifndef CmResourceTypeMemory
#define CmResourceTypeMemory 3
#endif

/*===========================================================================
  Version Information
============================================================================*/

#define AMDBC250_DREAM_V3_VERSION_MAJOR    4
#define AMDBC250_DREAM_V3_VERSION_MINOR    3
#define AMDBC250_DREAM_V3_VERSION_PATCH    0
#define AMDBC250_DREAM_V3_VERSION_STRING   L"4.3.0.0"
#define AMDBC250_DREAM_V3_DESCRIPTION      L"AMD BC-250 Dream Drivers v4.3 (RDNA2/Cyan Skillfish)"

/*===========================================================================
  Pool Tags
============================================================================*/

#define DREAM_V3_TAG_DEVICE                '3vDA'
#define DREAM_V3_TAG_RING                  '3rDA'
#define DREAM_V3_TAG_FENCE                 '3fDA'
#define DREAM_V3_TAG_ALLOCATION            '3aDA'
#define DREAM_V3_TAG_CONTEXT               '3cDA'
#define DREAM_V3_TAG_VM                    '3mDA'

/*===========================================================================
  Ring Buffer Descriptor
===========================================================================*/

typedef struct _DREAM_V3_RING_BUFFER {
    PHYSICAL_ADDRESS    PhysicalAddress;
    PVOID               VirtualAddress;
    SIZE_T              SizeInBytes;
    ULONG               ReadPointer;
    ULONG               WritePointer;
    ULONG               DoorbellOffset;
    BOOLEAN             Initialized;
    KSPIN_LOCK          Lock;
} DREAM_V3_RING_BUFFER, *PDREAM_V3_RING_BUFFER;

/*===========================================================================
  Interrupt Handler Ring
===========================================================================*/

typedef struct _DREAM_V3_IH_RING {
    PHYSICAL_ADDRESS    PhysicalAddress;
    PVOID               VirtualAddress;
    SIZE_T              SizeInBytes;
    ULONG               ReadPointer;
    BOOLEAN             Initialized;
} DREAM_V3_IH_RING, *PDREAM_V3_IH_RING;

/*===========================================================================
  Fence Descriptor
===========================================================================*/

typedef struct _DREAM_V3_FENCE {
    PHYSICAL_ADDRESS    PhysicalAddress;
    volatile PULONG64   VirtualAddress;  /* 64-bit fences for GFX10 */
    ULONG64             LastSignaledValue;
    ULONG64             LastSubmittedValue;
    KEVENT              FenceEvent;
} DREAM_V3_FENCE, *PDREAM_V3_FENCE;

/*===========================================================================
  GPU Context
===========================================================================*/

typedef struct _DREAM_V3_GPU_CONTEXT {
    ULONG               ContextId;
    ULONG               VmId;
    ULONG               EngineType;       /* 0=GFX, 1=Compute, 2=SDMA */
    DREAM_V3_RING_BUFFER GfxRing;
    DREAM_V3_FENCE      Fence;
    LIST_ENTRY          AllocationList;
    KSPIN_LOCK          ContextLock;
    BOOLEAN             IsValid;
} DREAM_V3_GPU_CONTEXT, *PDREAM_V3_GPU_CONTEXT;

/*===========================================================================
  Memory Allocation
===========================================================================*/

typedef struct _DREAM_V3_ALLOCATION {
    LIST_ENTRY          ListEntry;
    PHYSICAL_ADDRESS    PhysicalAddress;
    PVOID               VirtualAddress;
    SIZE_T              SizeInBytes;
    ULONG               Alignment;
    ULONG               SegmentId;
    BOOLEAN             IsMapped;
    BOOLEAN             IsPinned;
} DREAM_V3_ALLOCATION, *PDREAM_V3_ALLOCATION;

/*===========================================================================
  Display Mode
===========================================================================*/

typedef struct _DREAM_V3_DISPLAY_MODE {
    ULONG               Width;
    ULONG               Height;
    ULONG               RefreshRate;
    ULONG               BitsPerPixel;
    ULONG               PixelClockKhz;
    D3DDDIFORMAT        Format;
    BOOLEAN             IsInterlaced;
} DREAM_V3_DISPLAY_MODE, *PDREAM_V3_DISPLAY_MODE;

/*===========================================================================
  Memory Management Structures
===========================================================================*/

/* Page table descriptor */
typedef struct _DREAM_V3_PAGE_TABLE {
    PHYSICAL_ADDRESS    PhysicalAddress;
    PVOID               VirtualAddress;
    SIZE_T              SizeInBytes;
    ULONG               NumEntries;
} DREAM_V3_PAGE_TABLE, *PDREAM_V3_PAGE_TABLE;

/* VM allocation entry */
typedef struct _DREAM_V3_VM_ALLOCATION {
    LIST_ENTRY          ListEntry;
    ULONG64             VirtualAddress;     /* GPU virtual address */
    PHYSICAL_ADDRESS    PhysicalAddress;    /* Physical address */
    SIZE_T              SizeInBytes;
    ULONG               Flags;              /* AMDBC250_VM_* flags */
    BOOLEAN             IsEvicted;          /* True if evicted to system */
} DREAM_V3_VM_ALLOCATION, *PDREAM_V3_VM_ALLOCATION;

/* GPU VM context */
typedef struct _DREAM_V3_VM_CONTEXT {
    UCHAR               VmId;               /* VMID (0-15) */
    BOOLEAN             IsValid;
    ULONG               PageTableDepth;     /* 3 = PML4->PD->PT */
    PHYSICAL_ADDRESS    PageTableBase;      /* PML4 physical address */
    
    /* Page table hierarchy (dynamically allocated) */
    DREAM_V3_PAGE_TABLE Pml4Table;          /* Level 0: 512 entries */
    PDREAM_V3_PAGE_TABLE PageDirectories;   /* Level 1: dynamically allocated PDs */
    PDREAM_V3_PAGE_TABLE PageTables;        /* Level 2: dynamically allocated PTs */
    ULONG               NumPageDirectories; /* Number of allocated PDs */
    ULONG               NumPageTables;      /* Number of allocated PTs */
    
    /* Allocations in this VM */
    LIST_ENTRY          AllocationList;
    KSPIN_LOCK          VmLock;
    ULONG               NumMappings;
} DREAM_V3_VM_CONTEXT, *PDREAM_V3_VM_CONTEXT;

/* GART (Graphics Aperture Remapping Table) */
typedef struct _DREAM_V3_GART_TABLE {
    PHYSICAL_ADDRESS    ApertureBase;       /* GPU virtual address */
    SIZE_T              ApertureSize;
    ULONG               NumEntries;
    ULONG               EntrySize;
    ULONG               TotalSize;
    ULONG               NumAllocated;
    
    DREAM_V3_PAGE_TABLE PageTable;
    RTL_BITMAP          AllocationBitmap;
    ULONG               AllocationBits[(16384 + 31) / 32];
} DREAM_V3_GART_TABLE, *PDREAM_V3_GART_TABLE;

/* Memory management state */
typedef struct _DREAM_V3_MEMORY {
    /* GART */
    DREAM_V3_GART_TABLE GartTable;
    
    /* VM contexts */
    DREAM_V3_VM_CONTEXT VmContexts[16];
    RTL_BITMAP          VmidBitmap;
    ULONG               VmidBits[(16 + 31) / 32];
    
    /* System aperture */
    PHYSICAL_ADDRESS    ScratchPagePhysical;
    PVOID               ScratchPageVirtual;
    
    /* Statistics */
    SIZE_T              TotalVramAllocated;
    SIZE_T              TotalGartAllocated;
    SIZE_T              TotalSystemAllocated;
    ULONG               PageFaultCount;
    ULONG               EvictionCount;
    ULONG               RestorationCount;
} DREAM_V3_MEMORY, *PDREAM_V3_MEMORY;

/*===========================================================================
  Power Management Structures
===========================================================================*/

/* Power telemetry data */
typedef struct _DREAM_V3_POWER_TELEMETRY {
    ULONG   CurrentPowerMilliwatts;     /* Current power draw */
    ULONG   PeakPowerMilliwatts;        /* Peak power since boot */
    ULONG   AveragePowerMilliwatts;     /* Average power */
    ULONG   PowerChangeCount;           /* Power state transitions */
    
    /* Thermal sensors */
    LONG    EdgeTempC;                  /* Edge temperature */
    LONG    JunctionTempC;              /* Junction/hotspot temperature */
    LONG    VrmTempC;                   /* VRM temperature */
    LONG    HbmTempC;                   /* HBM temperature (N/A for BC-250) */
    
    /* Clock tracking */
    ULONG   CurrentGpuClockMhz;
    ULONG   CurrentMemoryClockMhz;
    ULONG   ClockChangeCount;
    
    /* Fan control */
    ULONG   FanSpeedPercent;            /* Current fan speed 0-100% */
    ULONG   FanSpeedChangeCount;
    
    /* Thermal throttle history */
    BOOLEAN ThermalThrottleActive;      /* Currently throttling */
    ULONG   ThrottleEntryCount;         /* Times thermal throttle activated */
    ULONG64 ThrottleDurationSeconds;    /* Total time in throttle */
    
    /* Power limit */
    ULONG   PowerLimitWatts;            /* Current TDP limit */
} DREAM_V3_POWER_TELEMETRY, *PDREAM_V3_POWER_TELEMETRY;

/* Power state management */
typedef struct _DREAM_V3_POWER_STATE {
    DEVICE_POWER_STATE  CurrentPowerState;      /* D0, D1, D2, D3 */
    BOOLEAN             SmuInitialized;         /* SMU communication ready */
    ULONG               SmuFirmwareVersion;     /* SMU FW version */
    BOOLEAN             DpmEnabled;             /* Dynamic Power Management */
    
    /* Saved clocks before D3 */
    ULONG               SavedGpuClockMhz;
    ULONG               SavedMemoryClockMhz;
    ULONG               LastGpuClockMhz;
    ULONG               LastMemoryClockMhz;
    
    /* Thermal throttle */
    BOOLEAN             ThermalThrottleActive;  /* Currently throttling */
    ULONG               ThrottleHysteresisCount; /* Prevent oscillation */
    ULONG               ThrottleExitCount;       /* Times exited throttle */
    ULONG               EmergencyShutdownCount;  /* Emergency stops */
    
    /* Fan control */
    BOOLEAN             FanControlEnabled;
    ULONG               CurrentFanSpeedPercent;
    ULONG               FanSpeedChangeCount;
    
    /* Power limit */
    ULONG               PowerLimitWatts;
    
    /* Telemetry */
    DREAM_V3_POWER_TELEMETRY Telemetry;
    
    /* Timestamps */
    LARGE_INTEGER       D3EntryTime;            /* When entered D3 */
    LARGE_INTEGER       LastThermalCheck;       /* Last thermal sample */
    
    /* Counters */
    ULONG               LastSubmitCount;        /* For workload detection */
    ULONG               ClockChangeCount;
} DREAM_V3_POWER_STATE, *PDREAM_V3_POWER_STATE;

/*===========================================================================
  Main Device Extension
===========================================================================*/

typedef struct _DREAM_V3_DEVICE_EXTENSION {

    /* PCI Device Information */
    ULONG               VendorId;
    ULONG               DeviceId;
    ULONG               RevisionId;
    ULONG               SubsystemVendorId;
    ULONG               SubsystemId;
    PDEVICE_OBJECT      PhysicalDeviceObject;

    /* MMIO Mapping */
    PHYSICAL_ADDRESS    MmioPhysicalBase;
    PVOID               MmioVirtualBase;
    SIZE_T              MmioSize;

    /* Doorbell BAR */
    PHYSICAL_ADDRESS    DoorbellPhysicalBase;
    PVOID               DoorbellVirtualBase;
    SIZE_T              DoorbellSize;

    /* Framebuffer (GDDR6) */
    PHYSICAL_ADDRESS    FbPhysicalBase;
    PVOID               FbVirtualBase;
    SIZE_T              FbSize;
    SIZE_T              FbVisibleSize;    /* CPU-visible portion (~10GB) */

    /* Hardware State */
    BOOLEAN             HardwareInitialized;
    BOOLEAN             DeviceStarted;
    BOOLEAN             GpuResetInProgress;
    ULONG               GpuClockMhz;
    ULONG               MemoryClockMhz;
    ULONG               PowerFlags;          /* Power state flags */
    DEVICE_POWER_STATE  DevicePowerState;    /* Current D-state */

    /* Power Management */
    DREAM_V3_POWER_STATE PowerState;         /* Power/thermal state */

    /* Ring Buffers */
    DREAM_V3_RING_BUFFER GfxRing;          /* Graphics command ring */
    DREAM_V3_RING_BUFFER ComputeRing;      /* Compute ring (DISABLED - HW quirk) */
    DREAM_V3_RING_BUFFER SdmaRing;         /* SDMA ring */
    DREAM_V3_IH_RING    IhRing;            /* Interrupt handler ring */

    /* Global Fence */
    DREAM_V3_FENCE      GlobalFence;

    /* Context Management */
    PDREAM_V3_GPU_CONTEXT Contexts[16];
    ULONG               NumContexts;

    /* Memory Management */
    DREAM_V3_MEMORY       Memory;            /* GPUVM and GART state */
    LIST_ENTRY          AllocationList;
    KSPIN_LOCK          AllocationListLock;
    SIZE_T              TotalVramBytes;
    SIZE_T              UsedVramBytes;
    SIZE_T              VisibleVramBytes; /* ~10GB quirk */

    /* Display */
    DREAM_V3_DISPLAY_MODE CurrentMode;
    ULONG               NumDisplayPipes;

    /* Interrupt */
    ULONG               LastInterruptStatus;

    /* Thermal */
    LONG                CurrentTemperatureC;  /* Celsius */
    ULONG               ThermalCheckCount;

    /* Synchronization */
    FAST_MUTEX          DeviceMutex;
    KSPIN_LOCK          FenceLock;
    KEVENT              DeviceRemoved;

    /* WDDM Callbacks */
    HANDLE              DxgkDeviceHandle;    /* Device handle from DXGKRNL */
    DXGKRNL_INTERFACE   DxgkInterface;       /* DXGKRNL callbacks */

    /* PSP / NBIO state */
    BOOLEAN             PspInitialized;      /* PSP init attempted */
    BOOLEAN             PspAlive;            /* SOS detected alive */
    BOOLEAN             NbioUnlocked;        /* NBIO firewall bypassed */

    /* Diagnostics */
    ULONG               InterruptCount;
    ULONG               SubmitCount;
    ULONG               ResetCount;
    ULONG               ErrorCount;
    ULONG               ThermalThrottleCount;

    /* GPU VA management */
    UINT64              NextGpuVa;

} DREAM_V3_DEVICE_EXTENSION, *PDREAM_V3_DEVICE_EXTENSION;

/*===========================================================================
  Function Prototypes - Hardware Initialization
===========================================================================*/

NTSTATUS
DreamV3HwInitialize(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    );

NTSTATUS
DreamV3HwReset(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    );

VOID
DreamV3HwShutdown(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    );

NTSTATUS
DreamV3HwInitGfxRing(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    );

NTSTATUS
DreamV3HwInitIhRing(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    );

NTSTATUS
DreamV3HwInitSdmaRing(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    );

NTSTATUS
DreamV3HwInitDisplay(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    );

NTSTATUS
DreamV3HwProgramGoldenRegs(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    );

VOID
DreamV3HdpFlush(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    );

LONG
DreamV3ReadTemperature(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    );

NTSTATUS
DreamV3PspHardwareInit(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    );

/*===========================================================================
  Function Prototypes - Power Management (amdbc250_dream_power.c)
===========================================================================*/

NTSTATUS
DreamV3SmuInitialize(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    );

NTSTATUS
DreamV3SmuShutdown(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    );

NTSTATUS
DreamV3UpdateClocks(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    );

NTSTATUS
DreamV3UpdateFanSpeed(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    );

NTSTATUS
DreamV3CheckThermalThrottle(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    );

NTSTATUS
DreamV3GetPowerUsage(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _Out_ PULONG PowerMilliwatts
    );

NTSTATUS
DreamV3SetPowerLimit(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ ULONG PowerLimitWatts
    );

NTSTATUS
DreamV3GetTelemetry(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _Out_ PDREAM_V3_POWER_TELEMETRY Telemetry
    );

/*===========================================================================
  Function Prototypes - Memory Management (amdbc250_dream_vm.c)
===========================================================================*/

/* GART functions */
NTSTATUS
DreamV3GartInitialize(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    );

NTSTATUS
DreamV3GartMapPage(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ ULONG GartIndex,
    _In_ PHYSICAL_ADDRESS PhysicalAddr,
    _In_ ULONG Flags
    );

NTSTATUS
DreamV3GartUnmapPage(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ ULONG GartIndex
    );

ULONG
DreamV3GartAllocateRange(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ ULONG NumPages
    );

/* GPUVM functions */
NTSTATUS
DreamV3VmInitialize(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    );

NTSTATUS
DreamV3VmShutdown(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    );

NTSTATUS
DreamV3VmCreateContext(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ ULONG RequestedVmId,
    _Out_ PDREAM_V3_VM_CONTEXT* OutContext
    );

NTSTATUS
DreamV3VmDestroyContext(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ ULONG VmId
    );

NTSTATUS
DreamV3VmMapRange(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ PDREAM_V3_VM_CONTEXT VmCtx,
    _In_ ULONG64 VirtualAddress,
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ SIZE_T SizeInBytes,
    _In_ ULONG Flags
    );

NTSTATUS
DreamV3VmUnmapRange(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ PDREAM_V3_VM_CONTEXT VmCtx,
    _In_ ULONG64 VirtualAddress,
    _In_ SIZE_T SizeInBytes
    );

NTSTATUS
DreamV3VmEvictMemory(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ PDREAM_V3_VM_CONTEXT VmCtx,
    _In_ ULONG64 VirtualAddress,
    _In_ SIZE_T SizeInBytes
    );

NTSTATUS
DreamV3VmRestoreMemory(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ PDREAM_V3_VM_CONTEXT VmCtx,
    _In_ ULONG64 VirtualAddress,
    _In_ SIZE_T SizeInBytes
    );

NTSTATUS
DreamV3VmConfigureSystemAperture(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    );

NTSTATUS
DreamV3VmInsertMapping(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ PDREAM_V3_VM_CONTEXT VmCtx,
    _In_ ULONG64 VirtualAddress,
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ ULONG Flags
    );

/*===========================================================================
  Register Access Helpers
===========================================================================*/

FORCEINLINE
ULONG
DreamV3ReadRegister(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ ULONG                      RegisterOffset
    )
{
    return READ_REGISTER_ULONG(
        (PULONG)((PUCHAR)DevExt->MmioVirtualBase + RegisterOffset)
        );
}

FORCEINLINE
VOID
DreamV3WriteRegister(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ ULONG                      RegisterOffset,
    _In_ ULONG                      Value
    )
{
    WRITE_REGISTER_ULONG(
        (PULONG)((PUCHAR)DevExt->MmioVirtualBase + RegisterOffset),
        Value
        );
}

/*===========================================================================
  Function Prototypes - WDDM DDI Callbacks
===========================================================================*/

NTSTATUS
APIENTRY
DreamV3DdiAddDevice(
    _In_  CONST PDEVICE_OBJECT  PhysicalDeviceObject,
    _Out_ PVOID                 *MiniportDeviceContext
    );

NTSTATUS
APIENTRY
DreamV3DdiStartDevice(
    _In_  PVOID                     MiniportDeviceContext,
    _In_  PDXGK_START_INFO          DxgkStartInfo,
    _In_  PDXGKRNL_INTERFACE        DxgkInterface,
    _Out_ PULONG                    NumberOfVideoPresentSources,
    _Out_ PULONG                    NumberOfChildren
    );

NTSTATUS
APIENTRY
DreamV3DdiStopDevice(
    _In_ PVOID MiniportDeviceContext
    );

NTSTATUS
APIENTRY
DreamV3DdiRemoveDevice(
    _In_ PVOID MiniportDeviceContext
    );

VOID
APIENTRY
DreamV3DdiResetDevice(
    _In_ PVOID MiniportDeviceContext
    );

VOID
APIENTRY
DreamV3DdiUnload(
    VOID
    );

BOOLEAN
APIENTRY
DreamV3DdiInterruptRoutine(
    _In_ PVOID  MiniportDeviceContext,
    _In_ ULONG  MessageNumber
    );

VOID
APIENTRY
DreamV3DdiDpcRoutine(
    _In_ PVOID MiniportDeviceContext
    );

NTSTATUS
APIENTRY
DreamV3DdiQueryAdapterInfo(
    _In_ CONST HANDLE                   hAdapter,
    _In_ CONST DXGKARG_QUERYADAPTERINFO *pQueryAdapterInfo
    );

NTSTATUS
APIENTRY
DreamV3DdiCreateDevice(
    _In_    CONST HANDLE             hAdapter,
    _Inout_ DXGKARG_CREATEDEVICE     *pCreateDevice
    );

NTSTATUS
APIENTRY
DreamV3DdiDestroyDevice(
    _In_ CONST HANDLE hDevice
    );

NTSTATUS
APIENTRY
DreamV3DdiCreateAllocation(
    _In_    CONST HANDLE                    hAdapter,
    _Inout_ DXGKARG_CREATEALLOCATION        *pCreateAllocation
    );

NTSTATUS
APIENTRY
DreamV3DdiDestroyAllocation(
    _In_ CONST HANDLE                   hAdapter,
    _In_ CONST DXGKARG_DESTROYALLOCATION *pDestroyAllocation
    );

NTSTATUS
APIENTRY
DreamV3DdiSubmitCommand(
    _In_ CONST HANDLE               hAdapter,
    _In_ CONST DXGKARG_SUBMITCOMMAND *pSubmitCommand
    );

NTSTATUS
APIENTRY
DreamV3DdiQueryCurrentFence(
    _In_    CONST HANDLE                hAdapter,
    _Inout_ DXGKARG_QUERYCURRENTFENCE   *pCurrentFence
    );

NTSTATUS
APIENTRY
DreamV3DdiPresent(
    _In_    CONST HANDLE        hContext,
    _Inout_ DXGKARG_PRESENT     *pPresent
    );

NTSTATUS
APIENTRY
DreamV3DdiRender(
    _In_    CONST HANDLE    hContext,
    _Inout_ DXGKARG_RENDER  *pRender
    );

NTSTATUS
APIENTRY
DreamV3DdiBuildPagingBuffer(
    _In_    CONST HANDLE                hAdapter,
    _Inout_ DXGKARG_BUILDPAGINGBUFFER   *pBuildPagingBuffer
    );

NTSTATUS
APIENTRY
DreamV3DdiPreemptCommand(
    _In_ CONST HANDLE               hAdapter,
    _In_ CONST DXGKARG_PREEMPTCOMMAND *pPreemptCommand
    );

/* VidPN and display management */
NTSTATUS
APIENTRY
DreamV3DdiRecommendFunctionalVidPn(
    _In_ CONST HANDLE                           hAdapter,
    _In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN *pRecommendFunctionalVidPn
    );

NTSTATUS
APIENTRY
DreamV3DdiEnumVidPnCofuncModality(
    _In_ CONST HANDLE                           hAdapter,
    _In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY  *pEnumCofuncModality
    );

NTSTATUS
APIENTRY
DreamV3DdiCommitVidPn(
    _In_ CONST HANDLE               hAdapter,
    _In_ CONST DXGKARG_COMMITVIDPN  *pCommitVidPn
    );

NTSTATUS
APIENTRY
DreamV3DdiSetVidPnSourceAddress(
    _In_ CONST HANDLE                       hAdapter,
    _In_ CONST DXGKARG_SETVIDPNSOURCEADDRESS *pSetVidPnSourceAddress
    );

NTSTATUS
APIENTRY
DreamV3DdiSetVidPnSourceVisibility(
    _In_ CONST HANDLE                           hAdapter,
    _In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY *pSetVidPnSourceVisibility
    );

NTSTATUS
APIENTRY
DreamV3DdiUpdateActiveVidPnPresentPath(
    _In_ CONST HANDLE                               hAdapter,
    _In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH *pUpdateActiveVidPnPresentPath
    );

NTSTATUS
APIENTRY
DreamV3DdiRecommendMonitorModes(
    _In_ CONST HANDLE                       hAdapter,
    _In_ CONST DXGKARG_RECOMMENDMONITORMODES *pRecommendMonitorModes
    );

NTSTATUS
APIENTRY
DreamV3DdiGetScanLine(
    _In_    CONST HANDLE            hAdapter,
    _Inout_ DXGKARG_GETSCANLINE     *pGetScanLine
    );

NTSTATUS
APIENTRY
DreamV3DdiControlInterrupt(
    _In_ CONST HANDLE               hAdapter,
    _In_ CONST DXGK_INTERRUPT_TYPE  InterruptType,
    _In_ BOOLEAN                    EnableInterrupt
    );

/* Child device */
NTSTATUS
APIENTRY
DreamV3DdiQueryChildRelations(
    _In_    PVOID                   MiniportDeviceContext,
    _Inout_ PDXGK_CHILD_DESCRIPTOR  ChildRelations,
    _In_    ULONG                   ChildRelationsSize
    );

NTSTATUS
APIENTRY
DreamV3DdiQueryChildStatus(
    _In_    PVOID               MiniportDeviceContext,
    _Inout_ PDXGK_CHILD_STATUS  ChildStatus,
    _In_    BOOLEAN             NonDestructiveOnly
    );

NTSTATUS
APIENTRY
DreamV3DdiQueryDeviceDescriptor(
    _In_    PVOID                       MiniportDeviceContext,
    _In_    ULONG                       ChildUid,
    _Inout_ PDXGK_DEVICE_DESCRIPTOR     DeviceDescriptor
    );

/* Power management */
NTSTATUS
APIENTRY
DreamV3DdiSetPowerState(
    _In_ PVOID              MiniportDeviceContext,
    _In_ ULONG              DeviceUid,
    _In_ DEVICE_POWER_STATE DevicePowerState,
    _In_ POWER_ACTION       ActionType
    );

NTSTATUS
APIENTRY
DreamV3DdiNotifyAcpiEvent(
    _In_  PVOID             MiniportDeviceContext,
    _In_  DXGK_EVENT_TYPE   EventType,
    _In_  ULONG             EventCode,
    _In_  PVOID             Argument,
    _Out_ PULONG            AcpiFlags
    );

NTSTATUS
APIENTRY
DreamV3DdiQueryInterface(
    _In_ PVOID              MiniportDeviceContext,
    _In_ PQUERY_INTERFACE   QueryInterface
    );

NTSTATUS
APIENTRY
DreamV3DdiEscape(
    _In_ HANDLE                     hAdapter,
    _In_ CONST DXGKARG_ESCAPE*      pEscape
    );

/* ===== Memory Allocation Definitions ===== */
#define GPU_PAGE_SIZE           0x1000              // 4KB
#define GPU_PAGE_SHIFT          12
#define GPU_PAGE_ALIGN(x)       (((x) + GPU_PAGE_SIZE - 1) & ~(GPU_PAGE_SIZE - 1))
#define GPU_PAGE_MASK           (GPU_PAGE_SIZE - 1)

#define GPU_MIN_ALLOC           GPU_PAGE_SIZE       // 4KB minimum
#define GPU_MAX_ALLOC           (64 * 1024 * 1024)  // 64MB maximum

/* Physical address limits (40-bit for BC-250) */
#define GPU_LOWEST_PHYSICAL     0x0000000000000000ULL
#define GPU_HIGHEST_PHYSICAL    0x0000003FFFFFFFFFULL  // 40-bit = 1TB

/* Allocation entry for tracking */
typedef struct {
    LIST_ENTRY ListEntry;
    PMDL Mdl;
    PVOID VirtualAddress;
    SIZE_T Size;
    PHYSICAL_ADDRESS PhysicalAddress;
    ULONG Flags;
#define ALLOC_FLAG_VALID        0x00000001
#define ALLOC_FLAG_LOCKED       0x00000002
} GPU_ALLOCATION_ENTRY, *PGPU_ALLOCATION_ENTRY;

/* Global allocation manager */
typedef struct {
    LIST_ENTRY AllocationList;
    KSPIN_LOCK SpinLock;
    ULONG AllocationCount;
    ULONG64 TotalAllocatedBytes;
} GPU_ALLOCATION_MANAGER, *PGPU_ALLOCATION_MANAGER;

/* ===== DCN 2.1 Display Engine Registers ===== */

/* HUBP (HUB Pipe) base addresses */
#define HUBPREQ0_BASE                   0x1C00
#define HUBPREQ_SURFACE_ADDRESS         (HUBPREQ0_BASE + 0x04)      // [31:0]
#define HUBPREQ_SURFACE_ADDRESS_HIGH    (HUBPREQ0_BASE + 0x08)      // [39:32]
#define HUBPREQ_SURFACE_PITCH           (HUBPREQ0_BASE + 0x0C)
#define HUBPREQ_SURFACE_HEIGHT          (HUBPREQ0_BASE + 0x10)
#define HUBPREQ_SURFACE_FORMAT          (HUBPREQ0_BASE + 0x14)
#define HUBPREQ_ENABLE                  (HUBPREQ0_BASE + 0x18)
#define HUBPREQ_FLIP_CONTROL            (HUBPREQ0_BASE + 0x1C)

/* Pixel format constants */
#define HUBPREQ_FORMAT_ARGB8888         0x00000004
#define HUBPREQ_FORMAT_XRGB8888         0x00000001
#define HUBPREQ_FORMAT_RGB565           0x00000000

/* Display Flip Request Structure */
typedef struct {
    ULONG64 SurfaceGpuVa;       // GPU virtual address
    ULONG Width;                // Width in pixels
    ULONG Height;               // Height in pixels
    ULONG Pitch;                // Pitch in bytes (optional, auto-calc if 0)
    ULONG PixelFormat;          // HUBPREQ_FORMAT_*
    ULONG VidPnSourceId;        // Display output ID
} DISPLAY_FLIP_REQUEST, *PDISPLAY_FLIP_REQUEST;

/* ===== BC-250 (Cyan Skillfish) Corrected Register Offsets =====
 *
 * BC-250 has a non-standard BAR5 layout vs Navi10.
 * GC registers start at GC_BASE__INST0_SEG0 = 0x1260, not BAR5+0x0000.
 * All GC-relative offsets below must be shifted by GC_BASE.
 *
 * See: cyan_skillfish_ip_offset.h (GC_BASE__INST0_SEG0 = 0x00001260)
 */
#define AMDBC250_GC_BASE                        0x1260

/* MP1_BASE (SMU) — from cyan_skillfish_ip_offset.h: MP1_BASE__INST0_SEG0 = 0x00016000 */
#define AMDBC250_MP1_BASE                       0x16000

/* THM_BASE (Thermal) — from cyan_skillfish_ip_offset.h: THM_BASE__INST0_SEG0 = 0x00016600 */
#define AMDBC250_THM_BASE                       0x16600

/* CC_GC_SHADER_ARRAY_CONFIG — CU enumeration (Navi10: 0x2004) */
#define AMDBC250_REG_CC_GC_SHADER_ARRAY_CONFIG  (AMDBC250_GC_BASE + 0x2004)

/* SPI_PG_ENABLE_STATIC_WGP_MASK — WGP dispatch gate (Navi10: 0x229C) */
#define AMDBC250_REG_SPI_PG_ENABLE_STATIC_WGP_MASK (AMDBC250_GC_BASE + 0x229C)

/* GRBM_STATUS — Graphics Backend status (Navi10: 0x2000) */
#define AMDBC250_REG_GRBM_STATUS                (AMDBC250_GC_BASE + 0x2000)

/* RLC_PG_ALWAYS_ON_WGP_MASK — RLC power gating (Navi10: 0x2B04) */
#define AMDBC250_REG_RLC_PG_ALWAYS_ON_WGP_MASK  (AMDBC250_GC_BASE + 0x2B04)

#endif /* _AMDBC250_DREAM_V3_KMD_H_ */
