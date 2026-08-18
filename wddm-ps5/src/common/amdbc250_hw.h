/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    amdbc250_hw.h

Abstract:
    Hardware definitions and register maps for the AMD BC-250 APU
    (Cyan Skillfish / Ariel - RDNA2 iGPU, 24 Compute Units)

    This header defines PCI identifiers, MMIO register offsets,
    command submission structures, and hardware capability flags
    for the AMD BC-250 APU used in ASRock mining boards.

    GPU Architecture: RDNA2 (Navi 1x family, cyan_skillfish variant)
    CPU Architecture: AMD Zen 2 (6 cores / 12 threads)
    Memory: 16 GB GDDR6 (shared UMA)
    Compute Units: 24 CU (1536 Stream Processors)

Environment:
    Kernel mode (Windows Display Driver Model - WDDM 2.x)

--*/

#pragma once

#ifndef _AMDBC250_HW_H_
#define _AMDBC250_HW_H_

#include <ntddk.h>

/*===========================================================================
  PCI Identifiers - AMD BC-250 / Cyan Skillfish family
  Vendor ID: 0x1002 (Advanced Micro Devices, Inc.)
===========================================================================*/

#define AMD_VENDOR_ID                   0x1002

/* Primary BC-250 GPU device IDs (Cyan Skillfish / Ariel APU) */
#define AMDBC250_DEVICE_ID_PRIMARY      0x13FE  /* Main BC-250 variant       */
#define AMDBC250_DEVICE_ID_ALT1         0x143F  /* Alternate BC-250 variant  */
#define AMDBC250_DEVICE_ID_ALT2         0x13DB  /* Extended family member    */
#define AMDBC250_DEVICE_ID_ALT3         0x13F9  /* Extended family member    */
#define AMDBC250_DEVICE_ID_ALT4         0x13FA  /* Extended family member    */
#define AMDBC250_DEVICE_ID_ALT5         0x13FB  /* Extended family member    */
#define AMDBC250_DEVICE_ID_ALT6         0x13FC  /* Extended family member    */

/* Subsystem IDs (ASRock BC-250 mining board variants) */
#define ASROCK_SUBSYSTEM_VENDOR_ID      0x1849
#define ASROCK_BC250_SUBSYSTEM_ID_A     0x13FE
#define ASROCK_BC250_SUBSYSTEM_ID_B     0x143F

/* PCI Revision */
#define AMDBC250_PCI_REVISION           0x01

/* PCI BAR indices */
#define AMDBC250_BAR_MMIO               0       /* 256 MB MMIO aperture      */
#define AMDBC250_BAR_DOORBELL           2       /* Doorbell registers        */
#define AMDBC250_BAR_FRAMEBUFFER        4       /* VRAM aperture (UMA)       */
#define AMDBC250_BAR5_MMIO              5       /* GPU MMIO BAR (BC-250)     */

/* Known BC-250 GPU MMIO physical base (BAR5). The translated resource list
   enumerates BAR0 first, but BC-250's GPU registers live behind BAR5. */
#define AMDBC250_BAR5_MMIO_PHYSICAL_BASE  0xFE800000ULL
#define AMDBC250_BAR5_MMIO_SIZE           0x80000ULL   /* 512 KB */

/*===========================================================================
  Hardware Capabilities
===========================================================================*/

#define AMDBC250_NUM_COMPUTE_UNITS      24      /* RDNA2 CUs                 */
#define AMDBC250_NUM_SHADER_ENGINES     1       /* Single shader engine      */
#define AMDBC250_NUM_SHADER_ARRAYS      2       /* Shader arrays per SE      */
#define AMDBC250_WAVEFRONT_SIZE         32      /* RDNA2 wave32 mode         */
#define AMDBC250_MAX_WAVES_PER_CU       20      /* Max waves per CU          */
#define AMDBC250_CACHE_LINE_SIZE        64      /* L1 cache line (bytes)     */
#define AMDBC250_L2_CACHE_SIZE_KB       512     /* L2 cache size (KB)        */
#define AMDBC250_TOTAL_MEMORY_MB        16384   /* 16 GB GDDR6 shared (UMA)  */
#define AMDBC250_DEFAULT_VRAM_MB        16384   /* UMA dynamic pool baseline */
#define AMDBC250_MAX_VRAM_MB            16384   /* Max logical shared budget */
#define AMDBC250_MEMORY_BUS_WIDTH       256     /* Memory bus width (bits)   */
#define AMDBC250_BASE_CLOCK_MHZ         1000    /* Base GPU clock (MHz)      */
#define AMDBC250_BOOST_CLOCK_MHZ        2000    /* Boost GPU clock (MHz)     */
#define AMDBC250_MEMORY_CLOCK_MHZ       1750    /* GDDR6 memory clock (MHz)  */
#define AMDBC250_TDP_WATTS              220     /* Thermal Design Power      */

