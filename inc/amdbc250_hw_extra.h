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
#define AMDBC250_TOTAL_MEMORY_MB        16384   /* 16 GB GDDR6 shared        */
#define AMDBC250_DEFAULT_VRAM_MB        512     /* Default VRAM allocation   */
#define AMDBC250_MAX_VRAM_MB            8192    /* Max VRAM allocation       */
#define AMDBC250_MEMORY_BUS_WIDTH       256     /* Memory bus width (bits)   */
#define AMDBC250_BASE_CLOCK_MHZ         1000    /* Base GPU clock (MHz)      */
#define AMDBC250_BOOST_CLOCK_MHZ        2000    /* Boost GPU clock (MHz)     */
#define AMDBC250_MEMORY_CLOCK_MHZ       1750    /* GDDR6 memory clock (MHz)  */
#define AMDBC250_TDP_WATTS              220     /* Thermal Design Power      */

/*===========================================================================
  MMIO Register Offsets (GFX/COMPUTE/DISPLAY)
  Based on RDNA2 (GFX10.1) register specification
===========================================================================*/

/* --- System Registers --- */
#define AMDBC250_REG_SCRATCH_REG0       0x00008500  /* Scratch register 0    */
#define AMDBC250_REG_SCRATCH_REG1       0x00008504  /* Scratch register 1    */
#define AMDBC250_REG_SCRATCH_REG2       0x00008508  /* Scratch register 2    */
#define AMDBC250_REG_SCRATCH_REG3       0x0000850C  /* Scratch register 3    */

/* --- GPU Identification --- */
#define AMDBC250_REG_CHIP_FAMILY        0x00000E00  /* Chip family ID        */
#define AMDBC250_REG_CHIP_REVISION      0x00000E04  /* Chip revision         */
#define AMDBC250_REG_HW_ID              0x00000E08  /* Hardware ID           */
#define AMDBC250_REG_ASIC_REVISION      0x00000E0C  /* ASIC revision         */

/* --- GFX Command Processor --- */
#define AMDBC250_REG_CP_ME_CNTL         0x00008080  /* ME control            */
#define AMDBC250_REG_CP_PFP_UCODE_ADDR  0x000080A0  /* PFP microcode addr    */
#define AMDBC250_REG_CP_PFP_UCODE_DATA  0x000080A4  /* PFP microcode data    */
#define AMDBC250_REG_CP_ME_RAM_RADDR    0x000080AC  /* ME RAM read addr      */
#define AMDBC250_REG_CP_ME_RAM_WADDR    0x000080B0  /* ME RAM write addr     */
#define AMDBC250_REG_CP_ME_RAM_DATA     0x000080B4  /* ME RAM data           */
#define AMDBC250_REG_CP_MEC_CNTL        0x000080E0  /* MEC control           */
#define AMDBC250_REG_CP_HQD_ACTIVE      0x000080D8  /* HQD active            */
#define AMDBC250_REG_CP_HQD_VMID        0x000080DC  /* HQD VMID              */

/* --- Graphics Ring Buffer (GFX Ring 0) --- */
#define AMDBC250_REG_CP_RB0_BASE        0x0000C100  /* Ring buffer base addr */
#define AMDBC250_REG_CP_RB0_BASE_HI     0x0000C104  /* Ring buffer base high */
#define AMDBC250_REG_CP_RB0_CNTL        0x0000C108  /* Ring buffer control   */
#define AMDBC250_REG_CP_RB0_RPTR        0x0000C10C  /* Ring read pointer     */
#define AMDBC250_REG_CP_RB0_WPTR        0x0000C114  /* Ring write pointer    */
#define AMDBC250_REG_CP_RB0_WPTR_HI     0x0000C118  /* Ring write ptr high   */
#define AMDBC250_REG_CP_RB_VMID         0x0000C140  /* Ring VMID             */
#define AMDBC250_REG_CP_RB_DOORBELL_CTL 0x0000C148  /* Doorbell control      */

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

/* --- Display Controller (DCN 2.01) --- */
#define AMDBC250_REG_CRTC0_CONTROL      0x00006080  /* CRTC0 control         */
#define AMDBC250_REG_CRTC0_H_TOTAL      0x00006084  /* CRTC0 H total         */
#define AMDBC250_REG_CRTC0_V_TOTAL      0x00006088  /* CRTC0 V total         */
#define AMDBC250_REG_CRTC0_H_BLANK      0x0000608C  /* CRTC0 H blank         */
#define AMDBC250_REG_CRTC0_V_BLANK      0x00006090  /* CRTC0 V blank         */
#define AMDBC250_REG_CRTC0_H_SYNC       0x00006094  /* CRTC0 H sync          */
#define AMDBC250_REG_CRTC0_V_SYNC       0x00006098  /* CRTC0 V sync          */
#define AMDBC250_REG_CRTC0_STATUS       0x0000609C  /* CRTC0 status          */

/* --- Power Management (SMU) --- */
#define AMDBC250_REG_SMC_IND_INDEX      0x00000200  /* SMC indirect index    */
#define AMDBC250_REG_SMC_IND_DATA       0x00000204  /* SMC indirect data     */
#define AMDBC250_REG_MP1_SMN_C2PMSG_66  0x00016104  /* C2P message 66        */
#define AMDBC250_REG_MP1_SMN_C2PMSG_82  0x00016148  /* C2P message 82        */
#define AMDBC250_REG_MP1_SMN_C2PMSG_90  0x00016168  /* C2P message 90        */
#define AMDBC250_REG_MP1_SMN_P2CMSG_1   0x00016204  /* P2C message 1         */
#define AMDBC250_REG_MP1_SMN_P2CMSG_33  0x00016284  /* P2C message 33        */

/* --- GFX Configuration --- */
#define AMDBC250_REG_GC_USER_PRIM_CONFIG    0x00009B7C  /* Primitive config  */
#define AMDBC250_REG_GC_USER_RB_BACKEND_DISABLE 0x00009B80
#define AMDBC250_REG_GB_ADDR_CONFIG         0x0000263C  /* GB address config */
#define AMDBC250_REG_GB_ADDR_CONFIG_READ    0x00009800  /* GB addr config rd */

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
===========================================================================*/

#define AMDBC250_REG_SDMA0_GFX_RB_BASE          0x00001260
#define AMDBC250_REG_SDMA0_GFX_RB_BASE_HI       0x00001264
#define AMDBC250_REG_SDMA0_GFX_RB_CNTL          0x00001268
#define AMDBC250_REG_SDMA0_GFX_RB_RPTR          0x00001270
#define AMDBC250_REG_SDMA0_GFX_RB_WPTR          0x00001278
#define AMDBC250_REG_SDMA0_GFX_DOORBELL         0x000012A0
#define AMDBC250_REG_SDMA0_F32_CNTL             0x00001214
#define AMDBC250_REG_SDMA0_CNTL                 0x0000122C
#define AMDBC250_REG_SDMA0_STATUS_REG           0x00001234

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
