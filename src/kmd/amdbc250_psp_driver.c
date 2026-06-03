#include <ntddk.h>
#include <wdm.h>
#include <initguid.h>

#define IOCTL_PSP_GET_STATUS     0x80000BA4
#define IOCTL_PSP_INIT           0x80000B98
#define IOCTL_PSP_SEND_COMMAND   0x80000BA0
#define IOCTL_SMN_ACCESS         0x80000BC4

#define MAX_BARS 6

typedef struct _PSP_BAR_INFO {
    PHYSICAL_ADDRESS Address;
    ULONG Length;
    BOOLEAN Valid;
} PSP_BAR_INFO;

typedef struct _PSP_DEVICE_EXTENSION {
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_OBJECT LowerDeviceObject;
    PSP_BAR_INFO Bars[MAX_BARS];
    ULONG BarCount;
} PSP_DEVICE_EXTENSION, *PPSP_DEVICE_EXTENSION;

static PDEVICE_OBJECT g_ControlDeviceObject = NULL;

NTSTATUS PspDispatchPassThrough(PDEVICE_OBJECT deviceObject, PIRP irp) {
    IoSkipCurrentIrpStackLocation(irp);
    return IoCallDriver(((PPSP_DEVICE_EXTENSION)deviceObject->DeviceExtension)->LowerDeviceObject, irp);
}

NTSTATUS PspDispatchCreateClose(PDEVICE_OBJECT deviceObject, PIRP irp) {
    UNREFERENCED_PARAMETER(deviceObject);
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    irp->IoStatus.Status = STATUS_SUCCESS;
    irp->IoStatus.Information = 0;
    return STATUS_SUCCESS;
}

NTSTATUS PspDispatchDeviceControl(PDEVICE_OBJECT deviceObject, PIRP irp) {
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    ULONG ioctlCode = stack->Parameters.DeviceIoControl.IoControlCode;
    PVOID buffer = irp->AssociatedIrp.SystemBuffer;
    ULONG inLen = stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outLen = stack->Parameters.DeviceIoControl.OutputBufferLength;

    irp->IoStatus.Information = 0;

    switch (ioctlCode) {
        case IOCTL_PSP_GET_STATUS: {
            if (outLen >= 24) {
                PULONG out = (PULONG)buffer;
                out[0] = 0; // initialized = FALSE
                out[1] = 0; // sosAlive = FALSE
                out[2] = 0; // firmwareLoaded = FALSE
                out[3] = 0; // mmioBase = 0 (NBIO blocked)
                out[4] = 0; // sosRegister = 0
                out[5] = 0; // c2pmsg64 = 0
                irp->IoStatus.Information = 24;
            }
            irp->IoStatus.Status = STATUS_SUCCESS;
            break;
        }
        case IOCTL_PSP_INIT: {
            if (outLen >= 4) {
                *(PULONG)buffer = 21; // err=21 (NBIO firewall active)
                irp->IoStatus.Information = 4;
            }
            irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
            break;
        }
        case IOCTL_PSP_SEND_COMMAND: {
            irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
            break;
        }
        case IOCTL_SMN_ACCESS: {
            if (inLen >= 8 && outLen >= 4) {
                PULONG in = (PULONG)buffer;
                ULONG smnAddr = in[0];
                PULONG out = (PULONG)buffer;
                KdPrint(("BC250-PSP: SMN access blocked at 0x%08X\n", smnAddr));
                out[0] = 0;
                irp->IoStatus.Information = 4;
            }
            irp->IoStatus.Status = STATUS_SUCCESS;
            break;
        }
        default:
            irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
            break;
    }

    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return irp->IoStatus.Status;
}

NTSTATUS PspDispatchPnP(PDEVICE_OBJECT deviceObject, PIRP irp) {
    PPSP_DEVICE_EXTENSION devExt = deviceObject->DeviceExtension;
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);

    switch (stack->MinorFunction) {
        case IRP_MN_START_DEVICE: {
            PCM_PARTIAL_RESOURCE_LIST resourceList = NULL;
            PCM_RESOURCE_LIST rawResources = stack->Parameters.StartDevice.AllocatedResources;
            if (rawResources && rawResources->Count > 0) {
                resourceList = &rawResources->List[0].PartialResourceList;
            }
            if (resourceList) {
                devExt->BarCount = 0;
                for (ULONG i = 0; i < resourceList->Count && devExt->BarCount < MAX_BARS; i++) {
                    PCM_PARTIAL_RESOURCE_DESCRIPTOR res = &resourceList->PartialDescriptors[i];
                    if (res->Type == CmResourceTypeMemory) {
                        devExt->Bars[devExt->BarCount].Address = res->u.Memory.Start;
                        devExt->Bars[devExt->BarCount].Length = res->u.Memory.Length;
                        devExt->Bars[devExt->BarCount].Valid = TRUE;
                        KdPrint(("BC250-PSP: BAR%u MMIO = 0x%llX (len 0x%X)\n",
                            devExt->BarCount, res->u.Memory.Start.QuadPart, res->u.Memory.Length));
                        devExt->BarCount++;
                    }
                }
            }
            KdPrint(("BC250-PSP: Device started, %u BARs\n", devExt->BarCount));
            break;
        }
        case IRP_MN_STOP_DEVICE:
            KdPrint(("BC250-PSP: Device stopped\n"));
            break;
        case IRP_MN_REMOVE_DEVICE: {
            PDEVICE_OBJECT lower = devExt->LowerDeviceObject;
            KdPrint(("BC250-PSP: Device removed\n"));
            IoSkipCurrentIrpStackLocation(irp);
            NTSTATUS st = IoCallDriver(lower, irp);
            IoDetachDevice(lower);
            IoDeleteDevice(deviceObject);
            return st;
        }
    }

    IoSkipCurrentIrpStackLocation(irp);
    return IoCallDriver(devExt->LowerDeviceObject, irp);
}

