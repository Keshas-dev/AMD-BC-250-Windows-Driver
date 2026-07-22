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
#define IOCTL_INDEX             0x270

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

/* Read PCI config space BARs (for auto-detection when BootConfig is empty) */
#define IOCTL_AMDBC250_READ_PCI_BAR         CTL_CODE_AMDBC250(0x75, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* PSP (Platform Security Processor) - handled by separate PspDriver.sys */
#define IOCTL_AMDBC250_PSP_GET_STATUS       CTL_CODE_AMDBC250(0x79, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Read raw PCI config space by bus/device/function */
#define IOCTL_AMDBC250_READ_PCI_CONFIG      CTL_CODE_AMDBC250(0x7B, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Write to PCI config space (for enabling Memory Space) */
#define IOCTL_AMDBC250_WRITE_PCI_CONFIG     CTL_CODE_AMDBC250(0x7C, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Scan all ECAM bases + IO ports to find BC-250 */
#define IOCTL_AMDBC250_DISCOVER_PCI         CTL_CODE_AMDBC250(0x7D, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Report BAR addresses parsed from StartDevice resource list */
#define IOCTL_AMDBC250_GET_RESOURCE_BARS    CTL_CODE_AMDBC250(0x7E, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _AMDBC250_IOCTL_RESOURCE_BARS {
    UINT32 DeviceStarted;         /* 1=StartDevice was called */
    UINT32 MmioMapped;            /* 1=MMIO mapped */
    UINT32 MmioSize;              /* MMIO BAR size */
    UINT64 MmioPhysicalBase;      /* MMIO BAR physical address */
    UINT64 MmioVirtualBase;       /* Mapped virtual address */
    UINT32 FbSize;                /* Framebuffer BAR size */
    UINT64 FbPhysicalBase;        /* Framebuffer BAR physical address */
} AMDBC250_IOCTL_RESOURCE_BARS, *PAMDBC250_IOCTL_RESOURCE_BARS;

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

/* --- Init Hardware (user-mode provides MMIO and VRAM bases) --- */
#define AMDBC250_INIT_FLAG_NONE       0x00000000
#define AMDBC250_INIT_FLAG_NBIO_MAP   0x00000001  /* Map address without GPU init (for NBIO config access) */

typedef struct _AMDBC250_IOCTL_INIT_HARDWARE {
    UINT64 MmioPhysicalBase;    /* BAR2 physical address (MMIO registers, e.g. 0xD0000000) */
    UINT32 MmioSize;            /* BAR2 size (e.g. 0x200000 = 2MB) */
    UINT32 Flags;               /* AMDBC250_INIT_FLAG_* */
    UINT64 FbPhysicalBase;      /* BAR0 physical address (VRAM framebuffer, e.g. 0xC0000000) */
    UINT32 FbSize;              /* BAR0 size (e.g. 0x10000000 = 256MB) */
} AMDBC250_IOCTL_INIT_HARDWARE, *PAMDBC250_IOCTL_INIT_HARDWARE;

