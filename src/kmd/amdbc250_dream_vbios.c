/*++

Copyright (c) 2026 AMD BC-250 "Dream Drivers" Project — Version 4.3

Module Name:
    amdbc250_dream_vbios.c

Abstract:
    VBIOS/ATOMBIOS reading and VRAM size detection for AMD BC-250.
    
    BC-250 uses UMA (unified memory architecture) where VRAM is part of
    the 16GB GDDR6 pool. The correct VRAM size and visible aperture are
    determined by:
    1. MC_VM_FB_LOCATION hardware register read (BAR5 + 0x9520)
    2. PCI ROM BAR VBIOS read (fallback)
    3. File-based VBIOS read from \SystemRoot\System32\drivers\bc-250\vbios.bin
    
    VBIOS provides ATOMBIOS tables with FB location, clock/voltage info,
    and display configuration. On BC-250, Linux reads VBIOS from ACPI VFCT.

Environment:
    Kernel mode (IRQL <= DISPATCH_LEVEL)

--*/

#include "amdbc250_dream_kmd.h"

/* VBIOS signature at offset 0 */
#define VBIOS_SIGNATURE         0xAA55

/* ATOMBIOS ROM header signature */
#define ATOM_ROM_SIGNATURE      0xAA55

/* Offset to ATOM_ROM_HEADER pointer in VBIOS */
#define ROM_HEADER_PTR_OFFSET   0x0048

/* PCI ROM BAR config offset */
#define PCI_ROM_BAR_OFFSET      0x0030

/* Firmware info table tags */
#define ATOM_TAG_FW_INFO_V21   0x05  /* FIRMWARE_INFO V2.1 table tag */
#define ATOM_TAG_FW_INFO_V22   0x07  /* FIRMWARE_INFO V2.2 */
#define ATOM_TAG_FW_INFO_V31   0x08  /* FIRMWARE_INFO V3.1 */

/* I/O ports for PCI config space access */
#define PCI_CONFIG_ADDR         0x0CF8
#define PCI_CONFIG_DATA         0x0CFC

/*===========================================================================
  ATOMBIOS ROM Header (minimal version)
===========================================================================*/

typedef struct _ATOM_ROM_HEADER {
    UINT16  Signature;          /* 0xAA55 */
    UINT8   Size;               /* Size in 512-byte blocks */
    UINT8   Checksum;
    UINT8   Reserved[16];
    UINT16  MasterDataTableOffset;
    UINT16  FirmwareInfoTableOffset;
    UINT16  MasterCommandTableOffset;
} ATOM_ROM_HEADER;

/*===========================================================================
  ATOM Firmware Info Table V2.1
===========================================================================*/

typedef struct _ATOM_FW_INFO_V21 {
    UINT8   TableTag;           /* 0x05 for V2.1 */
    UINT8   TableSize;
    UINT32  BootUpDevices;
    UINT32  BootUpCliocks;
    UINT32  BootUpMemCliocks;
    UINT32  BootUpEngineClock;  /* Engine clock in 10kHz units */
    UINT32  BootUpMemoryClock;  /* Memory clock in 10kHz units */
    UINT16  MaxEngineClock;
    UINT16  MaxMemoryClock;
    UINT16  FbBaseLo;           /* Framebuffer base lo (64KB units) */
    UINT16  FbSize;             /* Framebuffer size in 64KB units */
    UINT16  FbBaseHi;           /* Framebuffer base hi */
} ATOM_FW_INFO_V21;

/*===========================================================================
  Read PCI config space via I/O ports
===========================================================================*/

static UINT32
ReadPciConfigViaIoPorts(
    _In_ UINT32 Bus,
    _In_ UINT32 Device,
    _In_ UINT32 Function,
    _In_ UINT32 Offset
    )
{
    WRITE_PORT_ULONG((PULONG)(UINT_PTR)PCI_CONFIG_ADDR,
        0x80000000 | (Bus << 16) | (Device << 11) | (Function << 8) | (Offset & 0xFC));
    return READ_PORT_ULONG((PULONG)(UINT_PTR)PCI_CONFIG_DATA);
}

static VOID
WritePciConfigViaIoPorts(
    _In_ UINT32 Bus,
    _In_ UINT32 Device,
    _In_ UINT32 Function,
    _In_ UINT32 Offset,
    _In_ UINT32 Value
    )
{
    WRITE_PORT_ULONG((PULONG)(UINT_PTR)PCI_CONFIG_ADDR,
        0x80000000 | (Bus << 16) | (Device << 11) | (Function << 8) | (Offset & 0xFC));
    WRITE_PORT_ULONG((PULONG)(UINT_PTR)PCI_CONFIG_DATA, Value);
}

