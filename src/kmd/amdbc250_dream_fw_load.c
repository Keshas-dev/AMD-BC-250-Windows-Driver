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
#include "amdbc250_psp.h"

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
#define REG_MEC_IC_CNTL         0x17398   /* MEC IC control */
#define REG_MEC_IC_LO           0x17390   /* MEC IC base low */
#define REG_MEC_IC_HI           0x17394   /* MEC IC base high */

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
  DreamV3LoadAllFirmware — Load all CP firmware during initialization
   
  This function is called from DreamV3HwInitialize() after MMIO is mapped.
  It loads ME, PFP, CE, MEC, MEC2, SDMA0, and SDMA1 firmware from disk
  via IC_BASE DMA, matching Linux AMDGPU_FW_LOAD_DIRECT.
   
  Firmware files are read from C:\Windows\System32\drivers\bc-250\ which
  is populated by the PSP driver's INF during installation.
   
  Parameters:
    DevExt - Device extension with BAR5 mapping
     
  Returns:
    STATUS_SUCCESS if at least ME+PFP loaded successfully
===========================================================================*/

typedef struct _FW_LOAD_ENTRY {
    UINT32 FwType;
    PCWSTR FileName;
} FW_LOAD_ENTRY;

static const FW_LOAD_ENTRY g_FwLoadTable[] = {
    { FW_TYPE_ME,  L"\\SystemRoot\\System32\\drivers\\bc-250\\cyan_skillfish2_me.bin" },
    { FW_TYPE_PFP, L"\\SystemRoot\\System32\\drivers\\bc-250\\cyan_skillfish2_pfp.bin" },
    { FW_TYPE_CE,  L"\\SystemRoot\\System32\\drivers\\bc-250\\cyan_skillfish2_ce.bin" },
    { FW_TYPE_MEC, L"\\SystemRoot\\System32\\drivers\\bc-250\\cyan_skillfish2_mec.bin" },
};

static NTSTATUS
DreamV3LoadFirmwareFromFile(
    _In_ PCWSTR FileName,
    _Out_ PUCHAR *OutData,
    _Out_ ULONG *OutSize
    )
{
    NTSTATUS status;
    OBJECT_ATTRIBUTES objAttr;
    UNICODE_STRING uniName;
    IO_STATUS_BLOCK ioStatus;
    HANDLE handle = NULL;
    FILE_STANDARD_INFORMATION fileInfo;

    RtlInitUnicodeString(&uniName, FileName);
    InitializeObjectAttributes(&objAttr, &uniName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    status = ZwCreateFile(&handle, GENERIC_READ, &objAttr, &ioStatus, NULL,
        FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
    if (!NT_SUCCESS(status)) return status;

    status = ZwQueryInformationFile(handle, &ioStatus, &fileInfo, sizeof(fileInfo), FileStandardInformation);
    if (!NT_SUCCESS(status) || fileInfo.EndOfFile.QuadPart == 0) {
        ZwClose(handle);
        return status;
    }

    ULONG fileSize = (ULONG)fileInfo.EndOfFile.QuadPart;
    PUCHAR buffer = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, fileSize, 'fw');
    if (!buffer) { ZwClose(handle); return STATUS_INSUFFICIENT_RESOURCES; }

    status = ZwReadFile(handle, NULL, NULL, NULL, &ioStatus, buffer, fileSize, NULL, NULL);
    ZwClose(handle);

    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(buffer, 'fw');
        return status;
    }

    *OutData = buffer;
    *OutSize = fileSize;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250-FW: Loaded '%wZ' (%u bytes)\n", &uniName, fileSize));
    return STATUS_SUCCESS;
}

NTSTATUS
DreamV3LoadAllFirmware(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    UINT32 loadedCount = 0;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250-FW: Loading all CP firmware via PSP KIQ mailbox (LOAD_IP_FW)\n"));

    /* We deliberately do NOT use the host IC_BASE DMA path (DreamV3LoadSingleFirmware):
     * on BC-250 the host cannot un-halt the CP after loading microcode — doing so
     * makes the GPU run the loaded firmware and perform a rogue host DMA write,
     * which corrupts system memory (0x1A MEMORY_MANAGEMENT). Instead we hand the
     * firmware blob to the PSP driver, which loads it through the SOS secure
     * mailbox (GFX_CMD_ID_LOAD_IP_FW) — the same path Linux uses for this ASIC.
     * The PSP/SOS owns CP/ring bring-up; the host must not touch CP_ME_CNTL. */

    /* Ensure the PSP proxy is open (initializes SOS context). */
    if (!NT_SUCCESS(Amdbc250PspKiqInit())) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
            "AMDBC250-FW: PSP/SOS proxy init failed — cannot load firmware via mailbox\n"));
        return STATUS_DEVICE_NOT_READY;
    }

    if (!Amdbc250PspGetContext() || !Amdbc250PspGetContext()->SosAlive) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
            "AMDBC250-FW: PSP/SOS not available — cannot load firmware via mailbox\n"));
        return STATUS_DEVICE_NOT_READY;
    }

    for (UINT32 i = 0; i < ARRAYSIZE(g_FwLoadTable); i++) {
        PUCHAR fwData = NULL;
        ULONG fwSize = 0;

        NTSTATUS loadStatus = DreamV3LoadFirmwareFromFile(
            g_FwLoadTable[i].FileName, &fwData, &fwSize);

        if (!NT_SUCCESS(loadStatus)) {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                "AMDBC250-FW: Failed to load type=%u from file (0x%08X)\n",
                g_FwLoadTable[i].FwType, loadStatus));
            continue;
        }

        /* Copy blob into a contiguous shared buffer the PSP can DMA from. */
        NTSTATUS bufStatus = Amdbc250PspAllocateFirmwareBuffer(fwSize);
        if (!NT_SUCCESS(bufStatus)) {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                "AMDBC250-FW: Failed to alloc fw buffer for type=%u (0x%08X)\n",
                g_FwLoadTable[i].FwType, bufStatus));
            ExFreePoolWithTag(fwData, 'fw');
            continue;
        }
        bufStatus = Amdbc250PspCopyFirmwareData(fwData, fwSize);
        ExFreePoolWithTag(fwData, 'fw');
        if (!NT_SUCCESS(bufStatus)) {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                "AMDBC250-FW: Failed to copy fw data for type=%u (0x%08X)\n",
                g_FwLoadTable[i].FwType, bufStatus));
            continue;
        }

        PHYSICAL_ADDRESS fwPa = Amdbc250PspFirmwarePa();

        NTSTATUS fwStatus = Amdbc250PspKiqLoadFirmware(
            g_FwLoadTable[i].FwType, fwSize, fwPa);

        if (NT_SUCCESS(fwStatus)) {
            loadedCount++;
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                "AMDBC250-FW: Loaded type=%u via PSP OK\n", g_FwLoadTable[i].FwType));
        } else {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                "AMDBC250-FW: PSP load type=%u failed (0x%08X)\n",
                g_FwLoadTable[i].FwType, fwStatus));
        }
    }

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250-FW: Loaded %u/%llu firmware types via PSP mailbox\n",
        loadedCount, (ULONGLONG)ARRAYSIZE(g_FwLoadTable)));

    return (loadedCount > 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
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
