/*++

Copyright (c) 2026 AMD BC-250 "Dream Drivers" Project — Version 4.3

Module Name:
    amdbc250_ioctl.h

Abstract:
    IOCTL interface between Kernel-Mode Driver (KMD) and User-Mode Driver (UMD).
    Defines command codes, structures, and communication protocol.

Environment:
    User mode and Kernel mode

--*/

#pragma once

#ifndef _AMDBC250_IOCTL_H_
#define _AMDBC250_IOCTL_H_

#include <windef.h>

/*===========================================================================
  IOCTL Device Path
============================================================================*/

#define AMDBC250_DEVICE_PATH    L"\\\\.\\AMDBC250DreamV43"
#define AMDBC250_DEVICE_NAME    L"AMDBC250DreamV43"

/*===========================================================================
  IOCTL Control Codes
============================================================================*/

#define FILE_DEVICE_AMDBC250    0x8000
#define IOCTL_INDEX             0x800

#define CTL_CODE_AMDBC250(Function, Method, Access) \
    CTL_CODE(FILE_DEVICE_AMDBC250, IOCTL_INDEX + (Function), Method, Access)

/* Device management */
#define IOCTL_AMDBC250_GET_CAPS             CTL_CODE_AMDBC250(0x00, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AMDBC250_GET_VRAM_INFO        CTL_CODE_AMDBC250(0x01, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AMDBC250_GET_TEMP_INFO       CTL_CODE_AMDBC250(0x02, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Memory management */
#define IOCTL_AMDBC250_ALLOC_VIDMEM         CTL_CODE_AMDBC250(0x10, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AMDBC250_FREE_VIDMEM          CTL_CODE_AMDBC250(0x11, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AMDBC250_MAP_VIDMEM           CTL_CODE_AMDBC250(0x12, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AMDBC250_UNMAP_VIDMEM         CTL_CODE_AMDBC250(0x13, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AMDBC250_MAP_SYSTEM_MEM       CTL_CODE_AMDBC250(0x14, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Command submission */
#define IOCTL_AMDBC250_SUBMIT_COMMANDS      CTL_CODE_AMDBC250(0x20, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AMDBC250_WAIT_FENCE           CTL_CODE_AMDBC250(0x21, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AMDBC250_SIGNAL_FENCE         CTL_CODE_AMDBC250(0x22, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AMDBC250_RESET_DEVICE         CTL_CODE_AMDBC250(0x23, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Display */
#define IOCTL_AMDBC250_SET_DISPLAY_MODE     CTL_CODE_AMDBC250(0x30, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AMDBC250_FLIP_DISPLAY         CTL_CODE_AMDBC250(0x31, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AMDBC250_GET_DISPLAY_INFO     CTL_CODE_AMDBC250(0x32, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Power management */
#define IOCTL_AMDBC250_SET_POWER_STATE      CTL_CODE_AMDBC250(0x40, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AMDBC250_GET_POWER_TELEMETRY  CTL_CODE_AMDBC250(0x41, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* SDMA Copy Engine */
#define IOCTL_AMDBC250_SDMA_COPY            CTL_CODE_AMDBC250(0x50, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AMDBC250_SDMA_FILL            CTL_CODE_AMDBC250(0x51, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* TDR Reset */
#define IOCTL_AMDBC250_TDR_RESET            CTL_CODE_AMDBC250(0x54, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* EDID / Monitor */
#define IOCTL_AMDBC250_READ_EDID            CTL_CODE_AMDBC250(0x58, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AMDBC250_GET_CHILD_RELATIONS  CTL_CODE_AMDBC250(0x59, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Shader */
#define IOCTL_AMDBC250_SHADER_COMPILE       CTL_CODE_AMDBC250(0x5C, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* 40 CU Unlock */
#define IOCTL_AMDBC250_UNLOCK_40CU          CTL_CODE_AMDBC250(0x60, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AMDBC250_GET_CU_STATUS        CTL_CODE_AMDBC250(0x61, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Hardware Init (from user-mode — maps MMIO, inits rings/fence) */
#define IOCTL_AMDBC250_INIT_HARDWARE        CTL_CODE_AMDBC250(0x70, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Send raw PM4 commands to GFX ring */
#define IOCTL_AMDBC250_SEND_PM4             CTL_CODE_AMDBC250(0x71, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Read GPU register (for testing MMIO) */
#define IOCTL_AMDBC250_READ_REG             CTL_CODE_AMDBC250(0x72, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Write GPU register (for testing MMIO) */
#define IOCTL_AMDBC250_WRITE_REG            CTL_CODE_AMDBC250(0x73, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Get hardware status (rings, fence, MMIO state) */
#define IOCTL_AMDBC250_GET_HW_STATUS        CTL_CODE_AMDBC250(0x74, METHOD_BUFFERED, FILE_ANY_ACCESS)

/*===========================================================================
  Capability Flags
============================================================================*/