/*===========================================================================
  VBIOS size detection — scan from end of ROM for valid signature
===========================================================================*/

static UINT32
GetVbiosImageSize(
    _In_reads_bytes_(MaxSize) const UINT8 *Image,
    _In_ UINT32 MaxSize
    )
{
    if (MaxSize < 0x10000) return MaxSize;
    for (UINT32 offset = 0x10000; offset < MaxSize; offset += 0x10000) {
        if (*(const UINT16 *)(Image + offset) == VBIOS_SIGNATURE) {
            return offset;
        }
    }
    return MaxSize;
}

/*===========================================================================
  DreamV3ReadVbiosFromFile — Load VBIOS from disk file
===========================================================================*/

static NTSTATUS
DreamV3ReadVbiosFromFile(
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

    RtlInitUnicodeString(&uniName,
        L"\\SystemRoot\\System32\\drivers\\bc-250\\vbios.bin");
    InitializeObjectAttributes(&objAttr, &uniName,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    status = ZwCreateFile(&handle, GENERIC_READ, &objAttr, &ioStatus, NULL,
        FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OPEN,
        FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
    if (!NT_SUCCESS(status)) return status;

    status = ZwQueryInformationFile(handle, &ioStatus, &fileInfo,
        sizeof(fileInfo), FileStandardInformation);
    if (!NT_SUCCESS(status) || fileInfo.EndOfFile.QuadPart == 0) {
        ZwClose(handle);
        return status;
    }

    ULONG fileSize = (ULONG)fileInfo.EndOfFile.QuadPart;
    PUCHAR buffer = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED,
        fileSize, 'vBI');
    if (!buffer) { ZwClose(handle); return STATUS_INSUFFICIENT_RESOURCES; }

    status = ZwReadFile(handle, NULL, NULL, NULL, &ioStatus,
        buffer, fileSize, NULL, NULL);
    ZwClose(handle);

    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(buffer, 'vBI');
        return status;
    }

    *OutData = buffer;
    *OutSize = fileSize;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250-VBIOS: Loaded vbios.bin (%u bytes)\n", fileSize));
    return STATUS_SUCCESS;
}

/*===========================================================================
  DreamV3ReadVbiosFromRomBar — Read VBIOS from PCI ROM BAR
  
  Uses IO port PCI config access to read ROM BAR and map it.
  Falls back to probing legacy ROM region at 0xC0000.
===========================================================================*/

static NTSTATUS
DreamV3ReadVbiosFromRomBar(
    _Out_ PUCHAR *OutData,
    _Out_ ULONG *OutSize
    )
{
    UINT32 bus = 0, dev = 0, func = 0;
    BOOLEAN found = FALSE;

    for (bus = 0; bus < 256 && !found; bus++) {
        for (dev = 0; dev < 32 && !found; dev++) {
            for (func = 0; func < 8 && !found; func++) {
                UINT32 id = ReadPciConfigViaIoPorts(bus, dev, func, 0);
                if ((id & 0xFFFF) == 0x1002 && ((id >> 16) & 0xFFFF) == 0x13FE) {
                    found = TRUE;
                }
            }
        }
    }
    if (!found) return STATUS_NOT_FOUND;

    bus--; dev--; func--;

    UINT32 romBar = ReadPciConfigViaIoPorts(bus, dev, func, PCI_ROM_BAR_OFFSET);

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250-VBIOS: PCI ROM BAR=0x%08X (bus=%u dev=%u func=%u)\n",
        romBar, bus, dev, func));

    if ((romBar & 1) == 0) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
            "AMDBC250-VBIOS: ROM BAR not enabled, enabling...\n"));
        WritePciConfigViaIoPorts(bus, dev, func, PCI_ROM_BAR_OFFSET, romBar | 1);
        romBar = ReadPciConfigViaIoPorts(bus, dev, func, PCI_ROM_BAR_OFFSET);
        if ((romBar & 1) == 0) {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                "AMDBC250-VBIOS: Cannot enable ROM BAR\n"));
            return STATUS_NOT_SUPPORTED;
        }
    }

    PHYSICAL_ADDRESS romPhysAddr;
    romPhysAddr.QuadPart = (ULONG64)(romBar & 0xFFFFF800);

    UINT32 romSize = 0;
    WritePciConfigViaIoPorts(bus, dev, func, PCI_ROM_BAR_OFFSET, 0xFFFFFFFE);
    romSize = ReadPciConfigViaIoPorts(bus, dev, func, PCI_ROM_BAR_OFFSET);
    WritePciConfigViaIoPorts(bus, dev, func, PCI_ROM_BAR_OFFSET, romBar | 1);
    romSize = ~(romSize & 0xFFFFF800) + 1;

    if (romSize == 0 || romSize > 0x20000) romSize = 0x10000;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250-VBIOS: ROM PA=0x%llX size=0x%X\n",
        romPhysAddr.QuadPart, romSize));

    PVOID romVa = MmMapIoSpace(romPhysAddr, romSize, MmNonCached);
    if (!romVa) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
            "AMDBC250-VBIOS: Failed to map ROM BAR\n"));
        return STATUS_UNSUCCESSFUL;
    }

    UINT16 signature = *(volatile UINT16 *)romVa;
    if (signature != VBIOS_SIGNATURE) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
            "AMDBC250-VBIOS: Bad signature 0x%04X at ROM BAR\n", signature));
        MmUnmapIoSpace(romVa, romSize);
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    UINT32 actualSize = GetVbiosImageSize((const UINT8 *)romVa, romSize);
    PUCHAR buffer = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED,
        actualSize, 'vBI');
    if (!buffer) {
        MmUnmapIoSpace(romVa, romSize);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyMemory(buffer, romVa, actualSize);
    MmUnmapIoSpace(romVa, romSize);

    *OutData = buffer;
    *OutSize = actualSize;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250-VBIOS: Read VBIOS from ROM BAR (%u bytes)\n", actualSize));
    return STATUS_SUCCESS;
}

