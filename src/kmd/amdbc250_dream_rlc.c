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
  DreamV3ReadRegDword — read a DWORD from the driver Parameters key.
     Returns non-success if the value is absent (caller treats as "unset").
  ===========================================================================*/
  static NTSTATUS
  DreamV3ReadRegDword(
      _In_  PCWSTR ValueName,
      _Out_ PULONG Out
      )
  {
      UNICODE_STRING path;
      RtlInitUnicodeString(&path,
          L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\atikmdag\\Parameters");
      OBJECT_ATTRIBUTES oa;
      InitializeObjectAttributes(&oa, &path, OBJ_CASE_INSENSITIVE, NULL, NULL);
      HANDLE hKey = NULL;
      NTSTATUS st = ZwOpenKey(&hKey, KEY_READ, &oa);
      if (!NT_SUCCESS(st)) return st;
      UNICODE_STRING vn;
      RtlInitUnicodeString(&vn, ValueName);
      UCHAR buf[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)] = {0};
      ULONG ret = 0;
      st = ZwQueryValueKey(hKey, &vn, KeyValuePartialInformation,
                           buf, sizeof(buf), &ret);
      if (NT_SUCCESS(st)) {
          PKEY_VALUE_PARTIAL_INFORMATION pi = (PKEY_VALUE_PARTIAL_INFORMATION)buf;
          if (pi->DataLength == sizeof(ULONG))
              *Out = *(PULONG)pi->Data;
          else st = STATUS_INVALID_PARAMETER;
      }
      ZwClose(hKey);
      return st;
  }
  
  /*===========================================================================
     DreamV3InitRlc — RLC RESUME (GATED experiment)
  
     Linux gfx_v10_0_hw_init() calls gfx_v10_0_rlc_resume() BEFORE
     cp_resume()/ring tests. rlc_resume programs RLC_CNTL (RLC_ENABLE_F32)
     and the RLC save/restore buffer. WITHOUT it, the Command Processor
     (CP) cannot schedule/run rings — so a CP-level ring test
     (SET_UCONFIG_REG -> SCRATCH) fails with RPTR stuck, even though
     the WGP shader fuse is a SEPARATE, upstream concern.
  
     This is the most likely reason our CP-level ring test fails.
  
     The old code SKIPPED this entirely ("RLC registers in freeze zone
     0x3A00+"), assuming it was not needed. That assumption was
     never verified — and it contradicts the Linux init order.
  
     GATED: RlcResumeEnabled (DWORD, default 0) must be set to 1 in
       HKLM\SYSTEM\CurrentControlSet\Services\atikmdag\Parameters
     to actually program RLC_CNTL. Default 0 = identical to old
     behavior (no register writes, no hang risk).
  
     RlcCntlOffset (DWORD, default 0x3A00) selects the BAR5 offset
     of RLC_CNTL. It is UNVERIFIED on BC-250 (candidates seen in
     test tools: 0x3A00 [driver], 0x4A80 [Linux formula
     GC_BASE+mmRLC_CNTL*4], 0x4B20 [ring-probe], 0x4C00 [Gemini],
     0x3398). Tune via registry after probing (see rl* test tools).
  
     SEH-guarded: a wrong/frozen offset may fault MMIO. The guard
     converts a clean fault to a no-op. (A hard bus freeze still
     needs a reboot — which is why this is OFF by default.)
  
     NOTE: RLC_CP_SCHEDULERS (0xECA8) is already programmed in
     the KIQ HQD init (hw_init.c) — this function adds the master
     RLC_CNTL enable that was previously skipped.
  ===========================================================================*/
  
  NTSTATUS
  DreamV3InitRlc(
      _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
      )
  {
      ULONG enabled = 0;
      if (!NT_SUCCESS(DreamV3ReadRegDword(L"RlcResumeEnabled", &enabled)) || enabled == 0) {
          KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
              "AMDBC250-RLC: RLC resume SKIPPED (RlcResumeEnabled=0).\n"
              "  Set ...\\Parameters\\RlcResumeEnabled=1 to attempt RLC_CNTL program.\n"));
          return STATUS_SUCCESS;
      }
  
      /* Offset is unverified — make it registry-tunable, default 0x3A00. */
      ULONG off = 0x3A00;
      ULONG ignore = 0;
      if (NT_SUCCESS(DreamV3ReadRegDword(L"RlcCntlOffset", &ignore)))
          off = ignore;
  
      KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
          "AMDBC250-RLC: RLC resume ENABLED — writing RLC_CNTL at BAR5+0x%X\n", off));
  
      NTSTATUS st = STATUS_SUCCESS;
      __try {
          ULONG val = RLC_CNTL__RLC_ENABLE_F32 | RLC_CNTL__ENABLE;
          DreamV3WriteRegister(DevExt, off, val);
          ULONG rb = DreamV3ReadRegister(DevExt, off);
          KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL,
              "AMDBC250-RLC: RLC_CNTL written 0x%08X, readback 0x%08X\n", val, rb));
          /* Re-assert KIQ scheduler slot (already wired in KIQ HQD init). */
          DreamV3WriteRegister(DevExt,
                               AMDBC250_REG_RLC_CP_SCHEDULERS,
                               AMDBC250_RLC_CP_SCHEDULERS_KIQ_VAL);
      } __except (EXCEPTION_EXECUTE_HANDLER) {
          st = STATUS_DEVICE_NOT_READY;
          KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
              "AMDBC250-RLC: EXCEPTION writing RLC_CNTL at 0x%X (freeze?)\n", off));
      }
      return st;
  }
