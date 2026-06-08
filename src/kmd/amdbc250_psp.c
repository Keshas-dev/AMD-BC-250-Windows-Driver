#include <ntddk.h>
#include <wdm.h>
#include "amdbc250_psp.h"

#define GPU_BAR5_PHYSICAL             0xFE800000ULL
#define GPU_BAR5_SIZE                 0x80000

/* PSP driver IOCTL codes (must match PspDriver.sys) */
#define PSP_IOCTL_READ_REG    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define PSP_IOCTL_WRITE_REG   CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define PSP_IOCTL_REG_PROG    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x815, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define PSP_IOCTL_GET_GPU_INFO CTL_CODE(FILE_DEVICE_UNKNOWN, 0x816, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define PSP_DEVICE_NAME       L"\\Device\\AmdBcPsp"

static HANDLE g_PspProxyHandle = NULL;
static BOOLEAN g_PspProxyAvailable = FALSE;
static BOOLEAN g_KiqAvailable = FALSE;
static ULONG64 g_KiqRingPa = 0;
static ULONG g_KiqRingSize = 0;
static PVOID g_KiqRingVa = NULL;     /* Mapped KIQ ring VA */
static ULONG g_KiqWptr = 0;           /* Current KIQ write pointer */

/* Initialize PSP proxy - open handle to PSP driver for GPU register access */
static BOOLEAN PspProxyInit(VOID)
{
    NTSTATUS status;
    UNICODE_STRING deviceName;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;

    if (g_PspProxyHandle) return TRUE;

    RtlInitUnicodeString(&deviceName, L"\\DosDevices\\AmdBcPsp");
    InitializeObjectAttributes(&oa, &deviceName, OBJ_KERNEL_HANDLE, NULL, NULL);

    status = ZwCreateFile(&g_PspProxyHandle,
        GENERIC_READ | GENERIC_WRITE, &oa, &iosb, NULL,
        FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE,
        FILE_OPEN_IF, FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);

    if (NT_SUCCESS(status)) {
        g_PspProxyAvailable = TRUE;

        /* Query PSP for GPU/KIQ info */
        ULONG gpuInfo[8] = {0};
        IO_STATUS_BLOCK iosb2;
        status = ZwDeviceIoControlFile(g_PspProxyHandle, NULL, NULL, NULL,
            &iosb2, PSP_IOCTL_GET_GPU_INFO, NULL, 0, gpuInfo, sizeof(gpuInfo));
        if (NT_SUCCESS(status) && gpuInfo[4]) {
            g_KiqRingPa = ((ULONG64)gpuInfo[5] << 32) | gpuInfo[4];
            g_KiqRingSize = gpuInfo[6];
            /* Map KIQ ring into our VA space for direct PM4 writes */
            PHYSICAL_ADDRESS kiqPa;
            kiqPa.QuadPart = g_KiqRingPa;
            g_KiqRingVa = MmMapIoSpace(kiqPa, g_KiqRingSize, MmNonCached);
            if (g_KiqRingVa) {
                g_KiqAvailable = TRUE;
                KdPrint(("BC250-PSP: KIQ ring mapped PA=0x%llX VA=%p size=%u\n",
                    g_KiqRingPa, g_KiqRingVa, g_KiqRingSize));
            }
        } else {
            KdPrint(("BC250-PSP: Proxy opened, KIQ not available (GPU FW status=%u)\n", gpuInfo[0]));
        }
        return TRUE;
    }
    return FALSE;
}

/* Read GPU register via PSP/KIQ proxy */
ULONG Amdbc250PspProxyReadReg(ULONG GpuRegOffset)
{
    ULONG inBuf[3] = { GpuRegOffset, 0, 1 }; /* 1 = read */
    ULONG outBuf[2] = { 0, 0 };
    IO_STATUS_BLOCK iosb;

    if (!g_PspProxyHandle && !PspProxyInit()) {
        return Amdbc250PspReadRegister(GpuRegOffset);
    }

    if (g_KiqAvailable) {
        /* Use KIQ: send register read command via PSP REG_PROG */
        NTSTATUS status = ZwDeviceIoControlFile(g_PspProxyHandle, NULL, NULL, NULL,
            &iosb, PSP_IOCTL_REG_PROG, inBuf, sizeof(inBuf), outBuf, sizeof(outBuf));
        if (NT_SUCCESS(status)) return outBuf[0];
    }

    /* Fallback: direct PSP MMIO read */
    NTSTATUS status = ZwDeviceIoControlFile(g_PspProxyHandle, NULL, NULL, NULL,
        &iosb, PSP_IOCTL_READ_REG, inBuf, sizeof(inBuf), outBuf, sizeof(outBuf));
    if (NT_SUCCESS(status)) return outBuf[0];

    return Amdbc250PspReadRegister(GpuRegOffset);
}

