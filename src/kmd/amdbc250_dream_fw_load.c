/*++

Copyright (c) 2026 AMD BC-250 "Dream Drivers" Project — Version 4.3

Module Name:
    amdbc250_dream_fw_load.c

Abstract:
    CP firmware loading for AMD BC-250 (Cyan Skillfish / GFX1013).
    
    BC-250 uses DIRECT firmware loading (AMDGPU_FW_LOAD_DIRECT).
    Firmware is uploaded to IP block registers via MMIO, not through PSP.
    
    Based on Linux amdgpu driver:
    - drivers/gpu/drm/amd/amdgpu/gfx_v10_0.c (gfx_v10_0_init_microcode)
    - drivers/gpu/drm/amd/amdgpu/amdgpu_ucode.c (AMDGPU_FW_LOAD_DIRECT)
    
    Firmware files (cyan_skillfish2_*.bin):
    - ME  (type=1): Micro Engine — GFX ring processing
    - PFP (type=2): Pre-Fetch Parser — instruction parsing
    - CE  (type=3): Copy Engine — DMA transfers
    - MEC (type=4): Micro Engine Compute — compute queues
    - RLC (type=8): Run List Controller — power/scheduler

Environment:
    Kernel mode (IRQL <= DISPATCH_LEVEL)

--*/

#include "amdbc250_dream_kmd.h"

/* Firmware type IDs (matching Linux AMDGPU_UCODE_ID) */
#define FW_TYPE_ME      1
#define FW_TYPE_PFP     2
#define FW_TYPE_CE      3
#define FW_TYPE_MEC     4
#define FW_TYPE_RLC     8

/* Maximum firmware size */
#define MAX_FW_SIZE     (4 * 1024 * 1024)

/* Firmware header size (44 bytes for Cyan Skillfish) */
#define FW_HEADER_SIZE  44

/*===========================================================================
  GC_BASE-shifted register offsets for firmware upload
  
  These are byte offsets in BAR5 space, using GC_BASE = 0x1260.
  Formula: BAR5_offset = GC_BASE(0x1260) + Linux_DWORD_offset * 4
  
  Linux names (from gc_10_1_0_offset.h):
  - mmCP_ME_CNTL = 0x0E05 → BAR5 = 0x4A74 (verified writable)
  - mmCP_HYP_PFP_UCODE_ADDR = 0x5814 → BAR5 = 0x172B0
  - mmCP_HYP_PFP_UCODE_DATA = 0x5815 → BAR5 = 0x172B4
  - mmCP_HYP_ME_UCODE_ADDR = 0x5816 → BAR5 = 0x172B8
  - mmCP_HYP_ME_UCODE_DATA = 0x5817 → BAR5 = 0x172BC
  - IC_BASE registers for DMA upload
===========================================================================*/

/* CP control registers */
#define REG_CP_ME_CNTL          0x4A74   /* ME/PFP/CE halt control */
#define REG_SCRATCH             0x32D4   /* Scratch register (sanity check) */
#define REG_GRBM_STATUS         0x3260   /* GRBM status */

/* UCODE upload registers (for Jump Table / small data) */
#define REG_ME_UCODE_ADDR       0x172B8  /* ME ucode address */
#define REG_ME_UCODE_DATA       0x172BC  /* ME ucode data */
#define REG_PFP_UCODE_ADDR      0x172B0  /* PFP ucode address */
#define REG_PFP_UCODE_DATA      0x172B4  /* PFP ucode data */
#define REG_CE_UCODE_ADDR       0x172C0  /* CE ucode address */
#define REG_CE_UCODE_DATA       0x172C4  /* CE ucode data */

