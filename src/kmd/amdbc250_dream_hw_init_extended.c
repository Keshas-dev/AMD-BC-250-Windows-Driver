/*++
 Copyright (c) 2026 AMD BC-250 "Dream Drivers" Project — Extended HW Init
 Module: amdbc250_dream_hw_init_extended.c
 Abstract: Linux-correct init order for Cyan Skillfish APU (single DF).
 Linux order (amdgpu): soc15_common -> discovery -> gmc(gart/vm hub) -> psp(ring+TMR) -> gfx(golden/constants/get_cu_info/rlc/cp) -> sdma/ih/nbio/hdp
 Windows before: PSP before GART/VM (inverted) + golden 1/34 + PSP ring stub. This file fixes order and uses DF 00:00.0 B8/BC primary for SMN.
--*/
#include "amdbc250_dream_kmd.h"
#include "amdbc250_psp.h"

// Forward from existing hw_init.c / vm.c / psp.c / golden.c / rlc.c
extern NTSTATUS DreamV3LoadPspFirmware(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt);
extern NTSTATUS DreamV3GartInitialize(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt);
extern NTSTATUS DreamV3VmInitialize(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt);
extern NTSTATUS DreamV3HwInitDisplay(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt);
extern NTSTATUS DreamV3HwInitGfxRing(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt);
extern NTSTATUS DreamV3HwInitSdmaRing(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt);
extern NTSTATUS DreamV3InitRlc(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt);
extern VOID DreamV3MarkHwInitStep(_In_ ULONG Step);
extern NTSTATUS DreamV3ProgramGoldenSettings(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt);
extern NTSTATUS DreamV3HwInitFence(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt);
NTSTATUS Amdbc250PspRingCreate(PVOID G, ULONG T, ULONG L, ULONG H, ULONG S); // psp.c

