#include "ntddk.h"
#include "initguid.h"
#include <wdm.h>

// PSP firmware data
#include "firmware_data.h"

// PSP registers (relative to PSP MMIO base)
#define PSP_C2PMSG_35   0x0088  // Bootloader command
#define PSP_C2PMSG_36   0x008C  // Firmware address
#define PSP_C2PMSG_64   0x0100  // Ring creation / TOS
#define PSP_C2PMSG_67   0x010C  // Ring write pointer
#define PSP_C2PMSG_69   0x0114  // Ring address low
#define PSP_C2PMSG_70   0x0118  // Ring address high
#define PSP_C2PMSG_71   0x011C  // Ring size
#define PSP_C2PMSG_81   0x0144  // SOS alive

#define PSP_BL_READY     0x80000000
#define PSP_BL_LOAD_SOS  0x00000008
#define PSP_RING_SIZE    0x1000
#define PSP_MAX_WAIT_MS  5000

typedef struct _PSP_DEVICE_EXTENSION {
    PDEVICE_OBJECT DeviceObject;
    PUCHAR MmioBase;
    ULONG MmioSize;
    PHYSICAL_ADDRESS MmioPhysicalBase;
    BOOLEAN PspInitialized;
} PSP_DEVICE_EXTENSION, *PPSP_DEVICE_EXTENSION;

// Globals
static PDEVICE_OBJECT g_PspDeviceObject = NULL;
static PPSP_DEVICE_EXTENSION g_PspDevExt = NULL;

// PSP register access
ULONG PspReadReg(PPSP_DEVICE_EXTENSION devExt, ULONG offset) {
    if (!devExt || !devExt->MmioBase) return 0;
    return READ_REGISTER_ULONG((PULONG)(devExt->MmioBase + offset));
}

VOID PspWriteReg(PPSP_DEVICE_EXTENSION devExt, ULONG offset, ULONG value) {
    if (!devExt || !devExt->MmioBase) return;
    WRITE_REGISTER_ULONG((PULONG)(devExt->MmioBase + offset), value);
}

NTSTATUS PspWaitForBit(PPSP_DEVICE_EXTENSION devExt, ULONG reg, ULONG mask, ULONG timeoutMs) {
    LARGE_INTEGER delay;
    delay.QuadPart = -10000LL;
    for (ULONG i = 0; i < timeoutMs; i++) {
        if ((PspReadReg(devExt, reg) & mask) == mask)
            return STATUS_SUCCESS;
        KeDelayExecutionThread(KernelMode, FALSE, &delay);
    }
    return STATUS_TIMEOUT;
}

NTSTATUS PspInit(PPSP_DEVICE_EXTENSION devExt) {
    NTSTATUS status;
    ULONG reg;

    KdPrint(("BC250-PSP: Initializing PSP...\n"));

    // 1. Check if bootloader is ready
    status = PspWaitForBit(devExt, PSP_C2PMSG_35, PSP_BL_READY, 1000);
    if (!NT_SUCCESS(status)) {
        KdPrint(("BC250-PSP: Bootloader not ready (0x%08X)\n", PspReadReg(devExt, PSP_C2PMSG_35)));
    }

    // 2. Check SOS alive
    reg = PspReadReg(devExt, PSP_C2PMSG_81);
    KdPrint(("BC250-PSP: C2PMSG_81 (SOS alive) = 0x%08X\n", reg));

    // 3. Check ring status
    reg = PspReadReg(devExt, PSP_C2PMSG_64);
    KdPrint(("BC250-PSP: C2PMSG_64 (ring) = 0x%08X\n", reg));

    devExt->PspInitialized = TRUE;
    KdPrint(("BC250-PSP: PSP initialization complete\n"));

    return STATUS_SUCCESS;
}