/*===========================================================================
  DreamV3ParseVbiosForVram — Parse ATOMBIOS for VRAM info
===========================================================================*/

static VOID
DreamV3ParseVbiosForVram(
    _In_ const UINT8 *Vbios,
    _In_ UINT32 VbiosSize,
    _Out_ UINT64 *FbBase,
    _Out_ UINT64 *FbSize
    )
{
    *FbBase = 0;
    *FbSize = 0;

    if (VbiosSize < ROM_HEADER_PTR_OFFSET + 2) return;
    if (*(const UINT16 *)Vbios != VBIOS_SIGNATURE) return;

    UINT16 romHeaderOffset = *(const UINT16 *)(Vbios + ROM_HEADER_PTR_OFFSET);
    if (romHeaderOffset == 0 || romHeaderOffset + sizeof(ATOM_ROM_HEADER) > VbiosSize) return;

    const ATOM_ROM_HEADER *romHdr =
        (const ATOM_ROM_HEADER *)(Vbios + romHeaderOffset);

    if (romHdr->FirmwareInfoTableOffset == 0) return;

    UINT16 fwInfoOffset = romHeaderOffset + romHdr->FirmwareInfoTableOffset;
    if (fwInfoOffset + sizeof(ATOM_FW_INFO_V21) > VbiosSize) return;

    const ATOM_FW_INFO_V21 *fwInfo =
        (const ATOM_FW_INFO_V21 *)(Vbios + fwInfoOffset);

    if (fwInfo->TableTag < ATOM_TAG_FW_INFO_V21 ||
        fwInfo->TableTag > ATOM_TAG_FW_INFO_V31) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
            "AMDBC250-VBIOS: Unknown firmware info tag 0x%02X\n",
            fwInfo->TableTag));
        return;
    }

    UINT64 base = ((UINT64)fwInfo->FbBaseHi << 32) |
                   ((UINT64)fwInfo->FbBaseLo << 16);
    UINT64 size = (UINT64)fwInfo->FbSize * 0x10000;

    if (base != 0 && size != 0) {
        *FbBase = base;
        *FbSize = size;
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
            "AMDBC250-VBIOS: ATOM FB base=0x%llX size=0x%llX (%llu MB)\n",
            base, size, size / (1024 * 1024)));
    }
}

/*===========================================================================
  DreamV3ProbeFbLocation — Read MC_VM_FB_LOCATION register for VRAM info
  
  Probes both known register offsets for MC_VM_FB_LOCATION:
  - 0x9520 (from v2.0 hw_extra.h)
  - 0x0520 (from v3.0 dream_hw.h as DWORD offset → BAR5 = 0x1480)
  - 0x0524 (FB location top from dream_hw.h)
===========================================================================*/