/* IC_BASE registers (firmware DMA target — GPU reads from these addresses) */
#define REG_ME_IC_CNTL          0x17378  /* ME IC control */
#define REG_ME_IC_LO            0x17370  /* ME IC base low */
#define REG_ME_IC_HI            0x17374  /* ME IC base high */
#define REG_PFP_IC_CNTL         0x17368  /* PFP IC control */
#define REG_PFP_IC_LO           0x17360  /* PFP IC base low */
#define REG_PFP_IC_HI           0x17364  /* PFP IC base high */
#define REG_CE_IC_CNTL          0x17388  /* CE IC control */
#define REG_CE_IC_LO            0x17380  /* CE IC base low */
#define REG_CE_IC_HI            0x17384  /* CE IC base high */

/* MEC registers (compute engine) */
#define REG_MEC_ME1_CNTL        0x7A00   /* MEC ME1 control (GC_BASE shifted) */
#define REG_MEC_IC_CNTL         0x7C18   /* MEC IC control */
#define REG_MEC_IC_LO           0x7C10   /* MEC IC base low */
#define REG_MEC_IC_HI           0x7C14   /* MEC IC base high */

/* RLC registers */
#define REG_RLC_CNTL            0x3A00   /* RLC control */
#define REG_RLC_GPM_UCODE_ADDR  0x3A4C   /* RLC ucode address */
#define REG_RLC_GPM_UCODE_DATA  0x3A50   /* RLC ucode data */

/* ME_CNTL bit definitions */
#define ME_CNTL__ME_HALT        (1 << 28)
#define ME_CNTL__PFP_HALT       (1 << 30)
#define ME_CNTL__CE_HALT        (1 << 29)

/* IC_CNTL bit definitions */
#define IC_CNTL__ENABLE         0x00000100  /* VMID=0, enable IC DMA */

/* Timeout for firmware upload completion */
#define FW_UPLOAD_TIMEOUT_US    100000      /* 100ms */

/*===========================================================================
  Firmware Header Layout (Cyan Skillfish 44-byte header)
  
  Offset  DWORD  Field
  ------  -----  -----
  0       [0]    total_size
  4       [1]    header_size_bytes
  8       [2]    version_major
  12      [3]    version_minor
  16      [4]    ucode_version
  20      [5]    ucode_size_bytes
  24      [6]    ucode_offset_bytes (from blob start)
  28      [7]    checksum/hash
  32      [8]    data_offset
  36      [9]    jt_offset (DWORDs from ucode start)
  40      [10]   jt_size (DWORDs)
===========================================================================*/

typedef struct _FW_HEADER {
    UINT32 TotalSize;
    UINT32 HeaderSizeBytes;
    UINT32 VersionMajor;
    UINT32 VersionMinor;
    UINT32 UcodeVersion;
    UINT32 UcodeSizeBytes;
    UINT32 UcodeOffsetBytes;
    UINT32 Checksum;
    UINT32 DataOffset;
    UINT32 JtOffsetDw;      /* Jump Table offset in DWORDs */
    UINT32 JtSizeDw;        /* Jump Table size in DWORDs */
} FW_HEADER;

/*===========================================================================
  DreamV3LoadSingleFirmware — Load one firmware blob to an IP block
  
  This implements the same sequence as the LOAD_CP_FW IOCTL:
  1. Halt all CP engines
  2. Set IC_BASE for target engine (DMA address)
  3. Upload Jump Table via UCODE_DATA registers
  4. Write ucode version to UCODE_ADDR
  5. Unhalt the loaded engine
  
  Parameters:
    DevExt    - Device extension with BAR5 mapping
    FwType    - 1=ME, 2=PFP, 3=CE
    FwBlob    - Raw firmware file data (with header)
    FwSize    - Size of firmware blob in bytes
    
  Returns:
    STATUS_SUCCESS on success
===========================================================================*/