/* --- Send PM4 --- */
typedef struct _AMDBC250_IOCTL_SEND_PM4 {
    UINT32 Commands[64];        /* Up to 64 DWORDs of PM4 commands */
    UINT32 CommandCount;        /* Number of DWORDs */
    UINT32 Padding;             /* alignment */
    UINT64 FenceValue;          /* 64-bit fence to signal after execution */
    UINT32 QueueType;           /* 0=GFX, 1=Compute(broken), 2=SDMA */
    UINT32 Padding2;            /* alignment */
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

/* --- PCI BAR Info (one BAR) --- */
typedef struct _AMDBC250_IOCTL_PCI_BAR_INFO {
    UINT64 PhysicalAddress;     /* BAR physical address (0 = not present) */
    UINT32 Size;                /* BAR size in bytes (0 = not present) */
    UINT32 IsMemoryBar;         /* 1=Memory BAR, 0=I/O BAR */
    UINT32 Is64Bit;             /* 1=64-bit BAR, 0=32-bit BAR */
} AMDBC250_IOCTL_PCI_BAR_INFO, *PAMDBC250_IOCTL_PCI_BAR_INFO;

/* --- GCVM Page Table Setup --- */
#define IOCTL_AMDBC250_SETUP_PAGE_TABLES CTL_CODE(FILE_DEVICE_AMDBC250, 0x263, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* --- PCI Config Read Result --- */
typedef struct _AMDBC250_IOCTL_PCI_CONFIG {
    UINT16 VendorId;
    UINT16 DeviceId;
    UINT16 Command;
    UINT16 Status;
    UINT32 RevisionId;
    UINT32 ClassCode;
    AMDBC250_IOCTL_PCI_BAR_INFO Bars[6];  /* BAR0-BAR5 */
    UINT32 Bus;
    UINT32 Device;
    UINT32 Function;
} AMDBC250_IOCTL_PCI_CONFIG, *PAMDBC250_IOCTL_PCI_CONFIG;

/* --- Read raw PCI config space by bus/device/function --- */
typedef struct _AMDBC250_IOCTL_READ_PCI_CONFIG {
    UINT32 Bus;                   /* PCI bus number */
    UINT32 Device;                /* PCI device number */
    UINT32 Function;              /* PCI function number */
    UINT32 BytesRead;             /* OUT: bytes actually read */
    UINT8  ConfigData[256];       /* OUT: raw PCI config space (256 bytes) */
} AMDBC250_IOCTL_READ_PCI_CONFIG, *PAMDBC250_IOCTL_READ_PCI_CONFIG;

/* --- Write PCI config space (one DWORD at bus/dev/func/offset) --- */
typedef struct _AMDBC250_IOCTL_WRITE_PCI_CONFIG {
    UINT32 Bus;                   /* PCI bus number */
    UINT32 Device;                /* PCI device number */
    UINT32 Function;              /* PCI function number */
    UINT32 Offset;                /* Register offset in config space (0-255, must be DWORD-aligned) */
    UINT32 Value;                 /* Value to write */
} AMDBC250_IOCTL_WRITE_PCI_CONFIG, *PAMDBC250_IOCTL_WRITE_PCI_CONFIG;

/* --- Discover PCI device (BC-250) via multiple methods --- */
typedef struct _AMDBC250_IOCTL_DISCOVER_PCI {
    UINT32 VendorFound;           /* OUT: 1 if BC-250 found */
    UINT32 FoundBus;              /* OUT: bus number where found */
    UINT32 FoundDevice;           /* OUT: device number where found */
    UINT32 FoundFunction;         /* OUT: function number where found */
    UINT32 MethodUsed;            /* OUT: 0=HAL, 1=ECAM, 2=IO ports */
    AMDBC250_IOCTL_PCI_CONFIG PciConfig; /* OUT: full BAR info */
} AMDBC250_IOCTL_DISCOVER_PCI, *PAMDBC250_IOCTL_DISCOVER_PCI;

/* --- Force-enable MMIO: set PCI Command reg, test scratch write --- */
#define IOCTL_AMDBC250_FORCE_ENABLE_MMIO  CTL_CODE_AMDBC250(0x7F, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _AMDBC250_IOCTL_FORCE_ENABLE_MMIO {
    UINT32 HalSetBusResult;          /* OUT: 1=HalSetBusDataByOffset worked */
    UINT32 IoPortWriteResult;        /* OUT: 1=IO port write worked */
    UINT32 CommandBefore;            /* OUT: PCI Command reg before */
    UINT32 CommandAfter;             /* OUT: PCI Command reg after */
    UINT32 ScratchWriteVal;          /* IN: test value to write */
    UINT32 ScratchReadVal;           /* OUT: value read back */
    UINT32 ScratchOffset;            /* IN: scratch register offset (bytes) */
    UINT32 GpuIdBefore;              /* OUT: GPU_ID register before enable */
    UINT32 GpuIdAfter;               /* OUT: GPU_ID register after enable */
    UINT32 Bus;                      /* IN: PCI bus */
    UINT32 Device;                   /* IN: PCI device */
    UINT32 Function;                 /* IN: PCI function */
    UINT64 MmioPhysicalBase;         /* IN: MMIO physical address */
    UINT32 MmioSize;                 /* IN: MMIO size in bytes */
} AMDBC250_IOCTL_FORCE_ENABLE_MMIO, *PAMDBC250_IOCTL_FORCE_ENABLE_MMIO;

