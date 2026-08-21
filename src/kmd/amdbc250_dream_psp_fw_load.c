/* amdbc250_dream_psp_fw_load.c - PSP firmware load (SYSDRV/SOS/SMC) via C2PMSG
 * Loads PSP bootloader + Secure OS + SMU firmware before SOS is alive.
 *
 * REWORKED 2026-08-18: was using SMN addresses 0x03B10A08/48/68/44 which the
 * psp-ring-probe proved are SMU C2PMSG_66/82/90 (NOT PSP). PSP C2PMSG live
 * directly in GPU BAR5 at MP0 base 0x58000 (verified by psp-ring-submit-test
 * 2026-08-18):
 *   C2PMSG_35 = 0x5818C, C2PMSG_36 = 0x58190, C2PMSG_37 = 0x58194,
 *   C2PMSG_81 = 0x58244. The earlier 0x103D0-based offsets were WRONG —
 *   they read back idle values. */

#include "amdbc250_dream_kmd.h"
#include "amdbc250_dream_hw.h"
#include "amdbc250_psp.h"

extern NTSTATUS DreamV3LoadFirmwareFromFile(_In_ PCWSTR FileName, _Out_ PUCHAR *OutData, _Out_ ULONG *OutSize);

/* Corrected BAR5 direct C2PMSG offsets (MP0 base 0x58000, verified 2026-08-18
 * by psp-ring-submit-test. The earlier 0x103D0-based offsets 0x1055C/0x10560/
 * 0x10614 were WRONG — they read back idle values. Base 0x58000 = ip_discovery
 * MP0 base 0x16000 (dwords) * 4.) */
#define C2PMSG_35_OFF   0x5818C  /* PSP command */
#define C2PMSG_36_OFF   0x58190  /* PSP data (PA low, 1MB units for bootloader) */
#define C2PMSG_37_OFF   0x58194  /* PSP data (PA high) */
#define C2PMSG_81_OFF   0x58244  /* PSP response / SOS status */

/* Bootloader command codes (Linux amdgpu_psp.h psp_bootloader_cmd).
 * NOTE: these are the REAL v11 bootloader values, NOT the older
 * direct-mbox GFX_CMD values (0x04/0x08/0x0A) used previously. */
#define PSP_CMD_LOAD_SYSDRV  0x10000
#define PSP_CMD_LOAD_SOSDRV  0x20000

#define FW_PATH_SYSDRV  L"\\SystemRoot\\System32\\drivers\\bc-250\\Sysdrv.bin"
#define FW_PATH_SOS     L"\\SystemRoot\\System32\\drivers\\bc-250\\Sos.bin"

static ULONG PspFwRead(PVOID Bar5Va, ULONG Off)
{
    if (!Bar5Va) return 0xFFFFFFFF;
    return READ_REGISTER_ULONG((PULONG)((PUCHAR)Bar5Va + Off));
}

static VOID PspFwWrite(PVOID Bar5Va, ULONG Off, ULONG Val)
{
    if (!Bar5Va) return;
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Bar5Va + Off), Val);
}

static NTSTATUS PspFwWaitReady(PVOID Bar5Va, ULONG timeoutMs)
{
    for(ULONG i=0; i<timeoutMs; i++){
        ULONG val = PspFwRead(Bar5Va, C2PMSG_81_OFF);
        if(val != 0) return STATUS_SUCCESS;
        KeStallExecutionProcessor(1000);
    }
    return STATUS_TIMEOUT;
}

static NTSTATUS PspFwSendCommand(PVOID Bar5Va, ULONG cmd, ULONG paMb)
{
    /* Bootloader protocol (Linux psp_v11_0_bootloader_load_*):
     *   C2PMSG_36 = firmware PA in 1MB units (PA >> 20)
     *   C2PMSG_35 = bootloader command (PSP_BL__LOAD_*)
     * Bootloader signals ready/completion with C2PMSG_35 bit31 SET
     * (psp_wait_for mask 0x80000000). */
    PspFwWrite(Bar5Va, C2PMSG_36_OFF, paMb);
    KeMemoryBarrier();
    PspFwWrite(Bar5Va, C2PMSG_35_OFF, cmd);
    KeMemoryBarrier();
    for(ULONG i=0; i<5000; i++){
        ULONG val = PspFwRead(Bar5Va, C2PMSG_35_OFF);
        if(val & 0x80000000) return STATUS_SUCCESS;
        KeStallExecutionProcessor(1000);
    }
    return STATUS_TIMEOUT;
}

