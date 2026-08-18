//
// Simple WDM driver for BC-250 register access
// Exposes IOCTL_AMDBC250_READ_REG and IOCTL_AMDBC250_WRITE_REG
//

#include <ntddk.h>

#define DEVICE_NAME L"\\Device\\AMDBC250Reg"
#define SYMLINK_NAME L"\\DosDevices\\AMDBC250Reg"

// BC-250 GPU BAR5 physical address
#define BC250_BAR5_PHYSICAL_BASE  0xFE800000ULL
#define BC250_BAR5_SIZE           0x80000  // 512KB

// SMN addresses for GFXOFF control (NBIO 0x38/0x3C path)
#define SMN_GFXOFF_FEATURE_LOW   0x03B10528
#define SMN_GFXOFF_FEATURE_HIGH  0x03B10998

// GFXOFF disable bit (bit 2 of feature mask)
#define GFXOFF_BIT  0x00000004

// IOCTL definitions (from amdbc250_ioctl.h)
#define FILE_DEVICE_AMDBC250    0x8000
#define IOCTL_INDEX             0x270
#define CTL_CODE_AMDBC250(Function, Method, Access) \
    CTL_CODE(FILE_DEVICE_AMDBC250, IOCTL_INDEX + (Function), Method, Access)

#define IOCTL_AMDBC250_READ_REG  CTL_CODE_AMDBC250(0x72, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AMDBC250_WRITE_REG CTL_CODE_AMDBC250(0x73, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _AMDBC250_REG_ACCESS {
    ULONG RegisterOffset;
    ULONG Value;
} AMDBC250_REG_ACCESS, *PAMDBC250_REG_ACCESS;

PDEVICE_OBJECT g_DeviceObject = NULL;
PVOID g_Bar5Va = NULL;

// Read SMN register via NBIO 0x38/0x3C
static ULONG SmnRead(PVOID Bar5Va, ULONG smnAddr) {
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Bar5Va + 0x38), smnAddr);
    READ_REGISTER_ULONG((PULONG)((PUCHAR)Bar5Va + 0x38)); // dummy read for latency
    return READ_REGISTER_ULONG((PULONG)((PUCHAR)Bar5Va + 0x3C));
}

// Write SMN register via NBIO 0x38/0x3C
static VOID SmnWrite(PVOID Bar5Va, ULONG smnAddr, ULONG value) {
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Bar5Va + 0x38), smnAddr);
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Bar5Va + 0x3C), value);
}