/* --- Read/write I/O port (for PCI I/O BAR access like GPU doorbell) --- */
#define IOCTL_AMDBC250_PORT_IO              CTL_CODE_AMDBC250(0x82, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _AMDBC250_IOCTL_PORT_IO {
    UINT32 Port;                    /* IN: I/O port number (e.g. 0xE000) */
    UINT32 IsWrite;                 /* IN: 0=read, 1=write */
    UINT32 Value;                   /* IN: value to write / OUT: value read */
    UINT32 Width;                   /* IN: 1=BYTE, 2=WORD, 4=DWORD */
    UINT32 Result;                  /* OUT: 1=success, 0=failure */
} AMDBC250_IOCTL_PORT_IO, *PAMDBC250_IOCTL_PORT_IO;

/* --- SMN (System Management Network) read/write via MMIO index/data ports --- */
#define IOCTL_AMDBC250_SMN_ACCESS           CTL_CODE_AMDBC250(0x81, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _AMDBC250_IOCTL_SMN_ACCESS {
    UINT32 SmnAddress;              /* IN: SMN register address (byte offset) */
    UINT32 SmnData;                 /* IN/OUT: data to write / data read */
    UINT32 IsWrite;                 /* IN: 0=read, 1=write */
    UINT32 Result;                  /* OUT: 1=success, 0=failure */
    UINT32 IndexPort;               /* IN: MMIO index port address (0 = default 0x3B10528) */
    UINT32 DataPort;                /* IN: MMIO data port address (0 = default 0x3B10564) */
} AMDBC250_IOCTL_SMN_ACCESS, *PAMDBC250_IOCTL_SMN_ACCESS;

/* --- Direct MMIO test: map any physical address and read/write --- */
#define IOCTL_AMDBC250_MMIO_TEST            CTL_CODE_AMDBC250(0x80, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _AMDBC250_IOCTL_MMIO_TEST {
    UINT64 PhysicalAddress;         /* IN: physical address to map */
    UINT32 Size;                    /* IN: size to map (bytes) */
    UINT32 OffsetRead;              /* IN: offset to read from */
    UINT32 ValueRead;               /* OUT: value read */
    UINT32 OffsetWrite;             /* IN: offset to write (0 = skip write) */
    UINT32 ValueWrite;              /* IN: value to write */
    UINT32 ValueWrittenBack;        /* OUT: value read back after write */
    UINT32 MapResult;               /* OUT: 1=mapped OK, 0=failed */
    UINT32 Padding;                 /* alignment */
} AMDBC250_IOCTL_MMIO_TEST, *PAMDBC250_IOCTL_MMIO_TEST;

/* --- PSP Mailbox: direct firmware loading via GPU BAR5 (no PSP driver needed) --- */
/* NOTE: using raw CTL_CODE with high function codes to avoid collision with
   CTL_CODE_AMDBC250(0x90+) which overlaps existing raw CTL_CODE entries. */
#define IOCTL_AMDBC250_PSP_LOAD_IP_FW      CTL_CODE(FILE_DEVICE_AMDBC250, 0x920, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _AMDBC250_IOCTL_PSP_LOAD_IP_FW {
    UINT32 FwType;              /* IN: GFX_FW_TYPE (1=ME, 2=PFP, 3=CE, 4=MEC, 8=RLC, 9=SDMA0) */
    UINT32 FwSize;              /* IN: firmware blob size in bytes */
    UINT32 Result;              /* OUT: 0=fail, 1=success */
    UINT32 C2Pmsg35After;       /* OUT: C2PMSG_35 after command */
    UINT32 C2Pmsg81After;       /* OUT: C2PMSG_81 after command (0xF0000010 = OK) */
    /* Firmware data follows immediately after this struct */
} AMDBC250_IOCTL_PSP_LOAD_IP_FW, *PAMDBC250_IOCTL_PSP_LOAD_IP_FW;