#define AMDBC250_CAP_NONE              0x00000000
#define AMDBC250_CAP_D3D12             0x00000001
#define AMDBC250_CAP_COMPUTE           0x00000002  /* Disabled - HW quirk */
#define AMDBC250_CAP_DISPLAY           0x00000004
#define AMDBC250_CAP_RAY_TRACING       0x00000008
#define AMDBC250_CAP_VIDEO_DECODE      0x00000010  /* Blocked by Sony */
#define AMDBC250_CAP_VIDEO_ENCODE      0x00000020  /* Blocked by Sony */

/*===========================================================================
  Structures
============================================================================*/

#pragma pack(push, 8)

/* --- Get Caps --- */
typedef struct _AMDBC250_IOCTL_CAPS {
    UINT32 Version;             /* Driver version */
    UINT32 Caps;                /* AMDBC250_CAP_* flags */
    UINT32 MaxClockMhz;         /* Max GPU clock */
    UINT32 MemoryClockMhz;      /* Memory clock */
    UINT32 ComputeUnits;        /* Number of CUs */
    UINT32 ShaderProcessors;    /* Number of SPs */
    UINT32 RTAccelrators;       /* RT cores */
} AMDBC250_IOCTL_CAPS, *PAMDBC250_IOCTL_CAPS;

/* --- VRAM Info --- */
typedef struct _AMDBC250_IOCTL_VRAM_INFO {
    UINT64 TotalVramBytes;      /* Total VRAM */
    UINT64 VisibleVramBytes;    /* CPU-visible VRAM (quirk: ~10GB) */
    UINT64 UsedVramBytes;       /* Currently allocated */
    UINT32 SegmentCount;        /* Number of memory segments */
} AMDBC250_IOCTL_VRAM_INFO, *PAMDBC250_IOCTL_VRAM_INFO;

/* --- Temperature --- */
typedef struct _AMDBC250_IOCTL_TEMP_INFO {
    INT32  EdgeTempC;           /* Edge temperature */
    INT32  JunctionTempC;       /* Junction/hotspot */
    INT32  VrmTempC;            /* VRM temperature */
    UINT32 FanSpeedPercent;     /* Fan speed 0-100% */
    BOOLEAN ThrottleActive;     /* Thermal throttle */
} AMDBC250_IOCTL_TEMP_INFO, *PAMDBC250_IOCTL_TEMP_INFO;

/* --- Allocate Video Memory --- */
typedef struct _AMDBC250_IOCTL_ALLOC_VIDMEM {
    UINT64 Size;                /* Bytes to allocate */
    UINT64 Alignment;           /* Required alignment */
    UINT32 Flags;               /* AMDBC250_VM_* flags */
    UINT32 SegmentId;           /* 0=VRAM, 1=System */
} AMDBC250_IOCTL_ALLOC_VIDMEM, *PAMDBC250_IOCTL_ALLOC_VIDMEM;

typedef struct _AMDBC250_IOCTL_ALLOC_VIDMEM_RESULT {
    UINT64 GpuVirtualAddress;   /* GPU VA */
    UINT64 PhysicalAddress;     /* Physical address */
    UINT64 Handle;              /* Allocation handle */
} AMDBC250_IOCTL_ALLOC_VIDMEM_RESULT, *PAMDBC250_IOCTL_ALLOC_VIDMEM_RESULT;

/* --- Free Video Memory --- */
typedef struct _AMDBC250_IOCTL_FREE_VIDMEM {
    UINT64 Handle;              /* Allocation handle */
} AMDBC250_IOCTL_FREE_VIDMEM, *PAMDBC250_IOCTL_FREE_VIDMEM;

/* --- Map Video Memory (for CPU access) --- */
typedef struct _AMDBC250_IOCTL_MAP_VIDMEM {
    UINT64 Handle;              /* Allocation handle */
    UINT64 Offset;              /* Offset within allocation */
    UINT64 Size;                /* Size to map */
} AMDBC250_IOCTL_MAP_VIDMEM, *PAMDBC250_IOCTL_MAP_VIDMEM;

typedef struct _AMDBC250_IOCTL_MAP_VIDMEM_RESULT {
    UINT64 CpuAddress;          /* CPU-accessible address */
    UINT64 PhysicalAddress;     /* Physical address */
} AMDBC250_IOCTL_MAP_VIDMEM_RESULT, *PAMDBC250_IOCTL_MAP_VIDMEM_RESULT;

/* --- Submit Commands --- */
typedef struct _AMDBC250_IOCTL_SUBMIT_COMMANDS {
    UINT64 DmaBufferGpuVa;      /* GPU VA of command buffer */
    UINT64 DmaBufferSize;       /* Size in bytes */
    UINT32 FenceValue;          /* Fence to signal */
    UINT32 QueueType;           /* 0=GFX, 1=Compute, 2=SDMA */
} AMDBC250_IOCTL_SUBMIT_COMMANDS, *PAMDBC250_IOCTL_SUBMIT_COMMANDS;

/* --- Wait Fence --- */
typedef struct _AMDBC250_IOCTL_WAIT_FENCE {
    UINT32 FenceValue;          /* Fence value to wait for */
    UINT32 TimeoutMs;           /* Timeout in milliseconds */
} AMDBC250_IOCTL_WAIT_FENCE, *PAMDBC250_IOCTL_WAIT_FENCE;