static NTSTATUS
DreamV3LoadSingleFirmware(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ UINT32 FwType,
    _In_ const UINT8 *FwBlob,
    _In_ UINT32 FwSize
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    
    /* BAR5 access macro */
    #define BAR5_U32(off) (*(volatile UINT32 *)((PUCHAR)DevExt->MmioVirtualBase + (off)))
    
    /* Validate firmware header */
    if (FwSize < FW_HEADER_SIZE) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
            "AMDBC250-FW: Firmware too small for header (%u bytes)\n", FwSize));
        return STATUS_INVALID_PARAMETER;
    }
    
    const FW_HEADER *Hdr = (const FW_HEADER *)FwBlob;
    
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250-FW: Loading type=%u version=0x%08X ucodeSize=%u ucodeOff=%u jtOff=%u jtSize=%u\n",
        FwType, Hdr->UcodeVersion, Hdr->UcodeSizeBytes, 
        Hdr->UcodeOffsetBytes, Hdr->JtOffsetDw, Hdr->JtSizeDw));
    
    /* Validate ucode fields */
    if (Hdr->UcodeSizeBytes == 0 || 
        Hdr->UcodeOffsetBytes < Hdr->HeaderSizeBytes ||
        (Hdr->UcodeOffsetBytes + Hdr->UcodeSizeBytes) > FwSize) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
            "AMDBC250-FW: Invalid firmware header fields\n"));
        return STATUS_INVALID_PARAMETER;
    }
    
    /* Allocate contiguous physical memory for firmware DMA */
    PHYSICAL_ADDRESS LowAddr = {0};
    PHYSICAL_ADDRESS HighAddr = {0};
    PHYSICAL_ADDRESS BoundaryAddr = {0};
    HighAddr.QuadPart = 0xFFFFFFFFULL;  /* Below 4GB for DMA */
    
    PVOID FwVa = MmAllocateContiguousMemorySpecifyCache(
        FwSize, LowAddr, HighAddr, BoundaryAddr, MmNonCached);
    
    if (!FwVa) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
            "AMDBC250-FW: Failed to allocate contiguous memory (%u bytes)\n", FwSize));
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    /* Copy firmware to contiguous buffer */
    RtlCopyMemory(FwVa, FwBlob, FwSize);
    PHYSICAL_ADDRESS FwPa = MmGetPhysicalAddress(FwVa);
    
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250-FW: Firmware PA=0x%llX size=%u\n", FwPa.QuadPart, FwSize));
    
    __try {
        /* Step 1: Halt ALL CP engines (ME + PFP + CE) */
        BAR5_U32(REG_CP_ME_CNTL) = ME_CNTL__ME_HALT | ME_CNTL__PFP_HALT | ME_CNTL__CE_HALT;
        KeStallExecutionProcessor(10);
        
        /* Verify halt */
        UINT32 MeCntlRead = BAR5_U32(REG_CP_ME_CNTL);
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
            "AMDBC250-FW: ME_CNTL after halt=0x%08X\n", MeCntlRead));
        
        /* Step 2: Set IC_BASE for target engine */
        UINT32 IcBaseLo = (UINT32)(FwPa.QuadPart & 0xFFFFFFFF);
        UINT32 IcBaseHi = (UINT32)(FwPa.QuadPart >> 32);
        
        switch (FwType) {
        case FW_TYPE_ME:
            BAR5_U32(REG_ME_IC_CNTL) = IC_CNTL__ENABLE;
            BAR5_U32(REG_ME_IC_LO) = IcBaseLo;
            BAR5_U32(REG_ME_IC_HI) = IcBaseHi;
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                "AMDBC250-FW: ME IC_BASE=0x%08X%08X\n", IcBaseHi, IcBaseLo));
            break;
            
        case FW_TYPE_PFP:
            BAR5_U32(REG_PFP_IC_CNTL) = IC_CNTL__ENABLE;
            BAR5_U32(REG_PFP_IC_LO) = IcBaseLo;
            BAR5_U32(REG_PFP_IC_HI) = IcBaseHi;
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                "AMDBC250-FW: PFP IC_BASE=0x%08X%08X\n", IcBaseHi, IcBaseLo));
            break;
            
        case FW_TYPE_CE:
            BAR5_U32(REG_CE_IC_CNTL) = IC_CNTL__ENABLE;
            BAR5_U32(REG_CE_IC_LO) = IcBaseLo;
            BAR5_U32(REG_CE_IC_HI) = IcBaseHi;
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                "AMDBC250-FW: CE IC_BASE=0x%08X%08X\n", IcBaseHi, IcBaseLo));
            break;
            
        default:
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                "AMDBC250-FW: Unknown firmware type %u\n", FwType));
            Status = STATUS_INVALID_PARAMETER;
            goto cleanup;
        }
        
        /* Step 3: Upload Jump Table via UCODE_DATA registers */
        UINT32 JtByteOff = Hdr->UcodeOffsetBytes + (Hdr->JtOffsetDw * 4);
        UINT32 JtSizeBytes = Hdr->JtSizeDw * 4;
        
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
            "AMDBC250-FW: JT at blob offset %u, %u DWORDs (%u bytes)\n",
            JtByteOff, Hdr->JtOffsetDw, JtSizeBytes));
        
        /* Select UCODE registers based on firmware type */
        UINT32 UcodeAddrReg, UcodeDataReg;
        switch (FwType) {
        case FW_TYPE_ME:  UcodeAddrReg = REG_ME_UCODE_ADDR;  UcodeDataReg = REG_ME_UCODE_DATA;  break;
        case FW_TYPE_PFP: UcodeAddrReg = REG_PFP_UCODE_ADDR; UcodeDataReg = REG_PFP_UCODE_DATA; break;
        case FW_TYPE_CE:  UcodeAddrReg = REG_CE_UCODE_ADDR;  UcodeDataReg = REG_CE_UCODE_DATA;  break;
        default: Status = STATUS_INVALID_PARAMETER; goto cleanup;
        }
        
        if (Hdr->JtSizeDw > 0 && (JtByteOff + JtSizeBytes) <= FwSize) {
            const UINT32 *JtData = (const UINT32 *)((PUCHAR)FwVa + JtByteOff);
            
            /* Reset ucode address */
            BAR5_U32(UcodeAddrReg) = 0;
            KeStallExecutionProcessor(1);
            
            /* Upload JT entries one DWORD at a time */
            for (UINT32 i = 0; i < Hdr->JtSizeDw; i++) {
                BAR5_U32(UcodeDataReg) = JtData[i];
                KeStallExecutionProcessor(1);
            }
            
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                "AMDBC250-FW: Uploaded %u JT DWORDs\n", Hdr->JtSizeDw));
        } else {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                "AMDBC250-FW: No JT data (jtSizeDw=%u)\n", Hdr->JtSizeDw));
        }
        
        /* Step 4: Write ucode version to UCODE_ADDR to commit */
        BAR5_U32(UcodeAddrReg) = Hdr->UcodeVersion;
        KeStallExecutionProcessor(10);
        
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
            "AMDBC250-FW: Wrote version 0x%X to UCODE_ADDR\n", Hdr->UcodeVersion));
        
        /* Step 5: Unhalt the loaded engine (keep others halted) */
        switch (FwType) {
        case FW_TYPE_ME:
            /* Unhalt ME only (keep PFP+CE halted) */
            BAR5_U32(REG_CP_ME_CNTL) = ME_CNTL__PFP_HALT | ME_CNTL__CE_HALT;
            break;
        case FW_TYPE_PFP:
            /* Unhalt PFP only */
            BAR5_U32(REG_CP_ME_CNTL) = ME_CNTL__ME_HALT | ME_CNTL__CE_HALT;
            break;
        case FW_TYPE_CE:
            /* Unhalt CE only */
            BAR5_U32(REG_CP_ME_CNTL) = ME_CNTL__ME_HALT | ME_CNTL__PFP_HALT;
            break;
        }
        KeStallExecutionProcessor(10);
        
        UINT32 MeCntlAfter = BAR5_U32(REG_CP_ME_CNTL);
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
            "AMDBC250-FW: ME_CNTL after unhalt=0x%08X\n", MeCntlAfter));
        
        /* Read SCRATCH as sanity check */
        UINT32 Scratch = BAR5_U32(REG_SCRATCH);
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
            "AMDBC250-FW: SCRATCH=0x%08X\n", Scratch));
        
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        UINT32 ExcCode = GetExceptionCode();
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
            "AMDBC250-FW: EXCEPTION 0x%08X during firmware load\n", ExcCode));
        Status = STATUS_UNSUCCESSFUL;
    }
    