/*===========================================================================
  MMIO Register Offsets (GFX/COMPUTE/DISPLAY)
  BC-250 register bases (verified 2026-07-31 ip_discovery):
    GC_BASE = 0x1260, HDP = 0x0F20, MMHUB = 0x1A000, MP0/MP1 = 0x16000,
    DCN = 0xD300, NBIO = 0x0000, SDMA at GC offsets.
  GC/IP registers are GC_BASE-shifted; NBIO regs are unshifted; MP1 C2PMSG
  live in SMN space reachable via the NBIO SMN window (BAR5+0x38/0x3C).
  Corrections follow inc/amdbc250_dream_hw.h (verified on hardware).
===========================================================================*/

#define AMDBC250_GC_BASE                        0x1260
#define AMDBC250_DCN_BASE                       0x0000D300

/* --- System Registers --- */
#define AMDBC250_REG_SCRATCH_REG0       (AMDBC250_GC_BASE + 0x00002074)  /* 0x32D4 */
#define AMDBC250_REG_SCRATCH_REG1       (AMDBC250_GC_BASE + 0x00002078)  /* 0x32D8 */
#define AMDBC250_REG_SCRATCH_REG2       (AMDBC250_GC_BASE + 0x0000207C)  /* 0x32DC */
#define AMDBC250_REG_SCRATCH_REG3       (AMDBC250_GC_BASE + 0x00002080)  /* 0x32E0 */

/* --- GPU Identification --- */
#define AMDBC250_REG_CHIP_FAMILY        0x00000E00  /* Chip family ID        */
#define AMDBC250_REG_CHIP_REVISION      0x00000E04  /* Chip revision         */
#define AMDBC250_REG_HW_ID              0x00000E08  /* Hardware ID           */
#define AMDBC250_REG_ASIC_REVISION      0x00000E0C  /* ASIC revision         */

/* --- GFX Command Processor (GC_BASE-shifted; mm from gc_10_1_0_offset.h) --- */
#define AMDBC250_REG_CP_ME_CNTL         (AMDBC250_GC_BASE + 0x00003814)  /* 0x4A74 */
#define AMDBC250_REG_CP_PFP_UCODE_ADDR  (AMDBC250_GC_BASE + 0x00016050)  /* 0x172B0 */
#define AMDBC250_REG_CP_PFP_UCODE_DATA  (AMDBC250_GC_BASE + 0x00016054)  /* 0x172B4 */
#define AMDBC250_REG_CP_ME_RAM_RADDR    (AMDBC250_GC_BASE + 0x00016058)  /* 0x172B8 */
#define AMDBC250_REG_CP_ME_RAM_WADDR    (AMDBC250_GC_BASE + 0x0001605C)  /* 0x172BC */
#define AMDBC250_REG_CP_ME_RAM_DATA     (AMDBC250_GC_BASE + 0x00016060)  /* 0x172C0 */
#define AMDBC250_REG_CP_MEC_CNTL        0x0000C0E0  /* MEC (compute) control (NBIO) */
#define AMDBC250_REG_CP_MEC_CNTL_GC     (AMDBC250_GC_BASE + 0x000038B4)  /* 0x4B14 */
#define AMDBC250_REG_CP_HQD_ACTIVE      (AMDBC250_GC_BASE + 0x00007EAC)  /* 0x910C */
#define AMDBC250_REG_CP_HQD_VMID        (AMDBC250_GC_BASE + 0x00007EB0)  /* 0x9110 */

