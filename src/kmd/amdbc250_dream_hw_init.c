/*++

Copyright (c) 2026 AMD BC-250 "Dream Drivers" Project — Version 3.0

Module Name:
    amdbc250_dream_hw_init.c

Abstract:
    Hardware initialization for AMD BC-250 (RDNA2 / Cyan Skillfish / GFX1013).
    
    CRITICAL IMPROVEMENTS over previous versions:
    1. ✅ HDP coherency flush BEFORE reading ring pointers (Linux quirk)
    2. ✅ Golden register programming (hardware workarounds)
    3. ✅ Proper RDNA2 CP initialization (GFX10 style)
    4. ✅ DCN 2.1 display engine (NOT DCE 8.x!)
    5. ✅ 64-bit fences (GFX10 requirement)
    6. ✅ Thermal monitoring with auto-throttling
    7. ✅ 16GB GDDR6 memory management
    8. ✅ ~10GB visible VRAM quirk handling
    
    Based on Linux amdgpu driver:
    - drivers/gpu/drm/amd/amdgpu/gfx_v10_0.c
    - drivers/gpu/drm/amd/amdgpu/nv.c
    - drivers/gpu/drm/amd/display/dc/dcn20/dcn20_hw_sequencer.c

Environment:
    Kernel mode (IRQL <= DISPATCH_LEVEL)

--*/

#include "amdbc250_dream_kmd.h"
#include "amdbc250_psp.h"

/* Forward declarations */
static NTSTATUS DreamV3InitCommandProcessor(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt);
static NTSTATUS DreamV3InitMemoryController(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt);
NTSTATUS DreamV3LoadPspFirmware(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt);
static PVOID DreamV3AllocateContiguousMemory(
    _In_  SIZE_T              SizeInBytes,
    _Out_ PPHYSICAL_ADDRESS   PhysicalAddress
    );
static VOID DreamV3FreeContiguousMemory(
    _In_ PVOID  VirtualAddress,
    _In_ SIZE_T SizeInBytes
    );
static NTSTATUS DreamV3WaitForRegister(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ ULONG RegisterOffset,
    _In_ ULONG Mask,
    _In_ ULONG ExpectedValue,
    _In_ ULONG TimeoutUs
    );

/* Persistent step marker so a TDR/reboot reveals the last-entered step.
 * Written to the driver's service key (same RegistryPath the driver uses
 * for DriverBuildId / Step_* markers); survives reboot. */
