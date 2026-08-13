/* amdbc250_dream_psp_fw_load.c - PSP firmware load (SYSDRV/SOS/SMC) via C2PMSG
 * Loads PSP bootloader + Secure OS + SMU firmware before SOS is alive.
 * Uses GPU driver's SMN access (NBIO 0x38/0x3C) for C2PMSG registers. */

#include "amdbc250_dream_kmd.h"
#include "amdbc250_dream_hw.h"

extern NTSTATUS Amdbc250PspSmnRead(PVOID GpuBar5Va, ULONG SmnAddress, PULONG OutValue);
extern NTSTATUS Amdbc250PspSmnWrite(PVOID GpuBar5Va, ULONG SmnAddress, ULONG Value);
extern NTSTATUS Amdbc250PspInit(ULONG64 MmioPhysicalBase);
extern NTSTATUS DreamV3LoadFirmwareFromFile(_In_ PCWSTR FileName, _Out_ PUCHAR *OutData, _Out_ ULONG *OutSize);

#define C2PMSG_35_SMN   0x03B10A08  /* PSP command */
#define C2PMSG_36_SMN   0x03B10A48  /* PSP data (PA low) */
#define C2PMSG_37_SMN   0x03B10A68  /* PSP data (PA high) */
#define C2PMSG_81_SMN   0x03B10A44  /* PSP response / SOS status */

#define PSP_CMD_LOAD_SYSDRV  0x04
#define PSP_CMD_LOAD_SOSDRV  0x08
#define PSP_CMD_LOAD_SMC     0x0A

#define FW_PATH_SYSDRV  L"\\SystemRoot\\System32\\drivers\\bc-250\\Sysdrv.bin"
#define FW_PATH_SOS     L"\\SystemRoot\\System32\\drivers\\bc-250\\Sos.bin"
#define FW_PATH_SMC     L"\\SystemRoot\\System32\\drivers\\bc-250\\Smu.bin"

static NTSTATUS PspFwWaitReady(PVOID Bar5Va, ULONG timeoutMs)
{
    for(ULONG i=0; i<timeoutMs; i++){
        ULONG val=0;
        Amdbc250PspSmnRead(Bar5Va, C2PMSG_81_SMN, &val);
        if((val & 0x80000000) || val==0) return STATUS_SUCCESS;
        KeStallExecutionProcessor(1000);
    }
    return STATUS_TIMEOUT;
}

static NTSTATUS PspFwSendCommand(PVOID Bar5Va, ULONG cmd, ULONG low, ULONG high)
{
    Amdbc250PspSmnWrite(Bar5Va, C2PMSG_35_SMN, 0);
    Amdbc250PspSmnWrite(Bar5Va, C2PMSG_36_SMN, low);
    Amdbc250PspSmnWrite(Bar5Va, C2PMSG_37_SMN, high);
    Amdbc250PspSmnWrite(Bar5Va, C2PMSG_35_SMN, cmd);
    for(ULONG i=0; i<5000; i++){
        ULONG val=0;
        Amdbc250PspSmnRead(Bar5Va, C2PMSG_35_SMN, &val);
        if(val==0) return STATUS_SUCCESS;
        KeStallExecutionProcessor(1000);
    }
    return STATUS_TIMEOUT;
}