/* --- Graphics Ring Buffer (GFX Ring 0; mm=0x1DE0..0x1DF5, GC_BASE-shifted) --- */
#define AMDBC250_REG_CP_RB0_BASE        (AMDBC250_GC_BASE + 0x00007780)  /* 0x89E0 */
#define AMDBC250_REG_CP_RB0_BASE_HI     (AMDBC250_GC_BASE + 0x00007944)  /* 0x8BA4 */
#define AMDBC250_REG_CP_RB0_CNTL        (AMDBC250_GC_BASE + 0x00007784)  /* 0x89E4 */
#define AMDBC250_REG_CP_RB0_RPTR        (AMDBC250_GC_BASE + 0x00003D80)  /* 0x4FE0 */
#define AMDBC250_REG_CP_RB0_WPTR        (AMDBC250_GC_BASE + 0x000077D0)  /* 0x8A30 */
#define AMDBC250_REG_CP_RB0_WPTR_HI     (AMDBC250_GC_BASE + 0x000077D4)  /* 0x8A34 */
#define AMDBC250_REG_CP_RB_VMID         (AMDBC250_GC_BASE + 0x00007788)  /* 0x89E8 */
#define AMDBC250_REG_CP_RB_DOORBELL_CTL (AMDBC250_GC_BASE + 0x0000C820)  /* 0xDA80 */

/* --- Memory Controller --- */
#define AMDBC250_REG_MC_VM_FB_LOCATION  0x00009520  /* Framebuffer location  */
#define AMDBC250_REG_MC_VM_AGP_BASE     0x00009524  /* AGP base              */
#define AMDBC250_REG_MC_VM_AGP_TOP      0x00009528  /* AGP top               */
#define AMDBC250_REG_MC_VM_AGP_BOT      0x0000952C  /* AGP bottom            */
#define AMDBC250_REG_MC_VM_SYSTEM_APERTURE_LOW   0x00009540
#define AMDBC250_REG_MC_VM_SYSTEM_APERTURE_HIGH  0x00009544

/* --- VM/GART (IOMMU/address translation) --- */
#define AMDBC250_REG_VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32  0x00009B00
#define AMDBC250_REG_VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32  0x00009B04
#define AMDBC250_REG_VM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32 0x00009B08
#define AMDBC250_REG_VM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32 0x00009B0C
#define AMDBC250_REG_VM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32   0x00009B10
#define AMDBC250_REG_VM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32   0x00009B14
#define AMDBC250_REG_VM_INVALIDATE_ENG0_REQ                 0x00009B40
#define AMDBC250_REG_VM_INVALIDATE_ENG0_ACK                 0x00009B80

/* --- Interrupt Controller --- */
#define AMDBC250_REG_IH_RB_BASE         0x00003800  /* IH ring base          */
#define AMDBC250_REG_IH_RB_BASE_HI      0x00003804  /* IH ring base high     */
#define AMDBC250_REG_IH_RB_CNTL         0x00003808  /* IH ring control       */
#define AMDBC250_REG_IH_RB_RPTR         0x00003810  /* IH ring read ptr      */
#define AMDBC250_REG_IH_RB_WPTR         0x00003814  /* IH ring write ptr     */
#define AMDBC250_REG_IH_DOORBELL_RPTR   0x00003818  /* IH doorbell read ptr  */
#define AMDBC250_REG_IH_CNTL            0x00003820  /* IH control            */
#define AMDBC250_REG_IH_CNTL2           0x00003824  /* IH control 2          */
#define AMDBC250_REG_IH_STATUS          0x00003830  /* IH status             */

/* --- Display Controller (DCN 2.01) ---
   BC-250 DCN timings live at DCN_BASE + mm*4 (mm from dcn offset tables).
   CRTC0_* macros map to OTG0 (*OTG* timing) registers, verified live on HW:
   OTG0_OTG_CONTROL (0x14004) = 0x80011311 ENABLED, frame counter LIVE. */