/* Write GPU register via PSP/KIQ proxy */
VOID Amdbc250PspProxyWriteReg(ULONG GpuRegOffset, ULONG Value)
{
    ULONG inBuf[3] = { GpuRegOffset, Value, 0 }; /* 0 = write */
    IO_STATUS_BLOCK iosb;

    if (!g_PspProxyHandle && !PspProxyInit()) {
        Amdbc250PspWriteRegister(GpuRegOffset, Value);
        return;
    }

    if (g_KiqAvailable) {
        ZwDeviceIoControlFile(g_PspProxyHandle, NULL, NULL, NULL,
            &iosb, PSP_IOCTL_REG_PROG, inBuf, 3 * sizeof(ULONG), NULL, 0);
        return;
    }

    /* Fallback: direct PSP MMIO write */
    ZwDeviceIoControlFile(g_PspProxyHandle, NULL, NULL, NULL,
        &iosb, PSP_IOCTL_WRITE_REG, inBuf, 3 * sizeof(ULONG), NULL, 0);
}

/* Check if PSP proxy is available */
BOOLEAN Amdbc250PspProxyAvailable(VOID)
{
    if (!g_PspProxyHandle) PspProxyInit();
    return g_PspProxyAvailable;
}

/* Check if KIQ is available for GPU register access */
BOOLEAN Amdbc250PspKiqAvailable(VOID)
{
    if (!g_PspProxyHandle) PspProxyInit();
    return g_KiqAvailable;
}

/* Submit PM4 commands via KIQ ring + trigger via PSP */
NTSTATUS Amdbc250PspKiqSubmit(ULONG* Pm4Commands, ULONG DwordCount)
{
    IO_STATUS_BLOCK iosb;
    ULONG wptrReg[2] = { 0, 0 };

    if (!g_KiqRingVa || !g_PspProxyHandle) return STATUS_DEVICE_NOT_READY;
    if (DwordCount == 0 || DwordCount > g_KiqRingSize / sizeof(ULONG) - 8)
        return STATUS_INVALID_PARAMETER;

    PUCHAR ringByte = (PUCHAR)g_KiqRingVa;
    ULONG offset = g_KiqWptr % g_KiqRingSize;
    if (offset + DwordCount * sizeof(ULONG) > g_KiqRingSize)
        offset = 0;

    RtlCopyMemory(ringByte + offset, Pm4Commands, DwordCount * sizeof(ULONG));
    g_KiqWptr = offset + DwordCount * sizeof(ULONG);

    wptrReg[1] = g_KiqWptr;
    ZwDeviceIoControlFile(g_PspProxyHandle, NULL, NULL, NULL,
        &iosb, PSP_IOCTL_REG_PROG, wptrReg, sizeof(wptrReg), NULL, 0);

    return STATUS_SUCCESS;
}

/* Read GPU register via KIQ COPY_DATA PM4 packet */
ULONG Amdbc250PspKiqReadReg(ULONG GpuRegOffset)
{
    if (!g_KiqRingVa || !g_PspProxyHandle) return 0xFFFFFFFF;

    /* Reserve 2 DWORDS at end of ring for response */
    ULONG respOffset = (g_KiqRingSize / sizeof(ULONG)) - 2;
    volatile PULONG ring = (volatile PULONG)g_KiqRingVa;
    ring[respOffset] = 0xDEADDEAD;
    ring[respOffset + 1] = 0xDEADDEAD;

    /* Build COPY_DATA PM4 packet: read from GPU reg, write to ring */
    ULONG pkt[8] = {0};
    ULONG64 respVa = g_KiqRingPa + respOffset * sizeof(ULONG);

    /* PACKET3_COPY_DATA: IT_OPCODE=0x41, copies GPU register to memory */
    pkt[0] = 0xC0034100 | 4;  /* IT_COPY_DATA, 4 DWORD payload */
    pkt[1] = GpuRegOffset;     /* SRC_LO (GPU register offset) */
    pkt[2] = 0xFE800000;        /* SRC_HI (BAR5 base) */
    pkt[3] = (ULONG)(respVa & 0xFFFFFFFF);   /* DST_LO */
    pkt[4] = (ULONG)(respVa >> 32);           /* DST_HI */

    /* Submit */
    g_KiqWptr = 0;
    RtlCopyMemory(g_KiqRingVa, pkt, sizeof(pkt));
    IO_STATUS_BLOCK iosb;
    ULONG wptrReg[2] = { 0, sizeof(pkt) };
    ZwDeviceIoControlFile(g_PspProxyHandle, NULL, NULL, NULL,
        &iosb, PSP_IOCTL_REG_PROG, wptrReg, sizeof(wptrReg), NULL, 0);

    /* Wait for response (poll ring) */
    ULONG waited = 0;
    while (waited < 100) {
        if (ring[respOffset] != 0xDEADDEAD) return ring[respOffset];
        KeStallExecutionProcessor(1000); /* 1ms */
        waited++;
    }
    return 0xFFFFFFFF;
}