/* --- PSP Mailbox: send SMU message via BAR5+0x38/0x3C (no PSP driver needed) --- */
#define IOCTL_AMDBC250_PSP_SMU_MSG          CTL_CODE(FILE_DEVICE_AMDBC250, 0x924, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _AMDBC250_IOCTL_PSP_SMU_MSG {
    UINT32 Message;             /* IN: SMU message ID (e.g. 0x02=GetSmuVersion) */
    UINT32 Argument;            /* IN: argument */
    UINT32 Response;            /* OUT: response from C2PMSG_82 */
    UINT32 ResponseStatus;      /* OUT: C2PMSG_90 (1=OK, 0xFF=error) */
    UINT32 Result;              /* OUT: 0=fail, 1=success */
} AMDBC250_IOCTL_PSP_SMU_MSG, *PAMDBC250_IOCTL_PSP_SMU_MSG;

/* --- Get BAR5 virtual address (for PSP driver mailbox access) --- */
#define IOCTL_AMDBC250_BAR5_READ_PROXY      CTL_CODE_AMDBC250(0x83, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _AMDBC250_IOCTL_BAR5_READ_PROXY {
    UINT64 Bar5VirtualAddress;        /* OUT: BAR5 virtual address */
    UINT32 Bar5Size;                  /* OUT: BAR5 size in bytes */
    UINT32 Padding;                   /* alignment */
} AMDBC250_IOCTL_BAR5_READ_PROXY, *PAMDBC250_IOCTL_BAR5_READ_PROXY;

/* --- GPU-local KIQ ring test (bypasses PSP entirely) --- */
#define IOCTL_AMDBC250_GPU_KIQ_TEST   CTL_CODE_AMDBC250(0x84, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _AMDBC250_IOCTL_GPU_KIQ_TEST {
    UINT32 Result;                    /* OUT: 0=fail, else success code */
    UINT32 ScratchBefore;             /* OUT: SCRATCH before PM4 */
    UINT32 ScratchAfter;              /* OUT: SCRATCH after PM4 */
    UINT32 MmioMapped;                /* OUT: 1 if BAR5 mapped */
    UINT32 RingAllocated;             /* OUT: 1 if ring allocated */
    UINT32 HqdProgrammed;             /* OUT: 1 if HQD/IB registers written */
    UINT32 Pm4Submitted;              /* OUT: 1 if PM4 written to ring */
    UINT32 UseIB;                     /* IN: 1=use IB (0x3BAC/0x3BB0/0x3BC0) instead of KIQ/HQD */
    UINT32 HqdRptr;                  /* OUT: HQD_PQ_RPTR (0x912C) after submit (HW-consumed ptr) */
    UINT32 KiQ_RP;                   /* OUT: KIQ_RPTR (0xE06C) after submit */
    UINT32 GrbmStat;                /* OUT: GRBM_STATUS (0x3260) after submit */
    UINT32 FwLoaded;                 /* OUT: 1 if DreamV3LoadAllFirmware succeeded */
    UINT32 MecCntlBefore;            /* OUT: CP_MEC_CNTL (0x4B14) at entry */
    UINT32 MecUnhalted;              /* OUT: 1 if we cleared MEC halt before submit */
    UINT32 ScratchAfter2;            /* OUT: SCRATCH after MEC-unhalt retry submit */
    UINT32 HqdRptr2;                /* OUT: HQD_PQ_RPTR after retry */
    UINT32 KiQ_RP2;                  /* OUT: KIQ_RPTR after retry */
    UINT32 GrbmStat2;                /* OUT: GRBM_STATUS after retry */
    UINT32 FbLocationBase;           /* OUT: MC_VM_FB_LOCATION_BASE (RO, VRAM GPU-VA base) */
    UINT32 FbOffset;                /* OUT: MC_VM_FB_OFFSET (RO) */
    UINT64 RingGpuVa;              /* OUT: ring GPU virtual address programmed into PQ_BASE */
    UINT32 HqdPqWptrRb;           /* OUT: HQD_PQ_WPTR_LO readback (CP view) */
    UINT32 DoorKicked;              /* OUT: 1 if we kicked via doorbell */
    UINT32 DbLoRb;                 /* OUT: CP_MEC_DOORBELL_RANGE_LOWER readback */
    UINT32 DbHiRb;                 /* OUT: CP_MEC_DOORBELL_RANGE_UPPER readback */
    UINT32 DbCtlRb;                /* OUT: CP_HQD_PQ_DOORBELL_CONTROL readback */
} AMDBC250_IOCTL_GPU_KIQ_TEST, *PAMDBC250_IOCTL_GPU_KIQ_TEST;