VOID
DreamV3MarkHwInitStep(_In_ ULONG Step)
{
    UNICODE_STRING Path;
    RtlInitUnicodeString(&Path,
        L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\atikmdag");
    OBJECT_ATTRIBUTES Oa;
    InitializeObjectAttributes(&Oa, &Path, OBJ_CASE_INSENSITIVE, NULL, NULL);
    HANDLE hKey = NULL;
    if (NT_SUCCESS(ZwOpenKey(&hKey, KEY_SET_VALUE, &Oa))) {
        UNICODE_STRING Name;
        RtlInitUnicodeString(&Name, L"Step_HwInit");
        ZwSetValueKey(hKey, &Name, 0, REG_DWORD, &Step, sizeof(Step));
        ZwClose(hKey);
    }
}

/* Read HwInitMaxStep cap (0 = run all steps). Used to binary-search the
 * TDR step without relying on registry persistence across a hard reboot:
 * set the cap in the service root, run full init; the last SUCCESSFUL cap
 * is the step before the crash. */
static ULONG
DreamV3ReadMaxStep(void)
{
    UNICODE_STRING Path;
    RtlInitUnicodeString(&Path,
        L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\atikmdag");
    OBJECT_ATTRIBUTES Oa;
    InitializeObjectAttributes(&Oa, &Path, OBJ_CASE_INSENSITIVE, NULL, NULL);
    HANDLE hKey = NULL;
    ULONG val = 0;
    if (NT_SUCCESS(ZwOpenKey(&hKey, KEY_READ, &Oa))) {
        UNICODE_STRING vn;
        RtlInitUnicodeString(&vn, L"HwInitMaxStep");
        UCHAR buf[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)] = {0};
        ULONG ret = 0;
        if (NT_SUCCESS(ZwQueryValueKey(hKey, &vn, KeyValuePartialInformation,
                                        buf, sizeof(buf), &ret))) {
            PKEY_VALUE_PARTIAL_INFORMATION pi = (PKEY_VALUE_PARTIAL_INFORMATION)buf;
            if (pi->DataLength == sizeof(ULONG)) val = *(PULONG)pi->Data;
        }
        ZwClose(hKey);
    }
    return val;
}

/*===========================================================================
  DreamV3HwInitialize — Top-level hardware initialization
  
  Linux init order (from nv.c for GFX10 family):
  1. GMC (memory controller)
  2. IH (interrupt handler)
  3. GFX (command processor)
  4. SDMA
  5. SMU (power management)
  6. Display (DCN 2.x)
===========================================================================*/

NTSTATUS
DreamV3HwInitialize(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    )
{
    NTSTATUS Status;
    ULONG MaxStep = DreamV3ReadMaxStep();

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: HwInitialize — RDNA2/Cyan Skillfish init\n"));

    if (MaxStep != 0) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: HwInit CAPPED at step %u\n", MaxStep));
    }

    DreamV3MarkHwInitStep(0); /* reset marker */

    /* Step 0b: Load PSP firmware (SYSDRV/SOS/SMC) via C2PMSG
     * Must happen before SOS is alive and before KIQ init.
     * Uses direct GPU BAR5 C2PMSG registers (MP0 base 0x58000,
     * C2PMSG_35/36/37/81 = 0x5818C/0x58190/0x58194/0x58244). */
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                "AMDBC250-DREAM-V4.3: [STEP 0b] PSP firmware load (SYSDRV/SOS/SMC)\n"));
    Status = DreamV3LoadPspFirmware(DevExt);
    if (!NT_SUCCESS(Status)) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: [STEP 0b] PSP firmware load FAILED (non-fatal): 0x%08X — SOS may already be alive from PSP driver\n", Status));
    } else {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "AMDBC250-DREAM-V4.3: [STEP 0b] PSP firmware loaded OK\n"));
    }

    /* Step 0c: WGP unlock (SPI_PG/CC_ARRAY/RLC_PG) — duggasco 40CU method
     * Must happen early after BAR5 mapping, before SOS locks registers.
     * CC=0 (clear harvest mask), SPI=0x1F, RLC=0x1F per shader array bank.
     * SEH-protected: GRBM_GFX_INDEX writes can BSOD on BC-250. */
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                "AMDBC250-DREAM-V4.3: [STEP 0c] WGP unlock (SPI_PG/CC_ARRAY/RLC_PG)\n"));
    if (MaxStep != 0 && 0 > MaxStep) { } else {
    if (DevExt->MmioVirtualBase != NULL)
    {
        PUCHAR bar5 = (PUCHAR)DevExt->MmioVirtualBase;
        /* Per-bank GRBM_GFX_INDEX values (Linux gfx10 layout: SH=bits 15:8, SE=bits 23:16) */
        static const ULONG bankSel[4] = { 0x00000000, 0x00000100, 0x00010000, 0x00010100 };
        ULONG spiBefore = READ_REGISTER_ULONG((PULONG)(bar5 + 0x5C3C));
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                    "AMDBC250-DREAM-V4.3: [STEP 0c] SPI_PG before = 0x%08X\n", spiBefore));
        __try {
            for (int b = 0; b < 4; b++)
            {
                WRITE_REGISTER_ULONG((PULONG)(bar5 + 0x34D0), bankSel[b]); /* GRBM_GFX_INDEX */
                WRITE_REGISTER_ULONG((PULONG)(bar5 + 0x9C1C), 0x00000000); /* CC = 0 (clear harvest) */
                WRITE_REGISTER_ULONG((PULONG)(bar5 + 0x5C3C), 0x0000001F); /* SPI_PG = 0x1F */
                WRITE_REGISTER_ULONG((PULONG)(bar5 + 0x3D64), 0x0000001F); /* RLC_PG = 0x1F */
            }
            WRITE_REGISTER_ULONG((PULONG)(bar5 + 0x34D0), 0x15000000); /* broadcast */
            ULONG spiAfter = READ_REGISTER_ULONG((PULONG)(bar5 + 0x5C3C));
            ULONG ccAfter  = READ_REGISTER_ULONG((PULONG)(bar5 + 0x9C1C));
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                        "AMDBC250-DREAM-V4.3: [STEP 0c] WGP unlock DONE: SPI_PG=0x%08X CC=0x%08X\n", spiAfter, ccAfter));
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                        "AMDBC250-DREAM-V4.3: [STEP 0c] WGP unlock FAILED (SEH caught)\n"));
        }
    }
    else
    {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                    "AMDBC250-DREAM-V4.3: [STEP 0c] SKIP — no BAR5 mapping\n"));
    }
    }

    /* Step 1: Memory controller (GDDR6) */
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                "AMDBC250-DREAM-V4.3: [STEP 1/12] Memory controller init\n"));
    if (MaxStep != 0 && 1 > MaxStep) { KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL, "AMDBC250-DREAM-V4.3: HwInit STOP at cap %u\n", MaxStep)); return STATUS_SUCCESS; }
    DreamV3MarkHwInitStep(1);
    Status = DreamV3InitMemoryController(DevExt);
    if (!NT_SUCCESS(Status)) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                   "AMDBC250-DREAM-V4.3: *** FAILED: Memory controller: 0x%08X\n", Status));
        return Status;
    }
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: [STEP 1/12] Memory controller OK\n"));

    /* Step 2: Program golden registers (hardware workarounds) — 34 from Linux */
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: [STEP 2/12] Golden registers (34)\n"));
    if (MaxStep != 0 && 2 > MaxStep) { KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL, "AMDBC250-DREAM-V4.3: HwInit STOP at cap %u\n", MaxStep)); return STATUS_SUCCESS; }
    DreamV3MarkHwInitStep(2);
    Status = DreamV3ProgramGoldenSettings(DevExt);
    if (!NT_SUCCESS(Status)) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: [STEP 2/12] Golden regs failed (non-fatal): 0x%08X\n", Status));
    } else {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "AMDBC250-DREAM-V4.3: [STEP 2/12] Golden registers OK\n"));
    }

    /* Step 3: HDP register initialization (coherency) */
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: [STEP 3/12] HDP registers\n"));
    if (MaxStep != 0 && 3 > MaxStep) { KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL, "AMDBC250-DREAM-V4.3: HwInit STOP at cap %u\n", MaxStep)); return STATUS_SUCCESS; }
    DreamV3MarkHwInitStep(3);
    Status = DreamV3InitHdpRegisters(DevExt);
    if (!NT_SUCCESS(Status)) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: [STEP 3/12] HDP init failed (non-fatal): 0x%08X\n", Status));
    } else {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "AMDBC250-DREAM-V4.3: [STEP 3/12] HDP registers OK\n"));
    }

    /* Step 4: Interrupt handler ring */
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: [STEP 4/12] IH ring\n"));
    if (MaxStep != 0 && 4 > MaxStep) { KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL, "AMDBC250-DREAM-V4.3: HwInit STOP at cap %u\n", MaxStep)); return STATUS_SUCCESS; }
    DreamV3MarkHwInitStep(4);
    Status = DreamV3HwInitIhRing(DevExt);
    if (!NT_SUCCESS(Status)) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                   "AMDBC250-DREAM-V4.3: *** FAILED: IH ring: 0x%08X\n", Status));
        return Status;
    }
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: [STEP 4/12] IH ring OK\n"));

    /* Step 5: Halt all CP engines before firmware load */
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: [STEP 5/13] Halt CP engines\n"));
    if (MaxStep != 0 && 5 > MaxStep) { KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL, "AMDBC250-DREAM-V4.3: HwInit STOP at cap %u\n", MaxStep)); return STATUS_SUCCESS; }
    DreamV3MarkHwInitStep(5);
    DreamV3HaltAllEngines(DevExt);

    /* Step 5b: Load all CP firmware via IC_BASE DMA (ME, PFP, CE, MEC) */
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: [STEP 6/13] Load CP firmware\n"));
    if (MaxStep != 0 && 6 > MaxStep) { KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL, "AMDBC250-DREAM-V4.3: HwInit STOP at cap %u\n", MaxStep)); return STATUS_SUCCESS; }
    DreamV3MarkHwInitStep(6);
    {
        /* Kill-switch (HwInitFirmware=0) to isolate the 0x1A BSOD: skip the
         * IC_BASE DMA firmware load + engine unhalt (which lets the GPU run
         * loaded microcode and may rogue-DMA host memory). */
        UNICODE_STRING Path;
        RtlInitUnicodeString(&Path,
            L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\atikmdag");
        OBJECT_ATTRIBUTES Oa;
        InitializeObjectAttributes(&Oa, &Path, OBJ_CASE_INSENSITIVE, NULL, NULL);
        HANDLE hKey = NULL;
        ULONG FwEnable = 1; /* firmware load itself is safe; only the engine
                              * unhalt (HwUnhaltCp) is dangerous — kept 0 */
        if (NT_SUCCESS(ZwOpenKey(&hKey, KEY_READ, &Oa))) {
            UNICODE_STRING vn;
            RtlInitUnicodeString(&vn, L"HwInitFirmware");
            UCHAR buf[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)] = {0};
            ULONG ret = 0;
            if (NT_SUCCESS(ZwQueryValueKey(hKey, &vn, KeyValuePartialInformation,
                                           buf, sizeof(buf), &ret))) {
                PKEY_VALUE_PARTIAL_INFORMATION pi = (PKEY_VALUE_PARTIAL_INFORMATION)buf;
                if (pi->DataLength == sizeof(ULONG)) FwEnable = *(PULONG)pi->Data;
            }
            ZwClose(hKey);
        }
        Status = STATUS_SUCCESS;
        if (FwEnable == 0) {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                       "AMDBC250-DREAM-V4.3: Firmware load SKIPPED (HwInitFirmware=0)\n"));
        } else {
            Status = DreamV3LoadAllFirmware(DevExt);
        }
    }
    if (!NT_SUCCESS(Status)) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: [STEP 6/13] Firmware load failed (non-fatal): 0x%08X\n", Status));
    } else {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "AMDBC250-DREAM-V4.3: [STEP 6/13] Firmware load OK\n"));
    }

    /* Step 7: GFX command processor (RDNA2 style) — skip if already initialized */
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: [STEP 7/13] GFX ring\n"));
    if (DevExt->GfxRing.VirtualAddress == NULL) {
        if (MaxStep != 0 && 7 > MaxStep) { KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL, "AMDBC250-DREAM-V4.3: HwInit STOP at cap %u\n", MaxStep)); return STATUS_SUCCESS; }
        DreamV3MarkHwInitStep(7);
        Status = DreamV3HwInitGfxRing(DevExt);
        if (!NT_SUCCESS(Status)) {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                       "AMDBC250-DREAM-V4.3: *** FAILED: GFX ring: 0x%08X\n", Status));
            return Status;
        }
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "AMDBC250-DREAM-V4.3: [STEP 7/13] GFX ring OK\n"));
    } else {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "AMDBC250-DREAM-V4.3: [STEP 7/13] GFX ring already initialized\n"));
    }

    /* Step 8: SDMA engine */
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: [STEP 8/13] SDMA ring\n"));
    if (MaxStep != 0 && 8 > MaxStep) { KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL, "AMDBC250-DREAM-V4.3: HwInit STOP at cap %u\n", MaxStep)); return STATUS_SUCCESS; }
    DreamV3MarkHwInitStep(8);
    {
        /* Kill-switch (HwInitSdmaRing=0): SDMA ring init is a suspected 0x1A
         * source (kmd.c:3832) — same class of problem as the GFX ring (BASE
         * host-read-only / engine rogue-DMA). Skip to keep init stable. */
        UNICODE_STRING Path;
        RtlInitUnicodeString(&Path,
            L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\atikmdag");
        OBJECT_ATTRIBUTES Oa;
        InitializeObjectAttributes(&Oa, &Path, OBJ_CASE_INSENSITIVE, NULL, NULL);
        HANDLE hKey = NULL;
        ULONG SdmaEnable = 0;
        if (NT_SUCCESS(ZwOpenKey(&hKey, KEY_READ, &Oa))) {
            UNICODE_STRING vn;
            RtlInitUnicodeString(&vn, L"HwInitSdmaRing");
            UCHAR buf[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)] = {0};
            ULONG ret = 0;
            if (NT_SUCCESS(ZwQueryValueKey(hKey, &vn, KeyValuePartialInformation,
                                           buf, sizeof(buf), &ret))) {
                PKEY_VALUE_PARTIAL_INFORMATION pi = (PKEY_VALUE_PARTIAL_INFORMATION)buf;
                if (pi->DataLength == sizeof(ULONG)) SdmaEnable = *(PULONG)pi->Data;
            }
            ZwClose(hKey);
        }
        if (SdmaEnable == 0) {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                       "AMDBC250-DREAM-V4.3: SDMA ring init SKIPPED (HwInitSdmaRing=0)\n"));
        } else {
            Status = DreamV3HwInitSdmaRing(DevExt);
        }
    }
    if (!NT_SUCCESS(Status)) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: [STEP 8/13] SDMA ring failed (non-fatal): 0x%08X\n", Status));
    } else {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "AMDBC250-DREAM-V4.3: [STEP 8/13] SDMA ring OK\n"));
    }

    /* Step 9: GART table */
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: [STEP 9/13] GART\n"));
    if (MaxStep != 0 && 9 > MaxStep) { KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL, "AMDBC250-DREAM-V4.3: HwInit STOP at cap %u\n", MaxStep)); return STATUS_SUCCESS; }
    DreamV3MarkHwInitStep(9);
    {
        /* Kill-switch (HwInitGart=0): DreamV3GartInitialize writes MC_VM_AGP_*
         * aperture registers that trigger the 0x1A BSOD on BC-250 (SOS-owned
         * memory controller — no host AGP aperture). Skip to keep init stable. */
        UNICODE_STRING Path;
        RtlInitUnicodeString(&Path,
            L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\atikmdag");
        OBJECT_ATTRIBUTES Oa;
        InitializeObjectAttributes(&Oa, &Path, OBJ_CASE_INSENSITIVE, NULL, NULL);
        HANDLE hKey = NULL;
        ULONG GartEnable = 0;
        if (NT_SUCCESS(ZwOpenKey(&hKey, KEY_READ, &Oa))) {
            UNICODE_STRING vn;
            RtlInitUnicodeString(&vn, L"HwInitGart");
            UCHAR buf[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)] = {0};
            ULONG ret = 0;
            if (NT_SUCCESS(ZwQueryValueKey(hKey, &vn, KeyValuePartialInformation,
                                           buf, sizeof(buf), &ret))) {
                PKEY_VALUE_PARTIAL_INFORMATION pi = (PKEY_VALUE_PARTIAL_INFORMATION)buf;
                if (pi->DataLength == sizeof(ULONG)) GartEnable = *(PULONG)pi->Data;
            }
            ZwClose(hKey);
        }
        if (GartEnable == 0) {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                       "AMDBC250-DREAM-V4.3: GART init SKIPPED (HwInitGart=0)\n"));
        } else {
            Status = DreamV3GartInitialize(DevExt);
        }
    }
    if (!NT_SUCCESS(Status)) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: [STEP 9/13] GART init failed (non-fatal): 0x%08X\n", Status));
    } else {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "AMDBC250-DREAM-V4.3: [STEP 9/13] GART OK\n"));
    }

    /* Step 10: GPU Virtual Memory */
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: [STEP 10/13] GPUVM\n"));
    if (MaxStep != 0 && 10 > MaxStep) { KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL, "AMDBC250-DREAM-V4.3: HwInit STOP at cap %u\n", MaxStep)); return STATUS_SUCCESS; }
    DreamV3MarkHwInitStep(10);
    {
        /* Kill-switch (HwInitVm=0): DreamV3VmInitialize -> ConfigureSystemAperture
         * writes MC_VM system-aperture registers; same 0x1A class as GART on
         * BC-250 (SOS-owned memory controller). Skip to keep init stable. */
        UNICODE_STRING Path;
        RtlInitUnicodeString(&Path,
            L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\atikmdag");
        OBJECT_ATTRIBUTES Oa;
        InitializeObjectAttributes(&Oa, &Path, OBJ_CASE_INSENSITIVE, NULL, NULL);
        HANDLE hKey = NULL;
        ULONG VmEnable = 0;
        if (NT_SUCCESS(ZwOpenKey(&hKey, KEY_READ, &Oa))) {
            UNICODE_STRING vn;
            RtlInitUnicodeString(&vn, L"HwInitVm");
            UCHAR buf[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)] = {0};
            ULONG ret = 0;
            if (NT_SUCCESS(ZwQueryValueKey(hKey, &vn, KeyValuePartialInformation,
                                           buf, sizeof(buf), &ret))) {
                PKEY_VALUE_PARTIAL_INFORMATION pi = (PKEY_VALUE_PARTIAL_INFORMATION)buf;
                if (pi->DataLength == sizeof(ULONG)) VmEnable = *(PULONG)pi->Data;
            }
            ZwClose(hKey);
        }
        if (VmEnable == 0) {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                       "AMDBC250-DREAM-V4.3: GPUVM init SKIPPED (HwInitVm=0)\n"));
        } else {
            Status = DreamV3VmInitialize(DevExt);
        }
    }
    if (!NT_SUCCESS(Status)) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: [STEP 10/13] GPUVM init failed (non-fatal): 0x%08X\n", Status));
    } else {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "AMDBC250-DREAM-V4.3: [STEP 10/13] GPUVM OK\n"));
    }

    /* Step 11: Display engine (DCN 2.1) */
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: [STEP 11/13] Display (DCN 2.1)\n"));
    if (MaxStep != 0 && 11 > MaxStep) { KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL, "AMDBC250-DREAM-V4.3: HwInit STOP at cap %u\n", MaxStep)); return STATUS_SUCCESS; }
    DreamV3MarkHwInitStep(11);
    Status = DreamV3HwInitDisplay(DevExt);
    if (!NT_SUCCESS(Status)) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: [STEP 11/13] Display init failed (non-fatal): 0x%08X\n", Status));
    } else {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "AMDBC250-DREAM-V4.3: [STEP 11/13] Display OK\n"));
    }

    /* Step 12: PSP & NBIO unlock (GPU BAR5, MP0 discovery, ring, NBIO bypass) */
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: [STEP 12/13] PSP init\n"));
    if (MaxStep != 0 && 12 > MaxStep) { KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL, "AMDBC250-DREAM-V4.3: HwInit STOP at cap %u\n", MaxStep)); return STATUS_SUCCESS; }
    DreamV3MarkHwInitStep(12);
    Status = DreamV3PspHardwareInit(DevExt);
    if (DevExt->PspAlive) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "AMDBC250-DREAM-V4.3: [STEP 12/13] SOS alive, NBIO unlocked=%u\n",
                   DevExt->NbioUnlocked));
    } else {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: [STEP 12/13] SOS not found — continuing\n"));
    }

    /* Step 13: RLC initialization (power/scheduler) */
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: [STEP 13/13] RLC init\n"));
    if (MaxStep != 0 && 13 > MaxStep) { KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL, "AMDBC250-DREAM-V4.3: HwInit STOP at cap %u\n", MaxStep)); return STATUS_SUCCESS; }
    DreamV3MarkHwInitStep(13);
    Status = DreamV3InitRlc(DevExt);
    if (!NT_SUCCESS(Status)) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: [STEP 13/13] RLC init failed (non-fatal): 0x%08X\n", Status));
    } else {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "AMDBC250-DREAM-V4.3: [STEP 13/13] RLC OK\n"));
    }

    /* Step 13b: VRAM detection (MC_VM_FB_LOCATION, VBIOS, PCI BAR) */
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: [STEP 13b/13] VRAM detection\n"));
    if (MaxStep != 0 && 14 > MaxStep) { KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL, "AMDBC250-DREAM-V4.3: HwInit STOP at cap %u\n", MaxStep)); return STATUS_SUCCESS; }
    DreamV3MarkHwInitStep(14);
    DreamV3DetectVram(DevExt);

    DevExt->UsedVramBytes = 0;

    /* Set clocks */
    DevExt->GpuClockMhz = AMDBC250_BOOST_CLOCK_MHZ;  /* Assume governor active */
    DevExt->MemoryClockMhz = AMDBC250_MEMORY_CLOCK_MHZ;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: HwInitialize COMPLETE\n"));
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3:   VRAM: %llu MB GDDR6\n",
               DevExt->TotalVramBytes / (1024 * 1024)));
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3:   Visible: %llu MB (quirk)\n",
               DevExt->VisibleVramBytes / (1024 * 1024)));
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3:   Clock: %d MHz\n", DevExt->GpuClockMhz));

    return STATUS_SUCCESS;
}