/* Close PSP proxy handle */
VOID Amdbc250PspProxyCleanup(VOID)
{
    if (g_KiqRingVa) {
        MmUnmapIoSpace(g_KiqRingVa, g_KiqRingSize);
        g_KiqRingVa = NULL;
    }
    if (g_PspProxyHandle) {
        ZwClose(g_PspProxyHandle);
        g_PspProxyHandle = NULL;
        g_PspProxyAvailable = FALSE;
        g_KiqAvailable = FALSE;
    }
}

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
    *Alive = (sol & 0x80000000) ? TRUE : FALSE;
    return STATUS_SUCCESS;
}

static NTSTATUS Amdbc250PspDiscoverMp0Base(VOID)
{
    /* Primary scan: fine-grained check of the 0x0-0x8000 range
       where the actual PSP mailbox registers live.
       The PSP driver confirmed working MP0 base at ~0x40F4. */
    ULONG tryOffsets[] = {
        0x00000, 0x04000, 0x040F0, 0x040F4, 0x040F8, 0x04100, 0x0410C, 0x04200,
        0x04400, 0x04800, 0x05000, 0x06000, 0x08000, 0x10000,
    };
    ULONG i;
    for (i = 0; i < sizeof(tryOffsets) / sizeof(tryOffsets[0]); i++) {
        g_Mp0BaseDword = tryOffsets[i];
        ULONG sol = Amdbc250PspReadRegister(MP0_C2PMSG_81_BYTE);
        if (sol != 0 && sol != 0xFFFFFFFF) {
            /* Verify that C2PMSG_35 is also accessible (writable check) */
            ULONG test35 = Amdbc250PspReadRegister(MP0_C2PMSG_35_BYTE);
            if (test35 != 0xFFFFFFFF) {
                KdPrint(("BC250-PSP: MP0 base found at DWORD offset 0x%05X (SOL=0x%08X, C35=0x%08X)\n",
                    tryOffsets[i], sol, test35));
                return STATUS_SUCCESS;
            }
            KdPrint(("BC250-PSP: Candidate at 0x%05X (SOL=0x%08X) but C2PMSG_35 blocked\n",
                tryOffsets[i], sol));
        }
    }
    /* Fallback: wider scan (original behavior) */
    ULONG tryOffsets2[] = { 0x10000, 0x14000, 0x16000, 0x18000, 0x1C000, 0x1E000, 0x20000,
        0x22000, 0x24000, 0x28000, 0x2C000, 0x30000, 0x34000, 0x38000, 0x3C000 };
    for (i = 0; i < sizeof(tryOffsets2) / sizeof(tryOffsets2[0]); i++) {
        g_Mp0BaseDword = tryOffsets2[i];
        ULONG sol = Amdbc250PspReadRegister(MP0_C2PMSG_81_BYTE);
        if (sol != 0 && sol != 0xFFFFFFFF) {
            KdPrint(("BC250-PSP: MP0 base found (fallback) at DWORD offset 0x%05X (SOL=0x%08X)\n", tryOffsets2[i], sol));
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
    
    /* Use caller-provided physical base if supplied */
    if (MmioPhysicalBase != 0)
        pspMmioPhysical.QuadPart = MmioPhysicalBase;
    else
        pspMmioPhysical.QuadPart = GPU_BAR5_PHYSICAL;
    g_PspContext.MmioSize = GPU_BAR5_SIZE;  /* Safe 512KB - PSP driver handles 2MB */
    g_PspContext.MmioBase = (PUCHAR)MmMapIoSpace(pspMmioPhysical, GPU_BAR5_SIZE, MmNonCached);
    if (!g_PspContext.MmioBase) return STATUS_INSUFFICIENT_RESOURCES;
    
    /* Discover MP0 base for SOL check only */
    status = Amdbc250PspDiscoverMp0Base();
    if (!NT_SUCCESS(status)) {
        ULONG tryOffsets2[] = { 0x22000, 0x24000, 0x28000, 0x2C000, 0x30000, 0x34000, 0x38000, 0x3C000 };
        ULONG i;
        for (i = 0; i < sizeof(tryOffsets2) / sizeof(tryOffsets2[0]); i++) {
            g_Mp0BaseDword = tryOffsets2[i];
            ULONG sol = Amdbc250PspReadRegister(MP0_C2PMSG_81_BYTE);
            if (sol != 0 && sol != 0xFFFFFFFF) {
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
    
    /* Check if SOS is already alive (loaded by PSP driver) */
    {
        ULONG sol = Amdbc250PspReadRegister(MP0_C2PMSG_81_BYTE);
        g_PspContext.SosAlive = (sol & 0x80000000) ? TRUE : FALSE;
        g_PspContext.Initialized = TRUE;
        KdPrint(("BC250-PSP: Init OK - MP0 base=0x%05X SOL=0x%08X SOS=%u\n",
            g_Mp0BaseDword, sol, g_PspContext.SosAlive));
    }
    
    return STATUS_SUCCESS;
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
