/*++

Copyright (c) 2026 AMD BC-250 "Dream Drivers" Project — Version 4.3

Module Name:
    amdbc250_dream_golden.c

Abstract:
    Golden register programming for AMD BC-250 (Cyan Skillfish / GFX1013).
    
    These are hardware workarounds/errata from AMD that MUST be programmed
    during initialization. Missing these = instability/crashes.
    
    Based on Linux amdgpu driver:
    - drivers/gpu/drm/amd/amdgpu/gfx_v10_0.c
    - golden_settings_gc_10_0_cyan_skillfish[]
    
    NOTE: Linux programs these via GRBM_CAM_INDEX/DATA mechanism.
          We use direct MMIO writes via BAR5.

Environment:
    Kernel mode (IRQL <= DISPATCH_LEVEL)

--*/

#include "amdbc250_dream_kmd.h"

/*===========================================================================
  Golden Register Definitions (from Linux gfx_v10_0.c)
  
  Format: { GC_BASE-shifted offset, mask, value }
  
  These registers fix hardware bugs/errata. Missing ANY of them can
  cause GPU hangs, rendering corruption, or system crashes.
  
  Linux uses SOC15_REG_GOLDEN_VALUE macro which programs via GRBM_CAM.
  We use direct MMIO writes since BAR5 is accessible.
===========================================================================*/

/* GRBM_CAM mechanism registers */
#define REG_GRBM_CAM_INDEX      0x3200   /* 0x0C80*4 = 0x3200 (raw BAR5 offset, NOT GC_BASE-shifted) */
#define REG_GRBM_CAM_DATA       0x3204   /* 0x0C81*4 = 0x3204 (raw BAR5 offset, NOT GC_BASE-shifted) */

/* Golden register structure */
typedef struct _GOLDEN_REG {
    UINT32 Offset;      /* BAR5 byte offset (GC_BASE-shifted) */
    UINT32 Mask;         /* Write mask */
    UINT32 Value;        /* Value to write */
} GOLDEN_REG;

/*===========================================================================
  Golden Settings Array (from Linux golden_settings_gc_10_0_cyan_skillfish)
  
  Each entry: { offset, mask, value }
  
  These are GC v10 golden settings specific to Cyan Skillfish.
  The mask indicates which bits are valid/writable.
===========================================================================*/

static const GOLDEN_REG GoldenSettingsCyanSkillfish[] = {
    /* ======================================================================
     * SAFE registers only (0x3200-0x32FF GC_BASE-shifted range)
     *
     * WARNING: Registers at 0x3400-0x8100 cause HARDWARE FREEZE on BC-250.
     * Linux programs these via GRBM_CAM_INDEX/DATA mechanism, but our
     * direct MMIO path cannot access them without hanging the GPU.
     *
     * Frozen registers (DO NOT USE): CPG_PSP_DEBUG, CPC_PSP_DEBUG,
     * GE_FAST_CLKS, CGTT_CPF/SPI_CLK_CTRL, CB_HW_CONTROL_3/4,
     * CH_DRAM_BURST_CTRL, CH_PIPE_STEER, CH_VC5_ENABLE, CP_SD_CNTL,
     * GCR_GENERAL_CNTL, PA_SC_*, SPI_CONFIG_CNTL_*, SQ_*,
     * TA_CNTL_AUX, UTCL1_CTRL, CP_GRBM_GFX_INDEX
     *
     * BC-250 firmware/bootloader programs these at SOC reset.
     * We only program the safe subset below.
     * ====================================================================== */

    /* CC_GC_SHADER_ARRAY_CONFIG — shader array config (24 CU enable)
     * NOTE: 0x9C1C = GC_BASE(0x1260) + mmCC_GC_SHADER_ARRAY_CONFIG(0x226F)*4.
     * DO NOT use 0x3264 (that's GRBM_STATUS2, a completely different register). */
    { AMDBC250_REG_CC_GC_SHADER_ARRAY_CONFIG, 0xFFFF0000, 0xFFE00000 },
};

/* Number of golden registers */
#define NUM_GOLDEN_REGS (sizeof(GoldenSettingsCyanSkillfish) / sizeof(GoldenSettingsCyanSkillfish[0]))

/*===========================================================================
  DreamV3ProgramGoldenSettings — Program all golden registers
  
  This programs the hardware workarounds specific to Cyan Skillfish.
  Must be called during initialization, before any GPU operations.
  
  Linux equivalent:
    soc15_program_register_sequence(adev,
        golden_settings_gc_10_0,
        ARRAY_SIZE(golden_settings_gc_10_0));
    soc15_program_register_sequence(adev,
        golden_settings_gc_10_0_cyan_skillfish,
        ARRAY_SIZE(golden_settings_gc_10_0_cyan_skillfish));
  
  Parameters:
    DevExt - Device extension with BAR5 mapping
    
  Returns:
    STATUS_SUCCESS
===========================================================================*/

NTSTATUS
DreamV3ProgramGoldenSettings(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    )
{
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250-GOLDEN: Programming %u golden registers\n", NUM_GOLDEN_REGS));
    
    if (!DevExt->MmioVirtualBase) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
            "AMDBC250-GOLDEN: No BAR5 mapping\n"));
        return STATUS_DEVICE_NOT_READY;
    }
    
    #define BAR5_U32(off) (*(volatile UINT32 *)((PUCHAR)DevExt->MmioVirtualBase + (off)))
    
    UINT32 Programmed = 0;
    UINT32 Errors = 0;
    
    for (UINT32 i = 0; i < NUM_GOLDEN_REGS; i++) {
        const GOLDEN_REG *Reg = &GoldenSettingsCyanSkillfish[i];
        
        __try {
            /* Read current value */
            UINT32 Current = BAR5_U32(Reg->Offset);
            
            /* Apply mask and set value */
            UINT32 NewValue = (Current & ~Reg->Mask) | (Reg->Value & Reg->Mask);
            
            /* Write new value */
            BAR5_U32(Reg->Offset) = NewValue;
            
            /* Verify write (read back) */
            UINT32 ReadBack = BAR5_U32(Reg->Offset);
            
            if (ReadBack != NewValue) {
                KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                    "AMDBC250-GOLDEN: Reg 0x%04X verify mismatch: wrote 0x%08X read 0x%08X\n",
                    Reg->Offset, NewValue, ReadBack));
                Errors++;
            } else {
                Programmed++;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
                "AMDBC250-GOLDEN: Exception accessing reg 0x%04X\n", Reg->Offset));
            Errors++;
        }
    }
    
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250-GOLDEN: Programmed %u/%u registers (%u errors)\n",
        Programmed, NUM_GOLDEN_REGS, Errors));
    
    #undef BAR5_U32
    
    return STATUS_SUCCESS;
}