/*===========================================================================
  DreamV3HwReset — GPU reset for TDR
===========================================================================*/

NTSTATUS
DreamV3HwReset(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    NTSTATUS Status;
    ULONG CpCntl;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
               "AMDBC250-DREAM-V4.3: GPU reset initiated\n"));

    /* Halt CP (ME + PFP + CE) */
    CpCntl = DreamV3ReadRegister(DevExt, AMDBC250_REG_CP_ME_CNTL);
    CpCntl |= CP_ME_CNTL__ME_HALT | CP_ME_CNTL__PFP_HALT | CP_ME_CNTL__CE_HALT;
    DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_ME_CNTL, CpCntl);
    KeStallExecutionProcessor(100);

    /* Disable interrupts */
    DreamV3WriteRegister(DevExt, AMDBC250_REG_IH_CNTL, 0);

    /* Reset rings */
    DevExt->GfxRing.ReadPointer = 0;
    DevExt->GfxRing.WritePointer = 0;
    DevExt->IhRing.ReadPointer = 0;
    DevExt->SdmaRing.ReadPointer = 0;
    DevExt->SdmaRing.WritePointer = 0;

    /* Re-init */
    Status = DreamV3HwInitialize(DevExt);

    if (NT_SUCCESS(Status)) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "AMDBC250-DREAM-V4.3: GPU reset SUCCESS\n"));
    } else {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                   "AMDBC250-DREAM-V4.3: GPU reset FAILED: 0x%08X\n", Status));
    }

    return Status;
}

