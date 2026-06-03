#include "ntddk.h"
#include "amdbc250_dream_kmd.h"
#include "amdbc250_psp_v11.h"

#define PSP_MMIO_BASE_OFFSET          0x10000
#define PSP_MMIO_SIZE                 0x4000

#define MP0_SMN_C2PMSG_0              0x0000
#define MP0_SMN_C2PMSG_35             0x0088
#define MP0_SMN_C2PMSG_36             0x008C
#define MP0_SMN_C2PMSG_64             0x0100
#define MP0_SMN_C2PMSG_67             0x010C
#define MP0_SMN_C2PMSG_69             0x0114
#define MP0_SMN_C2PMSG_70             0x0118
#define MP0_SMN_C2PMSG_71             0x011C
#define MP0_SMN_C2PMSG_81             0x0144
#define MP0_SMN_C2PMSG_101            0x0194

#define MBOX_TOS_READY_FLAG           0x80000000
#define MBOX_TOS_READY_MASK           0x80000000
#define MBOX_TOS_RESP_FLAG            0x80000000
#define MBOX_TOS_RESP_MASK            0x80000000

#define PSP_BL__LOAD_SYSDRV          0x00000004
#define PSP_BL__LOAD_SOSDRV          0x00000008

#define GFX_CTRL_CMD_ID_DESTROY_RINGS 0x00020000

#define PSP_MAX_WAIT_MS               5000
#define PSP_BOOTLOADER_WAIT_MS        1000
#define PSP_RING_SIZE                 0x1000

static AMDBC250_PSP_CONTEXT g_PspContext = {0};

VOID Amdbc250PspUnmapRegisters(VOID)
{
    if (g_PspContext.MmioBase) {
        MmUnmapIoSpace(g_PspContext.MmioBase, g_PspContext.MmioSize);
        g_PspContext.MmioBase = NULL;
    }
}

ULONG Amdbc250PspReadRegister(ULONG RegisterOffset)
{
    if (!g_PspContext.MmioBase || RegisterOffset >= g_PspContext.MmioSize)
        return 0;
    return READ_REGISTER_ULONG((PULONG)(g_PspContext.MmioBase + RegisterOffset));
}

VOID Amdbc250PspWriteRegister(ULONG RegisterOffset, ULONG Value)
{
    if (!g_PspContext.MmioBase || RegisterOffset >= g_PspContext.MmioSize)
        return;
    WRITE_REGISTER_ULONG((PULONG)(g_PspContext.MmioBase + RegisterOffset), Value);
}

static NTSTATUS Amdbc250PspWaitForRegister(ULONG RegisterOffset, ULONG ExpectedValue, ULONG Mask, ULONG TimeoutMs)
{
    ULONG i;
    ULONG regValue;
    LARGE_INTEGER delay;

    delay.QuadPart = -10000LL;

    for (i = 0; i < TimeoutMs; i++) {
        regValue = Amdbc250PspReadRegister(RegisterOffset);
        if ((regValue & Mask) == (ExpectedValue & Mask))
            return STATUS_SUCCESS;
        KeDelayExecutionThread(KernelMode, FALSE, &delay);
    }

    KdPrint(("Amdbc250Psp: Timeout reg 0x%03X (got 0x%08X, want 0x%08X mask 0x%08X)\n",
             RegisterOffset, regValue, ExpectedValue, Mask));
    return STATUS_TIMEOUT;
}

