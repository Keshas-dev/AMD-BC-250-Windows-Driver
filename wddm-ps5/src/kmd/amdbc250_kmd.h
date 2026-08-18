/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    amdbc250_kmd.h

Abstract:
    Kernel-Mode Display Miniport Driver (KMD) interface definitions
    for the AMD BC-250 APU (Cyan Skillfish / RDNA2).

    Defines the main device extension structure, callback prototypes,
    and internal data structures used by the KMD component.

Environment:
    Kernel mode only (IRQL <= DISPATCH_LEVEL for most operations)

--*/

#pragma once

#ifndef _AMDBC250_KMD_H_
#define _AMDBC250_KMD_H_

#include <ntddk.h>
#include <wdm.h>
#include <dispmprt.h>       /* WDDM Display Miniport interfaces */
#include <d3dkmddi.h>       /* D3D Kernel-Mode Driver Interface */
#include "amdbc250_hw.h"

/*===========================================================================
  Driver Version Information
===========================================================================*/

#define AMDBC250_DRIVER_MAJOR_VERSION   1
#define AMDBC250_DRIVER_MINOR_VERSION   0
#define AMDBC250_DRIVER_BUILD_NUMBER    101
#define AMDBC250_DRIVER_VERSION_STRING  L"1.0.101.0"

/*===========================================================================
  Pool Tags (for memory allocation tracking)
===========================================================================*/

#define AMDBC250_TAG_DEVICE_EXT         'xEDA'  /* Device extension         */
#define AMDBC250_TAG_RING_BUFFER        'gRDA'  /* Ring buffer              */
#define AMDBC250_TAG_FENCE              'cFDA'  /* Fence memory             */
#define AMDBC250_TAG_ALLOCATION         'lADA'  /* GPU allocation           */
#define AMDBC250_TAG_COMMAND_BUFFER     'bCDA'  /* Command buffer           */
#define AMDBC250_TAG_IH_RING            'hIDA'  /* Interrupt handler ring   */
#define AMDBC250_TAG_VIDMM              'mVDA'  /* Video memory manager     */
#define AMDBC250_TAG_CONTEXT            'xCDA'  /* GPU context              */

/*===========================================================================
  Ring Buffer Descriptor
===========================================================================*/

typedef struct _AMDBC250_RING_BUFFER {
    PHYSICAL_ADDRESS    PhysicalAddress;    /* Physical address of ring     */
    PVOID               VirtualAddress;    /* Kernel virtual address        */
    SIZE_T              SizeInBytes;        /* Total ring size               */
    ULONG               ReadPointer;        /* Current read pointer (RPTR)   */
    ULONG               WritePointer;       /* Current write pointer (WPTR)  */
    ULONG               DoorBellOffset;     /* Doorbell register offset      */
    BOOLEAN             Initialized;        /* Ring initialization flag      */
    KSPIN_LOCK          Lock;               /* Ring access spinlock          */
} AMDBC250_RING_BUFFER, *PAMDBC250_RING_BUFFER;

/*===========================================================================
  Interrupt Handler (IH) Ring Descriptor
===========================================================================*/

typedef struct _AMDBC250_IH_RING {
    PHYSICAL_ADDRESS    PhysicalAddress;    /* Physical address              */
    PVOID               VirtualAddress;    /* Kernel virtual address        */
    SIZE_T              SizeInBytes;        /* Ring size                     */
    ULONG               ReadPointer;        /* Current read pointer          */
    ULONG               WritePointer;       /* Current write pointer         */
    BOOLEAN             Initialized;        /* Initialization flag           */
} AMDBC250_IH_RING, *PAMDBC250_IH_RING;

/*===========================================================================
  Fence Descriptor
===========================================================================*/

typedef struct _AMDBC250_FENCE {
    PHYSICAL_ADDRESS    PhysicalAddress;    /* Physical address of fence mem */
    volatile PULONG     VirtualAddress;    /* Kernel virtual address        */
    ULONG               LastSignaledValue;  /* Last signaled fence value     */
    ULONG               LastSubmittedValue; /* Last submitted fence value    */
    KEVENT              FenceEvent;         /* Kernel event for fence wait   */
} AMDBC250_FENCE, *PAMDBC250_FENCE;

