/*++

Copyright (c) 2026 AMD BC-250 "Dream Drivers" Project — Version 4.3

Module Name:
    amdbc250_dream_hdp.c

Abstract:
    HDP (Host Data Path) register initialization for AMD BC-250.
    
    HDP controls coherency between CPU and GPU memory accesses.
    Proper initialization is CRITICAL for system stability.
    
    Based on Linux amdgpu driver:
    - drivers/gpu/drm/amd/amdgpu/hdp_v5_0.c
    - drivers/gpu/drm/amd/amdgpu/gmc_v10_0.c (gmc_v10_0_gart_enable)
    
    HDP IP version: 5.0.1 (from cyan_skillfish_ip_offset.h)

Environment:
    Kernel mode (IRQL <= DISPATCH_LEVEL)

--*/

#include "amdbc250_dream_kmd.h"

/*===========================================================================
  HDP Register Offsets (from hdp_v5_0_0_offset.h)
  
  These are NOT GC_BASE-shifted — they are at their natural offsets.
  HDP_BASE = 0x0F20 (from cyan_skillfish_ip_offset.h)
  
  Formula: BAR5_offset = HDP_BASE(0x0F20) + reg_offset
===========================================================================*/

/* HDP registers (byte offsets in BAR5) */
#define HDP_REG_MEM_COHERENCY_FLUSH_CNTL   0x12A0  /* Flush HDP cache */
#define HDP_REG_DEBUG0                     0x12B0  /* Invalidate cache */
#define HDP_REG_NONSURFACE_INFO            0x12C0  /* Non-surface info */
#define HDP_REG_NONSURFACE_SIZE            0x12C4  /* Non-surface size */
#define HDP_REG_NONSURFACE_BASE            0x12C8  /* Non-surface base */
#define HDP_REG_NONSURFACE_BASE_HI         0x12CC  /* Non-surface base high */

/* HDP Memory Coherency — Linux uses these during GART enable.
 * HDP_BASE = 0x0F20 (from cyan_skillfish_ip_offset.h).
 * These are HDP-relative offsets, absolute BAR5 = HDP_BASE + offset. */
#define HDP_REG_FB_OFFSET                  (0x0F20 + 0x0000)  /* FB offset */
#define HDP_REG_FB_BASE                    (0x0F20 + 0x0004)  /* FB base */
#define HDP_REG_FB_TOP                     (0x0F20 + 0x0008)  /* FB top */

/* HDP register values */
#define HDP_MEM_COHERENCY_FLUSH_CNTL__FLUSH_CACHE  (1 << 0)
#define HDP_DEBUG0__INVALIDATE_CACHE              (1 << 0)

/*===========================================================================
  DreamV3InitHdpRegisters — Initialize HDP for coherency
  
  This function initializes the Host Data Path registers to ensure
  proper coherency between CPU and GPU memory accesses.
  
  Without this, the GPU may read stale data from system memory,
  causing rendering corruption or system crashes.
  
  Linux equivalent:
    adev->hdp.funcs->init_registers(adev);
    adev->hdp.funcs->flush_hdp(adev, NULL);
  
  Parameters:
    DevExt - Device extension with BAR5 mapping
    
  Returns:
    STATUS_SUCCESS
===========================================================================*/

NTSTATUS
DreamV3InitHdpRegisters(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    )
{
    KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
        "AMDBC250-HDP: Initializing HDP registers\n"));
    
    if (!DevExt->MmioVirtualBase) {
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
            "AMDBC250-HDP: No BAR5 mapping\n"));
        return STATUS_DEVICE_NOT_READY;
    }
    
    #define BAR5_U32(off) (*(volatile UINT32 *)((PUCHAR)DevExt->MmioVirtualBase + (off)))
    
    __try {
        /* Step 1: Set HDP non-surface info for coherency */
        BAR5_U32(HDP_REG_NONSURFACE_INFO) = 0x00000001;
        
        /* Step 2: Configure FB location if VRAM info available */
        if (DevExt->FbSize > 0) {
            UINT32 FbBase = (UINT32)(DevExt->FbPhysicalBase.QuadPart >> 24);
            UINT32 FbTop = (UINT32)((DevExt->FbPhysicalBase.QuadPart + DevExt->FbSize) >> 24) - 1;
            
            BAR5_U32(HDP_REG_FB_BASE) = FbBase;
            BAR5_U32(HDP_REG_FB_TOP) = FbTop;
            
            KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
                "AMDBC250-HDP: FB base=0x%08X top=0x%08X\n", FbBase, FbTop));
        }
        
        /* Step 3: Flush HDP read cache */
        BAR5_U32(HDP_REG_MEM_COHERENCY_FLUSH_CNTL) = HDP_MEM_COHERENCY_FLUSH_CNTL__FLUSH_CACHE;
        
        /* Step 4: Invalidate HDP write cache */
        BAR5_U32(HDP_REG_DEBUG0) = HDP_DEBUG0__INVALIDATE_CACHE;
        
        /* Step 5: Memory barrier + stall to ensure flush completes before any subsequent reads */
        KeMemoryBarrier();
        KeStallExecutionProcessor(10);
        KeMemoryBarrier();
        
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
            "AMDBC250-HDP: HDP registers initialized successfully\n"));
        
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        UINT32 ExcCode = GetExceptionCode();
        KdPrintEx((DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL,
            "AMDBC250-HDP: EXCEPTION 0x%08X during HDP init\n", ExcCode));
    }
    
    #undef BAR5_U32
    
    return STATUS_SUCCESS;
}

/*===========================================================================
  DreamV3FlushHdp — Flush HDP caches (called before reading ring pointers)
  
  This is a CRITICAL Linux amdgpu quirk: without this, the CPU reads
  stale data from ring buffers because GPU writes aren't coherent.
  
  Linux equivalent:
    WREG32(mmHDP_MEM_COHERENCY_FLUSH_CNTL, 1);
  
  Parameters:
    DevExt - Device extension with BAR5 mapping
===========================================================================*/

VOID
DreamV3FlushHdp(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    )
{
    if (!DevExt->MmioVirtualBase) return;
    
    #define BAR5_U32(off) (*(volatile UINT32 *)((PUCHAR)DevExt->MmioVirtualBase + (off)))
    
    /* Flush HDP read cache */
    BAR5_U32(HDP_REG_MEM_COHERENCY_FLUSH_CNTL) = HDP_MEM_COHERENCY_FLUSH_CNTL__FLUSH_CACHE;
    
    /* Invalidate HDP write cache */
    BAR5_U32(HDP_REG_DEBUG0) = HDP_DEBUG0__INVALIDATE_CACHE;
    
    /* Memory barrier + stall to ensure flush completes before subsequent reads */
    KeMemoryBarrier();
    KeStallExecutionProcessor(10);
    KeMemoryBarrier();
    
    #undef BAR5_U32
}