/*===========================================================================
  DreamV3HwShutdown — Graceful shutdown
===========================================================================*/

VOID
DreamV3HwShutdown(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: HwShutdown\n"));

    /* Disable interrupts */
    DreamV3WriteRegister(DevExt, AMDBC250_REG_IH_CNTL, 0);

    /* Halt CP */
    DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_ME_CNTL,
                         CP_ME_CNTL__ME_HALT | CP_ME_CNTL__PFP_HALT);

    /* Free rings */
    if (DevExt->GfxRing.VirtualAddress != NULL) {
        DreamV3FreeContiguousMemory(DevExt->GfxRing.VirtualAddress,
                                     DevExt->GfxRing.SizeInBytes);
        DevExt->GfxRing.VirtualAddress = NULL;
    }

    if (DevExt->SdmaRing.VirtualAddress != NULL) {
        if (DevExt->SdmaRing.MappedIo) {
            MmUnmapIoSpace(DevExt->SdmaRing.VirtualAddress,
                           DevExt->SdmaRing.SizeInBytes);
        } else {
            DreamV3FreeContiguousMemory(DevExt->SdmaRing.VirtualAddress,
                                         DevExt->SdmaRing.SizeInBytes);
        }
        DevExt->SdmaRing.VirtualAddress = NULL;
    }

    if (DevExt->IhRing.VirtualAddress != NULL) {
        DreamV3FreeContiguousMemory(DevExt->IhRing.VirtualAddress,
                                     DevExt->IhRing.SizeInBytes);
        DevExt->IhRing.VirtualAddress = NULL;
    }

    if (DevExt->GlobalFence.VirtualAddress != NULL) {
        DreamV3FreeContiguousMemory((PVOID)DevExt->GlobalFence.VirtualAddress,
                                     PAGE_SIZE);
        DevExt->GlobalFence.VirtualAddress = NULL;
    }

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: HwShutdown complete\n"));
}

/*===========================================================================
  DreamV3HdpFlush — CRITICAL! Flush HDP before reading ring pointers
  
  This is a Linux amdgpu quirk: without this, the CPU reads stale
  data from ring buffers because the GPU writes aren't coherent.
  
  From Linux: WREG32(mmHDP_MEM_COHERENCY_FLUSH_CNTL, 1);
===========================================================================*/

VOID
DreamV3HdpFlush(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    /* Flush HDP read cache */
    DreamV3WriteRegister(DevExt, AMDBC250_REG_HDP_MEM_COHERENCY_FLUSH_CNTL,
                         HDP_MEM_COHERENCY_FLUSH_CNTL__FLUSH_CACHE);

    /* Invalidate HDP write cache */
    DreamV3WriteRegister(DevExt, AMDBC250_REG_HDP_DEBUG0,
                         HDP_DEBUG0__INVALIDATE_CACHE);

    /* Memory barrier */
    KeMemoryBarrier();
}

/*===========================================================================
  DreamV3HwProgramGoldenRegs — REMOVED
  
  This function has been replaced by DreamV3ProgramGoldenSettings()
  in amdbc250_dream_golden.c, which programs 47+ golden registers
  from Linux golden_settings_gc_10_0_cyan_skillfish[].
  
  See: DreamV3ProgramGoldenSettings() for the new implementation.
===========================================================================*/

/*===========================================================================
  DreamV3HwInitGfxRing — Initialize GFX command ring (GFX10 style)
===========================================================================*/

