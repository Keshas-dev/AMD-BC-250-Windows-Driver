#include <ntddk.h>
#include <wdm.h>
#include "firmware_data.h"
#include "amdbc250_psp_v11.h"

#define GPU_BAR5_PHYSICAL             0xFE800000ULL
#define GPU_BAR5_SIZE                 0x80000

#define MP0_C2PMSG_35_BYTE            0x018C
#define MP0_C2PMSG_36_BYTE            0x0190
#define MP0_C2PMSG_64_BYTE            0x0200
#define MP0_C2PMSG_67_BYTE            0x020C
#define MP0_C2PMSG_69_BYTE            0x0214
#define MP0_C2PMSG_70_BYTE            0x0218
#define MP0_C2PMSG_71_BYTE            0x021C
#define MP0_C2PMSG_81_BYTE            0x0244
#define MP0_C2PMSG_101_BYTE           0x0294

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

static ULONG g_Mp0BaseDword = 0;

VOID Amdbc250PspUnmapRegisters(VOID)
{
    if (g_PspContext.MmioBase) {
        MmUnmapIoSpace(g_PspContext.MmioBase, g_PspContext.MmioSize);
        g_PspContext.MmioBase = NULL;
    }
    g_Mp0BaseDword = 0;
}

static ULONG PspReg(ULONG RegByteOffset)
{
    return (g_Mp0BaseDword * 4) + RegByteOffset;
}

ULONG Amdbc250PspReadRegister(ULONG RegisterOffset)
{
    ULONG off = PspReg(RegisterOffset);
    if (!g_PspContext.MmioBase || off >= g_PspContext.MmioSize)
        return 0;
    return READ_REGISTER_ULONG((PULONG)(g_PspContext.MmioBase + off));
}

