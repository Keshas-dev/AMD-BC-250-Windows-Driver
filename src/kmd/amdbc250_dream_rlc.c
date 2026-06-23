/*++

Copyright (c) 2026 AMD BC-250 "Dream Drivers" Project — Version 4.3

Module Name:
    amdbc250_dream_rlc.c

Abstract:
    RLC (Run List Controller) initialization for AMD BC-250.
    
    The RLC controls:
    - Power gating (clock gating, power gating)
    - Scheduler interrupts
    - GFX/SDMA/Compute queue scheduling
    
    Based on Linux amdgpu driver:
    - drivers/gpu/drm/amd/amdgpu/gfx_v10_0.c (gfx_v10_0_rlc_init)
    
    NOTE: BC-250 has cg_flags=0, pg_flags=0 (no clock/power gating).
          RLC is still needed for scheduler control.

Environment:
    Kernel mode (IRQL <= DISPATCH_LEVEL)

--*/

#include "amdbc250_dream_kmd.h"

/*===========================================================================
  RLC Register Offsets (from gc_10_1_0_offset.h)
  
  These are GC_BASE-shifted byte offsets in BAR5.
  Formula: BAR5_offset = GC_BASE(0x1260) + Linux_DWORD_offset * 4
===========================================================================*/

/* RLC control registers */
#define RLC_REG_CNTL             0x3A00   /* RLC control */
#define RLC_REG_GPM_UCODE_ADDR   0x3A4C   /* RLC ucode address */
#define RLC_REG_GPM_UCODE_DATA   0x3A50   /* RLC ucode data */
#define RLC_REG_CNTL2            0x3A04   /* RLC control 2 */
#define RLC_REG_CNTL3            0x3A08   /* RLC control 3 */
#define RLC_REG_MGCG_CTRL        0x3A10   /* MGCG control */
#define RLC_REG_CLK_CNTL         0x3A14   /* Clock control */
#define RLC_REG_SPARE_BOOL       0x3A1C   /* Spare boolean */

/* RLC safe mode registers */
#define RLC_REG_SMU_SAFE_MODE    0x3A30   /* SMU safe mode */
#define RLC_REG_SMU_MSG_CNTL     0x3A34   /* SMU message control */

/* RLC interrupt registers */
#define RLC_REG_INT_CNTL         0x3A40   /* Interrupt control */
#define RLC_REG_INT_STATUS       0x3A44   /* Interrupt status */
#define RLC_REG_INT_CLEAR        0x3A48   /* Interrupt clear */

/* RLC bit definitions */
#define RLC_CNTL__ENABLE              (1 << 0)
#define RLC_CNTL__RLC_ENABLE_F32      (1 << 1)
#define RLC_CNTL__FPGA_HALT           (1 << 8)

/*===========================================================================
  DreamV3InitRlc — Initialize RLC for scheduler control
  
  The RLC (Run List Controller) is responsible for GFX power management
  and scheduler control. Even though BC-250 has cg_flags=0 and pg_flags=0
  (no clock/power gating), the RLC must be initialized for proper
  queue scheduling.
  
  Linux sequence:
    1. Halt RLC
    2. Load RLC firmware via UCODE_ADDR/DATA
    3. Configure RLC parameters
    4. Unhalt RLC
    
  Parameters:
    DevExt - Device extension with BAR5 mapping
    
  Returns:
    STATUS_SUCCESS on success
===========================================================================*/

NTSTATUS
DreamV3InitRlc(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    )
{
    UNREFERENCED_PARAMETER(DevExt);

    /* ======================================================================
     * RLC registers (0x3A00-0x3A50) are in the 0x3400-0x8100 FREEZE ZONE.
     * Direct MMIO access causes hardware hang on BC-250.
     *
     * BC-250 has cg_flags=0, pg_flags=0 (no clock/power gating).
     * RLC firmware is loaded by BIOS/SMU. OS-level RLC programming
     * is NOT needed for basic GFX ring operation.
     *
     * TODO: If RLC programming is needed later, use PSP proxy IOCTLs.
     * ====================================================================== */

    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250-RLC: RLC init SKIPPED (registers in freeze zone 0x3A00+)\n"));
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250-RLC: BC-250 cg_flags=0 pg_flags=0 — RLC not needed for basic GFX\n"));

    return STATUS_SUCCESS;
}