#define AMDBC250_REG_CRTC0_STATUS       (AMDBC250_DCN_BASE + 0x1B49 * 4)  /* 0x14024 OTG0 status       */
#define AMDBC250_REG_CRTC0_CONTROL      (AMDBC250_DCN_BASE + 0x1B41 * 4)  /* 0x14004 OTG0_OTG_CONTROL  */
#define AMDBC250_REG_CRTC0_H_TOTAL      (AMDBC250_DCN_BASE + 0x1B2A * 4)  /* 0x13FA8 OTG0_H_TOTAL      */
#define AMDBC250_REG_CRTC0_V_TOTAL      (AMDBC250_DCN_BASE + 0x1B2F * 4)  /* 0x13FBC OTG0_V_TOTAL      */
#define AMDBC250_REG_CRTC0_H_BLANK      (AMDBC250_DCN_BASE + 0x1B2B * 4)  /* 0x13FAC OTG0_H_BLANK      */
#define AMDBC250_REG_CRTC0_V_BLANK      (AMDBC250_DCN_BASE + 0x1B36 * 4)  /* 0x13FD8 OTG0_V_BLANK      */
#define AMDBC250_REG_CRTC0_H_SYNC       (AMDBC250_DCN_BASE + 0x1B2C * 4)  /* 0x13FB0 OTG0_H_SYNC_A     */
#define AMDBC250_REG_CRTC0_V_SYNC       (AMDBC250_DCN_BASE + 0x1B37 * 4)  /* 0x13FDC OTG0_V_SYNC_A     */
#define AMDBC250_REG_CRTC0_BASE_ADDRESS_LO (AMDBC250_DCN_BASE + 0x1B5A * 4)  /* 0x14068 OTG0_BASE_ADDRESS_LO */
#define AMDBC250_REG_CRTC0_BASE_ADDRESS_HI (AMDBC250_DCN_BASE + 0x1B5B * 4)  /* 0x1406C OTG0_BASE_ADDRESS_HI */

/* --- Power Management (SMU/MP1) ---
   MP1 C2PMSG live in SMN space (0x03B10Axx) reachable via NBIO SMN window
   (BAR5+0x38/0x3C), NOT directly in BAR5. BAR5 offsets (MP1_BASE 0x16000 +
   mm*4) still hold the raw register slots for direct-probe purposes. */
#define AMDBC250_REG_SMC_IND_INDEX      0x00000200  /* SM indirect index    */
#define AMDBC250_REG_SMC_IND_DATA       0x00000204  /* SM indirect data     */
#define AMDBC250_REG_MP1_SMN_C2PMSG_66  0x00016A08  /* C2P 66 (msg, 0x16000+0x0282*4) */
#define AMDBC250_REG_MP1_SMN_C2PMSG_82  0x00016A48  /* C2P 82 (arg, 0x16000+0x0292*4) */
#define AMDBC250_REG_MP1_SMN_C2PMSG_90  0x00016A68  /* C2P 90 (rsp, 0x16000+0x029A*4) */
#define AMDBC250_REG_MP1_SMN_P2CMSG_1   0x00016204  /* P2C message 1         */
#define AMDBC250_REG_MP1_SMN_P2CMSG_33  0x00016284  /* P2C message 33        */

/* --- GFX Configuration ---
   Corrected 2026-08-03 (per inc/amdbc250_dream_hw.h): Linux mmGB_ADDR_CONFIG
   = 0x13DE, BAR5 offset = 0x61D8 (verified), read = 0x61DC. */
#define AMDBC250_REG_GC_USER_PRIM_CONFIG    0x00009B7C  /* Primitive config  */
#define AMDBC250_REG_GC_USER_RB_BACKEND_DISABLE 0x00009B80
#define AMDBC250_REG_GB_ADDR_CONFIG         0x000061D8  /* GB address config */
#define AMDBC250_REG_GB_ADDR_CONFIG_READ    0x000061DC  /* GB addr config rd */

/*===========================================================================
  Register Bit Fields
===========================================================================*/

/* CP_ME_CNTL bits */
#define CP_ME_CNTL__ME_HALT_MASK        0x10000000
#define CP_ME_CNTL__PFP_HALT_MASK       0x40000000
#define CP_ME_CNTL__CE_HALT_MASK        0x20000000

