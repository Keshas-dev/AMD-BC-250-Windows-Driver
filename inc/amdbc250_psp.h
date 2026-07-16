#ifndef AMDBC250_PSP_V11_H
#define AMDBC250_PSP_V11_H

#include <ntddk.h>

/* MP0 C2PMSG register byte offsets within the PSP mailbox block */
#define MP0_C2PMSG_35_BYTE            0x018C
#define MP0_C2PMSG_36_BYTE            0x0190
#define MP0_C2PMSG_64_BYTE            0x0200
#define MP0_C2PMSG_67_BYTE            0x020C
#define MP0_C2PMSG_69_BYTE            0x0214
#define MP0_C2PMSG_70_BYTE            0x0218
#define MP0_C2PMSG_71_BYTE            0x021C
#define MP0_C2PMSG_81_BYTE            0x0244
#define MP0_C2PMSG_101_BYTE           0x0294

/* PSP driver IOCTL to load GPU IP firmware via the SOS secure mailbox
 * (C2PMSG_35/36/37/81). This is the working path on BC-250. */
#define PSP_IOCTL_LOAD_IP_FW_DIRECT  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x824, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _PSP_LOAD_IP_FW_REQUEST {
    ULONG FwType;
    ULONG FwSize;
    /* Firmware blob (FwSize bytes) follows immediately after this header. */
} PSP_LOAD_IP_FW_REQUEST, *PPSP_LOAD_IP_FW_REQUEST;

typedef struct _AMDBC250_PSP_CONTEXT {
    PUCHAR MmioBase;
    ULONG MmioSize;
    PUCHAR SosFirmware;
    ULONG SosFirmwareSize;
    PUCHAR AsdFirmware;
    ULONG AsdFirmwareSize;
    PUCHAR TaFirmware;
    ULONG TaFirmwareSize;
    PVOID FirmwareBuffer;
    PHYSICAL_ADDRESS FirmwarePhysical;
    ULONG FirmwareBufferSize;
    PMDL FirmwareMdl;              /* Save MDL for proper cleanup */
    PVOID RingBuffer;
    PHYSICAL_ADDRESS RingPhysical;
    ULONG RingSize;
    ULONG RingWptr;
    BOOLEAN Initialized;
    BOOLEAN FirmwareLoaded;
    BOOLEAN SosAlive;
} AMDBC250_PSP_CONTEXT, *PAMDBC250_PSP_CONTEXT;

NTSTATUS Amdbc250PspInit(ULONG64 MmioPhysicalBase);
VOID Amdbc250PspCleanup(VOID);
NTSTATUS Amdbc250PspSendCommand(ULONG Command, PUCHAR Data, ULONG DataSize);
PAMDBC250_PSP_CONTEXT Amdbc250PspGetContext(VOID);
PVOID Amdbc250PspGetGpuBar5Va(VOID);
VOID Amdbc250PspPublishGpuBar5(PVOID Va);
VOID Amdbc250PspSetGpuBar5Va(PVOID Va);
ULONG Amdbc250PspReadRegister(ULONG RegisterOffset);
VOID Amdbc250PspWriteRegister(ULONG RegisterOffset, ULONG Value);
VOID Amdbc250PspUnmapRegisters(VOID);
NTSTATUS Amdbc250PspTryUnlockNbio(VOID);
BOOLEAN Amdbc250PspValidateFirmware(PUCHAR FirmwareData, ULONG FirmwareSize, ULONG FirmwareType);

/* PSP Proxy - GPU register access via PSP driver (bypasses NBIO firewall) */
ULONG Amdbc250PspProxyReadReg(ULONG GpuRegOffset);
VOID Amdbc250PspProxyWriteReg(ULONG GpuRegOffset, ULONG Value);
BOOLEAN Amdbc250PspProxyAvailable(VOID);
BOOLEAN Amdbc250PspKiqAvailable(VOID);
NTSTATUS Amdbc250PspKiqInit(VOID);
NTSTATUS Amdbc250PspKiqSubmit(ULONG* Pm4Commands, ULONG DwordCount);
BOOLEAN Amdbc250PspKiqIsInitialized(VOID);
ULONG Amdbc250PspKiqReadReg(ULONG GpuRegOffset);
NTSTATUS Amdbc250PspKiqLoadFirmware(ULONG FwType, ULONG FwSize, PHYSICAL_ADDRESS FwPa);
NTSTATUS Amdbc250PspAllocateFirmwareBuffer(ULONG Size);
NTSTATUS Amdbc250PspCopyFirmwareData(PUCHAR FirmwareData, ULONG Size);
PHYSICAL_ADDRESS Amdbc250PspFirmwarePa(VOID);
VOID Amdbc250PspKiqCleanup(VOID);
VOID Amdbc250PspProxyCleanup(VOID);

#endif