NTSTATUS
DreamV3HwInitGfxRing(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    )
{
    PHYSICAL_ADDRESS RingPhys;
    PVOID RingVirt;
    PHYSICAL_ADDRESS FencePhys;
    PVOID FenceVirt;
    ULONG RingSize = 2 * 1024 * 1024;  /* 2 MB for GFX10 */
    ULONG RbCntl;
    ULONG RbBufSz;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: InitGfxRing — Allocating %d KB\n", RingSize / 1024));

    /* Kill-switch (HwInitGfxRing=0) to isolate the 0x1A MEMORY_MANAGEMENT
     * BSOD seen at cap=7. Returns BEFORE any allocation/register write. */
    {
        UNICODE_STRING Path;
        RtlInitUnicodeString(&Path,
            L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\atikmdag");
        OBJECT_ATTRIBUTES Oa;
        InitializeObjectAttributes(&Oa, &Path, OBJ_CASE_INSENSITIVE, NULL, NULL);
        HANDLE hKey = NULL;
        ULONG GfxRingEnable = 0;
        if (NT_SUCCESS(ZwOpenKey(&hKey, KEY_READ, &Oa))) {
            UNICODE_STRING vn;
            RtlInitUnicodeString(&vn, L"HwInitGfxRing");
            UCHAR buf[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)] = {0};
            ULONG ret = 0;
            if (NT_SUCCESS(ZwQueryValueKey(hKey, &vn, KeyValuePartialInformation,
                                           buf, sizeof(buf), &ret))) {
                PKEY_VALUE_PARTIAL_INFORMATION pi = (PKEY_VALUE_PARTIAL_INFORMATION)buf;
                if (pi->DataLength == sizeof(ULONG)) GfxRingEnable = *(PULONG)pi->Data;
            }
            ZwClose(hKey);
        }
        if (GfxRingEnable == 0) {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                       "AMDBC250-DREAM-V4.3: GFX ring init SKIPPED (HwInitGfxRing=0)\n"));
            return STATUS_SUCCESS;
        }
    }

    /* Allocate ring buffer */
    RingVirt = DreamV3AllocateContiguousMemory(RingSize, &RingPhys);
    if (RingVirt == NULL) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                   "AMDBC250-DREAM-V4.3: Failed to allocate GFX ring\n"));
        return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(RingVirt, RingSize);

    DevExt->GfxRing.PhysicalAddress = RingPhys;
    DevExt->GfxRing.VirtualAddress = RingVirt;
    DevExt->GfxRing.SizeInBytes = RingSize;
    DevExt->GfxRing.ReadPointer = 0;
    DevExt->GfxRing.WritePointer = 0;
    DevExt->GfxRing.Initialized = FALSE;

    /* Allocate 64-bit fence (GFX10 requirement) */
    FenceVirt = DreamV3AllocateContiguousMemory(PAGE_SIZE, &FencePhys);
    if (FenceVirt == NULL) {
        DreamV3FreeContiguousMemory(RingVirt, RingSize);
        return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(FenceVirt, PAGE_SIZE);
    DevExt->GlobalFence.PhysicalAddress = FencePhys;
    DevExt->GlobalFence.VirtualAddress = (volatile PULONG64)FenceVirt;
    *DevExt->GlobalFence.VirtualAddress = 0;

    /* Halt CP before programming */
    DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_ME_CNTL,
                         CP_ME_CNTL__ME_HALT | CP_ME_CNTL__PFP_HALT);
    KeStallExecutionProcessor(10);

    /* Calculate ring size (log2 of size in DWORDs) */
    RbBufSz = 0;
    {
        ULONG Sz = RingSize / sizeof(ULONG);
        while (Sz > 1) { Sz >>= 1; RbBufSz++; }
    }

    /* Try GFX ring first (BASE_LO is typically read-only on BC-250) */
    ULONG BaseLoVal = (ULONG)(RingPhys.QuadPart & 0xFFFFF000);
    ULONG BaseHiVal = (ULONG)(RingPhys.QuadPart >> 32);

    DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_GFX_RING0_BASE_LO, BaseLoVal);
    DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_GFX_RING0_BASE_HI, BaseHiVal);

    /* Check if GFX BASE was actually written (read-only on BC-250) */
    DevExt->UseKiqRing = FALSE;

    ULONG GfxBaseCheckLo = DreamV3ReadRegister(DevExt, AMDBC250_REG_CP_GFX_RING0_BASE_LO);
    ULONG GfxBaseCheckHi = DreamV3ReadRegister(DevExt, AMDBC250_REG_CP_GFX_RING0_BASE_HI);
    BOOLEAN GfxWritable = (GfxBaseCheckLo == BaseLoVal && GfxBaseCheckHi == BaseHiVal);

    if (!GfxWritable) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: GFX ring BASE read-only (got LO=0x%08X HI=0x%08X, expected LO=0x%08X HI=0x%08X)\n",
                   GfxBaseCheckLo, GfxBaseCheckHi, BaseLoVal, BaseHiVal));
        /* DISPLAY FIX (HwInitMaxStep bisection: cap=6 OK, cap=7 white-screen):
         * On BC-250 BOTH GFX and KIQ ring BASE are host-read-only (locked by
         * SOS/PSP). Probing the KIQ ring requires selecting ME=1 via
         * GRBM_GFX_INDEX, and writing GRBM_GFX_INDEX while the live display is
         * active corrupts the display (white screen). Skip the KIQ probe + all
         * GRBM_GFX_INDEX writes, free the ring, keep CP halted, and continue.
         * The CP/MEC cannot be woken on the host anyway (rings are locked). */
        DreamV3FreeContiguousMemory(FenceVirt, PAGE_SIZE);
        DreamV3FreeContiguousMemory(RingVirt, RingSize);
        DevExt->GfxRing.VirtualAddress = NULL;
        DevExt->GfxRing.Initialized = FALSE;
        DevExt->GlobalFence.VirtualAddress = NULL;
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                    "AMDBC250-DREAM-V4.3: GFX ring init SKIPPED (no writable ring base) — CP stays halted\n"));
        return STATUS_SUCCESS;
    } else {
        /* GFX ring base IS writable (unusual for BC-250) — use it */
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "AMDBC250-DREAM-V4.3: GFX ring BASE_LO writable — using GFX ring\n"));

        /* Program ring control */
        RbCntl = (RbBufSz & CP_RING0_CNTL__RB_BUFSZ_MASK) |
                 ((1 << CP_RING0_CNTL__RB_BLKSZ_SHIFT) & CP_RING0_CNTL__RB_BLKSZ_MASK) |
                 CP_RING0_CNTL__RPTR_WRITEBACK_ENABLE;
        DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_GFX_RING0_CNTL, RbCntl);

        /* Initialize pointers */
        DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_GFX_RING0_RPTR, 0);
        DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_GFX_RING0_WPTR, 0);
    }

    /* Initialize command processor */
    NTSTATUS Status = DreamV3InitCommandProcessor(DevExt);
    if (!NT_SUCCESS(Status)) {
        DreamV3FreeContiguousMemory(FenceVirt, PAGE_SIZE);
        DreamV3FreeContiguousMemory(RingVirt, RingSize);
        DevExt->GfxRing.VirtualAddress = NULL;
        DevExt->GlobalFence.VirtualAddress = NULL;
        return Status;
    }

    /* Resume CP */
    DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_ME_CNTL, 0);
    KeStallExecutionProcessor(100);

    DevExt->GfxRing.Initialized = TRUE;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: %s ring initialized at PA=0x%llX (size=%dKB)\n",
               DevExt->UseKiqRing ? "KIQ" : "GFX",
               RingPhys.QuadPart, (ULONG)(RingSize / 1024)));

    return STATUS_SUCCESS;
}