NTSTATUS PspCreateDevice(PDRIVER_OBJECT driverObject) {
    NTSTATUS status;
    PDEVICE_OBJECT deviceObject;
    PPSP_DEVICE_EXTENSION devExt;

    // Create device
    UNICODE_STRING deviceName;
    RtlInitUnicodeString(&deviceName, L"\\Device\\BC250PSP");
    
    status = IoCreateDevice(driverObject, sizeof(PSP_DEVICE_EXTENSION),
                           &deviceName, FILE_DEVICE_UNKNOWN, 0, FALSE, &deviceObject);
    if (!NT_SUCCESS(status)) {
        KdPrint(("BC250-PSP: IoCreateDevice failed: 0x%08X\n", status));
        return status;
    }

    // Create symbolic link
    UNICODE_STRING symLink;
    RtlInitUnicodeString(&symLink, L"\\DosDevices\\BC250PSP");
    status = IoCreateSymbolicLink(&symLink, &deviceName);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(deviceObject);
        return status;
    }

    devExt = (PPSP_DEVICE_EXTENSION)deviceObject->DeviceExtension;
    RtlZeroMemory(devExt, sizeof(PSP_DEVICE_EXTENSION));
    devExt->DeviceObject = deviceObject;

    g_PspDeviceObject = deviceObject;
    g_PspDevExt = devExt;

    KdPrint(("BC250-PSP: Device created\n"));
    return STATUS_SUCCESS;
}

NTSTATUS PspMapMmio(PPSP_DEVICE_EXTENSION devExt) {
    // Try to map PSP MMIO at various addresses
    PHYSICAL_ADDRESS pspMmioBases[] = {
        {0xFE810000},  // GPU BAR + 0x10000
        {0xFE820000},  // Alternative
        {0xFE830000},  // Alternative
    };

    for (int i = 0; i < 3; i++) {
        devExt->MmioBase = (PUCHAR)MmMapIoSpace(pspMmioBases[i], 0x4000, MmNonCached);
        if (devExt->MmioBase) {
            ULONG testVal = READ_REGISTER_ULONG((PULONG)devExt->MmioBase);
            if (testVal != 0xFFFFFFFF && testVal != 0) {
                KdPrint(("BC250-PSP: MMIO mapped at 0x%llX, test=0x%08X\n",
                    pspMmioBases[i].QuadPart, testVal));
                devExt->MmioPhysicalBase = pspMmioBases[i];
                return STATUS_SUCCESS;
            }
            MmUnmapIoSpace(devExt->MmioBase, 0x4000);
            devExt->MmioBase = NULL;
        }
    }
    
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS PspUnload(PDRIVER_OBJECT driverObject) {
    if (g_PspDevExt && g_PspDevExt->MmioBase) {
        MmUnmapIoSpace(g_PspDevExt->MmioBase, 0x4000);
    }
    
    UNICODE_STRING symLink;
    RtlInitUnicodeString(&symLink, L"\\DosDevices\\BC250PSP");
    IoDeleteSymbolicLink(&symLink);
    
    if (g_PspDeviceObject) {
        IoDeleteDevice(g_PspDeviceObject);
    }
    
    KdPrint(("BC250-PSP: Unloaded\n"));
    return STATUS_SUCCESS;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING registryPath) {
    NTSTATUS status;
    UNREFERENCED_PARAMETER(registryPath);

    KdPrint(("BC250-PSP: DriverEntry\n"));

    driverObject->DriverUnload = PspUnload;

    // Create device
    status = PspCreateDevice(driverObject);
    if (!NT_SUCCESS(status)) return status;

    // Try to map PSP MMIO
    status = PspMapMmio(g_PspDevExt);
    if (!NT_SUCCESS(status)) {
        KdPrint(("BC250-PSP: Cannot map PSP MMIO! NBIO firewall active?\n"));
    } else {
        // Initialize PSP
        PspInit(g_PspDevExt);
    }

    // Try to get device resources from PCI
    KdPrint(("BC250-PSP: Firmware data - SOS=%u bytes, ASD=%u bytes, TA=%u bytes\n",
        (ULONG)sizeof(g_SosFirmwareData),
        (ULONG)sizeof(g_AsdFirmwareData),
        (ULONG)sizeof(g_TaFirmwareData)));

    KdPrint(("BC250-PSP: Driver loaded successfully\n"));
    return STATUS_SUCCESS;
}