/* --- Direct CP firmware load via MMIO (bypasses PSP entirely) --- */
#define IOCTL_AMDBC250_LOAD_CP_FW    CTL_CODE_AMDBC250(0x85, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _AMDBC250_IOCTL_LOAD_CP_FW {
    UINT32 FwType;                  /* IN: 1=ME, 2=PFP, 3=CE */
    UINT32 FwSize;                  /* IN: total firmware blob size in bytes */
    UINT32 Result;                  /* OUT: 0=fail, 1=success, error codes */
    UINT32 UcodeVersion;            /* OUT: firmware version from header */
    /* Firmware data follows immediately after this struct */
} AMDBC250_IOCTL_LOAD_CP_FW, *PAMDBC250_IOCTL_LOAD_CP_FW;

/* --- GPU register state dump (read-only, safe) --- */
#define IOCTL_AMDBC250_REG_DUMP     CTL_CODE_AMDBC250(0x86, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _AMDBC250_IOCTL_REG_DUMP {
    UINT32 Result;                  /* OUT: 1=success */
    /* GC registers (GC_BASE-shifted) */
    UINT32 GpuId;                   /* OUT: GPU_ID */
    UINT32 GrbmStatus;              /* OUT: GRBM_STATUS */
    UINT32 GrbmStatusSe0;           /* OUT: GRBM_STATUS_SE0 */
    UINT32 GrbmStatusSe1;           /* OUT: GRBM_STATUS_SE1 */
    UINT32 CcShaderArrayConfig;     /* OUT: CC_GC_SHADER_ARRAY_CONFIG */
    UINT32 Scratch;                 /* OUT: SCRATCH */
    UINT32 SpiWgpMask;              /* OUT: SPI_WGP_MASK */
    UINT32 GrbmGfxIndex;            /* OUT: GRBM_GFX_INDEX */
    /* CP registers (BC-250 raw BAR5 offsets — NOT Navi10) */
    UINT32 MeCntl;                  /* OUT: ME_CNTL (0x4A74) */
    UINT32 PfpCntl;                 /* OUT: PFP_CNTL */
    UINT32 CeCntl;                  /* OUT: CE_CNTL */
    /* KIQ ring registers */
    UINT32 KiqBaseLo;               /* OUT: KIQ_BASE_LO (0xE060) */
    UINT32 KiqBaseHi;               /* OUT: KIQ_BASE_HI (0xE064) */
    UINT32 KiqCntl;                 /* OUT: KIQ_CNTL (0xE068) */
    UINT32 KiqRptr;                 /* OUT: KIQ_RPTR (0xE06C) */
    UINT32 KiqWptr;                 /* OUT: KIQ_WPTR (0xE078) */
    /* HQD KIQ queue registers */
    UINT32 HqdActiveKiq;            /* OUT: HQD_ACTIVE KIQ (0xDAC0) */
    UINT32 HqdPqBaseKiq;            /* OUT: HQD_PQ_BASE KIQ (0xDAD8) */
    UINT32 HqdPqBaseHiKiq;          /* OUT: HQD_PQ_BASE_HI KIQ (0xDADC) */
    UINT32 HqdPqRptrKiq;            /* OUT: HQD_PQ_RPTR KIQ (0xDAE0) */
    UINT32 HqdPqWptrLoKiq;          /* OUT: HQD_PQ_WPTR_LO KIQ (0xDB90) */
    UINT32 HqdVmidKiq;              /* OUT: HQD_VMID KIQ (0xDAC4) */
    /* HQD compute/GFX ring registers */
    UINT32 HqdActiveCmp;            /* OUT: HQD_ACTIVE compute (0xDCF4) */
    UINT32 HqdPqBaseCmp;            /* OUT: HQD_PQ_BASE compute (0xDBC8) */
    UINT32 HqdPqBaseHiCmp;          /* OUT: HQD_PQ_BASE_HI compute (0xDBCC) */
    UINT32 HqdPqRptrCmp;            /* OUT: HQD_PQ_RPTR compute (0xDBD0) */
    UINT32 HqdPqWptrCmp;            /* OUT: HQD_PQ_WPTR compute (0xDBD4) */
    UINT32 HqdVmidCmp;              /* OUT: HQD_VMID compute (0xDCF0) */
    UINT32 HqdAqCntlCmp;            /* OUT: HQD_AQ_CNTL compute (0xDBC0) */
    /* GCVM registers */
    UINT32 GcvmL2Cntl;              /* OUT: GCVM_L2_CNTL (0x0B360) */
    UINT32 GcvmContext0Cntl;        /* OUT: GCVM_CONTEXT0_CNTL (0x0B460) */
    UINT32 GcvmPtBaseLo;            /* OUT: GCVM_CONTEXT0_PT_BASE_LO (0x0B608) */
    UINT32 GcvmPtBaseHi;            /* OUT: GCVM_CONTEXT0_PT_BASE_HI (0x0B60C) */
    /* BIOS Context0 TLB entries (0x0B408-0x0B45C) */
    UINT32 Ctx0[20];                /* OUT: 20 DWORDs from 0x0B408-0x0B454 */
    /* RLC/SDMA */
    UINT32 RlcCntl;                 /* OUT: RLC_CNTL */
    UINT32 Sdma0Cntl;               /* OUT: SDMA0_CNTL */
    /* Extra CP ring probes (raw BAR5 offsets) */
    UINT32 CpRb0BaseProbe[4];       /* OUT: probe 0xDA60-0xDA6C */
    UINT32 CpRb1BaseProbe[4];       /* OUT: probe 0xDA70-0xDA7C */
} AMDBC250_IOCTL_REG_DUMP, *PAMDBC250_IOCTL_REG_DUMP;