/* --- Set Display Mode --- */
typedef struct _AMDBC250_IOCTL_SET_DISPLAY_MODE {
    UINT32 Width;
    UINT32 Height;
    UINT32 RefreshRate;
    UINT32 BitsPerPixel;
} AMDBC250_IOCTL_SET_DISPLAY_MODE, *PAMDBC250_IOCTL_SET_DISPLAY_MODE;

/* --- Flip Display --- */
typedef struct _AMDBC250_IOCTL_FLIP_DISPLAY {
    UINT64 SurfacePhysicalAddress;
    UINT32 Width;
    UINT32 Height;
    UINT32 Pitch;
    UINT32 Format;              /* D3DDDIFMT_* */
    UINT32 FlipInterval;        /* 0=immediate, 1=vsync, 2=2vsync, 3=3vsync */
} AMDBC250_IOCTL_FLIP_DISPLAY, *PAMDBC250_IOCTL_FLIP_DISPLAY;

/* --- Display Info --- */
typedef struct _AMDBC250_IOCTL_DISPLAY_INFO {
    UINT32 CurrentWidth;
    UINT32 CurrentHeight;
    UINT32 CurrentRefreshRate;
    UINT32 MaxWidth;
    UINT32 MaxHeight;
    UINT32 OutputTypes;         /* AMDBC250_OUTPUT_* flags */
    UINT32 NumPipes;            /* DCN 2.1 display pipes */
} AMDBC250_IOCTL_DISPLAY_INFO, *PAMDBC250_IOCTL_DISPLAY_INFO;

/* --- Power State --- */
typedef struct _AMDBC250_IOCTL_POWER_STATE {
    UINT32 PowerState;          /* 0=D0, 1=D1, 2=D2, 3=D3 */
    UINT32 GpuClockMhz;
    UINT32 MemoryClockMhz;
} AMDBC250_IOCTL_POWER_STATE, *PAMDBC250_IOCTL_POWER_STATE;

/* --- Power Telemetry --- */
typedef struct _AMDBC250_IOCTL_POWER_TELEMETRY {
    UINT32 PowerMilliwatts;
    UINT32 PeakPowerMilliwatts;
    UINT32 GpuClockMhz;
    UINT32 MemoryClockMhz;
    UINT32 FanSpeedPercent;
    INT32  EdgeTempC;
    INT32  JunctionTempC;
    UINT32 ThrottleActive;
    UINT32 ThrottleCount;
} AMDBC250_IOCTL_POWER_TELEMETRY, *PAMDBC250_IOCTL_POWER_TELEMETRY;

/* --- Init Hardware (user-mode provides MMIO base) --- */
typedef struct _AMDBC250_IOCTL_INIT_HARDWARE {
    UINT64 MmioPhysicalBase;    /* BAR0 physical address from PCI config */
    UINT32 MmioSize;            /* BAR0 size from PCI config */
    UINT32 Flags;               /* Reserved, must be 0 */
} AMDBC250_IOCTL_INIT_HARDWARE, *PAMDBC250_IOCTL_INIT_HARDWARE;

/* --- Send PM4 --- */
typedef struct _AMDBC250_IOCTL_SEND_PM4 {
    UINT32 Commands[64];        /* Up to 64 DWORDs of PM4 commands */
    UINT32 CommandCount;        /* Number of DWORDs */
    UINT32 FenceValue;          /* Fence to signal after execution */
    UINT32 QueueType;           /* 0=GFX, 1=Compute(broken), 2=SDMA */
} AMDBC250_IOCTL_SEND_PM4, *PAMDBC250_IOCTL_SEND_PM4;

/* --- Read/Write Register --- */
typedef struct _AMDBC250_IOCTL_REG_ACCESS {
    UINT32 RegisterOffset;      /* GPU register offset (byte) */
    UINT32 Value;               /* Register value */
} AMDBC250_IOCTL_REG_ACCESS, *PAMDBC250_IOCTL_REG_ACCESS;

/* --- HW Status --- */
typedef struct _AMDBC250_IOCTL_HW_STATUS {
    UINT32 MmioMapped;          /* 1=MMIO mapped */
    UINT32 RingsInitialized;    /* 1=rings ready */
    UINT32 FenceInitialized;    /* 1=fence ready */
    UINT64 GfxRingPhysAddr;     /* Ring physical address */
    UINT32 GfxRingSize;         /* Ring size */
    UINT32 GfxRingWptr;         /* Current write pointer */
    UINT32 GfxRingRptr;         /* Current read pointer */
    UINT64 FencePhysAddr;       /* Fence physical address */
    UINT64 FenceValue;          /* Current fence value */
    UINT64 LastSubmittedFence;  /* Last submitted fence */
} AMDBC250_IOCTL_HW_STATUS, *PAMDBC250_IOCTL_HW_STATUS;

#pragma pack(pop)

#endif /* _AMDBC250_IOCTL_H_ */