/*===========================================================================
  GPU Context Descriptor
===========================================================================*/

typedef struct _AMDBC250_GPU_CONTEXT {
    ULONG               ContextId;          /* Unique context identifier     */
    ULONG               VmId;               /* VM/VMID for address space     */
    ULONG               EngineType;         /* GFX=0, COMPUTE=1, SDMA=2     */
    AMDBC250_RING_BUFFER GfxRing;           /* GFX command ring              */
    AMDBC250_RING_BUFFER ComputeRing;       /* Compute command ring          */
    AMDBC250_FENCE      Fence;              /* Context fence                 */
    LIST_ENTRY          AllocationList;     /* List of GPU allocations       */
    KSPIN_LOCK          ContextLock;        /* Context access spinlock       */
    BOOLEAN             IsValid;            /* Context validity flag         */
} AMDBC250_GPU_CONTEXT, *PAMDBC250_GPU_CONTEXT;

/*===========================================================================
  GPU Memory Allocation Descriptor
===========================================================================*/

typedef enum _AMDBC250_ALLOC_TYPE {
    AllocTypeFrameBuffer    = 0,    /* VRAM allocation                       */
    AllocTypeSystemMemory   = 1,    /* System memory (AGP/aperture)          */
    AllocTypeCommandBuffer  = 2,    /* Command buffer                        */
    AllocTypeFence          = 3,    /* Fence memory                          */
    AllocTypePageTable      = 4,    /* Page table memory                     */
} AMDBC250_ALLOC_TYPE;

typedef struct _AMDBC250_ALLOCATION {
    LIST_ENTRY          ListEntry;          /* Linked list entry             */
    AMDBC250_ALLOC_TYPE AllocationType;     /* Type of allocation            */
    PHYSICAL_ADDRESS    PhysicalAddress;    /* Physical address              */
    PVOID               VirtualAddress;    /* Kernel virtual address        */
    SIZE_T              SizeInBytes;        /* Allocation size               */
    ULONG               Alignment;          /* Required alignment            */
    ULONG               GpuVirtualAddress;  /* GPU virtual address (32-bit)  */
    BOOLEAN             IsMapped;           /* Is mapped to GPU VA space     */
    BOOLEAN             IsPinned;           /* Is pinned in memory           */
} AMDBC250_ALLOCATION, *PAMDBC250_ALLOCATION;

/*===========================================================================
  Display Mode Information
===========================================================================*/

typedef struct _AMDBC250_DISPLAY_MODE {
    ULONG               Width;              /* Horizontal resolution         */
    ULONG               Height;             /* Vertical resolution           */
    ULONG               RefreshRate;        /* Refresh rate (Hz)             */
    ULONG               BitsPerPixel;       /* Color depth                   */
    ULONG               PixelClock;         /* Pixel clock (kHz)             */
    D3DDDIFORMAT        Format;             /* D3D surface format            */
    BOOLEAN             IsInterlaced;       /* Interlaced mode flag          */
} AMDBC250_DISPLAY_MODE, *PAMDBC250_DISPLAY_MODE;

/*===========================================================================
  Main Device Extension Structure
  (One per physical GPU device, allocated by WDDM framework)
===========================================================================*/