static NTSTATUS PspFwLoadBlob(PVOID Bar5Va, ULONG cmd, PCWSTR FwPath)
{
    NTSTATUS status;
    PUCHAR fwData = NULL;
    ULONG fwSize = 0;
    status = DreamV3LoadFirmwareFromFile(FwPath, &fwData, &fwSize);
    if (!NT_SUCCESS(status) || !fwData || fwSize == 0) {
        KdPrint(("PSP-FW: read %ws failed 0x%08X\n", FwPath, status));
        return status;
    }

    /* PSP DMA-reads the whole blob from the base PA, so it MUST live in a
     * physically-contiguous buffer. Pool blobs are only virtually contiguous. */
    status = Amdbc250PspAllocateFirmwareBuffer(fwSize);
    if (NT_SUCCESS(status))
        status = Amdbc250PspCopyFirmwareData(fwData, fwSize);

    ExFreePoolWithTag(fwData, 'fw');
    if (!NT_SUCCESS(status)) {
        KdPrint(("PSP-FW: contiguous buffer for %ws failed 0x%08X\n", FwPath, status));
        return status;
    }

    PHYSICAL_ADDRESS pa = Amdbc250PspFirmwarePa();
    status = PspFwSendCommand(Bar5Va, cmd, (ULONG)(pa.QuadPart >> 20));
    KdPrint(("PSP-FW: %ws cmd 0x%08X PA=0x%llX (MB=0x%X) -> 0x%08X\n",
        FwPath, cmd, pa.QuadPart, (ULONG)(pa.QuadPart >> 20), status));
    return status;
}

NTSTATUS DreamV3LoadPspFirmware(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    NTSTATUS status;
    PVOID bar5 = DevExt ? DevExt->MmioVirtualBase : NULL;
    if(!bar5 || DevExt->MmioSize < 0x58248) return STATUS_DEVICE_NOT_READY;

    /* Check SOS already alive (loaded by BIOS/PSP driver). */
    ULONG sol = PspFwRead(bar5, C2PMSG_81_OFF);
    if(sol != 0){
        KdPrint(("PSP-FW: SOS already alive (0x%08X), skip load\n", sol));
        return STATUS_SUCCESS;
    }

    /* SOS not alive: try PSP driver bootloader path first. */
    if (Amdbc250PspProxyAvailable()) {
        KdPrint(("PSP-FW: SOS not alive, trying PSP driver bootloader path\n"));
        IO_STATUS_BLOCK iosb;
        ULONG bootCmd = 0;
        status = ZwDeviceIoControlFile(g_PspProxyHandle, NULL, NULL, NULL,
            &iosb, PSP_IOCTL_BOOT_SEQ, &bootCmd, sizeof(bootCmd), NULL, 0);
        /* Re-verify SOS alive even if the proxy reports success. */
        sol = PspFwRead(bar5, C2PMSG_81_OFF);
        if (NT_SUCCESS(status) && (sol != 0)) {
            KdPrint(("PSP-FW: PSP driver bootloader OK\n"));
            return STATUS_SUCCESS;
        }
        KdPrint(("PSP-FW: PSP driver bootloader failed 0x%08X (sol=0x%08X), fallback to direct BAR5\n",
            status, sol));
    }

    /* Fallback: direct BAR5 C2PMSG_35/36/37 at corrected offsets.
     * NOTE: Linux loads TOS (Ta.bin) as PSP_BL__LOAD_TOS_SPL_TABLE
     * BEFORE SOS is alive; SMC is loaded later via the ring, not here. */
    KdPrint(("PSP-FW: Using direct BAR5 C2PMSG fallback\n"));

    status = PspFwLoadBlob(bar5, PSP_CMD_LOAD_SYSDRV, FW_PATH_SYSDRV);
    if (!NT_SUCCESS(status)) {
        KdPrint(("PSP-FW: SYSDRV load failed 0x%08X, aborting bootloader path\n", status));
        return status;
    }
    status = PspFwLoadBlob(bar5, PSP_CMD_LOAD_SOSDRV, FW_PATH_SOS);
    if (!NT_SUCCESS(status)) {
        KdPrint(("PSP-FW: SOS load failed 0x%08X, aborting bootloader path\n", status));
        return status;
    }
    PspFwWaitReady(bar5, 5000);

    sol = PspFwRead(bar5, C2PMSG_81_OFF);
    KdPrint(("PSP-FW: C2PMSG_81 = 0x%08X %s\n", sol,
        (sol != 0) ? "SOS ALIVE!" : "SOS NOT alive"));

    return (sol != 0) ? STATUS_SUCCESS : STATUS_DEVICE_NOT_READY;
}