static VOID
DreamV3ProbeFbLocation(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _Out_ UINT64 *FbBase,
    _Out_ UINT64 *FbSize
    )
{
    *FbBase = 0;
    *FbSize = 0;

    if (!DevExt->MmioVirtualBase) return;

    UINT32 probeOffsets[] = { 0x9520, 0x1480, 0x0520, 0x0524 };
    UINT32 values[4];
    BOOLEAN anyValid = FALSE;

    for (UINT32 i = 0; i < 4; i++) {
        values[i] = DreamV3ReadRegister(DevExt, probeOffsets[i]);
        if (values[i] != 0 && values[i] != 0xFFFFFFFF) anyValid = TRUE;
    }

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250-VBIOS: MC_VM probe 0x9520=0x%08X 0x1480=0x%08X 0x0520=0x%08X 0x0524=0x%08X\n",
        values[0], values[1], values[2], values[3]));

    if (!anyValid) return;

    if (values[0] != 0 && values[0] != 0xFFFFFFFF) {
        UINT32 fbBase = (values[0] & 0xFF000000) >> 8;  /* in 64KB units */
        UINT32 fbSize = (values[0] & 0x00FFFFFF) * 64;   /* in KB */
        *FbBase = (UINT64)fbBase * 1024;                  /* convert to bytes */
        *FbSize = (UINT64)fbSize * 1024;
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
            "AMDBC250-VBIOS: MC_VM_FB_LOCATION(0x9520): base=0x%llX size=0x%llX (%llu MB)\n",
            *FbBase, *FbSize, *FbSize / (1024 * 1024)));
    }
}

/*===========================================================================
  DreamV3DetectVram — Main VRAM detection entry point
  
  Tries multiple sources in order:
  1. MC_VM_FB_LOCATION hardware register
  2. VBIOS from file (vbios.bin)
  3. VBIOS from PCI ROM BAR
  4. PCI BAR descriptor size (existing heuristic)
  5. Hardcoded 16GB fallback
  
  Updates DevExt->TotalVramBytes, FbSize, FbPhysicalBase, VisibleVramBytes.
===========================================================================*/

NTSTATUS
DreamV3DetectVram(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    )
{
    UINT64 fbBase = 0;
    UINT64 fbSize = 0;
    NTSTATUS Status = STATUS_SUCCESS;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250-VBIOS: VRAM detection\n"));

    /* Method 1: MC_VM_FB_LOCATION hardware register */
    DreamV3ProbeFbLocation(DevExt, &fbBase, &fbSize);

    /* Method 2: VBIOS from file */
    if (fbSize == 0) {
        PUCHAR vbiosData = NULL;
        ULONG vbiosSize = 0;
        NTSTATUS vbiosStatus = DreamV3ReadVbiosFromFile(&vbiosData, &vbiosSize);

        if (NT_SUCCESS(vbiosStatus)) {
            UINT64 fileBase = 0, fileSize = 0;
            DreamV3ParseVbiosForVram(vbiosData, vbiosSize, &fileBase, &fileSize);
            ExFreePoolWithTag(vbiosData, 'vBI');

            if (fileSize > 0) {
                fbBase = fileBase;
                fbSize = fileSize;
            }
        }
    }

    /* Method 3: VBIOS from PCI ROM BAR */
    if (fbSize == 0) {
        PUCHAR vbiosData = NULL;
        ULONG vbiosSize = 0;
        NTSTATUS vbiosStatus = DreamV3ReadVbiosFromRomBar(&vbiosData, &vbiosSize);

        if (NT_SUCCESS(vbiosStatus)) {
            UINT64 romBase = 0, romSize = 0;
            DreamV3ParseVbiosForVram(vbiosData, vbiosSize, &romBase, &romSize);
            ExFreePoolWithTag(vbiosData, 'vBI');

            if (romSize > 0) {
                fbBase = romBase;
                fbSize = romSize;
            }
        }
    }

    /* Apply detected VRAM values */
    if (fbSize > 0) {
        DevExt->FbPhysicalBase.QuadPart = fbBase;
        DevExt->FbSize = (SIZE_T)fbSize;
    }

    DevExt->TotalVramBytes = DevExt->FbSize;
    if (DevExt->TotalVramBytes == 0) {
        DevExt->TotalVramBytes = 16ULL * 1024 * 1024 * 1024;
    }

    if (DevExt->FbVisibleSize > 0) {
        DevExt->VisibleVramBytes = DevExt->FbVisibleSize;
    }
    if (DevExt->VisibleVramBytes == 0) {
        DevExt->VisibleVramBytes = min(
            10ULL * 1024 * 1024 * 1024, DevExt->TotalVramBytes);
    }

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250-VBIOS: VRAM total=%llu MB visible=%llu MB FB=0x%llX\n",
        DevExt->TotalVramBytes / (1024 * 1024),
        DevExt->VisibleVramBytes / (1024 * 1024),
        DevExt->FbPhysicalBase.QuadPart));

    return Status;
}