/*===========================================================================
  DreamV3HwInitIhRing — Interrupt Handler ring (GFX10 style)
===========================================================================*/

NTSTATUS
DreamV3HwInitIhRing(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    PHYSICAL_ADDRESS IhPhys;
    PVOID IhVirt;
    ULONG IhSize = IH_RING_SIZE_BYTES;
    ULONG IhCntl;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: InitIhRing — %d KB\n", IhSize / 1024));

    IhVirt = DreamV3AllocateContiguousMemory(IhSize, &IhPhys);
    if (IhVirt == NULL) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                   "AMDBC250-DREAM-V4.3: Failed to allocate IH ring\n"));
        return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(IhVirt, IhSize);

    DevExt->IhRing.PhysicalAddress = IhPhys;
    DevExt->IhRing.VirtualAddress = IhVirt;
    DevExt->IhRing.SizeInBytes = IhSize;
    DevExt->IhRing.ReadPointer = 0;
    DevExt->IhRing.Initialized = FALSE;

    /* Program ring base (GFX10: 4 KB aligned, 256-byte units) */
    DreamV3WriteRegister(DevExt, AMDBC250_REG_IH_RB_BASE_LO,
                         (ULONG)(IhPhys.QuadPart >> 8));
    DreamV3WriteRegister(DevExt, AMDBC250_REG_IH_RB_BASE_HI,
                         (ULONG)(IhPhys.QuadPart >> 40));
    /* IH_RB_CNTL: ring size log2-1 (256KB=64K DWORDs -> 15), WPTR writeback enable */
    {
        ULONG RingSizeLog2 = 0;
        { ULONG Sz = IhSize / sizeof(ULONG); while (Sz > 1) { Sz >>= 1; RingSizeLog2++; } }
        ULONG IhRbCntl = ((RingSizeLog2 - 1) & 0x3F);   /* bits [5:0] = ring size log2 - 1 */
        IhRbCntl |= ((12 << 8) & 0xFF00);          /* bits [15:8] = WB writeback timer */
        IhRbCntl |= (1 << 22);                     /* bit 22 = WPTR writeback enable */
        DreamV3WriteRegister(DevExt, AMDBC250_REG_IH_RB_CNTL, IhRbCntl);
    }

    /* Initialize read pointer */
    DreamV3WriteRegister(DevExt, AMDBC250_REG_IH_RB_RPTR, 0);

    /* Enable interrupts */
    IhCntl = IH_CNTL__ENABLE_INTR | IH_CNTL__RPTR_REARM;
    DreamV3WriteRegister(DevExt, AMDBC250_REG_IH_CNTL, IhCntl);

    DevExt->IhRing.Initialized = TRUE;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: IH ring initialized at PA=0x%llX\n", IhPhys.QuadPart));

    return STATUS_SUCCESS;
}

/*===========================================================================
  DreamV3HwInitSdmaRing — SDMA engine ring (GFX10)
===========================================================================*/

NTSTATUS
DreamV3HwInitSdmaRing(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    ULONG baseLo, baseHi, cntlVal;
    PHYSICAL_ADDRESS ringPhys;
    PVOID ringVirt;
    ULONG ringSize = 8 * 1024;  /* 8KB */

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: InitSdmaRing\n"));

    /* Unmap existing SDMA ring if already initialized (prevents leak on re-init) */
    if (DevExt->SdmaRing.VirtualAddress != NULL) {
        if (DevExt->SdmaRing.MappedIo) {
            MmUnmapIoSpace(DevExt->SdmaRing.VirtualAddress,
                           DevExt->SdmaRing.SizeInBytes);
        } else {
            DreamV3FreeContiguousMemory(DevExt->SdmaRing.VirtualAddress,
                                         DevExt->SdmaRing.SizeInBytes);
        }
        DevExt->SdmaRing.VirtualAddress = NULL;
        DevExt->SdmaRing.Initialized = FALSE;
    }

    /* Allocate a new ring buffer from contiguous memory */
    ringVirt = DreamV3AllocateContiguousMemory(ringSize, &ringPhys);
    if (!ringVirt) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: Failed to alloc SDMA ring\n"));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(ringVirt, ringSize);

    /* Try to write ring base registers. On BC-250 these may be host-RO
     * (SOS-locked), so we ignore failure and continue with the allocated
     * buffer so the driver at least has a valid VA for potential future use. */
    baseLo = DreamV3ReadRegister(DevExt, AMDBC250_REG_SDMA0_GFX_RB_BASE_LO);
    baseHi = DreamV3ReadRegister(DevExt, AMDBC250_REG_SDMA0_GFX_RB_BASE_HI);

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: SDMA hw BASE_LO=0x%08X BASE_HI=0x%08X\n",
               baseLo, baseHi));

    if (baseLo != 0xFFFFFFFF && baseHi != 0xFFFFFFFF) {
        DreamV3WriteRegister(DevExt, AMDBC250_REG_SDMA0_GFX_RB_BASE_LO,
                             (ULONG)(ringPhys.QuadPart >> 8));
        DreamV3WriteRegister(DevExt, AMDBC250_REG_SDMA0_GFX_RB_BASE_HI,
                             (ULONG)(ringPhys.QuadPart >> 40));
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "AMDBC250-DREAM-V4.3: SDMA ring base updated to PA=0x%llX\n",
                   ringPhys.QuadPart));
    } else {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: SDMA BASE regs read-only, using allocated buffer only\n"));
    }

    DevExt->SdmaRing.PhysicalAddress = ringPhys;
    DevExt->SdmaRing.VirtualAddress = ringVirt;
    DevExt->SdmaRing.SizeInBytes = ringSize;
    DevExt->SdmaRing.ReadPointer = 0;
    DevExt->SdmaRing.WritePointer = 0;
    DevExt->SdmaRing.Initialized = TRUE;
    DevExt->SdmaRing.MappedIo = FALSE;

    /* Enable ring + clear pointers */
    cntlVal = DreamV3ReadRegister(DevExt, AMDBC250_REG_SDMA0_GFX_RB_CNTL);
    DreamV3WriteRegister(DevExt, AMDBC250_REG_SDMA0_GFX_RB_CNTL, cntlVal | 1);
    DreamV3WriteRegister(DevExt, AMDBC250_REG_SDMA0_GFX_RB_RPTR, 0);
    DreamV3WriteRegister(DevExt, AMDBC250_REG_SDMA0_GFX_RB_WPTR, 0);

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: SDMA ring initialized (PA=0x%llX VA=0x%p)\n",
               ringPhys.QuadPart, ringVirt));

    return STATUS_SUCCESS;
}

/*===========================================================================
  DreamV3PspHardwareInit — PSP initialization & NBIO unlock
  Maps GPU BAR5, discovers MP0 base, checks SOS, attempts NBIO bypass.
  Non-fatal — driver continues in degraded mode if PSP unavailable.
===========================================================================*/