NTSTATUS DreamV3LoadPspFirmware(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    NTSTATUS status;
    PVOID bar5 = DevExt->MmioVirtualBase;
    if(!bar5) return STATUS_DEVICE_NOT_READY;

    /* Check SOS already alive (loaded by PSP driver) */
    ULONG sol=0;
    Amdbc250PspSmnRead(bar5, C2PMSG_81_SMN, &sol);
    if(sol & 0x80000000){
        KdPrint(("PSP-FW: SOS already alive (0x%08X), skip load\n", sol));
        return STATUS_SUCCESS;
    }

    /* Load SYSDRV */
    PUCHAR fwData=NULL; ULONG fwSize=0;
    status = DreamV3LoadFirmwareFromFile(FW_PATH_SYSDRV, &fwData, &fwSize);
    if(NT_SUCCESS(status) && fwData && fwSize>0){
        PHYSICAL_ADDRESS pa = MmGetPhysicalAddress(fwData);
        status = PspFwSendCommand(bar5, PSP_CMD_LOAD_SYSDRV,
            (ULONG)(pa.QuadPart & 0xFFFFFFFF), (ULONG)(pa.QuadPart >> 32));
        ExFreePoolWithTag(fwData, 'fw');
        KdPrint(("PSP-FW: SYSDRV load 0x%08X\n", status));
    }

    /* Load SOS */
    fwData=NULL; fwSize=0;
    status = DreamV3LoadFirmwareFromFile(FW_PATH_SOS, &fwData, &fwSize);
    if(NT_SUCCESS(status) && fwData && fwSize>0){
        PHYSICAL_ADDRESS pa = MmGetPhysicalAddress(fwData);
        status = PspFwSendCommand(bar5, PSP_CMD_LOAD_SOSDRV,
            (ULONG)(pa.QuadPart & 0xFFFFFFFF), (ULONG)(pa.QuadPart >> 32));
        ExFreePoolWithTag(fwData, 'fw');
        KdPrint(("PSP-FW: SOS load 0x%08X\n", status));
        PspFwWaitReady(bar5, 5000);
    }

    /* Load SMC */
    fwData=NULL; fwSize=0;
    status = DreamV3LoadFirmwareFromFile(FW_PATH_SMC, &fwData, &fwSize);
    if(NT_SUCCESS(status) && fwData && fwSize>0){
        PHYSICAL_ADDRESS pa = MmGetPhysicalAddress(fwData);
        status = PspFwSendCommand(bar5, PSP_CMD_LOAD_SMC,
            (ULONG)(pa.QuadPart & 0xFFFFFFFF), (ULONG)(pa.QuadPart >> 32));
        ExFreePoolWithTag(fwData, 'fw');
        KdPrint(("PSP-FW: SMC load 0x%08X\n", status));
    }

    /* Check SOS alive */
    sol=0;
    Amdbc250PspSmnRead(bar5, C2PMSG_81_SMN, &sol);
    KdPrint(("PSP-FW: C2PMSG_81 = 0x%08X %s\n", sol,
        (sol & 0x80000000) ? "SOS ALIVE!" : "SOS NOT alive"));

    /* WGP unlock — per-bank writes using CORRECT Linux gfx10 GRBM_GFX_INDEX layout
     * Linux gfx_v10_0_select_se_sh() uses: SH_INDEX bits 15:8, SE_INDEX bits 23:16
     * Previous research used WRONG layout (INSTANCE bits 25:24) — that's why it failed!
     * Must write EACH bank individually (broadcast doesn't work for SPI_PG). */
    {
        /* CORRECT Linux gfx10 GRBM_GFX_INDEX per-bank values */
        static const ULONG BankSelectsLinux[] = {
            0x00000000,  /* SE0/SH0 */
            0x00000100,  /* SE0/SH1 (SH_INDEX=1 at bits 15:8) */
            0x00010000,  /* SE1/SH0 (SE_INDEX=1 at bits 23:16) */
            0x00010100   /* SE1/SH1 */
        };

        ULONG spiBefore = READ_REGISTER_ULONG((PULONG)((PUCHAR)bar5 + 0x5C3C));
        ULONG ccBefore  = READ_REGISTER_ULONG((PULONG)((PUCHAR)bar5 + 0x9C1C));
        KdPrint(("PSP-FW: WGP unlock — Before: SPI=0x%08X CC=0x%08X\n", spiBefore, ccBefore));

        /* Write each bank individually (Linux does this in gfx_v10_0_get_cu_info) */
        for(int b=0; b<4; b++){
            /* Select this SE/SH bank */
            WRITE_REGISTER_ULONG((PULONG)((PUCHAR)bar5 + 0x34D0), BankSelectsLinux[b]);

            /* Write unlock values per-bank */
            WRITE_REGISTER_ULONG((PULONG)((PUCHAR)bar5 + 0x9C1C), 0xFFE00000);  /* CC: 40 CU */
            WRITE_REGISTER_ULONG((PULONG)((PUCHAR)bar5 + 0x5C3C), 0x1F);        /* SPI: WGP0-4 */
            WRITE_REGISTER_ULONG((PULONG)((PUCHAR)bar5 + 0x3D64), 0x1F);        /* RLC: WGP0-4 */
        }

        /* Restore broadcast select */
        WRITE_REGISTER_ULONG((PULONG)((PUCHAR)bar5 + 0x34D0), 0x15000000);

        /* Read back (broadcast reads last written bank) */
        ULONG spiAfter = READ_REGISTER_ULONG((PULONG)((PUCHAR)bar5 + 0x5C3C));
        ULONG ccAfter  = READ_REGISTER_ULONG((PULONG)((PUCHAR)bar5 + 0x9C1C));
        KdPrint(("PSP-FW: WGP unlock — After: SPI=0x%08X CC=0x%08X\n", spiAfter, ccAfter));

        if(spiAfter == 0x1F){
            KdPrint(("PSP-FW: *** WGP UNLOCK SUCCESS! ***\n"));
        } else {
            KdPrint(("PSP-FW: WGP unlock FAILED (SPI=0x%08X, expected 0x1F) — trying alternate layout...\n", spiAfter));

            /* Fallback: try alternate layout (instance index at bits 25:24, SE at bit 28) */
            static const ULONG BankSelectsAlt[] = {
                0x00000000, 0x01000000, 0x10000000, 0x11000000
            };
            for(int b=0; b<4; b++){
                WRITE_REGISTER_ULONG((PULONG)((PUCHAR)bar5 + 0x34D0), BankSelectsAlt[b]);
                WRITE_REGISTER_ULONG((PULONG)((PUCHAR)bar5 + 0x9C1C), 0xFFE00000);
                WRITE_REGISTER_ULONG((PULONG)((PUCHAR)bar5 + 0x5C3C), 0x1F);
                WRITE_REGISTER_ULONG((PULONG)((PUCHAR)bar5 + 0x3D64), 0x1F);
            }
            WRITE_REGISTER_ULONG((PULONG)((PUCHAR)bar5 + 0x34D0), 0x15000000);
            spiAfter = READ_REGISTER_ULONG((PULONG)((PUCHAR)bar5 + 0x5C3C));
            KdPrint(("PSP-FW: WGP unlock — After alt: SPI=0x%08X\n", spiAfter));
        }
    }

    return (sol & 0x80000000) ? STATUS_SUCCESS : STATUS_DEVICE_NOT_READY;
}