static NTSTATUS Amdbc250PspAllocateSharedMemory(VOID)
{
    PHYSICAL_ADDRESS low = {0};
    PHYSICAL_ADDRESS high;
    PHYSICAL_ADDRESS skip = {0};

    high.QuadPart = 0x3FFFFFFFFFULL;

    g_PspContext.FirmwareBufferSize = 256 * 1024;

    PMDL mdl = MmAllocatePagesForMdlEx(low, high, skip, g_PspContext.FirmwareBufferSize, MmCached, 0);
    if (!mdl) {
        KdPrint(("Amdbc250Psp: Failed to allocate firmware MDL\n"));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    g_PspContext.FirmwareBuffer = MmMapLockedPagesSpecifyCache(
        mdl, KernelMode, MmCached, NULL, FALSE, NormalPagePriority);
    if (!g_PspContext.FirmwareBuffer) {
        MmFreePagesFromMdl(mdl);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    g_PspContext.FirmwarePhysical = MmGetPhysicalAddress(g_PspContext.FirmwareBuffer);

    g_PspContext.RingSize = PSP_RING_SIZE;
    g_PspContext.RingBuffer = MmAllocateContiguousMemory(g_PspContext.RingSize, low);
    if (!g_PspContext.RingBuffer) {
        KdPrint(("Amdbc250Psp: Failed to allocate ring buffer\n"));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    g_PspContext.RingPhysical = MmGetPhysicalAddress(g_PspContext.RingBuffer);
    g_PspContext.RingWptr = 0;

    KdPrint(("Amdbc250Psp: Firmware VA=0x%p PA=0x%llX, Ring VA=0x%p PA=0x%llX\n",
             g_PspContext.FirmwareBuffer, g_PspContext.FirmwarePhysical.QuadPart,
             g_PspContext.RingBuffer, g_PspContext.RingPhysical.QuadPart));

    return STATUS_SUCCESS;
}

static VOID Amdbc250PspFreeSharedMemory(VOID)
{
    if (g_PspContext.RingBuffer) {
        MmFreeContiguousMemory(g_PspContext.RingBuffer);
        g_PspContext.RingBuffer = NULL;
    }
    if (g_PspContext.FirmwareBuffer) {
        MmUnmapLockedPages(g_PspContext.FirmwareBuffer, NULL);
        g_PspContext.FirmwareBuffer = NULL;
    }
}

static NTSTATUS Amdbc250PspWaitForBootloader(VOID)
{
    return Amdbc250PspWaitForRegister(MP0_SMN_C2PMSG_35, 0x80000000, 0x8000FFFF, PSP_BOOTLOADER_WAIT_MS);
}

static NTSTATUS Amdbc250PspIsSosAlive(PBOOLEAN Alive)
{
    ULONG sol = Amdbc250PspReadRegister(MP0_SMN_C2PMSG_81);
    *Alive = (sol != 0x0);
    return STATUS_SUCCESS;
}

static NTSTATUS Amdbc250PspBootloaderLoadSysdrv(VOID)
{
    NTSTATUS status;
    BOOLEAN alive;

    KdPrint(("Amdbc250Psp: Loading sysdrv...\n"));

    status = Amdbc250PspIsSosAlive(&alive);
    if (!NT_SUCCESS(status) || alive) {
        KdPrint(("Amdbc250Psp: SOS already alive, skip sysdrv\n"));
        return STATUS_SUCCESS;
    }

    status = Amdbc250PspWaitForBootloader();
    if (!NT_SUCCESS(status)) return status;

    if (!g_PspContext.SosFirmware || g_PspContext.SosFirmwareSize == 0) {
        KdPrint(("Amdbc250Psp: No sysdrv firmware\n"));
        return STATUS_NO_SUCH_DEVICE;
    }

    RtlCopyMemory(g_PspContext.FirmwareBuffer, g_PspContext.SosFirmware, g_PspContext.SosFirmwareSize);

    Amdbc250PspWriteRegister(MP0_SMN_C2PMSG_36, (ULONG)(g_PspContext.FirmwarePhysical.QuadPart >> 20));
    Amdbc250PspWriteRegister(MP0_SMN_C2PMSG_35, PSP_BL__LOAD_SYSDRV);

    return Amdbc250PspWaitForBootloader();
}

static NTSTATUS Amdbc250PspBootloaderLoadSos(VOID)
{
    NTSTATUS status;
    BOOLEAN alive;
    ULONG i;
    LARGE_INTEGER delay;

    KdPrint(("Amdbc250Psp: Loading SOS...\n"));

    status = Amdbc250PspIsSosAlive(&alive);
    if (!NT_SUCCESS(status) || alive) {
        KdPrint(("Amdbc250Psp: SOS already alive\n"));
        return STATUS_SUCCESS;
    }

    status = Amdbc250PspWaitForBootloader();
    if (!NT_SUCCESS(status)) return status;

    if (!g_PspContext.SosFirmware || g_PspContext.SosFirmwareSize == 0) {
        KdPrint(("Amdbc250Psp: No SOS firmware\n"));
        return STATUS_NO_SUCH_DEVICE;
    }

    RtlCopyMemory(g_PspContext.FirmwareBuffer, g_PspContext.SosFirmware, g_PspContext.SosFirmwareSize);

    Amdbc250PspWriteRegister(MP0_SMN_C2PMSG_36, (ULONG)(g_PspContext.FirmwarePhysical.QuadPart >> 20));
    Amdbc250PspWriteRegister(MP0_SMN_C2PMSG_35, PSP_BL__LOAD_SOSDRV);

    delay.QuadPart = -200000LL;
    KeDelayExecutionThread(KernelMode, FALSE, &delay);

    for (i = 0; i < 50; i++) {
        delay.QuadPart = -100000LL;
        KeDelayExecutionThread(KernelMode, FALSE, &delay);

        status = Amdbc250PspIsSosAlive(&alive);
        if (!NT_SUCCESS(status)) continue;
        if (alive) {
            KdPrint(("Amdbc250Psp: SOS loaded!\n"));
            g_PspContext.SosAlive = TRUE;
            return STATUS_SUCCESS;
        }
    }

    KdPrint(("Amdbc250Psp: Timeout waiting for SOS\n"));
    return STATUS_TIMEOUT;
}

static NTSTATUS Amdbc250PspRingCreate(VOID)
{
    NTSTATUS status;
    LARGE_INTEGER delay;

    KdPrint(("Amdbc250Psp: Creating ring...\n"));

    status = Amdbc250PspWaitForRegister(MP0_SMN_C2PMSG_64, MBOX_TOS_READY_FLAG, MBOX_TOS_READY_MASK, PSP_MAX_WAIT_MS);
    if (!NT_SUCCESS(status)) return status;

    Amdbc250PspWriteRegister(MP0_SMN_C2PMSG_69, (ULONG)(g_PspContext.RingPhysical.LowPart));
    Amdbc250PspWriteRegister(MP0_SMN_C2PMSG_70, (ULONG)(g_PspContext.RingPhysical.HighPart));
    Amdbc250PspWriteRegister(MP0_SMN_C2PMSG_71, g_PspContext.RingSize);
    Amdbc250PspWriteRegister(MP0_SMN_C2PMSG_64, 0);

    delay.QuadPart = -20000LL;
    KeDelayExecutionThread(KernelMode, FALSE, &delay);

    status = Amdbc250PspWaitForRegister(MP0_SMN_C2PMSG_64, MBOX_TOS_RESP_FLAG, MBOX_TOS_RESP_MASK, PSP_MAX_WAIT_MS);
    if (!NT_SUCCESS(status)) return status;

    g_PspContext.RingWptr = 0;
    KdPrint(("Amdbc250Psp: Ring created\n"));
    return STATUS_SUCCESS;
}

static NTSTATUS Amdbc250PspLoadFirmwareFile(PUCHAR *OutBuffer, PULONG OutSize, const WCHAR *FilePath)
{
    UNICODE_STRING fileName;
    OBJECT_ATTRIBUTES objAttr;
    IO_STATUS_BLOCK ioStatus;
    HANDLE fileHandle;
    FILE_STANDARD_INFORMATION fileInfo;
    PUCHAR buffer;

    RtlInitUnicodeString(&fileName, FilePath);
    InitializeObjectAttributes(&objAttr, &fileName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    NTSTATUS     status = ZwCreateFile(&fileHandle, GENERIC_READ, &objAttr, &ioStatus,
                                   NULL, FILE_ATTRIBUTE_NORMAL, 0, FILE_OPEN,
                                   FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
    if (!NT_SUCCESS(status)) {
        KdPrint(("Amdbc250Psp: Cannot open %wZ (0x%08X)\n", &fileName, status));
        return status;
    }

    status = ZwQueryInformationFile(fileHandle, &ioStatus, &fileInfo,
                                    sizeof(FILE_STANDARD_INFORMATION), FileStandardInformation);
    if (!NT_SUCCESS(status) || fileInfo.EndOfFile.LowPart == 0 || fileInfo.EndOfFile.LowPart > 1024*1024) {
        KdPrint(("Amdbc250Psp: Bad firmware size\n"));
        ZwClose(fileHandle);
        return STATUS_INVALID_FILE_FOR_SECTION;
    }

    buffer = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, fileInfo.EndOfFile.LowPart, 'FrmP');
    if (!buffer) {
        ZwClose(fileHandle);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    status = ZwReadFile(fileHandle, NULL, NULL, NULL, &ioStatus, buffer, fileInfo.EndOfFile.LowPart, NULL, NULL);
    ZwClose(fileHandle);

    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(buffer, 'FrmP');
        return status;
    }

    *OutBuffer = buffer;
    *OutSize = fileInfo.EndOfFile.LowPart;
    KdPrint(("Amdbc250Psp: Loaded %wZ (%u bytes)\n", &fileName, *OutSize));
    return STATUS_SUCCESS;
}

NTSTATUS Amdbc250PspInit(PDEVICE_OBJECT DeviceObject)
{
    NTSTATUS status;
    PDREAM_V3_DEVICE_EXTENSION extension;

    KdPrint(("Amdbc250Psp: Starting PSP init...\n"));

    RtlZeroMemory(&g_PspContext, sizeof(AMDBC250_PSP_CONTEXT));

    extension = (PDREAM_V3_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

    PHYSICAL_ADDRESS pspMmioPhysical;
    pspMmioPhysical.QuadPart = extension->MmioPhysicalBase.QuadPart + PSP_MMIO_BASE_OFFSET;

    g_PspContext.MmioSize = PSP_MMIO_SIZE;
    g_PspContext.MmioBase = (PUCHAR)MmMapIoSpace(pspMmioPhysical, PSP_MMIO_SIZE, MmNonCached);
    if (!g_PspContext.MmioBase) {
        KdPrint(("Amdbc250Psp: Failed to map PSP MMIO at 0x%llX\n", pspMmioPhysical.QuadPart));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    KdPrint(("Amdbc250Psp: PSP MMIO mapped VA=0x%p\n", g_PspContext.MmioBase));

    status = Amdbc250PspAllocateSharedMemory();
    if (!NT_SUCCESS(status)) {
        KdPrint(("Amdbc250Psp: Shared memory alloc failed: 0x%08X\n", status));
        Amdbc250PspUnmapRegisters();
        return status;
    }

    /* Auto-load firmware from third-party\firmware\ directory */
    /* Cyan Skillfish uses navi10 firmware (same PSP v11 IP) */
    if (!g_PspContext.SosFirmware) {
        Amdbc250PspLoadFirmwareFile(&g_PspContext.SosFirmware, &g_PspContext.SosFirmwareSize,
                                   L"\\SystemRoot\\System32\\drivers\\amdgpu\\navi10_sos.bin");
    }
    if (!g_PspContext.AsdFirmware) {
        Amdbc250PspLoadFirmwareFile(&g_PspContext.AsdFirmware, &g_PspContext.AsdFirmwareSize,
                                   L"\\SystemRoot\\System32\\drivers\\amdgpu\\navi10_asd.bin");
    }
    if (!g_PspContext.TaFirmware) {
        Amdbc250PspLoadFirmwareFile(&g_PspContext.TaFirmware, &g_PspContext.TaFirmwareSize,
                                   L"\\SystemRoot\\System32\\drivers\\amdgpu\\navi10_ta.bin");
    }

    KdPrint(("Amdbc250Psp: Firmware: SOS=%s (%u), ASD=%s (%u), TA=%s (%u)\n",
             g_PspContext.SosFirmware ? "OK" : "missing", g_PspContext.SosFirmwareSize,
             g_PspContext.AsdFirmware ? "OK" : "missing", g_PspContext.AsdFirmwareSize,
             g_PspContext.TaFirmware ? "OK" : "missing", g_PspContext.TaFirmwareSize));

    status = Amdbc250PspBootloaderLoadSysdrv();
    if (!NT_SUCCESS(status))
        KdPrint(("Amdbc250Psp: Sysdrv load failed: 0x%08X (continuing)\n", status));

    status = Amdbc250PspBootloaderLoadSos();
    if (!NT_SUCCESS(status))
        KdPrint(("Amdbc250Psp: SOS load failed: 0x%08X (continuing)\n", status));

    status = Amdbc250PspRingCreate();
    if (!NT_SUCCESS(status))
        KdPrint(("Amdbc250Psp: Ring create failed: 0x%08X (continuing)\n", status));

    g_PspContext.Initialized = TRUE;
    KdPrint(("Amdbc250Psp: PSP init complete\n"));
    return STATUS_SUCCESS;
}

VOID Amdbc250PspCleanup(VOID)
{
    KdPrint(("Amdbc250Psp: Cleaning up...\n"));

    if (g_PspContext.RingBuffer) {
        ULONG cmd = GFX_CTRL_CMD_ID_DESTROY_RINGS;
        Amdbc250PspWriteRegister(MP0_SMN_C2PMSG_64, cmd);
    }

    if (g_PspContext.SosFirmware) ExFreePoolWithTag(g_PspContext.SosFirmware, 'FrmP');
    if (g_PspContext.AsdFirmware) ExFreePoolWithTag(g_PspContext.AsdFirmware, 'FrmP');
    if (g_PspContext.TaFirmware) ExFreePoolWithTag(g_PspContext.TaFirmware, 'FrmP');

    Amdbc250PspFreeSharedMemory();
    Amdbc250PspUnmapRegisters();

    RtlZeroMemory(&g_PspContext, sizeof(AMDBC250_PSP_CONTEXT));
    KdPrint(("Amdbc250Psp: Cleanup done\n"));
}

NTSTATUS Amdbc250PspSendCommand(ULONG Command, PUCHAR Data, ULONG DataSize)
{
    if (!g_PspContext.Initialized || !g_PspContext.RingBuffer)
        return STATUS_DEVICE_NOT_READY;

    PUCHAR ringBuffer = (PUCHAR)g_PspContext.RingBuffer;

    *(PULONG)(ringBuffer + g_PspContext.RingWptr) = Command;
    if (Data && DataSize > 0 && (g_PspContext.RingWptr + 8 + DataSize) < g_PspContext.RingSize) {
        *(PULONG)(ringBuffer + g_PspContext.RingWptr + 4) = DataSize;
        RtlCopyMemory(ringBuffer + g_PspContext.RingWptr + 8, Data, DataSize);
        g_PspContext.RingWptr += 8 + DataSize;
    } else {
        g_PspContext.RingWptr += 8;
    }

    if (g_PspContext.RingWptr >= g_PspContext.RingSize)
        g_PspContext.RingWptr = 0;

    Amdbc250PspWriteRegister(MP0_SMN_C2PMSG_67, g_PspContext.RingWptr);

    return STATUS_SUCCESS;
}

PAMDBC250_PSP_CONTEXT Amdbc250PspGetContext(VOID)
{
    return &g_PspContext;
}

NTSTATUS Amdbc250PspUnlockNbio(VOID)
{
    NTSTATUS status;
    ULONG cmd;
    ULONG response;
    LARGE_INTEGER delay;

    KdPrint(("Amdbc250Psp: Attempting NBIO unlock...\n"));

    if (!g_PspContext.Initialized) {
        KdPrint(("Amdbc250Psp: PSP not initialized\n"));
        return STATUS_DEVICE_NOT_READY;
    }

    /* Try MODE1_RESET to unlock NBIO */
    KdPrint(("Amdbc250Psp: Sending MODE1_RESET command...\n"));
    
    /* Wait for TOS ready */
    status = Amdbc250PspWaitForRegister(MP0_SMN_C2PMSG_64, MBOX_TOS_READY_FLAG, MBOX_TOS_READY_MASK, PSP_MAX_WAIT_MS);
    if (!NT_SUCCESS(status)) {
        KdPrint(("Amdbc250Psp: TOS not ready for MODE1_RESET\n"));
        return status;
    }

    /* Send MODE1_RESET command */
    cmd = 0x00020000; /* GFX_CTRL_CMD_ID_MODE1_RST */
    Amdbc250PspWriteRegister(MP0_SMN_C2PMSG_64, cmd);

    /* Wait for response */
    delay.QuadPart = -500000LL; /* 500ms */
    KeDelayExecutionThread(KernelMode, FALSE, &delay);

    /* Check response */
    response = Amdbc250PspReadRegister(MP0_SMN_C2PMSG_64);
    KdPrint(("Amdbc250Psp: MODE1_RESET response: 0x%08X\n", response));

    if (response & MBOX_TOS_RESP_FLAG) {
        KdPrint(("Amdbc250Psp: MODE1_RESET successful\n"));
    } else {
        KdPrint(("Amdbc250Psp: MODE1_RESET failed or timed out\n"));
    }

    /* Try to unlock NBIO by writing to signature registers */
    KdPrint(("Amdbc250Psp: Trying NBIO signature unlock...\n"));
    
    /* Write to NBIO signature registers */
    Amdbc250PspWriteRegister(0xC100, 0xFEDCBAEF); /* Magic signature 1 */
    Amdbc250PspWriteRegister(0xC180, 0xFEDCBADF); /* Magic signature 2 */
    
    /* Wait a bit */
    delay.QuadPart = -100000LL; /* 100ms */
    KeDelayExecutionThread(KernelMode, FALSE, &delay);

    /* Check if NBIO is unlocked by reading MMHUB register */
    response = Amdbc250PspReadRegister(0x50D0);
    KdPrint(("Amdbc250Psp: MMHUB[0x50D0] = 0x%08X\n", response));

    if (response != 0x00000000) {
        KdPrint(("Amdbc250Psp: NBIO unlock successful! MMHUB readable\n"));
        return STATUS_SUCCESS;
    }

    KdPrint(("Amdbc250Psp: NBIO unlock failed - MMHUB still unreadable\n"));
    return STATUS_UNSUCCESSFUL;
}