/* --- Clean KIQ NOP test (no BIOS state destruction) --- */
#define IOCTL_AMDBC250_KIQ_NOP_TEST CTL_CODE_AMDBC250(0x87, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _AMDBC250_IOCTL_KIQ_NOP_TEST {
    UINT32 Result;                  /* OUT: 0=fail, 1=RPTR advanced, 2=SCRATCH changed, else=error */
    UINT32 ScratchBefore;           /* OUT: SCRATCH before */
    UINT32 ScratchAfter;            /* OUT: SCRATCH after (should be 0xCAFEBABE if PM4 executed) */
    UINT32 KiqRptrBefore;           /* OUT: KIQ_RPTR before */
    UINT32 KiqRptrAfter;            /* OUT: KIQ_RPTR after */
    UINT32 KiqWptrSet;              /* OUT: WPTR value we wrote */
    UINT32 RingPaLo;                /* OUT: ring PA low 32 */
    UINT32 RingPaHi;                /* OUT: ring PA high 32 */
    UINT32 MeCntlBefore;            /* OUT: ME_CNTL before */
    UINT32 MeCntlAfter;             /* OUT: ME_CNTL after */
    UINT32 GcvmContext0CntlBefore;  /* OUT: GCVM_CONTEXT0_CNTL before */
    UINT32 GcvmContext0CntlAfter;   /* OUT: GCVM_CONTEXT0_CNTL after */
} AMDBC250_IOCTL_KIQ_NOP_TEST, *PAMDBC250_IOCTL_KIQ_NOP_TEST;