typedef struct _AMDBC250_DEVICE_EXTENSION {

    /* --- PCI Device Information --- */
    ULONG               VendorId;           /* PCI Vendor ID (0x1002)        */
    ULONG               DeviceId;           /* PCI Device ID                 */
    ULONG               SubsystemVendorId;  /* Subsystem vendor ID           */
    ULONG               SubsystemId;        /* Subsystem ID                  */
    ULONG               RevisionId;         /* PCI revision ID               */
    ULONG               BusNumber;          /* PCI bus number                */
    ULONG               DeviceNumber;       /* PCI device number             */
    ULONG               FunctionNumber;     /* PCI function number           */

    /* --- MMIO Mapping --- */
    PHYSICAL_ADDRESS    MmioPhysicalBase;   /* MMIO BAR physical base        */
    PVOID               MmioVirtualBase;    /* MMIO BAR virtual base (mapped)*/
    SIZE_T              MmioSize;           /* MMIO BAR size                 */

    /* --- Doorbell Mapping --- */
    PHYSICAL_ADDRESS    DoorbellPhysicalBase;
    PVOID               DoorbellVirtualBase;
    SIZE_T              DoorbellSize;

    /* --- Framebuffer Aperture --- */
    PHYSICAL_ADDRESS    FbPhysicalBase;     /* Framebuffer physical base     */
    PVOID               FbVirtualBase;      /* Framebuffer virtual base      */
    SIZE_T              FbSize;             /* Framebuffer aperture size     */
    SIZE_T              FbAllocatedSize;    /* Currently allocated VRAM      */

    /* --- Active Display / Present DisplayOnly --- */
    ULONG               FbWidth;            /* Active framebuffer width      */
    ULONG               FbHeight;           /* Active framebuffer height     */
    ULONG               FbPitch;            /* Active framebuffer stride     */
    D3DDDIFORMAT        FbColorFormat;      /* Active framebuffer format     */
    BOOLEAN             FbMapped;           /* Framebuffer VA is mapped      */
    BOOLEAN             FbIsActive;         /* Framebuffer in use            */
    BOOLEAN             SourceIsVisible;    /* VidPn source visibility       */

    /* --- Hardware State --- */
    BOOLEAN             HardwareInitialized;
    BOOLEAN             GpuResetInProgress;
    ULONG               GpuClockMhz;        /* Current GPU clock             */
    ULONG               MemoryClockMhz;     /* Current memory clock          */
    ULONG               PowerState;         /* Current power state           */

    /* --- Ring Buffers --- */
    AMDBC250_RING_BUFFER GfxRing;           /* GFX command ring (ring 0)     */
    AMDBC250_RING_BUFFER SdmaRing;          /* SDMA ring                     */

    /* --- Interrupt Handler Ring --- */
    AMDBC250_IH_RING    IhRing;             /* Interrupt handler ring        */

    /* --- Global Fence --- */
    AMDBC250_FENCE      GlobalFence;        /* Global GPU fence              */

    /* --- Context Management --- */
    PAMDBC250_GPU_CONTEXT Contexts[64];     /* Active GPU contexts           */
    ULONG               NumContexts;        /* Number of active contexts     */
    KSPIN_LOCK          ContextListLock;    /* Context list spinlock         */

    /* --- Memory Management --- */
    LIST_ENTRY          AllocationList;     /* Global allocation list        */
    KSPIN_LOCK          AllocationListLock; /* Allocation list spinlock      */
    SIZE_T              TotalVramBytes;     /* Total VRAM available          */
    SIZE_T              UsedVramBytes;      /* Currently used VRAM           */

    /* --- Display --- */
    AMDBC250_DISPLAY_MODE CurrentMode;      /* Current display mode          */
    ULONG               NumSupportedModes;  /* Number of supported modes     */
    PAMDBC250_DISPLAY_MODE SupportedModes;  /* Array of supported modes      */

    /* --- Interrupt --- */
    PKINTERRUPT         InterruptObject;    /* Kernel interrupt object       */
    KDPC                InterruptDpc;       /* Interrupt DPC                 */
    ULONG               LastInterruptStatus; /* Last interrupt status bits   */

    /* --- Synchronization --- */
    FAST_MUTEX          DeviceMutex;        /* Device-level mutex            */
    KEVENT              ResetCompleteEvent; /* GPU reset completion event    */

    /* --- WDDM Callbacks --- */
    DXGKRNL_INTERFACE   DxgkInterface;      /* DXGK interface callbacks      */
    HANDLE              DeviceHandle;       /* DXGK device handle            */

    /* --- Debug / Diagnostics --- */
    ULONG               InterruptCount;     /* Total interrupt count         */
    ULONG               ResetCount;         /* GPU reset count               */
    ULONG               ErrorCount;         /* Error count                   */
    ULONG               DebugStartStage;    /* StartDevice stage marker      */
    NTSTATUS            DebugStartStatus;   /* Last StartDevice status       */
    ULONG               DebugHwInitStage;   /* HwInitialize stage marker     */
    NTSTATUS            DebugHwInitStatus;  /* Last HwInitialize status      */
    ULONG               DebugFirstFailStage; /* First failing stage marker   */
    NTSTATUS            DebugFirstFailStatus; /* First failing NTSTATUS      */
    ULONG               DebugFirstFailDdiId; /* First failing DDI id        */
    ULONG               DebugFirstFailQueryType; /* First failing query type */
    ULONGLONG           DebugFirstFailOutSize; /* First failing output size */
    ULONG               DebugQueryCount;    /* QueryAdapterInfo call count   */
    ULONG               DebugQueryLastType; /* Last QueryAdapterInfo type    */
    ULONGLONG           DebugQueryLastOutSize; /* Last output size (bytes)   */
    NTSTATUS            DebugQueryLastStatus; /* Last QueryAdapterInfo status*/
    ULONGLONG           DebugQueryLastVramBytes; /* Last reported VRAM bytes */

} AMDBC250_DEVICE_EXTENSION, *PAMDBC250_DEVICE_EXTENSION;