NTSTATUS PspDispatchPower(PDEVICE_OBJECT deviceObject, PIRP irp) {
    IoSkipCurrentIrpStackLocation(irp);
    return IoCallDriver(((PPSP_DEVICE_EXTENSION)deviceObject->DeviceExtension)->LowerDeviceObject, irp);
}

NTSTATUS PspAddDevice(PDRIVER_OBJECT driverObject, PDEVICE_OBJECT physicalDeviceObject) {
    NTSTATUS status;
    PDEVICE_OBJECT fdo;
    PPSP_DEVICE_EXTENSION devExt;

    status = IoCreateDevice(driverObject, sizeof(PSP_DEVICE_EXTENSION),
                           NULL, FILE_DEVICE_UNKNOWN, 0, FALSE, &fdo);
    if (!NT_SUCCESS(status)) return status;

    devExt = fdo->DeviceExtension;
    RtlZeroMemory(devExt, sizeof(PSP_DEVICE_EXTENSION));
    devExt->DeviceObject = fdo;

    devExt->LowerDeviceObject = IoAttachDeviceToDeviceStack(fdo, physicalDeviceObject);
    if (!devExt->LowerDeviceObject) {
        IoDeleteDevice(fdo);
        return STATUS_NO_SUCH_DEVICE;
    }

    fdo->Flags |= DO_POWER_PAGABLE;
    fdo->Flags &= ~DO_DEVICE_INITIALIZING;

    KdPrint(("BC250-PSP: FDO attached\n"));
    return STATUS_SUCCESS;
}

NTSTATUS PspUnload(PDRIVER_OBJECT driverObject) {
    UNREFERENCED_PARAMETER(driverObject);

    if (g_ControlDeviceObject) {
        UNICODE_STRING symLink;
        RtlInitUnicodeString(&symLink, L"\\DosDevices\\BC250PSP");
        IoDeleteSymbolicLink(&symLink);
        IoDeleteDevice(g_ControlDeviceObject);
    }

    KdPrint(("BC250-PSP: Unloaded\n"));
    return STATUS_SUCCESS;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING registryPath) {
    NTSTATUS status;
    UNREFERENCED_PARAMETER(registryPath);

    KdPrint(("BC250-PSP: DriverEntry\n"));

    driverObject->DriverUnload = PspUnload;
    driverObject->DriverExtension->AddDevice = PspAddDevice;

    for (ULONG i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++) {
        driverObject->MajorFunction[i] = PspDispatchPassThrough;
    }
    driverObject->MajorFunction[IRP_MJ_CREATE] = PspDispatchCreateClose;
    driverObject->MajorFunction[IRP_MJ_CLOSE] = PspDispatchCreateClose;
    driverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = PspDispatchDeviceControl;
    driverObject->MajorFunction[IRP_MJ_PNP] = PspDispatchPnP;
    driverObject->MajorFunction[IRP_MJ_POWER] = PspDispatchPower;

    {
        UNICODE_STRING deviceName;
        RtlInitUnicodeString(&deviceName, L"\\Device\\BC250PSP");
        PDEVICE_OBJECT controlDevice;
        status = IoCreateDevice(driverObject, 0, &deviceName,
                               FILE_DEVICE_UNKNOWN, 0, FALSE, &controlDevice);
        if (NT_SUCCESS(status)) {
            UNICODE_STRING symLink;
            RtlInitUnicodeString(&symLink, L"\\DosDevices\\BC250PSP");
            status = IoCreateSymbolicLink(&symLink, &deviceName);
            if (!NT_SUCCESS(status)) {
                IoDeleteDevice(controlDevice);
                return status;
            }
            g_ControlDeviceObject = controlDevice;
            controlDevice->Flags &= ~DO_DEVICE_INITIALIZING;
            KdPrint(("BC250-PSP: Control device created\n"));
        }
    }

    KdPrint(("BC250-PSP: Driver loaded\n"));
    return STATUS_SUCCESS;
}