/* --- KIQ BIOS ring submit: map BIOS ring PA, write PM4, check execution --- */
#define IOCTL_AMDBC250_KIQ_BIOS_RING_SUBMIT CTL_CODE_AMDBC250(0x88, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _AMDBC250_IOCTL_KIQ_BIOS_RING_SUBMIT {
    UINT32 Result;                  /* OUT: 0=fail, 2=SCRATCH changed, else=error code */
    UINT32 ScratchBefore;           /* OUT: SCRATCH before */
    UINT32 ScratchAfter;            /* OUT: SCRATCH after */
    UINT32 KiqBaseLo;               /* IN/OUT: ring PA low (0=read from regs) */
    UINT32 KiqBaseHi;               /* IN/OUT: ring PA high */
    UINT32 KiqRptrBefore;           /* OUT: KIQ_RPTR before */
    UINT32 KiqRptrAfter;            /* OUT: KIQ_RPTR after */
    UINT32 KiqWptrSet;              /* OUT: WPTR value written */
    UINT32 MeCntlBefore;            /* OUT: ME_CNTL before */
    UINT32 MeCntlAfter;             /* OUT: ME_CNTL after */
    UINT32 RingDword0;              /* OUT: first DWORD of ring after write */
    UINT32 RingDword1;              /* OUT: second DWORD */
    UINT32 RingDword2;              /* OUT: third DWORD */
    UINT32 RingDword3;              /* OUT: fourth DWORD */
} AMDBC250_IOCTL_KIQ_BIOS_RING_SUBMIT, *PAMDBC250_IOCTL_KIQ_BIOS_RING_SUBMIT;

/* --- Direct CP IB (Indirect Buffer) test — bypasses KIQ/HQD entirely --- */
#define IOCTL_AMDBC250_GPU_IB_TEST     CTL_CODE_AMDBC250(0x89, METHOD_BUFFERED, FILE_ANY_ACCESS)
/* Reuses AMDBC250_IOCTL_GPU_KIQ_TEST struct (output fields same) */

/* --- Execute PM4 via ring (CPU fill + CP_HQD program + WPTR kick) --- */
#define IOCTL_AMDBC250_EXECUTE_RING_PM4  CTL_CODE_AMDBC250(0x8A, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _AMDBC250_IOCTL_EXECUTE_RING_PM4 {
    /* IN */
    UINT32 Commands[64];        /* PM4 commands (up to 64 DWORDs) */
    UINT32 CommandCount;        /* Number of valid DWORDs in Commands */
    UINT32 TimeoutMs;           /* Max wait ms for RPTR advance (0 = no wait) */

    /* OUT */
    UINT32 Result;              /* 0=OK (RPTR advanced), 1=timeout, 2=error */
    UINT32 WptrBefore;          /* CP_HQD_PQ_WPTR before kick */
    UINT32 WptrAfter;           /* CP_HQD_PQ_WPTR after kick */
    UINT32 RptrBefore;          /* CP_HQD_PQ_RPTR before kick */
    UINT32 RptrAfter;           /* CP_HQD_PQ_RPTR after (or after timeout) */
    UINT32 HqdActive;           /* CP_HQD_ACTIVE readback after programming */
    UINT32 ScratchBefore;       /* SCRATCH_REG0 before */
    UINT32 ScratchAfter;        /* SCRATCH_REG0 after */
    UINT64 RingPa;              /* Ring buffer physical address */
    UINT64 MqdPa;               /* MQD physical address */
    ULONG  RingDwords[4];       /* First 4 DWORDS of ring after write (readback) */
    UINT32 PqCtrlBefore;        /* CP_HQD_PQ_CONTROL (BASE_IDX=0) before write */
    UINT32 PqCtrlAfter;         /* CP_HQD_PQ_CONTROL (BASE_IDX=0) after write */
    UINT32 PqBaseReadback;      /* CP_HQD_PQ_BASE_LO (BASE_IDX=0) readback after write */
    UINT32 SwResult;            /* Software PM4 executor result: 0=OK, 1=failed */
    UINT32 SmuFeaturesMask;     /* SMU enabled features mask (after WakeGfx) */
    UINT32 SmuGfxFreqMhz;       /* GFX frequency in MHz (after WakeGfx, 0=unknown) */
    /* DISPATCH_DIRECT test */
    UINT32 DispatchResult;      /* 0=not attempted, 1=regs set, 2=triggered, 3=activity detected */
    UINT32 GrbmStatusBefore;    /* GRBM_STATUS before dispatch trigger */
    UINT32 GrbmStatusAfter;     /* GRBM_STATUS after dispatch trigger + short poll */
    UINT32 MqdLoadPgmLo;        /* COMPUTE_PGM_LO readback AFTER MQD activation (0=MQD failed) */
    UINT32 PgmLoReadback;       /* COMPUTE_PGM_LO readback after dispatch MMIO write */
    UINT32 PgmHiReadback;       /* COMPUTE_PGM_HI readback after dispatch MMIO write */
    UINT32 TmgMaskReadback;     /* COMPUTE_STATIC_THREAD_MGMT_SE0 readback */
} AMDBC250_IOCTL_EXECUTE_RING_PM4, *PAMDBC250_IOCTL_EXECUTE_RING_PM4;