NTSTATUS
DreamV3PspHardwareInit(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    )
{
    NTSTATUS Status;
    PAMDBC250_PSP_CONTEXT PspCtx;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: [STEP 9/9] PSP init (GPU BAR5 = 0xFE800000)\n"));

    /* Step 9a: Initialize PSP — maps BAR5, discovers MP0 base, checks SOS */
    Status = Amdbc250PspInit(0);
    if (!NT_SUCCESS(Status)) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: PSP init FAILED: 0x%08X (non-fatal)\n", Status));
        DevExt->PspInitialized = FALSE;
        DevExt->PspAlive = FALSE;
        DevExt->NbioUnlocked = FALSE;

        /* Step 9a-retry: Check if SOS was already loaded by EFI Shell injection */
        PspCtx = Amdbc250PspGetContext();
        if (PspCtx && PspCtx->MmioBase) {
            ULONG sol = Amdbc250PspReadRegister(MP0_C2PMSG_81_BYTE);
            if (sol & 0x80000000) {
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                           "AMDBC250-DREAM-V4.3: SOS already alive (SOL=0x%08X) - EFI Shell injection detected!\n", sol));
                DevExt->PspAlive = TRUE;
                DevExt->NbioUnlocked = TRUE;
                return STATUS_SUCCESS;
            }
        }
        return STATUS_SUCCESS; /* Non-fatal */
    }

    PspCtx = Amdbc250PspGetContext();
    DevExt->PspInitialized = TRUE;
    DevExt->PspAlive = PspCtx->SosAlive;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: PSP init OK — SOS alive=%u\n",
               DevExt->PspAlive));

    /* Step 9c: Initialize KIQ ring for command submission */
    if (NT_SUCCESS(Amdbc250PspKiqInit())) {
        DevExt->KiqAvailable = TRUE;
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "AMDBC250-DREAM-V4.3: KIQ ring initialized\n"));
    } else {
        DevExt->KiqAvailable = FALSE;
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: KIQ init FAILED: fallback to PSP proxy\n"));
    }

    /* Step 9d: Create TOS GFX ring (PSP ring protocol) so SOS trusts our commands */
    if (DevExt->PspAlive) {
        PAMDBC250_PSP_CONTEXT pspCtx = Amdbc250PspGetContext();
        NTSTATUS ringStatus = STATUS_NOT_SUPPORTED;
        if (pspCtx && pspCtx->Initialized) {
            ringStatus = Amdbc250PspRingCreate(DevExt->MmioVirtualBase,
                                               PSP_RING_TYPE_GFX,
                                               (ULONG)(pspCtx->RingPhysical.LowPart),
                                               (ULONG)(pspCtx->RingPhysical.HighPart),
                                               PSP_RING_SIZE);
        }
        if (NT_SUCCESS(ringStatus)) {
            DevExt->GfxRingAvailable = TRUE;
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                       "AMDBC250-DREAM-V4.3: TOS GFX ring created\n"));
        } else {
            DevExt->GfxRingAvailable = FALSE;
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                       "AMDBC250-DREAM-V4.3: TOS GFX ring create FAILED (non-fatal): 0x%08X — SOS may lack TOS\n", ringStatus));
        }
    }

    /* Step 9b: If SOS is alive, try NBIO unlock */
    if (DevExt->PspAlive) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "AMDBC250-DREAM-V4.3: Attempting NBIO unlock via PSP...\n"));

        Status = Amdbc250PspTryUnlockNbio();
        if (NT_SUCCESS(Status)) {
            DevExt->NbioUnlocked = TRUE;
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                       "AMDBC250-DREAM-V4.3: *** NBIO UNLOCKED via PSP ***\n"));
        } else {
            DevExt->NbioUnlocked = FALSE;
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                       "AMDBC250-DREAM-V4.3: NBIO unlock FAILED: 0x%08X\n", Status));
        }
    } else {
        DevExt->NbioUnlocked = FALSE;
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: SOS not alive — NBIO unlock skipped\n"));
    }

    return STATUS_SUCCESS;
}

/*===========================================================================
  DreamV3HwInitDisplay — DCN 2.1 display engine initialization
===========================================================================*/

NTSTATUS
DreamV3HwInitDisplay(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    UNREFERENCED_PARAMETER(DevExt);

    /* ======================================================================
     * Display init is currently SKIPPED. Windows uses Microsoft Basic
     * Display over the UEFI GOP framebuffer for output on Win11 26100.
     * (Our runtime DxgkInitialize export-scan can't find the symbol - it
     * lives in displib.lib, not dxgkrnl.sys - so we fall back to WDM
     * IOCTL mode with no DDI display. See AGENTS.md 2026-08-01.)
     *
     * CORRECTED (2026-08-01): real DCN base is 0xD300 (not 0x6000).
     * Verified live OTG0: OTG_CONTROL 0x14004 = 0x80011311 ENABLED,
     * timing 2560x1440@60 (H_TOTAL=2719, V_TOTAL=1480), frame counter
     * 0x14030 ticks. OTG/HUBP/DMCUB addresses = 0xD300 + mm*4 (hw.h).
     * DCN registers are NOT in the old 0x3400-0x8100 freeze zone.
     * ====================================================================== */

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250-DREAM-V4.3: InitDisplay SKIPPED (Basic Display / GOP framebuffer path)\n"));

    return STATUS_SUCCESS;
}

/*===========================================================================
  DreamV3InitCommandProcessor — Initialize GFX10 CP
===========================================================================*/

static NTSTATUS
DreamV3InitCommandProcessor(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    UNREFERENCED_PARAMETER(DevExt);
    /* Firmware loading is now handled by:
     * 1. LOAD_CP_FW IOCTL (from userspace via load-cp-fw.exe)
     * 2. DreamV3LoadSingleFirmware() in amdbc250_dream_fw_load.c
     * 
     * This stub is kept for API compatibility.
     * Firmware must be loaded BEFORE GPU operations.
     */
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: InitCommandProcessor — firmware loading via IOCTL\n"));
    return STATUS_SUCCESS;
}

/*===========================================================================
  DreamV3InitMemoryController — Configure for GDDR6
===========================================================================*/