// Disable GFXOFF to enable GPU register writes
static VOID DisableGfxoff(PVOID Bar5Va) {
    ULONG featLow = SmnRead(Bar5Va, SMN_GFXOFF_FEATURE_LOW);
    ULONG featHigh = SmnRead(Bar5Va, SMN_GFXOFF_FEATURE_HIGH);
    DbgPrint("AMDBC250Reg: GFXOFF features: low=0x%08X high=0x%08X\n", featLow, featHigh);
    
    // Clear GFXOFF bit (bit 2)
    featLow &= ~GFXOFF_BIT;
    SmnWrite(Bar5Va, SMN_GFXOFF_FEATURE_LOW, featLow);
    
    ULONG featLowAfter = SmnRead(Bar5Va, SMN_GFXOFF_FEATURE_LOW);
    DbgPrint("AMDBC250Reg: GFXOFF disabled: features now=0x%08X\n", featLowAfter);
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath);
VOID DriverUnload(PDRIVER_OBJECT DriverObject);
NTSTATUS DeviceCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS DeviceIoControl(PDEVICE_OBJECT DeviceObject, PIRP Irp);

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, DriverUnload)
#pragma alloc_text(PAGE, DeviceCreateClose)
#pragma alloc_text(PAGE, DeviceIoControl)
#endif

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    NTSTATUS status;
    UNICODE_STRING devName, symLinkName;

    UNREFERENCED_PARAMETER(RegistryPath);

    DriverObject->MajorFunction[IRP_MJ_CREATE] = DeviceCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = DeviceCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DeviceIoControl;
    DriverObject->DriverUnload = DriverUnload;

    RtlInitUnicodeString(&devName, DEVICE_NAME);
    RtlInitUnicodeString(&symLinkName, SYMLINK_NAME);

    status = IoCreateDevice(DriverObject, 0, &devName, FILE_DEVICE_UNKNOWN, 0, FALSE, &g_DeviceObject);
    if (!NT_SUCCESS(status)) return status;

    status = IoCreateSymbolicLink(&symLinkName, &devName);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(g_DeviceObject);
        return status;
    }

    // Map BAR5 using MmMapIoSpace (older API, more compatible)
    PHYSICAL_ADDRESS physAddr;
    physAddr.QuadPart = BC250_BAR5_PHYSICAL_BASE;
    g_Bar5Va = MmMapIoSpace(physAddr, BC250_BAR5_SIZE, MmNonCached);

    if (!g_Bar5Va) {
        DbgPrint("AMDBC250Reg: FAILED to map BAR5 at 0x%llX\n", BC250_BAR5_PHYSICAL_BASE);
        IoDeleteSymbolicLink(&symLinkName);
        IoDeleteDevice(g_DeviceObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    DbgPrint("AMDBC250Reg: Driver loaded, BAR5 mapped at %p (VA for PA 0x%llX)\n", g_Bar5Va, BC250_BAR5_PHYSICAL_BASE);
    
    // Quick test read
    ULONG testVal = READ_REGISTER_ULONG((PULONG)g_Bar5Va);
    DbgPrint("AMDBC250Reg: BAR5[0] = 0x%08X (expect 0x9FFF9714 for GPU_ID)\n", testVal);
    
    // Disable GFXOFF to enable GPU register writes
    DisableGfxoff(g_Bar5Va);
    
    // Test read after GFXOFF disable
    testVal = READ_REGISTER_ULONG((PULONG)g_Bar5Va);
    DbgPrint("AMDBC250Reg: BAR5[0] after GFXOFF disable = 0x%08X\n", testVal);
    
    return STATUS_SUCCESS;
}

VOID DriverUnload(PDRIVER_OBJECT DriverObject) {
    UNICODE_STRING symLinkName;
    RtlInitUnicodeString(&symLinkName, SYMLINK_NAME);

    if (g_Bar5Va) {
        MmUnmapIoSpace(g_Bar5Va, BC250_BAR5_SIZE);
        g_Bar5Va = NULL;
    }

    IoDeleteSymbolicLink(&symLinkName);
    if (g_DeviceObject) IoDeleteDevice(g_DeviceObject);
    DbgPrint("AMDBC250Reg: Driver unloaded\n");
}

NTSTATUS DeviceCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

NTSTATUS DeviceIoControl(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    ULONG bytesReturned = 0;
    PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);

    if (!g_Bar5Va) {
        status = STATUS_DEVICE_NOT_READY;
        DbgPrint("AMDBC250Reg: IOCTL but g_Bar5Va is NULL!\n");
        goto done;
    }

    PVOID buffer = Irp->AssociatedIrp.SystemBuffer;
    ULONG inSize = irpSp->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outSize = irpSp->Parameters.DeviceIoControl.OutputBufferLength;

    switch (irpSp->Parameters.DeviceIoControl.IoControlCode) {
    case IOCTL_AMDBC250_READ_REG:
        if (inSize >= sizeof(AMDBC250_REG_ACCESS) && outSize >= sizeof(AMDBC250_REG_ACCESS)) {
            PAMDBC250_REG_ACCESS req = (PAMDBC250_REG_ACCESS)buffer;
            if (req->RegisterOffset + 4 <= BC250_BAR5_SIZE) {
                req->Value = READ_REGISTER_ULONG((PULONG)((PUCHAR)g_Bar5Va + req->RegisterOffset));
                DbgPrint("AMDBC250Reg: READ [0x%04X] = 0x%08X\n", req->RegisterOffset, req->Value);
                bytesReturned = sizeof(AMDBC250_REG_ACCESS);
                status = STATUS_SUCCESS;
            } else {
                status = STATUS_INVALID_PARAMETER;
            }
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;

    case IOCTL_AMDBC250_WRITE_REG:
        if (inSize >= sizeof(AMDBC250_REG_ACCESS)) {
            PAMDBC250_REG_ACCESS req = (PAMDBC250_REG_ACCESS)buffer;
            if (req->RegisterOffset + 4 <= BC250_BAR5_SIZE) {
                DbgPrint("AMDBC250Reg: WRITE [0x%04X] = 0x%08X\n", req->RegisterOffset, req->Value);
                WRITE_REGISTER_ULONG((PULONG)((PUCHAR)g_Bar5Va + req->RegisterOffset), req->Value);
                status = STATUS_SUCCESS;
            } else {
                status = STATUS_INVALID_PARAMETER;
            }
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        break;
    }

done:
    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = bytesReturned;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}