VOID Amdbc250PspWriteRegister(ULONG RegisterOffset, ULONG Value)
{
    ULONG off = PspReg(RegisterOffset);
    if (!g_PspContext.MmioBase || off >= g_PspContext.MmioSize)
        return;
    WRITE_REGISTER_ULONG((PULONG)(g_PspContext.MmioBase + off), Value);
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
    KdPrint(("BC250-PSP: Timeout reg 0x%03X (got 0x%08X, want 0x%08X mask 0x%08X)\n",
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
    g_PspContext.FirmwareMdl = MmAllocatePagesForMdlEx(low, high, skip, g_PspContext.FirmwareBufferSize, MmCached, 0);
    if (!g_PspContext.FirmwareMdl) return STATUS_INSUFFICIENT_RESOURCES;
    g_PspContext.FirmwareBuffer = MmMapLockedPagesSpecifyCache(
        g_PspContext.FirmwareMdl, KernelMode, MmCached, NULL, FALSE, NormalPagePriority);
    if (!g_PspContext.FirmwareBuffer) { MmFreePagesFromMdl(g_PspContext.FirmwareMdl); return STATUS_INSUFFICIENT_RESOURCES; }
    g_PspContext.FirmwarePhysical = MmGetPhysicalAddress(g_PspContext.FirmwareBuffer);
    g_PspContext.RingSize = PSP_RING_SIZE;
    g_PspContext.RingBuffer = MmAllocateContiguousMemory(g_PspContext.RingSize, low);
    if (!g_PspContext.RingBuffer) return STATUS_INSUFFICIENT_RESOURCES;
    g_PspContext.RingPhysical = MmGetPhysicalAddress(g_PspContext.RingBuffer);
    g_PspContext.RingWptr = 0;
    KdPrint(("BC250-PSP: Firmware VA=0x%p PA=0x%llX, Ring VA=0x%p PA=0x%llX\n",
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
    if (g_PspContext.FirmwareBuffer && g_PspContext.FirmwareMdl) {
        MmUnmapLockedPages(g_PspContext.FirmwareBuffer, g_PspContext.FirmwareMdl);
        MmFreePagesFromMdl(g_PspContext.FirmwareMdl);
        g_PspContext.FirmwareBuffer = NULL;
        g_PspContext.FirmwareMdl = NULL;
    }
}

static NTSTATUS Amdbc250PspWaitForBootloader(VOID)
{
    return Amdbc250PspWaitForRegister(MP0_C2PMSG_35_BYTE, 0x80000000, 0x8000FFFF, PSP_BOOTLOADER_WAIT_MS);
}

static NTSTATUS Amdbc250PspIsSosAlive(PBOOLEAN Alive)
{
    ULONG sol = Amdbc250PspReadRegister(MP0_C2PMSG_81_BYTE);
    *Alive = (sol != 0x0);
    return STATUS_SUCCESS;
}

static NTSTATUS Amdbc250PspDiscoverMp0Base(VOID)
{
    ULONG tryOffsets[] = { 0x00000, 0x04000, 0x08000, 0x10000, 0x14000, 0x18000, 0x1C000, 0x1E000, 0x20000 };
    ULONG i;
    for (i = 0; i < sizeof(tryOffsets) / sizeof(tryOffsets[0]); i++) {
        g_Mp0BaseDword = tryOffsets[i];
        ULONG sol = Amdbc250PspReadRegister(MP0_C2PMSG_81_BYTE);
        if (sol != 0 && sol != 0xFFFFFFFF) {
            KdPrint(("BC250-PSP: MP0 base found at DWORD offset 0x%05X (SOL=0x%08X)\n", tryOffsets[i], sol));
            return STATUS_SUCCESS;
        }
    }
    KdPrint(("BC250-PSP: MP0 base not found (no SOS alive signal)\n"));
    g_Mp0BaseDword = 0;
    return STATUS_NOT_FOUND;
}

static NTSTATUS Amdbc250PspBootloaderLoadSysdrv(VOID)
{
    NTSTATUS status;
    BOOLEAN alive;
    status = Amdbc250PspIsSosAlive(&alive);
    if (!NT_SUCCESS(status) || alive) { return STATUS_SUCCESS; }
    status = Amdbc250PspWaitForBootloader();
    if (!NT_SUCCESS(status)) return status;
    if (!g_PspContext.SosFirmware || g_PspContext.SosFirmwareSize == 0) return STATUS_NO_SUCH_DEVICE;
    RtlCopyMemory(g_PspContext.FirmwareBuffer, g_PspContext.SosFirmware, g_PspContext.SosFirmwareSize);
    Amdbc250PspWriteRegister(MP0_C2PMSG_36_BYTE, (ULONG)(g_PspContext.FirmwarePhysical.QuadPart >> 20));
    Amdbc250PspWriteRegister(MP0_C2PMSG_35_BYTE, PSP_BL__LOAD_SYSDRV);
    return Amdbc250PspWaitForBootloader();
}

static NTSTATUS Amdbc250PspBootloaderLoadSos(VOID)
{
    NTSTATUS status;
    BOOLEAN alive;
    LARGE_INTEGER delay;
    ULONG i;
    status = Amdbc250PspIsSosAlive(&alive);
    if (!NT_SUCCESS(status) || alive) return STATUS_SUCCESS;
    status = Amdbc250PspWaitForBootloader();
    if (!NT_SUCCESS(status)) return status;
    if (!g_PspContext.SosFirmware || g_PspContext.SosFirmwareSize == 0) return STATUS_NO_SUCH_DEVICE;
    RtlCopyMemory(g_PspContext.FirmwareBuffer, g_PspContext.SosFirmware, g_PspContext.SosFirmwareSize);
    Amdbc250PspWriteRegister(MP0_C2PMSG_36_BYTE, (ULONG)(g_PspContext.FirmwarePhysical.QuadPart >> 20));
    Amdbc250PspWriteRegister(MP0_C2PMSG_35_BYTE, PSP_BL__LOAD_SOSDRV);
    delay.QuadPart = -200000LL;
    KeDelayExecutionThread(KernelMode, FALSE, &delay);
    for (i = 0; i < 50; i++) {
        delay.QuadPart = -100000LL;
        KeDelayExecutionThread(KernelMode, FALSE, &delay);
        status = Amdbc250PspIsSosAlive(&alive);
        if (!NT_SUCCESS(status)) continue;
        if (alive) { g_PspContext.SosAlive = TRUE; return STATUS_SUCCESS; }
    }
    return STATUS_TIMEOUT;
}

static NTSTATUS Amdbc250PspRingCreate(VOID)
{
    NTSTATUS status;
    LARGE_INTEGER delay;
    status = Amdbc250PspWaitForRegister(MP0_C2PMSG_64_BYTE, MBOX_TOS_READY_FLAG, MBOX_TOS_READY_MASK, PSP_MAX_WAIT_MS);
    if (!NT_SUCCESS(status)) return status;
    Amdbc250PspWriteRegister(MP0_C2PMSG_69_BYTE, (ULONG)(g_PspContext.RingPhysical.LowPart));
    Amdbc250PspWriteRegister(MP0_C2PMSG_70_BYTE, (ULONG)(g_PspContext.RingPhysical.HighPart));
    Amdbc250PspWriteRegister(MP0_C2PMSG_71_BYTE, g_PspContext.RingSize);
    Amdbc250PspWriteRegister(MP0_C2PMSG_64_BYTE, 0);
    delay.QuadPart = -20000LL;
    KeDelayExecutionThread(KernelMode, FALSE, &delay);
    status = Amdbc250PspWaitForRegister(MP0_C2PMSG_64_BYTE, MBOX_TOS_RESP_FLAG, MBOX_TOS_RESP_MASK, PSP_MAX_WAIT_MS);
    if (!NT_SUCCESS(status)) return status;
    g_PspContext.RingWptr = 0;
    return STATUS_SUCCESS;
}

NTSTATUS Amdbc250PspInit(ULONG64 MmioPhysicalBase)
{
    NTSTATUS status;
    PHYSICAL_ADDRESS pspMmioPhysical;
    /* Use the caller‑provided physical base if supplied; otherwise fall back to the
   historic hard‑coded address. This makes the driver adaptable to platforms
   where the PSP MMIO window is relocated. */
if (MmioPhysicalBase != 0)
    pspMmioPhysical.QuadPart = MmioPhysicalBase;
else
    pspMmioPhysical.QuadPart = GPU_BAR5_PHYSICAL;
    g_PspContext.MmioSize = GPU_BAR5_SIZE;
    g_PspContext.MmioBase = (PUCHAR)MmMapIoSpace(pspMmioPhysical, GPU_BAR5_SIZE, MmNonCached);
    if (!g_PspContext.MmioBase) return STATUS_INSUFFICIENT_RESOURCES;
    status = Amdbc250PspDiscoverMp0Base();
    if (!NT_SUCCESS(status)) {
        KdPrint(("BC250-PSP: MP0 base discovery failed, trying extended scan\n"));
        ULONG tryOffsets2[] = { 0x22000, 0x24000, 0x28000, 0x2C000, 0x30000, 0x34000, 0x38000, 0x3C000 };
        ULONG i;
        for (i = 0; i < sizeof(tryOffsets2) / sizeof(tryOffsets2[0]); i++) {
            g_Mp0BaseDword = tryOffsets2[i];
            ULONG sol = Amdbc250PspReadRegister(MP0_C2PMSG_81_BYTE);
            if (sol != 0 && sol != 0xFFFFFFFF) {
                KdPrint(("BC250-PSP: MP0 base found at DWORD offset 0x%05X (SOL=0x%08X)\n", tryOffsets2[i], sol));
                status = STATUS_SUCCESS;
                break;
            }
        }
        if (!NT_SUCCESS(status)) {
            g_Mp0BaseDword = 0;
            Amdbc250PspUnmapRegisters();
            return STATUS_NOT_FOUND;
        }
    }
    status = Amdbc250PspAllocateSharedMemory();
    if (!NT_SUCCESS(status)) { Amdbc250PspUnmapRegisters(); return status; }
    if (!Amdbc250PspValidateFirmware((PUCHAR)g_SosFirmwareData, sizeof(g_SosFirmwareData), 0)) {
        KdPrint(("BC250-PSP: SOS firmware validation FAILED (size=%zu)\n", sizeof(g_SosFirmwareData)));
        Amdbc250PspFreeSharedMemory(); Amdbc250PspUnmapRegisters(); return STATUS_IMAGE_CHECKSUM_MISMATCH;
    }
    if (!Amdbc250PspValidateFirmware((PUCHAR)g_AsdFirmwareData, sizeof(g_AsdFirmwareData), 1)) {
        KdPrint(("BC250-PSP: ASD firmware validation FAILED (size=%zu)\n", sizeof(g_AsdFirmwareData)));
        Amdbc250PspFreeSharedMemory(); Amdbc250PspUnmapRegisters(); return STATUS_IMAGE_CHECKSUM_MISMATCH;
    }
    if (!Amdbc250PspValidateFirmware((PUCHAR)g_TaFirmwareData, sizeof(g_TaFirmwareData), 2)) {
        KdPrint(("BC250-PSP: TA firmware validation FAILED (size=%zu)\n", sizeof(g_TaFirmwareData)));
        Amdbc250PspFreeSharedMemory(); Amdbc250PspUnmapRegisters(); return STATUS_IMAGE_CHECKSUM_MISMATCH;
    }
    g_PspContext.SosFirmware = (PUCHAR)g_SosFirmwareData;
    g_PspContext.SosFirmwareSize = sizeof(g_SosFirmwareData);
    g_PspContext.AsdFirmware = (PUCHAR)g_AsdFirmwareData;
    g_PspContext.AsdFirmwareSize = sizeof(g_AsdFirmwareData);
    g_PspContext.TaFirmware = (PUCHAR)g_TaFirmwareData;
    g_PspContext.TaFirmwareSize = sizeof(g_TaFirmwareData);
    /* Load the system driver into the PSP */
status = Amdbc250PspBootloaderLoadSysdrv();
if (!NT_SUCCESS(status)) {
    KdPrint(("BC250-PSP: Sysdrv load failed (0x%08X)\n", status));
    goto cleanup;
}

/* Load the SecureOS (SOS) firmware */
status = Amdbc250PspBootloaderLoadSos();
if (!NT_SUCCESS(status)) {
    KdPrint(("BC250-PSP: SOS load failed (0x%08X)\n", status));
    goto cleanup;
}

/* Create the communication ring */
status = Amdbc250PspRingCreate();
if (!NT_SUCCESS(status)) {
    KdPrint(("BC250-PSP: Ring creation failed (0x%08X)\n", status));
    goto cleanup;
}

g_PspContext.Initialized = TRUE;
return STATUS_SUCCESS;

cleanup:
/* Ensure we do not leak any allocated resources on partial failure */
Amdbc250PspFreeSharedMemory();
Amdbc250PspUnmapRegisters();
RtlZeroMemory(&g_PspContext, sizeof(g_PspContext));
return status;
}

VOID Amdbc250PspCleanup(VOID)
{
    if (g_PspContext.RingBuffer) {
        Amdbc250PspWriteRegister(MP0_C2PMSG_64_BYTE, GFX_CTRL_CMD_ID_DESTROY_RINGS);
    }
    Amdbc250PspFreeSharedMemory();
    Amdbc250PspUnmapRegisters();
    RtlZeroMemory(&g_PspContext, sizeof(AMDBC250_PSP_CONTEXT));
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
    Amdbc250PspWriteRegister(MP0_C2PMSG_67_BYTE, g_PspContext.RingWptr);
    return STATUS_SUCCESS;
}

PAMDBC250_PSP_CONTEXT Amdbc250PspGetContext(VOID)
{
    return &g_PspContext;
}

BOOLEAN Amdbc250PspValidateFirmware(PUCHAR FirmwareData, ULONG FirmwareSize, ULONG FirmwareType)
{
    if (FirmwareData == NULL || FirmwareSize < 256)
        return FALSE;

    /* Validate firmware size range for each firmware type */
    switch (FirmwareType) {
    case 0: /* SOS */
        if (FirmwareSize < 1024 || FirmwareSize > 256 * 1024) return FALSE;
        break;
    case 1: /* ASD */
        if (FirmwareSize < 1024 || FirmwareSize > 64 * 1024) return FALSE;
        break;
    case 2: /* TA */
        if (FirmwareSize < 1024 || FirmwareSize > 512 * 1024) return FALSE;
        break;
    default:
        return FALSE;
    }

    /* Verify firmware header: first 4 bytes should be the total size */
    ULONG headerSize = *(volatile ULONG*)FirmwareData;
    if (headerSize == 0 || headerSize > FirmwareSize + 256)
        return FALSE; /* Header size should be close to total size */
    if (headerSize > 256 * 1024)
        return FALSE;

    return TRUE;
}

NTSTATUS Amdbc250PspTryUnlockNbio(VOID)
{
    NTSTATUS status;
    LARGE_INTEGER delay;
    if (!g_PspContext.Initialized) return STATUS_DEVICE_NOT_READY;
    status = Amdbc250PspWaitForRegister(MP0_C2PMSG_64_BYTE, MBOX_TOS_READY_FLAG, MBOX_TOS_READY_MASK, PSP_MAX_WAIT_MS);
    if (!NT_SUCCESS(status)) return status;
    Amdbc250PspWriteRegister(MP0_C2PMSG_64_BYTE, 0x00020000);
    delay.QuadPart = -500000LL;
    KeDelayExecutionThread(KernelMode, FALSE, &delay);
    ULONG response = Amdbc250PspReadRegister(MP0_C2PMSG_64_BYTE);
    Amdbc250PspWriteRegister(0xC100, 0xFEDCBAEF);
    Amdbc250PspWriteRegister(0xC180, 0xFEDCBADF);
    delay.QuadPart = -100000LL;
    KeDelayExecutionThread(KernelMode, FALSE, &delay);
    response = Amdbc250PspReadRegister(0x50D0);
    if (response != 0) return STATUS_SUCCESS;
    return STATUS_UNSUCCESSFUL;
}
