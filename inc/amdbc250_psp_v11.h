#ifndef AMDBC250_PSP_V11_H
#define AMDBC250_PSP_V11_H

#include <ntddk.h>

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
ULONG Amdbc250PspReadRegister(ULONG RegisterOffset);
VOID Amdbc250PspWriteRegister(ULONG RegisterOffset, ULONG Value);
VOID Amdbc250PspUnmapRegisters(VOID);
NTSTATUS Amdbc250PspTryUnlockNbio(VOID);
BOOLEAN Amdbc250PspValidateFirmware(PUCHAR FirmwareData, ULONG FirmwareSize, ULONG FirmwareType);

#endif