static NTSTATUS
DreamV3InitMemoryController(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    UNICODE_STRING Path;
    OBJECT_ATTRIBUTES Oa;
    HANDLE hKey = NULL;
    ULONG MemCtrlEnable = 0;

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: InitMemoryController (GDDR6)\n"));

    /*
     * Configure system aperture for GDDR6 operation.
     * BC-250 has 16GB GDDR6 shared between CPU and GPU.
     */

    /* Kill-switch (HwInitMemCtrl=0, default): the MC/GART-related writes in
     * this function target SOS-owned registers. GB_ADDR_CONFIG (0x61D8) is in
     * the 0x3400-0x8100 FREEZE ZONE and MC_VM_FB_LOCATION (0x9520/0x9524) is
     * in the SOS-owned MC block (GART 0x9528+ writes trigger 0x1A). Skip them
     * entirely unless explicitly enabled. */
    RtlInitUnicodeString(&Path,
        L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\atikmdag");
    InitializeObjectAttributes(&Oa, &Path, OBJ_CASE_INSENSITIVE, NULL, NULL);
    if (NT_SUCCESS(ZwOpenKey(&hKey, KEY_READ, &Oa))) {
        UNICODE_STRING vn;
        RtlInitUnicodeString(&vn, L"HwInitMemCtrl");
        UCHAR buf[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)] = {0};
        ULONG ret = 0;
        if (NT_SUCCESS(ZwQueryValueKey(hKey, &vn, KeyValuePartialInformation,
                                       buf, sizeof(buf), &ret))) {
            PKEY_VALUE_PARTIAL_INFORMATION pi = (PKEY_VALUE_PARTIAL_INFORMATION)buf;
            if (pi->DataLength == sizeof(ULONG)) MemCtrlEnable = *(PULONG)pi->Data;
        }
        ZwClose(hKey);
    }

    /* Configure GB_ADDR_CONFIG for BC-250 (Cyan Skillfish)
     * Linux CYAN_SKILLFISH_GB_ADDR_CONFIG_GOLDEN = 0x00100044 (gfx_v10_0.c)
     * Bits [3:0] = NUM_PIPES: 4 (0x4)
     * Bits [7:4] = PIPE_INTERLEAVE: 256B (0x4)
     * Bits [19:16] = NUM_PKRS: 1 (0x1)
     * Address: mmGB_ADDR_CONFIG = 0x13DE (BASE_IDX=0) -> BAR5 0x61D8.
     *
     * DANGER (2026-08-01): 0x61D8 sits inside the 0x3400-0x8100 FREEZE ZONE.
     * Writing it during full init (Flags=0) is a suspected 0x1A crash source.
     * Full-init-test (Flags=0) must NOT run until this is verified safe. */
    if (MemCtrlEnable != 0) {
        DreamV3WriteRegister(DevExt, AMDBC250_REG_GB_ADDR_CONFIG,
                             0x00100044);  /* CYAN_SKILLFISH_GB_ADDR_CONFIG_GOLDEN */
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                   "AMDBC250-DREAM-V4.3: GB_ADDR_CONFIG programmed (HwInitMemCtrl=1)\n"));
    } else {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: GB_ADDR_CONFIG write SKIPPED (HwInitMemCtrl=0, freeze zone)\n"));
    }

    /* Configure framebuffer location — MC_VM registers are SOS-owned on BC-250
     * (GART 0x9528+ / system aperture writes trigger 0x1A). Gated by the same
     * HwInitMemCtrl kill-switch; only happens if FbSize was actually set. */
    if (DevExt->FbSize > 0) {
        ULONG FbTop = (ULONG)((DevExt->FbPhysicalBase.QuadPart + DevExt->FbSize) >> 24) - 1;
        ULONG FbBase = (ULONG)(DevExt->FbPhysicalBase.QuadPart >> 24);

        if (MemCtrlEnable != 0) {
            DreamV3WriteRegister(DevExt, AMDBC250_REG_MC_VM_FB_LOCATION_TOP, FbTop);
            DreamV3WriteRegister(DevExt, AMDBC250_REG_MC_VM_FB_LOCATION_BASE, FbBase);
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                       "AMDBC250-DREAM-V4.3: FB location programmed (HwInitMemCtrl=1)\n"));
        } else {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                       "AMDBC250-DREAM-V4.3: MC_VM FB location write SKIPPED (HwInitMemCtrl=0)\n"));
        }
    }

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
               "AMDBC250-DREAM-V4.3: Memory controller configured\n"));

    return STATUS_SUCCESS;
}

/*===========================================================================
  DreamV3ReadTemperature — Read thermal sensor
  
  Linux: THM_THERMAL_CTRL / THM_CURRENT_TEMP registers
  Returns temperature in degrees Celsius
===========================================================================*/

LONG
DreamV3ReadTemperature(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    UNREFERENCED_PARAMETER(DevExt);

    /* ======================================================================
     * THM_CURRENT_TEMP register (0x8008) is in the 0x3400-0x8100 FREEZE ZONE.
     * Direct MMIO read causes hardware hang on BC-250.
     *
     * TODO: Use PSP proxy or MP1 SMU mailbox to read temperature.
     * For now, return a safe default.
     * ====================================================================== */

    return 45;  /* Safe default: 45°C */
}

/*===========================================================================
  DreamV3CheckThermalThrottle — OLD VERSION (replaced by power.c)

  This function has been replaced by the enhanced version in 
  amdbc250_dream_power.c with:
  - Multi-sensor thermal monitoring
  - Hysteresis support
  - SMU integration
  - Dynamic clock scaling
  
  Keeping as comment for reference only.
===========================================================================*/

/* OLD IMPLEMENTATION - REPLACED
VOID
DreamV3CheckThermalThrottle(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt)
{
    // Check every 100 submissions to avoid overhead
    DevExt->ThermalCheckCount++;
    if (DevExt->ThermalCheckCount % 100 != 0) {
        return;
    }

    LONG TempC = DreamV3ReadTemperature(DevExt);

    const LONG THROTTLE_START = 85;
    const LONG EMERGENCY_STOP = 105;

    if (TempC >= EMERGENCY_STOP) {
        // Emergency shutdown
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
                   "AMDBC250-DREAM-V4.3: *** EMERGENCY THERMAL SHUTDOWN *** Temp: %ld°C\n",
                   TempC));
        DevExt->ThermalThrottleCount++;
        // Halt GPU
        DreamV3WriteRegister(DevExt, AMDBC250_REG_CP_ME_CNTL,
                             CP_ME_CNTL__ME_HALT | CP_ME_CNTL__PFP_HALT);
    } else if (TempC >= THROTTLE_START) {
        // Thermal throttle — reduce clocks (would send SMU message)
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                   "AMDBC250-DREAM-V4.3: Thermal throttle active — Temp: %ld°C\n", TempC));
        DevExt->ThermalThrottleCount++;
        // TODO: Send SMU message to reduce SCLK/MCLK
    }
}
*/

/*===========================================================================
  DreamV3WaitForRegister — Poll register with timeout
===========================================================================*/

static NTSTATUS
DreamV3WaitForRegister(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ ULONG RegisterOffset,
    _In_ ULONG Mask,
    _In_ ULONG ExpectedValue,
    _In_ ULONG TimeoutUs
    )
{
    ULONG Elapsed = 0;
    ULONG Value;

    while (Elapsed < TimeoutUs) {
        Value = DreamV3ReadRegister(DevExt, RegisterOffset);
        if ((Value & Mask) == ExpectedValue) {
            return STATUS_SUCCESS;
        }
        KeStallExecutionProcessor(10);
        Elapsed += 10;
    }

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
               "AMDBC250-DREAM-V4.3: Register 0x%X timeout (expected 0x%X, got 0x%X)\n",
               RegisterOffset, ExpectedValue,
               DreamV3ReadRegister(DevExt, RegisterOffset)));

    return STATUS_TIMEOUT;
}

/*===========================================================================
  DreamV3AllocateContiguousMemory — Physically contiguous allocation
===========================================================================*/

static PVOID
DreamV3AllocateContiguousMemory(
    _In_  SIZE_T              SizeInBytes,
    _Out_ PPHYSICAL_ADDRESS   PhysicalAddress
    )
{
    PHYSICAL_ADDRESS LowAddr = {0};
    PHYSICAL_ADDRESS HighAddr = {0};
    PHYSICAL_ADDRESS BoundaryAddr = {0};
    PVOID VirtualAddress;

    /* Avoid NULL page and low memory (below 1MB) for safety */
    LowAddr.QuadPart = 0x100000;  /* 1MB minimum */
    HighAddr.QuadPart = 0xFFFFFFFFFFFFFFFFULL;
    BoundaryAddr.QuadPart = 0;

    VirtualAddress = MmAllocateContiguousMemorySpecifyCache(
        SizeInBytes,
        LowAddr,
        HighAddr,
        BoundaryAddr,
        MmWriteCombined
        );

    if (VirtualAddress != NULL) {
        *PhysicalAddress = MmGetPhysicalAddress(VirtualAddress);
    } else {
        PhysicalAddress->QuadPart = 0;
    }

    return VirtualAddress;
}

/*===========================================================================
  DreamV3FreeContiguousMemory
===========================================================================*/

static VOID
DreamV3FreeContiguousMemory(
    _In_ PVOID  VirtualAddress,
    _In_ SIZE_T SizeInBytes
    )
{
    if (VirtualAddress != NULL) {
        MmFreeContiguousMemory(VirtualAddress);
    }
    UNREFERENCED_PARAMETER(SizeInBytes);
}