/*===========================================================================
  Function Prototypes - Diagnostics
===========================================================================*/

VOID
Bc250DiagWriteDword(
    _In_z_ PCWSTR ValueName,
    _In_ ULONG Value
    );

ULONG
Bc250DiagReadDword(
    _In_z_ PCWSTR ValueName,
    _In_ ULONG DefaultValue
    );

/*===========================================================================
  Function Prototypes - Hardware Initialization
===========================================================================*/

NTSTATUS
Bc250HwInitialize(
    _In_ PAMDBC250_DEVICE_EXTENSION DevExt
    );

NTSTATUS
Bc250HwReset(
    _In_ PAMDBC250_DEVICE_EXTENSION DevExt
    );

VOID
Bc250HwShutdown(
    _In_ PAMDBC250_DEVICE_EXTENSION DevExt
    );

NTSTATUS
Bc250HwInitGfxRing(
    _In_ PAMDBC250_DEVICE_EXTENSION DevExt
    );

NTSTATUS
Bc250HwInitIhRing(
    _In_ PAMDBC250_DEVICE_EXTENSION DevExt
    );

NTSTATUS
Bc250HwInitSdmaRing(
    _In_ PAMDBC250_DEVICE_EXTENSION DevExt
    );

NTSTATUS
Bc250HwInitDisplay(
    _In_ PAMDBC250_DEVICE_EXTENSION DevExt
    );

/*===========================================================================
  Function Prototypes - Register Access
===========================================================================*/

FORCEINLINE
ULONG
Bc250ReadMmio(
    _In_ PAMDBC250_DEVICE_EXTENSION DevExt,
    _In_ ULONG                      RegisterOffset
    )
{
    return READ_REGISTER_ULONG(
        (PULONG)((PUCHAR)DevExt->MmioVirtualBase + RegisterOffset)
        );
}