/* CP_RB0_CNTL bits */
#define CP_RB0_CNTL__RB_BUFSZ_MASK      0x0000003F
#define CP_RB0_CNTL__RB_BLKSZ_MASK      0x00003F00
#define CP_RB0_CNTL__RB_BLKSZ_SHIFT     8
#define CP_RB0_CNTL__RB_NO_UPDATE_MASK  0x08000000
#define CP_RB0_CNTL__RB_RPTR_WR_ENA_MASK 0x80000000

/* IH_CNTL bits */
#define IH_CNTL__ENABLE_INTR_MASK       0x00000001
#define IH_CNTL__MC_WRREQ_CREDIT_MASK   0x00000006
#define IH_CNTL__IH_IDLE_MASK           0x00010000

/* GB_ADDR_CONFIG bits (RDNA2 / Navi 10) */
#define GB_ADDR_CONFIG__NUM_PIPES_MASK          0x00000007
#define GB_ADDR_CONFIG__PIPE_INTERLEAVE_SIZE_MASK 0x00000070
#define GB_ADDR_CONFIG__MAX_COMPRESSED_FRAGS_MASK 0x00000300
#define GB_ADDR_CONFIG__NUM_PKRS_MASK           0x00007000

/*===========================================================================
  Command Packet Definitions (PM4 packets)
===========================================================================*/

/* PM4 packet type identifiers */
#define PM4_TYPE0_PKT                   0x00000000
#define PM4_TYPE2_PKT                   0x80000000
#define PM4_TYPE3_PKT                   0xC0000000

/* PM4 Type-3 opcodes */
#define PM4_IT_NOP                      0x10
#define PM4_IT_SET_BASE                 0x11
#define PM4_IT_CLEAR_STATE              0x12
#define PM4_IT_INDEX_BUFFER_SIZE        0x13
#define PM4_IT_DISPATCH_DIRECT          0x15
#define PM4_IT_DISPATCH_INDIRECT        0x16
#define PM4_IT_DRAW_INDEX_2             0x27
#define PM4_IT_DRAW_INDEX_AUTO          0x2D
#define PM4_IT_DRAW_INDIRECT            0x28
#define PM4_IT_DRAW_INDIRECT_MULTI      0x2C
#define PM4_IT_INDIRECT_BUFFER          0x3F
#define PM4_IT_COPY_DATA                0x40
#define PM4_IT_PFP_SYNC_ME              0x42
#define PM4_IT_SURFACE_SYNC             0x43
#define PM4_IT_EVENT_WRITE              0x46
#define PM4_IT_EVENT_WRITE_EOP          0x47
#define PM4_IT_RELEASE_MEM              0x49
#define PM4_IT_WAIT_REG_MEM             0x3C
#define PM4_IT_SET_CONFIG_REG           0x68
#define PM4_IT_SET_CONTEXT_REG          0x69
#define PM4_IT_SET_SH_REG               0x76
#define PM4_IT_WRITE_DATA               0x37
#define PM4_IT_FRAME_CONTROL            0x90

/* PM4 packet header macro */
#define PM4_HDR(op, count, type)        \
    (((type) << 30) | (((count) - 2) << 16) | ((op) << 8))

#define PM4_TYPE3_HDR(op, count)        PM4_HDR(op, count, 3)
#define PM4_NOP_DW                      PM4_TYPE3_HDR(PM4_IT_NOP, 2)

/*===========================================================================
  Interrupt Source IDs
===========================================================================*/

#define AMDBC250_IH_CLIENTID_GFX        0x0A
#define AMDBC250_IH_CLIENTID_SDMA0      0x12
#define AMDBC250_IH_CLIENTID_SDMA1      0x13
#define AMDBC250_IH_CLIENTID_VMC        0x11
#define AMDBC250_IH_CLIENTID_DCE        0x08

/* IH ring entry size (4 DWORDs = 16 bytes) */
#define AMDBC250_IH_RING_ENTRY_SIZE     16

/* IH ring size (must be power of 2) */
#define AMDBC250_IH_RING_SIZE           (64 * 1024)  /* 64 KB */