static ULONG DreamV3ReadMaxStepExt(void){
    UNICODE_STRING Path; RtlInitUnicodeString(&Path, L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\atikmdag");
    OBJECT_ATTRIBUTES Oa; InitializeObjectAttributes(&Oa, &Path, OBJ_CASE_INSENSITIVE, NULL, NULL);
    HANDLE hKey=NULL; ULONG v=0;
    if(NT_SUCCESS(ZwOpenKey(&hKey, KEY_READ, &Oa))){
        UNICODE_STRING vn; RtlInitUnicodeString(&vn, L"HwInitMaxStep");
        UCHAR buf[sizeof(KEY_VALUE_PARTIAL_INFORMATION)+sizeof(ULONG)]={0}; ULONG ret=0;
        if(NT_SUCCESS(ZwQueryValueKey(hKey,&vn,KeyValuePartialInformation,buf,sizeof(buf),&ret))){
            PKEY_VALUE_PARTIAL_INFORMATION pi=(PKEY_VALUE_PARTIAL_INFORMATION)buf;
            if(pi->DataLength==sizeof(ULONG)) v=*(PULONG)pi->Data;
        } ZwClose(hKey);
    } return v;
}
static ULONG DreamV3IsExtendedEnabled(void){
    UNICODE_STRING Path; RtlInitUnicodeString(&Path, L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\atikmdag");
    OBJECT_ATTRIBUTES Oa; InitializeObjectAttributes(&Oa, &Path, OBJ_CASE_INSENSITIVE, NULL, NULL);
    HANDLE hKey=NULL; ULONG v=1; // default ON for DF APU
    if(NT_SUCCESS(ZwOpenKey(&hKey, KEY_READ, &Oa))){
        UNICODE_STRING vn; RtlInitUnicodeString(&vn, L"HwInitExtended");
        UCHAR buf[sizeof(KEY_VALUE_PARTIAL_INFORMATION)+sizeof(ULONG)]={0}; ULONG ret=0;
        if(NT_SUCCESS(ZwQueryValueKey(hKey,&vn,KeyValuePartialInformation,buf,sizeof(buf),&ret))){
            PKEY_VALUE_PARTIAL_INFORMATION pi=(PKEY_VALUE_PARTIAL_INFORMATION)buf;
            if(pi->DataLength==sizeof(ULONG)) v=*(PULONG)pi->Data;
        } ZwClose(hKey);
    } return v;
}

NTSTATUS DreamV3HwInitializeExtended(_In_ PDREAM_V3_DEVICE_EXTENSION DevExt){
    NTSTATUS Status=STATUS_SUCCESS;
    ULONG MaxStep=DreamV3ReadMaxStepExt();
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL, "AMDBC250-DREAM-V4.3: HwInitializeExtended — Linux order DF APU (GMC->PSP->GFX)\n"));
    if(MaxStep) KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL, "AMDBC250: Extended CAPPED at %u\n", MaxStep));

    // Step 0: soc15_common doorbell/cg_flags emulation (log only, no reg write that hangs)
    DreamV3MarkHwInitStep(0);
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL, "AMDBC250: [EXT 0/13] soc15_common (cg_flags=0 pg_flags=0, DF primary)\n"));

    // Step 0b: global fence page. FIRST, ahead of every step that writes
    // GRBM_GFX_INDEX, because it is pure host memory (one contiguous page,
    // zeroed) with no MMIO write at all. Two reasons it lives here and not in
    // the ring step it was extracted from: the GFX ring base is host-read-only
    // on BC-250, so the ring step frees what it allocated and leaves no fence;
    // and the fence is the only driver-owned window the PM4 IT_DMA_DATA operand
    // resolver accepts, so it has to exist for that path to be testable.
    // Reachable with HwInitMaxStep=1, i.e. without crossing any hazard step.
    if(MaxStep && 1>MaxStep) return STATUS_SUCCESS;
    DreamV3MarkHwInitStep(AMDBC250_HWINIT_STEP_FENCE);
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL, "AMDBC250: [EXT 0b] global fence\n"));
    Status = DreamV3HwInitFence(DevExt);
    if(!NT_SUCCESS(Status)) KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL, "AMDBC250: [EXT 0b] fence failed 0x%08X (continue)\n", Status));
    else KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL, "AMDBC250: [EXT 0b] fence OK\n"));

    // Step 1-2: GMC early — GART + VM hub BEFORE PSP (linux gmc_v10_0 hw_init 962 + gfxhub gart_enable 345)
    // Previous Windows did PSP before GART (inverted) — fix here.
    if(MaxStep && 9>MaxStep) return STATUS_SUCCESS;
    DreamV3MarkHwInitStep(9);
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL, "AMDBC250: [EXT 9/13] GART (DF APU, before PSP)\n"));
    Status = DreamV3GartInitialize(DevExt);
    if(!NT_SUCCESS(Status)) KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL, "AMDBC250: [EXT 9] GART failed 0x%08X (continue)\n", Status));
    else KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL, "AMDBC250: [EXT 9] GART OK\n"));

    if(MaxStep && 10>MaxStep) return STATUS_SUCCESS;
    DreamV3MarkHwInitStep(10);
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL, "AMDBC250: [EXT 10/13] GPUVM (system aperture, before PSP)\n"));
    Status = DreamV3VmInitialize(DevExt);
    if(!NT_SUCCESS(Status)) KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL, "AMDBC250: [EXT 10] VM failed 0x%08X (continue)\n", Status));
    else KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL, "AMDBC250: [EXT 10] VM OK\n"));

    // Step 3: PSP — load firmware then ring_create with proper poll (psp_v11_0.c:318)
    if(MaxStep && 6>MaxStep) return STATUS_SUCCESS;
    DreamV3MarkHwInitStep(6);
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL, "AMDBC250: [EXT 6/13] PSP FW + ring (MP0 0x58000, after GART/VM)\n"));
    Status = DreamV3LoadPspFirmware(DevExt);
    if(!NT_SUCCESS(Status)) KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL, "AMDBC250: [EXT 6] PSP FW failed 0x%08X (continue)\n", Status));

    /* PSP hardware init: maps the PSP MMIO window, attempts the NBIO unlock and
     * sets DevExt->PspAlive / PspInitialized / NbioUnlocked / KiqAvailable /
     * GfxRingAvailable.
     *
     * The active path previously called Amdbc250PspRingCreate(NULL,0,0,0,0)
     * here, which is a stub that ignores every argument and returns
     * STATUS_NOT_SUPPORTED, so the step could only ever print a permanent
     * warning. Because DreamV3PspHardwareInit was never reached from this
     * file, g_PspContext.MmioBase stayed unmapped (every
     * Amdbc250PspReadRegister then returned 0xFFFFFFFF), those five flags were
     * never written, and DevExt->PspInitialized stayed FALSE - which in turn
     * skipped Amdbc250PspProxyCleanup in teardown and leaked g_FwBuffer. */
    {
        NTSTATUS ps = DreamV3PspHardwareInit(DevExt);
        if(!NT_SUCCESS(ps)) KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL, "AMDBC250: [EXT 6] PSP HW init failed 0x%08X (continue)\n", ps));
        else KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL, "AMDBC250: [EXT 6] PSP HW init OK (Mmio mapped, flags set)\n"));
    }

    // Step 4: Golden 34 regs — expand from 1/34 (CC only) to full via DreamV3ProgramGolden (uses RLC safe if available)
    if(MaxStep && 4>MaxStep) return STATUS_SUCCESS;
    DreamV3MarkHwInitStep(4);
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL, "AMDBC250: [EXT 4/13] Golden 34 (via RLC/GRBM_CAM)\n"));
    Status = DreamV3ProgramGoldenSettings(DevExt);
    if(!NT_SUCCESS(Status)) KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL, "AMDBC250: [EXT 4] Golden failed 0x%08X (continue)\n", Status));

    // Step 5: GFX constants + get_cu_info per-bank WGP unlock with correct gfx10.1 layout (after golden+GART/PSP, before RLC)
    // This is the Linux gfx_v10_0_get_cu_info location (constants_init 5340 + get_cu_info 10115 + select_se_sh 5057)
    if(MaxStep && 12>MaxStep) return STATUS_SUCCESS;
    DreamV3MarkHwInitStep(12);
    {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL, "AMDBC250: [EXT 12/13] WGP per-bank gfx10.1 SA=bit8 SE=bit16 (after golden, before RLC)\n"));
        // Use same values as fixed Step12b but here before RLC
        PUCHAR bar5 = (PUCHAR)DevExt->MmioVirtualBase;
        if(bar5){
            static const ULONG bankSel[4]={0x00000000,0x00000100,0x00010000,0x00010100};
            ULONG spiBefore = READ_REGISTER_ULONG((PULONG)(bar5 + 0x5C3C));
            ULONG ccAt9c1c = READ_REGISTER_ULONG((PULONG)(bar5 + 0x9C1C));
            ULONG ccAt529c = READ_REGISTER_ULONG((PULONG)(bar5 + 0x529C));
            /* Two addresses are in circulation for CC_GC_SHADER_ARRAY_CONFIG and
             * the 40 CU role is claimed for both, so report them rather than
             * picking one. gc_10_1_0_offset.h has mmCC_GC_SHADER_ARRAY_CONFIG at
             * 0x100f, which is 0x529C after the GC base shift; the 0x9C1C used
             * here comes from mm 0x226F, which is not in that header. Whichever
             * is right, the register reads back the fuse shadow, so writing
             * either changes enumeration and not the value. */
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL, "AMDBC250: [EXT 12a] CC 0x9C1C=0x%08X  CC 0x529C=0x%08X (candidates)\n", ccAt9c1c, ccAt529c));
            for(ULONG b=0;b<4;b++){
                WRITE_REGISTER_ULONG((PULONG)(bar5 + 0x34D0), bankSel[b]);
                /* CC is written at 0x9C1C. This was changed to 0x529C and that
                 * was wrong.
                 *
                 * gc_10_1_0_offset.h is the gfx10.1.0 header. This board is
                 * gfx10.1.3, and the offsets are not the same. Measured on this
                 * silicon, with a bank selected and a write issued:
                 *
                 *   0x9C1C accepted the write and read back 0x1F000000
                 *   0x529C did not move at all
                 *
                 * 0x9C1C has always been live; mm 0x226F is the correct index for
                 * this IP revision. SPI at 0x5C3C (mm 0x1277) was right in both
                 * headers, which is why it was never in question.
                 */
                WRITE_REGISTER_ULONG((PULONG)(bar5 + 0x9C1C), 0x00000000); /* CC=0 per duggasco */
                WRITE_REGISTER_ULONG((PULONG)(bar5 + 0x5C3C), 0x0000001F);
                /* RLC_PG_ALWAYS_ON_WGP_MASK is deliberately NOT written.
                 * gc_10_1_0_offset.h places it at mm 0x4c53, which is 0x143AC
                 * after the GC base shift, not the 0x3D64 that was here. 0x3D64
                 * corresponds to mm 0xac1, which is not in that header, and
                 * probing it through SMN from UEFI wedged the SMU. It is also a
                 * no-op: the VBIOS already leaves it at 0x1f from POST, so the
                 * write cannot change anything. */
            }
            WRITE_REGISTER_ULONG((PULONG)(bar5 + 0x34D0), AMDBC250_GRBM_GFX_INDEX_BROADCAST_VAL); /* 0x15000000 gfx10 broadcast (was 0xE0000000 soc15; see hw.h:445) */
            ULONG spiAfter = READ_REGISTER_ULONG((PULONG)(bar5 + 0x5C3C));
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL, "AMDBC250: [EXT 12b] SPI 0x%08X->0x%08X %s\n", spiBefore, spiAfter, (spiAfter&0x1F)==0x1F?"UNLOCKED":"STILL LOCKED"));
        }
    }

    // Step 6: RLC/CP enable (was gated by unreachable Parameters key)
    if(MaxStep && 13>MaxStep) return STATUS_SUCCESS;
    DreamV3MarkHwInitStep(13);
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL, "AMDBC250: [EXT 13/13] RLC/CP enable (safe_mode)\n"));
    Status = DreamV3InitRlc(DevExt);
    if(!NT_SUCCESS(Status)) KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL, "AMDBC250: [EXT 13] RLC failed 0x%08X\n", Status));

    // Step 7: GFX/SDMA rings (were gated by HwInitGfxRing=0) — now after golden+RLC, like Linux
    if(MaxStep && 7>MaxStep) return STATUS_SUCCESS;
    DreamV3MarkHwInitStep(7);
    Status = DreamV3HwInitGfxRing(DevExt);
    if(!NT_SUCCESS(Status)) KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL, "AMDBC250: [EXT 7] GFX ring 0x%08X\n", Status));
    if(MaxStep && 8>MaxStep) return STATUS_SUCCESS;
    DreamV3MarkHwInitStep(8);
    Status = DreamV3HwInitSdmaRing(DevExt);
    if(!NT_SUCCESS(Status)) KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL, "AMDBC250: [EXT 8] SDMA ring 0x%08X\n", Status));

    // Step 11: Display (DCN20) last, like Linux
    if(MaxStep && 11>MaxStep) return STATUS_SUCCESS;
    DreamV3MarkHwInitStep(11);
    Status = DreamV3HwInitDisplay(DevExt);
    if(!NT_SUCCESS(Status)) KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL, "AMDBC250: [EXT 11] Display 0x%08X\n", Status));

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL, "AMDBC250: Extended init complete (DF APU, no 0x1A)\n"));
    return STATUS_SUCCESS;
}

// Wrapper check — called from original DreamV3HwInitialize
BOOLEAN DreamV3UseExtendedInit(void){ return DreamV3IsExtendedEnabled() ? TRUE : FALSE; }