/* --- 40 CU Unlock --- */
#define IOCTL_AMDBC250_UNLOCK_40CU          CTL_CODE_AMDBC250(0x60, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _AMDBC250_IOCTL_UNLOCK_40CU {
    UINT32 Enable;                 /* IN: 1=enable, 0=disable */
    UINT32 Result;                 /* OUT: 0=fail, 1=success */
    UINT32 SpiWgpMaskBefore;       /* OUT: SPI_PG_ENABLE_STATIC_WGP_MASK before */
    UINT32 SpiWgpMaskAfter;        /* OUT: SPI_PG_ENABLE_STATIC_WGP_MASK after */
    UINT32 CcArrayConfigBefore;    /* OUT: CC_GC_SHADER_ARRAY_CONFIG before */
    UINT32 CcArrayConfigAfter;     /* OUT: CC_GC_SHADER_ARRAY_CONFIG after */
    UINT32 ActiveWgpBefore;        /* OUT: QueryActiveWgp before */
    UINT32 ActiveWgpAfter;         /* OUT: QueryActiveWgp after */
} AMDBC250_IOCTL_UNLOCK_40CU, *PAMDBC250_IOCTL_UNLOCK_40CU;

/* --- Get CU Status --- */
#define IOCTL_AMDBC250_GET_CU_STATUS        CTL_CODE_AMDBC250(0x61, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _AMDBC250_IOCTL_GET_CU_STATUS {
    UINT32 ActiveWgp;              /* OUT: active WGP count */
    UINT32 TotalCUs;               /* OUT: total CU count */
    UINT32 EnabledCUs;             /* OUT: enabled CU count */
    UINT32 SpiWgpMask;             /* OUT: SPI_PG_ENABLE_STATIC_WGP_MASK */
    UINT32 CcArrayConfig;          /* OUT: CC_GC_SHADER_ARRAY_CONFIG */
    UINT32 GrbmStatus;             /* OUT: GRBM_STATUS */
} AMDBC250_IOCTL_GET_CU_STATUS, *PAMDBC250_IOCTL_GET_CU_STATUS;

/* --- GCVM Page Table Setup --- */
#define IOCTL_AMDBC250_GCVM_PT_SETUP        CTL_CODE_AMDBC250(0x62, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _AMDBC250_IOCTL_GCVM_PT_SETUP {
    UINT32 PageTableCount;         /* IN: number of page table pages */
    UINT32 Flags;                  /* IN: setup flags */
    UINT32 Result;                 /* OUT: 0=fail, 1=success */
    UINT64 GpuVirtualAddress;      /* OUT: GPU VA of page table */
    UINT64 PhysicalAddress;        /* OUT: physical address */
} AMDBC250_IOCTL_GCVM_PT_SETUP, *PAMDBC250_IOCTL_GCVM_PT_SETUP;

/* --- SDMA Self Test --- */
#define IOCTL_AMDBC250_SDMA_SELFTEST        CTL_CODE_AMDBC250(0x63, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _AMDBC250_IOCTL_SDMA_SELFTEST {
    UINT32 Result;                 /* OUT: 0=fail, 0x600DCAFE=pass */
    UINT32 Pattern;                /* IN: fill pattern (default 0x600DC0DE) */
    UINT32 Size;                   /* IN: test size in bytes (max 4096) */
} AMDBC250_IOCTL_SDMA_SELFTEST, *PAMDBC250_IOCTL_SDMA_SELFTEST;

#pragma pack(pop)

#endif /* _AMDBC250_IOCTL_H_ */