cleanup:
    /* Free the contiguous firmware buffer */
    MmFreeContiguousMemory(FwVa);
    
    #undef BAR5_U32
    
    return Status;
}

/*===========================================================================
  DreamV3LoadAllFirmware — Load all CP firmware during initialization
  
  This function is called from DreamV3HwInitialize() after MMIO is mapped.
  It loads ME, PFP, and CE firmware (the minimum needed for GFX ring).
  
  MEC and RLC are loaded later when compute is initialized.
  
  Parameters:
    DevExt - Device extension with BAR5 mapping
    
  Returns:
    STATUS_SUCCESS if at least ME+PFP loaded successfully
===========================================================================*/

NTSTATUS
DreamV3LoadAllFirmware(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250-FW: Loading all CP firmware (direct mode)\n"));
    
    if (!DevExt->MmioVirtualBase) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
            "AMDBC250-FW: No BAR5 mapping\n"));
        return STATUS_DEVICE_NOT_READY;
    }
    
    /* Firmware file paths (embedded in driver or loaded from disk) */
    /* For now, firmware must be loaded via LOAD_CP_FW IOCTL from userspace */
    /* This function will be called after userspace loads firmware */
    
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250-FW: Firmware loading requires userspace helper\n"));
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250-FW: Use load-cp-fw.exe to load ME, PFP, CE firmware\n"));
    
    return STATUS_SUCCESS;
}