FORCEINLINE
VOID
Bc250WriteMmio(
    _In_ PAMDBC250_DEVICE_EXTENSION DevExt,
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
  Function Prototypes - WDDM Callbacks (KMD DDI)
===========================================================================*/

/* DxgkDdiAddDevice */
NTSTATUS
APIENTRY
Bc250DdiAddDevice(
    _In_  CONST PDEVICE_OBJECT  PhysicalDeviceObject,
    _Out_ PVOID                 *MiniportDeviceContext
    );

/* DxgkDdiStartDevice */
NTSTATUS
APIENTRY
Bc250DdiStartDevice(
    _In_  PVOID                     MiniportDeviceContext,
    _In_  PDXGK_START_INFO          DxgkStartInfo,
    _In_  PDXGKRNL_INTERFACE        DxgkInterface,
    _Out_ PULONG                    NumberOfVideoPresentSources,
    _Out_ PULONG                    NumberOfChildren
    );

/* DxgkDdiStopDevice */
NTSTATUS
APIENTRY
Bc250DdiStopDevice(
    _In_ PVOID MiniportDeviceContext
    );

/* DxgkDdiRemoveDevice */
NTSTATUS
APIENTRY
Bc250DdiRemoveDevice(
    _In_ PVOID MiniportDeviceContext
    );

/* DxgkDdiDispatchIoRequest */
NTSTATUS
APIENTRY
Bc250DdiDispatchIoRequest(
    _In_ CONST PVOID MiniportDeviceContext,
    _In_ ULONG VidPnSourceId,
    _In_ PVIDEO_REQUEST_PACKET VideoRequestPacket
    );

/* DxgkDdiQueryChildRelations */
NTSTATUS
APIENTRY
Bc250DdiQueryChildRelations(
    _In_    PVOID                   MiniportDeviceContext,
    _Inout_ PDXGK_CHILD_DESCRIPTOR  ChildRelations,
    _In_    ULONG                   ChildRelationsSize
    );

/* DxgkDdiQueryChildStatus */
NTSTATUS
APIENTRY
Bc250DdiQueryChildStatus(
    _In_    PVOID               MiniportDeviceContext,
    _Inout_ PDXGK_CHILD_STATUS  ChildStatus,
    _In_    BOOLEAN             NonDestructiveOnly
    );

/* DxgkDdiQueryDeviceDescriptor */
NTSTATUS
APIENTRY
Bc250DdiQueryDeviceDescriptor(
    _In_    PVOID                       MiniportDeviceContext,
    _In_    ULONG                       ChildUid,
    _Inout_ PDXGK_DEVICE_DESCRIPTOR     DeviceDescriptor
    );

/* DxgkDdiSetPowerState */
NTSTATUS
APIENTRY
Bc250DdiSetPowerState(
    _In_ PVOID              MiniportDeviceContext,
    _In_ ULONG              DeviceUid,
    _In_ DEVICE_POWER_STATE DevicePowerState,
    _In_ POWER_ACTION       ActionType
    );

/* DxgkDdiNotifyAcpiEvent */
NTSTATUS
APIENTRY
Bc250DdiNotifyAcpiEvent(
    _In_  PVOID             MiniportDeviceContext,
    _In_  DXGK_EVENT_TYPE   EventType,
    _In_  ULONG             EventCode,
    _In_  PVOID             Argument,
    _Out_ PULONG            AcpiFlags
    );

/* DxgkDdiResetDevice */
VOID
APIENTRY
Bc250DdiResetDevice(
    _In_ PVOID MiniportDeviceContext
    );

/* DxgkDdiUnload */
VOID
APIENTRY
Bc250DdiUnload(
    VOID
    );

/* DxgkDdiQueryInterface */
NTSTATUS
APIENTRY
Bc250DdiQueryInterface(
    _In_ PVOID              MiniportDeviceContext,
    _In_ PQUERY_INTERFACE   QueryInterface
    );

/* DxgkDdiInterruptRoutine */
BOOLEAN
APIENTRY
Bc250DdiInterruptRoutine(
    _In_ PVOID  MiniportDeviceContext,
    _In_ ULONG  MessageNumber
    );

/* DxgkDdiDpcRoutine */
VOID
APIENTRY
Bc250DdiDpcRoutine(
    _In_ PVOID MiniportDeviceContext
    );

/* DxgkDdiQueryAdapterInfo */
NTSTATUS
APIENTRY
Bc250DdiQueryAdapterInfo(
    _In_ CONST HANDLE               hAdapter,
    _In_ CONST DXGKARG_QUERYADAPTERINFO *pQueryAdapterInfo
    );

/* DxgkDdiSetPalette */
NTSTATUS
APIENTRY
Bc250DdiSetPalette(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_SETPALETTE *pSetPalette
    );

/* DxgkDdiSetPointerPosition */
NTSTATUS
APIENTRY
Bc250DdiSetPointerPosition(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_SETPOINTERPOSITION *pSetPointerPosition
    );

/* DxgkDdiSetPointerShape */
NTSTATUS
APIENTRY
Bc250DdiSetPointerShape(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_SETPOINTERSHAPE *pSetPointerShape
    );

/* DxgkDdiIsSupportedVidPn */
NTSTATUS
APIENTRY
Bc250DdiIsSupportedVidPn(
    _In_ CONST HANDLE hAdapter,
    _Inout_ DXGKARG_ISSUPPORTEDVIDPN *pIsSupportedVidPn
    );

/* DxgkDdiCreateDevice */
NTSTATUS
APIENTRY
Bc250DdiCreateDevice(
    _In_    CONST HANDLE        hAdapter,
    _Inout_ DXGKARG_CREATEDEVICE *pCreateDevice
    );

/* DxgkDdiDestroyDevice */
NTSTATUS
APIENTRY
Bc250DdiDestroyDevice(
    _In_ CONST HANDLE hDevice
    );

/* DxgkDdiCreateContext */
NTSTATUS
APIENTRY
Bc250DdiCreateContext(
    _In_    CONST HANDLE           hDevice,
    _Inout_ DXGKARG_CREATECONTEXT *pCreateContext
    );

/* DxgkDdiDestroyContext */
NTSTATUS
APIENTRY
Bc250DdiDestroyContext(
    _In_ CONST HANDLE hContext
    );

/* DxgkDdiCreateAllocation */
NTSTATUS
APIENTRY
Bc250DdiCreateAllocation(
    _In_    CONST HANDLE                    hAdapter,
    _Inout_ DXGKARG_CREATEALLOCATION        *pCreateAllocation
    );

/* DxgkDdiDestroyAllocation */
NTSTATUS
APIENTRY
Bc250DdiDestroyAllocation(
    _In_ CONST HANDLE                   hAdapter,
    _In_ CONST DXGKARG_DESTROYALLOCATION *pDestroyAllocation
    );

/* DxgkDdiOpenAllocation */
NTSTATUS
APIENTRY
Bc250DdiOpenAllocation(
    _In_ CONST HANDLE                 hDevice,
    _In_ CONST DXGKARG_OPENALLOCATION *pOpenAllocation
    );

/* DxgkDdiCloseAllocation */
NTSTATUS
APIENTRY
Bc250DdiCloseAllocation(
    _In_ CONST HANDLE                  hDevice,
    _In_ CONST DXGKARG_CLOSEALLOCATION *pCloseAllocation
    );

/* DxgkDdiBuildPagingBuffer */
NTSTATUS
APIENTRY
Bc250DdiBuildPagingBuffer(
    _In_    CONST HANDLE                hAdapter,
    _Inout_ DXGKARG_BUILDPAGINGBUFFER   *pBuildPagingBuffer
    );

/* DxgkDdiPatch */
NTSTATUS
APIENTRY
Bc250DdiPatch(
    _In_ CONST HANDLE         hAdapter,
    _In_ CONST DXGKARG_PATCH *pPatch
    );

/* DxgkDdiSubmitCommand */
NTSTATUS
APIENTRY
Bc250DdiSubmitCommand(
    _In_ CONST HANDLE               hAdapter,
    _In_ CONST DXGKARG_SUBMITCOMMAND *pSubmitCommand
    );

/* DxgkDdiPreemptCommand */
NTSTATUS
APIENTRY
Bc250DdiPreemptCommand(
    _In_ CONST HANDLE               hAdapter,
    _In_ CONST DXGKARG_PREEMPTCOMMAND *pPreemptCommand
    );

/* DxgkDdiQueryCurrentFence */
NTSTATUS
APIENTRY
Bc250DdiQueryCurrentFence(
    _In_    CONST HANDLE                hAdapter,
    _Inout_ DXGKARG_QUERYCURRENTFENCE   *pCurrentFence
    );

/* DxgkDdiSetVidPnSourceAddress */
NTSTATUS
APIENTRY
Bc250DdiSetVidPnSourceAddress(
    _In_ CONST HANDLE                       hAdapter,
    _In_ CONST DXGKARG_SETVIDPNSOURCEADDRESS *pSetVidPnSourceAddress
    );

/* DxgkDdiRecommendFunctionalVidPn */
NTSTATUS
APIENTRY
Bc250DdiRecommendFunctionalVidPn(
    _In_ CONST HANDLE                           hAdapter,
    _In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN *pRecommendFunctionalVidPn
    );

/* DxgkDdiEnumVidPnCofuncModality */
NTSTATUS
APIENTRY
Bc250DdiEnumVidPnCofuncModality(
    _In_ CONST HANDLE                           hAdapter,
    _In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY  *pEnumCofuncModality
    );

/* DxgkDdiSetVidPnSourceVisibility */
NTSTATUS
APIENTRY
Bc250DdiSetVidPnSourceVisibility(
    _In_ CONST HANDLE                           hAdapter,
    _In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY *pSetVidPnSourceVisibility
    );

/* DxgkDdiCommitVidPn */
NTSTATUS
APIENTRY
Bc250DdiCommitVidPn(
    _In_ CONST HANDLE               hAdapter,
    _In_ CONST DXGKARG_COMMITVIDPN  *pCommitVidPn
    );

/* DxgkDdiUpdateActiveVidPnPresentPath */
NTSTATUS
APIENTRY
Bc250DdiUpdateActiveVidPnPresentPath(
    _In_ CONST HANDLE                               hAdapter,
    _In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH *pUpdateActiveVidPnPresentPath
    );

/* DxgkDdiRecommendMonitorModes */
NTSTATUS
APIENTRY
Bc250DdiRecommendMonitorModes(
    _In_ CONST HANDLE                       hAdapter,
    _In_ CONST DXGKARG_RECOMMENDMONITORMODES *pRecommendMonitorModes
    );

/* DxgkDdiGetScanLine */
NTSTATUS
APIENTRY
Bc250DdiGetScanLine(
    _In_    CONST HANDLE            hAdapter,
    _Inout_ DXGKARG_GETSCANLINE     *pGetScanLine
    );

/* DxgkDdiQueryVidPnHWCapability */
NTSTATUS
APIENTRY
Bc250DdiQueryVidPnHwCapability(
    _In_ CONST HANDLE hAdapter,
    _Inout_ DXGKARG_QUERYVIDPNHWCAPABILITY *pVidPnHWCaps
    );

/* DxgkDdiControlInterrupt */
NTSTATUS
APIENTRY
Bc250DdiControlInterrupt(
    _In_ CONST HANDLE               hAdapter,
    _In_ CONST DXGK_INTERRUPT_TYPE  InterruptType,
    _In_ BOOLEAN                    EnableInterrupt
    );

/* DxgkDdiCreateOverlay */
NTSTATUS
APIENTRY
Bc250DdiCreateOverlay(
    _In_    CONST HANDLE                hAdapter,
    _Inout_ DXGKARG_CREATEOVERLAY       *pCreateOverlay
    );

/* DxgkDdiDestroyOverlay */
NTSTATUS
APIENTRY
Bc250DdiDestroyOverlay(
    _In_ CONST HANDLE hOverlay
    );

/* DxgkDdiPresent */
NTSTATUS
APIENTRY
Bc250DdiPresent(
    _In_    CONST HANDLE        hContext,
    _Inout_ DXGKARG_PRESENT     *pPresent
    );

/* DxgkDdiRender */
NTSTATUS
APIENTRY
Bc250DdiRender(
    _In_    CONST HANDLE        hContext,
    _Inout_ DXGKARG_RENDER      *pRender
    );

/* DxgkDdiPresentDisplayOnly (display-only adapter present path) */
NTSTATUS
APIENTRY
Bc250DdiPresentDisplayOnly(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_PRESENT_DISPLAYONLY *pPresentDisplayOnly
    );

/* DxgkDdiStopDeviceAndReleasePostDisplayOwnership (WDDM 1.2+) */
NTSTATUS
APIENTRY
Bc250DdiStopDeviceAndReleasePostDisplayOwnership(
    _In_ PVOID MiniportDeviceContext,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _Out_ PDXGK_DISPLAY_INFORMATION DisplayInfo
    );

/* DxgkDdiSystemDisplayEnable (WDDM 1.2+) */
NTSTATUS
APIENTRY
Bc250DdiSystemDisplayEnable(
    _In_ PVOID MiniportDeviceContext,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _In_ PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
    _Out_ UINT *Width,
    _Out_ UINT *Height,
    _Out_ D3DDDIFORMAT *ColorFormat
    );

/* DxgkDdiSystemDisplayWrite (WDDM 1.2+) */
VOID
APIENTRY
Bc250DdiSystemDisplayWrite(
    _In_ PVOID MiniportDeviceContext,
    _In_ PVOID Source,
    _In_ UINT SourceWidth,
    _In_ UINT SourceHeight,
    _In_ UINT SourceStride,
    _In_ UINT DestinationX,
    _In_ UINT DestinationY
    );


#endif /* _AMDBC250_KMD_H_ */