/*===========================================================================
  Fence / Synchronization
===========================================================================*/

#define AMDBC250_FENCE_INVALID          0xDEADBEEF
#define AMDBC250_FENCE_SIGNALED         0xCAFEBABE
#define AMDBC250_MAX_FENCE_VALUE        0xFFFFFFFF

/*===========================================================================
  Memory Alignment Requirements
===========================================================================*/

#define AMDBC250_RING_BUFFER_ALIGN      4096    /* 4 KB alignment            */
#define AMDBC250_FENCE_ALIGN            64      /* 64-byte alignment         */
#define AMDBC250_IH_RING_ALIGN          4096    /* 4 KB alignment            */
#define AMDBC250_PAGE_TABLE_ALIGN       4096    /* 4 KB alignment            */
#define AMDBC250_COMMAND_BUFFER_ALIGN   256     /* 256-byte alignment        */

/*===========================================================================
  GPU Reset / Init Timeouts (in microseconds)
===========================================================================*/

#define AMDBC250_RESET_TIMEOUT_US       100000  /* 100 ms reset timeout      */
#define AMDBC250_INIT_TIMEOUT_US        500000  /* 500 ms init timeout       */
#define AMDBC250_FENCE_TIMEOUT_US       5000000 /* 5 s fence timeout         */
#define AMDBC250_SMU_TIMEOUT_US         100000  /* 100 ms SMU timeout        */

/*===========================================================================
  SDMA (System DMA) Engine Registers
  Corrected 2026-08-03: SDMA regs live at GC offsets (ip_discovery reports
  SDMA base == GC base 0x1260), verified 0xE000-0xE018 from diagnostic probe.
===========================================================================*/

#define AMDBC250_REG_SDMA0_GFX_RB_BASE          0x0000E000
#define AMDBC250_REG_SDMA0_GFX_RB_BASE_HI       0x0000E004
#define AMDBC250_REG_SDMA0_GFX_RB_CNTL          0x0000E008
#define AMDBC250_REG_SDMA0_GFX_RB_RPTR          0x0000E00C
#define AMDBC250_REG_SDMA0_GFX_RB_WPTR          0x0000E010
#define AMDBC250_REG_SDMA0_GFX_DOORBELL         0x0000E014  /* GFX_RB_WPTR_POLL */
#define AMDBC250_REG_SDMA0_F32_CNTL             0x0000E018  /* SDMA0_CNTL       */
#define AMDBC250_REG_SDMA0_CNTL                 0x0000E018
#define AMDBC250_REG_SDMA0_STATUS_REG           0x0000E01C

/* SDMA packet opcodes */
#define SDMA_OP_NOP                     0x00
#define SDMA_OP_COPY                    0x01
#define SDMA_OP_WRITE                   0x02
#define SDMA_OP_INDIRECT                0x04
#define SDMA_OP_FENCE                   0x05
#define SDMA_OP_TRAP                    0x06
#define SDMA_OP_POLL_REGMEM             0x08
#define SDMA_OP_TIMESTAMP               0x0D
#define SDMA_OP_SRBM_WRITE              0x0E

/*===========================================================================
  Display Engine (DCN 2.01) - DisplayPort
===========================================================================*/

#define AMDBC250_NUM_DISPLAY_PIPES      1       /* Single DisplayPort output */
#define AMDBC250_MAX_DISPLAY_WIDTH      7680    /* 8K horizontal max         */
#define AMDBC250_MAX_DISPLAY_HEIGHT     4320    /* 8K vertical max           */
#define AMDBC250_MAX_PIXEL_CLOCK_KHZ    600000  /* 600 MHz max pixel clock   */

/* DP AUX channel registers */
#define AMDBC250_REG_DP_AUX0_AUX_CNTL  0x00004800
#define AMDBC250_REG_DP_AUX0_AUX_SW_DATA 0x00004804
#define AMDBC250_REG_DP_AUX0_AUX_LS_DATA 0x00004808
#define AMDBC250_REG_DP_AUX0_AUX_SW_STATUS 0x0000480C

#endif /* _AMDBC250_HW_H_ */