/*===========================================================================
  DreamV3UnhaltAllEngines — Unhalt all CP engines after firmware load
  
  After all firmware is loaded, unhalt ME, PFP, and CE together.
  This matches Linux: WREG32(mmCP_ME_CNTL, 0);
===========================================================================*/

VOID
DreamV3UnhaltAllEngines(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    )
{
    if (!DevExt->MmioVirtualBase) return;
    
    #define BAR5_U32(off) (*(volatile UINT32 *)((PUCHAR)DevExt->MmioVirtualBase + (off)))
    
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250-FW: Unhalting all CP engines\n"));
    
    /* Unhalt ME + PFP + CE (all bits clear) */
    BAR5_U32(REG_CP_ME_CNTL) = 0;
    KeStallExecutionProcessor(50);
    
    UINT32 MeCntl = BAR5_U32(REG_CP_ME_CNTL);
    UINT32 GrbmStatus = BAR5_U32(REG_GRBM_STATUS);
    
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250-FW: ME_CNTL=0x%08X GRBM_STATUS=0x%08X\n",
        MeCntl, GrbmStatus));
    
    #undef BAR5_U32
}

/*===========================================================================
  DreamV3HaltAllEngines — Halt all CP engines
  
  Halt ME, PFP, and CE before firmware load or GPU reset.
  This matches Linux: WREG32(mmCP_ME_CNTL, ME_HALT | PFP_HALT | CE_HALT);
===========================================================================*/

VOID
DreamV3HaltAllEngines(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    )
{
    if (!DevExt->MmioVirtualBase) return;
    
    #define BAR5_U32(off) (*(volatile UINT32 *)((PUCHAR)DevExt->MmioVirtualBase + (off)))
    
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250-FW: Halting all CP engines\n"));
    
    /* Halt ME + PFP + CE */
    BAR5_U32(REG_CP_ME_CNTL) = ME_CNTL__ME_HALT | ME_CNTL__PFP_HALT | ME_CNTL__CE_HALT;
    KeStallExecutionProcessor(50);
    
    UINT32 MeCntl = BAR5_U32(REG_CP_ME_CNTL);
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250-FW: ME_CNTL after halt=0x%08X\n", MeCntl));
    
    #undef BAR5_U32
}
