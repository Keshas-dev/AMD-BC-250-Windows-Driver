/*++

Copyright (c) 2026 AMD BC-250 "Dream Drivers" Project ??? Version 3.0

Module Name:
    amdbc250_dream_hw.h

Abstract:
    CORRECTED Hardware definitions for AMD BC-250 APU.
    
    ========================================
    CORRECTED ARCHITECTURE INFORMATION
    ========================================
    
    PREVIOUS VERSIONS (v1.0, v2.0) WERE WRONG:
    - ??? v1.0: Claimed "RDNA2 / Cyan Skillfish" but used wrong registers
    - ??? v2.0: Claimed "Kaveri / GCN 1.1" ??? COMPLETELY WRONG
    
    VERSION 3.0 ??? CORRECT INFORMATION:
    - ??? BC-250 is a CUT-DOWN PS5 APU variant
    - ??? Codename: "Cyan Skillfish"
    - ??? Architecture: RDNA 1.5 (GFX1013)
    - ??? GPU: 24 RDNA2 Compute Units (1536 shaders)
    - ??? Memory: 16GB GDDR6 (shared CPU/GPU)
    - ??? PCI Device ID: 0x13FE
    - ??? Has dedicated Ray Tracing cores (early generation)
    - ??? TDP: 220W
    - ??? CPU: 6?? Zen 2 cores @ ~3.5GHz
    
    Linux Support:
    - Kernel: amdgpu (5.15+)
    - Vulkan: RADV (Mesa 25.1+)
    - OpenGL: radeonsi
    - Windows: NO OFFICIAL DRIVER ??? this is our target!
    
    Based on:
    - Linux amdgpu driver: drivers/gpu/drm/amd/amdgpu/
    - Mesa RADV driver: src/amd/vulkan/
    - AMD open-source GFX10 register definitions
    - Community BC-250 documentation: https://elektricm.github.io/amd-bc250-docs/

Environment:
    Kernel mode (Windows Display Driver Model - WDDM 2.x/3.x)

--*/

#pragma once

#ifndef _AMDBC250_DREAM_V3_HW_H_
#define _AMDBC250_DREAM_V3_HW_H_

#include <ntddk.h>

/* Forward declaration ??? defined in amdbc250_dream_kmd.h */
struct _DREAM_V3_DEVICE_EXTENSION;
typedef struct _DREAM_V3_DEVICE_EXTENSION *PDREAM_V3_DEVICE_EXTENSION;

/*===========================================================================
  PCI Identifiers ??? AMD BC-250 / Cyan Skillfish (GFX1013)
  
  Vendor ID: 0x1002 (Advanced Micro Devices, Inc.)
  Device ID: 0x13FE (BC-250 specific)
  Subsystem: 0x1022:0x0000 (AMD reference)
===========================================================================*/

#define AMD_VENDOR_ID                   0x1002

/* BC-250 / Cyan Skillfish device IDs */
#define AMDBC250_DEVICE_ID_PRIMARY      0x13FE  /* BC-250 mining board     */

/* Related Cyan Skillfish variants (from Linux amdgpu driver) */
#define AMD_DEVICE_ID_CYAN_SKILLFISH_0  0x13E0
#define AMD_DEVICE_ID_CYAN_SKILLFISH_1  0x13E1
#define AMD_DEVICE_ID_CYAN_SKILLFISH_2  0x13E2
#define AMD_DEVICE_ID_CYAN_SKILLFISH_3  0x13E3
#define AMD_DEVICE_ID_CYAN_SKILLFISH_4  0x13E4
#define AMD_DEVICE_ID_CYAN_SKILLFISH_5  0x13E5
#define AMD_DEVICE_ID_CYAN_SKILLFISH_6  0x13E6
#define AMD_DEVICE_ID_CYAN_SKILLFISH_7  0x13E7
#define AMD_DEVICE_ID_CYAN_SKILLFISH_8  0x13E8
#define AMD_DEVICE_ID_CYAN_SKILLFISH_9  0x13E9
#define AMD_DEVICE_ID_CYAN_SKILLFISH_A  0x13EA
#define AMD_DEVICE_ID_CYAN_SKILLFISH_B  0x13EB
#define AMD_DEVICE_ID_CYAN_SKILLFISH_C  0x13EC
#define AMD_DEVICE_ID_CYAN_SKILLFISH_D  0x13ED
#define AMD_DEVICE_ID_CYAN_SKILLFISH_E  0x13EE
#define AMD_DEVICE_ID_CYAN_SKILLFISH_F  0x13EF

/* PCI Revision */
#define AMDBC250_PCI_REVISION           0x00

/* PCI BAR indices for RDNA2 / GFX10 */
#define AMDBC250_BAR_MMIO               0       /* 256 KB MMIO aperture    */
#define AMDBC250_BAR_DOORBELL           2       /* Doorbell registers      */
#define AMDBC250_BAR_FRAMEBUFFER        4       /* VRAM aperture (GDDR6)   */

/*===========================================================================
  GPU Architecture: Cyan Skillfish / RDNA 1.5 (GFX1013)
  
  This is a CUT-DOWN PS5 APU variant with:
  - 24 RDNA2 Compute Units (vs PS5's 36 CUs)
  - Dedicated Ray Tracing hardware (early generation)
  - 16GB GDDR6 shared memory
  - Zen 2 CPU cores (6C/12T)
  
  Based on AMD GFX10 (Navi) family register specification.
===========================================================================*/

/* --- Core Architecture --- */
#define AMDBC250_ARCHITECTURE           "Cyan Skillfish"
#define AMDBC250_FAMILY                 "GFX10"
#define AMDBC250_GFX_VERSION            10      /* GFX10.x family          */
#define AMDBC250_GFX_MINOR              1       /* GFX10.1                 */
#define AMDBC250_GFX_PATCH            3       /* GFX10.1.3 (1013)        */

/* --- Compute Units and Shaders --- */
#define AMDBC250_MAX_COMPUTE_UNITS      24      /* 24 RDNA2 CUs            */
#define AMDBC250_STREAM_PROCESSORS      1536    /* 24 ?? 64 = 1536          */
#define AMDBC250_SHADER_ENGINES         2       /* 2 shader engines        */
#define AMDBC250_SHADER_ARRAYS          2       /* 2 shader arrays         */
#define AMDBC250_CU_PER_SE              6       /* 6 CUs per SE (per SA)   */
#define AMDBC250_WGP_PER_CU             2       /* 2 Work Group Processors */

/* --- Wavefront Configuration (RDNA2) --- */
#define AMDBC250_WAVEFRONT_SIZE         32      /* RDNA2 uses wave32       */
#define AMDBC250_MAX_WAVES_PER_CU       32      /* Max wavefronts per CU   */
#define AMDBC250_MAX_WAVES_PER_WGP      64      /* Per WGP                 */
#define AMDBC250_VGPR_PER_WGP           512     /* Vector GPRs             */
#define AMDBC250_SGPR_PER_WGP           512     /* Scalar GPRs             */

/* --- Cache Hierarchy (RDNA2) --- */
#define AMDBC250_L1_CACHE_SIZE_KB       128     /* L1 (per WGP)            */
#define AMDBC250_L2_CACHE_SIZE_KB       2048    /* L2 (2 MB total)         */
#define AMDBC250_CACHE_LINE_SIZE        128     /* RDNA2: 128B lines       */

/* --- Memory Configuration (GDDR6 ??? Shared UMA) ---
  
  IMPORTANT: BC-250 uses Unified Memory Architecture (UMA).
  The 16GB GDDR6 is SHARED between CPU and GPU.
  VRAM allocation is configurable in BIOS:
  - Minimum: 512 MB (default for mining boards)
  - Recommended: 4-8 GB (for gaming/Linux desktop)
  - Maximum: ~15.5 GB (CPU gets minimal RAM)
  
  Typical BIOS splits:
  - Mining config: 512 MB GPU / 15.5 GB CPU
  - Balanced:      8 GB GPU / 8 GB CPU
  - GPU-heavy:     12 GB GPU / 4 GB CPU
  
  Bandwidth: 256-bit bus ?? 14 Gbps = ~448 GB/s
============================================================================*/

#define AMDBC250_MEMORY_TYPE            "GDDR6"
#define AMDBC250_TOTAL_MEMORY_MB        16384   /* 16 GB total (shared)    */
#define AMDBC250_MEMORY_BUS_WIDTH       256     /* 256-bit bus             */
#define AMDBC250_MEMORY_CLOCK_MHZ       1750    /* GDDR6 effective 14 Gbps */
#define AMDBC250_MEMORY_BANDWIDTH_GBPS  448     /* ~448 GB/s theoretical   */

/* --- VRAM Allocation (BIOS configurable) --- */
#define AMDBC250_VRAM_ALLOC_MIN_MB      512     /* Mining default          */
#define AMDBC250_VRAM_ALLOC_DEFAULT_MB  4096    /* 4GB balanced            */
#define AMDBC250_VRAM_ALLOC_MAX_MB      15872   /* Max (~15.5GB, CPU: 512MB)*/

/* --- Clock Speeds --- */
#define AMDBC250_BASE_CLOCK_MHZ         1000    /* Base GPU clock          */
#define AMDBC250_BOOST_CLOCK_MHZ        2000    /* Max boost (gov enabled) */
#define AMDBC250_STATIC_CLOCK_MHZ       1500    /* Without governor        */

/* --- Ray Tracing (Early Gen) --- */
#define AMDBC250_HAS_RAY_TRACING        TRUE    /* Dedicated RT cores      */
#define AMDBC250_RT_ACCELERATORS        24      /* 1 per CU                */
#define AMDBC250_RT_PERFORMANCE         "Low"   /* Early gen, poor in games*/

/* --- TDP and Power --- */
#define AMDBC250_TDP_WATTS              220     /* Total board power       */
#define AMDBC250_IDLE_POWER_WATTS       50      /* Idle power draw         */
#define AMDBC250_MAX_LOAD_POWER_WATTS   235     /* Peak power              */
#define AMDBC250_PSU_REQUIREMENT_WATTS  300     /* Recommended PSU         */

/*===========================================================================
  MMIO Register Offsets ??? GFX10 (RDNA2 / Navi family)
  
  Based on:
  - Linux amdgpu: drivers/gpu/drm/amd/include/navi10_enum.h
  - Linux amdgpu: drivers/gpu/drm/amd/include/gc/v10/gc_10_1_0_offset.h
  - Linux amdgpu: drivers/gpu/drm/amd/include/dc/dcn20/dcn20_enum.h
===========================================================================*/

/* --- GPU Identification Registers --- */
#define AMDBC250_REG_HW_ID              0x00000E08  /* Hardware ID           */
#define AMDBC250_REG_HW_ID2             0x00000E0C  /* Hardware ID 2         */
#define AMDBC250_REG_CHIP_FAMILY        0x00000E10  /* Chip family ID        */
#define AMDBC250_REG_ASIC_REVISION      0x00000E14  /* ASIC revision         */

/* --- Scratch Registers (BC-250 corrected: GC_BASE + 0x2074) --- */
#define AMDBC250_REG_SCRATCH_REG0       (AMDBC250_GC_BASE + 0x00002074)  /* 0x32D4 */
#define AMDBC250_REG_SCRATCH_REG1       (AMDBC250_GC_BASE + 0x00002078)  /* 0x32D8 */
#define AMDBC250_REG_SCRATCH_REG2       (AMDBC250_GC_BASE + 0x0000207C)  /* 0x32DC */
#define AMDBC250_REG_SCRATCH_REG3       (AMDBC250_GC_BASE + 0x00002080)  /* 0x32E0 */
#define AMDBC250_REG_SCRATCH_REG4       (AMDBC250_GC_BASE + 0x00002084)  /* 0x32E4 */
#define AMDBC250_REG_SCRATCH_REG5       (AMDBC250_GC_BASE + 0x00002088)  /* 0x32E8 */
#define AMDBC250_REG_SCRATCH_REG6       (AMDBC250_GC_BASE + 0x0000208C)  /* 0x32EC */
#define AMDBC250_REG_SCRATCH_REG7       (AMDBC250_GC_BASE + 0x00002090)  /* 0x32F0 */

/* --- Graphics Command Processor (GFX10 CP) --- */
/* NOTE: CP_ME_CNTL/MEC_CNTL at 0xC060-C0FF are NBIO addresses, NOT shifted by GC_BASE.
 *       NBIO firewall BLOCKS writes to 0xC000-0xCFFF from ALL paths.
 *       On BC-250, the GC_BASE-shifted alias for CP_ME_CNTL is at 0x4A74
 *       (mmCP_ME_CNTL = 0x0E05, byte offset = 0x3814, GC_BASE + 0x3814 = 0x4A74).
 *       Writes to 0xC060 are silently ignored. */
#define AMDBC250_REG_CP_ME_CNTL         (AMDBC250_GC_BASE + 0x00003814)  /* 0x4A74, GC_BASE-shifted */
#define AMDBC250_REG_CP_ME_STATUS       0x0000C064  /* CP ME status (NBIO)             */
#define AMDBC250_REG_CP_PFP_UCODE_ADDR  0x0000C0A0  /* PFP firmware addr (NBIO)        */
#define AMDBC250_REG_CP_PFP_UCODE_DATA  0x0000C0A4  /* PFP firmware data (NBIO)        */
#define AMDBC250_REG_CP_ME_UCODE_ADDR   0x0000C0B0  /* ME firmware addr (NBIO)         */
#define AMDBC250_REG_CP_ME_UCODE_DATA   0x0000C0B4  /* ME firmware data (NBIO)         */
#define AMDBC250_REG_CP_MEC_CNTL        0x0000C0E0  /* MEC (compute) control (NBIO)    */
#define AMDBC250_REG_CP_MEC_STATUS      0x0000C0E4  /* MEC status (NBIO)               */

/* --- CP Firmware Loading Registers (GC_BASE-shifted, bypasses NBIO firewall) ---
 * These are the HYP (hypervisor) variants of the ucode upload registers.
 * From Linux gc_10_1_0_offset.h: mm values are DWORD offsets, byte = mm*4.
 * GC_BASE = 0x1260, so byte offset = GC_BASE + (mm * 4).
 * CP_HYP_PFP_UCODE_ADDR: mm=0x5814, byte=0x16050, GC shifted=0x172B0
 * CP_HYP_PFP_UCODE_DATA: mm=0x5815, byte=0x16054, GC shifted=0x172B4
 * CP_HYP_ME_UCODE_ADDR:  mm=0x5816, byte=0x16058, GC shifted=0x172B8
 * CP_HYP_ME_UCODE_DATA:  mm=0x5817, byte=0x1605C, GC shifted=0x172BC
 * CP_HYP_CE_UCODE_ADDR:  mm=0x5818, byte=0x16060, GC shifted=0x172C0
 * CP_HYP_CE_UCODE_DATA:  mm=0x5819, byte=0x16064, GC shifted=0x172C4
 *
 * IC_BASE registers (firmware DMA target):
 * CP_PFP_IC_BASE_CNTL: mm=0x5842, byte=0x16108, GC shifted=0x17368
 * CP_PFP_IC_BASE_LO:   mm=0x5840, byte=0x16100, GC shifted=0x17360
 * CP_PFP_IC_BASE_HI:   mm=0x5841, byte=0x16104, GC shifted=0x17364
 * CP_ME_IC_BASE_CNTL:  mm=0x5846, byte=0x16118, GC shifted=0x17378
 * CP_ME_IC_BASE_LO:    mm=0x5844, byte=0x16110, GC shifted=0x17370
 * CP_ME_IC_BASE_HI:    mm=0x5845, byte=0x16114, GC shifted=0x17374
 * CP_CE_IC_BASE_CNTL:  mm=0x584A, byte=0x16128, GC shifted=0x17388
 * CP_CE_IC_BASE_LO:    mm=0x5848, byte=0x16120, GC shifted=0x17380
 * CP_CE_IC_BASE_HI:    mm=0x5849, byte=0x16124, GC shifted=0x17384
 */
#define AMDBC250_REG_CP_HYP_PFP_UCODE_ADDR  (AMDBC250_GC_BASE + 0x00016050)  /* 0x172B0 */
#define AMDBC250_REG_CP_HYP_PFP_UCODE_DATA  (AMDBC250_GC_BASE + 0x00016054)  /* 0x172B4 */
#define AMDBC250_REG_CP_HYP_ME_UCODE_ADDR   (AMDBC250_GC_BASE + 0x00016058)  /* 0x172B8 */
#define AMDBC250_REG_CP_HYP_ME_UCODE_DATA   (AMDBC250_GC_BASE + 0x0001605C)  /* 0x172BC */
#define AMDBC250_REG_CP_HYP_CE_UCODE_ADDR   (AMDBC250_GC_BASE + 0x00016060)  /* 0x172C0 */
#define AMDBC250_REG_CP_HYP_CE_UCODE_DATA   (AMDBC250_GC_BASE + 0x00016064)  /* 0x172C4 */

#define AMDBC250_REG_CP_PFP_IC_BASE_CNTL    (AMDBC250_GC_BASE + 0x00016108)  /* 0x17368 */
#define AMDBC250_REG_CP_PFP_IC_BASE_LO      (AMDBC250_GC_BASE + 0x00016100)  /* 0x17360 */
#define AMDBC250_REG_CP_PFP_IC_BASE_HI      (AMDBC250_GC_BASE + 0x00016104)  /* 0x17364 */
#define AMDBC250_REG_CP_ME_IC_BASE_CNTL     (AMDBC250_GC_BASE + 0x00016118)  /* 0x17378 */
#define AMDBC250_REG_CP_ME_IC_BASE_LO       (AMDBC250_GC_BASE + 0x00016110)  /* 0x17370 */
#define AMDBC250_REG_CP_ME_IC_BASE_HI       (AMDBC250_GC_BASE + 0x00016114)  /* 0x17374 */
#define AMDBC250_REG_CP_CE_IC_BASE_CNTL     (AMDBC250_GC_BASE + 0x00016128)  /* 0x17388 */
#define AMDBC250_REG_CP_CE_IC_BASE_LO       (AMDBC250_GC_BASE + 0x00016120)  /* 0x17380 */
#define AMDBC250_REG_CP_CE_IC_BASE_HI       (AMDBC250_GC_BASE + 0x00016124)  /* 0x17384 */
#define AMDBC250_REG_CP_MEC_IC_BASE_LO      (AMDBC250_GC_BASE + 0x00016130)  /* 0x17390 */
#define AMDBC250_REG_CP_MEC_IC_BASE_HI      (AMDBC250_GC_BASE + 0x00016134)  /* 0x17394 */
#define AMDBC250_REG_CP_MEC_IC_BASE_CNTL    (AMDBC250_GC_BASE + 0x00016138)  /* 0x17398 */

/* --- GFX10 Ring Buffer (GFX10 style, BC-250: shift by GC_BASE=0x1260) --- */
#define AMDBC250_REG_CP_GFX_RING0_BASE_LO   (AMDBC250_GC_BASE + 0x0000C800)  /* 0xDA60 */
#define AMDBC250_REG_CP_GFX_RING0_BASE_HI   (AMDBC250_GC_BASE + 0x0000C804)  /* 0xDA64 */
#define AMDBC250_REG_CP_GFX_RING0_CNTL      (AMDBC250_GC_BASE + 0x0000C808)  /* 0xDA68 */
#define AMDBC250_REG_CP_GFX_RING0_RPTR      (AMDBC250_GC_BASE + 0x0000C80C)  /* 0xDA6C */
#define AMDBC250_REG_CP_GFX_RING0_RPTR_ADDR_LO  (AMDBC250_GC_BASE + 0x0000C810)  /* 0xDA70 */
#define AMDBC250_REG_CP_GFX_RING0_RPTR_ADDR_HI  (AMDBC250_GC_BASE + 0x0000C814)  /* 0xDA74 */
#define AMDBC250_REG_CP_GFX_RING0_WPTR      (AMDBC250_GC_BASE + 0x0000C818)  /* 0xDA78 */
#define AMDBC250_REG_CP_GFX_RING0_WPTR_POLL (AMDBC250_GC_BASE + 0x0000C81C)  /* 0xDA7C */
#define AMDBC250_REG_CP_GFX_RING0_DOORBELL  (AMDBC250_GC_BASE + 0x0000C820)  /* 0xDA80 */

/* --- Compute Rings (GFX10, BC-250: shift by GC_BASE=0x1260) --- */
#define AMDBC250_REG_CP_COMPUTE_RING0_BASE_LO   (AMDBC250_GC_BASE + 0x0000C900)  /* 0xDB60 */
#define AMDBC250_REG_CP_COMPUTE_RING0_CNTL      (AMDBC250_GC_BASE + 0x0000C908)  /* 0xDB68 */
#define AMDBC250_REG_CP_COMPUTE_RING0_RPTR      (AMDBC250_GC_BASE + 0x0000C90C)  /* 0xDB6C */
#define AMDBC250_REG_CP_COMPUTE_RING0_WPTR      (AMDBC250_GC_BASE + 0x0000C918)  /* 0xDB78 */

/* --- COMPUTE engine registers (SEG1: GC_BASE + 0xA000 + Navi10 offset) --- */
#define AMDBC250_REG_COMPUTE_DISPATCH_DIRECT    (AMDBC250_GC_BASE + AMDBC250_GC_BASE_SEG1 + 0x00002A00)  /* 0xDC60 */
#define AMDBC250_REG_COMPUTE_DISPATCH_START     (AMDBC250_GC_BASE + AMDBC250_GC_BASE_SEG1 + 0x00002A04)  /* 0xDC64 */

/* --- GFX10 HQD (Hardware Queue Dispatcher, BC-250: shift by GC_BASE=0x1260) --- */
#define AMDBC250_REG_CP_MQD_BASE_ADDR       (AMDBC250_GC_BASE + 0x0000C858)  /* 0xDAB8 */
#define AMDBC250_REG_CP_MQD_BASE_ADDR_HI    (AMDBC250_GC_BASE + 0x0000C85C)  /* 0xDABC */
#define AMDBC250_REG_CP_HQD_ACTIVE          (AMDBC250_GC_BASE + 0x0000C860)  /* 0xDAC0 */
#define AMDBC250_REG_CP_HQD_VMID            (AMDBC250_GC_BASE + 0x0000C864)  /* 0xDAC4 */
#define AMDBC250_REG_CP_HQD_PERSISTENT_STATE (AMDBC250_GC_BASE + 0x0000C868) /* 0xDAC8 */
#define AMDBC250_REG_CP_HQD_PIPE_PRIORITY   (AMDBC250_GC_BASE + 0x0000C86C)  /* 0xDACC */
#define AMDBC250_REG_CP_HQD_QUEUE_PRIORITY  (AMDBC250_GC_BASE + 0x0000C870)  /* 0xDAD0 */
#define AMDBC250_REG_CP_HQD_QUANTUM         (AMDBC250_GC_BASE + 0x0000C874)  /* 0xDAD4 */
#define AMDBC250_REG_CP_HQD_PQ_BASE         (AMDBC250_GC_BASE + 0x0000C878)  /* 0xDAD8 */
#define AMDBC250_REG_CP_HQD_PQ_BASE_HI      (AMDBC250_GC_BASE + 0x0000C87C)  /* 0xDADC */
#define AMDBC250_REG_CP_HQD_PQ_RPTR         (AMDBC250_GC_BASE + 0x0000C880)  /* 0xDAE0 */
#define AMDBC250_REG_CP_HQD_PQ_RPTR_REPORT_ADDR   (AMDBC250_GC_BASE + 0x0000C884)  /* 0xDAE4 */
#define AMDBC250_REG_CP_HQD_PQ_RPTR_REPORT_ADDR_HI (AMDBC250_GC_BASE + 0x0000C888) /* 0xDAE8 */
#define AMDBC250_REG_CP_HQD_PQ_WPTR_POLL_ADDR     (AMDBC250_GC_BASE + 0x0000C88C)  /* 0xDAEC */
#define AMDBC250_REG_CP_HQD_PQ_WPTR_POLL_ADDR_HI  (AMDBC250_GC_BASE + 0x0000C890)  /* 0xDAF0 */
#define AMDBC250_REG_CP_HQD_PQ_DOORBELL_CONTROL   (AMDBC250_GC_BASE + 0x0000C894)  /* 0xDAF4 */

/* CP_HQD_PQ_WPTR_POLL_CNTL: mm value from gc_10_1_0_offset.h 0x1E56 */
#define AMDBC250_REG_CP_HQD_PQ_WPTR_POLL_CNTL (AMDBC250_GC_BASE + 0x0000C8A0)  /* 0xDB00 */
#define AMDBC250_REG_CP_HQD_PQ_CONTROL      (AMDBC250_GC_BASE + 0x0000C89C)  /* 0xDAFC */
#define AMDBC250_REG_CP_HQD_DEQUEUE_REQUEST (AMDBC250_GC_BASE + 0x0000C8B8)  /* 0xDB18 */
#define AMDBC250_REG_CP_HQD_EOP_BASE_ADDR   (AMDBC250_GC_BASE + 0x0000C8EC)  /* 0xDB4C */
#define AMDBC250_REG_CP_HQD_EOP_BASE_ADDR_HI (AMDBC250_GC_BASE + 0x0000C8F0) /* 0xDB50 */
#define AMDBC250_REG_CP_HQD_EOP_CONTROL     (AMDBC250_GC_BASE + 0x0000C8F4)  /* 0xDB54 */
#define AMDBC250_REG_CP_HQD_EOP_RPTR        (AMDBC250_GC_BASE + 0x0000C8F8)  /* 0xDB58 */
#define AMDBC250_REG_CP_HQD_EOP_WPTR        (AMDBC250_GC_BASE + 0x0000C8FC)  /* 0xDB5C */
#define AMDBC250_REG_CP_HQD_PQ_WPTR_LO      (AMDBC250_GC_BASE + 0x0000C930)  /* 0xDB90 */
#define AMDBC250_REG_CP_HQD_PQ_WPTR_HI      (AMDBC250_GC_BASE + 0x0000C934)  /* 0xDB94 */

/* --- GRBM / SRBM Selection (BC-250) --- */
/* GRBM_GFX_INDEX: DWORD offset mmGRBM_GFX_INDEX = 0x2200 → BAR5 = GC_BASE(0x1260) + 0x2200 = 0x34D0
 * Linux amdgpu uses this for SPM/indexed register access.
 * Probe result for GRBM_GFX_INDEX: returns 0xBA062100, WRITABLE.
 * Sienna_Cichlid Seg1 alias (0xA000+0x2200) NOT used on BC-250. */
#define AMDBC250_REG_GRBM_GFX_INDEX        (AMDBC250_GC_BASE + 0x00002270)  /* 0x34D0 */

/* GRBM_GFX_CNTL: DWORD offset mmGRBM_GFX_CNTL = 0x0dc2 → BAR5 = GC_BASE(0x1260) + 0x0dc2 = 0x2022
 * Linux amdgpu nv_grbm_select() writes THIS register for ME/PIPE/QUEUE selection,
 * NOT GRBM_GFX_INDEX (0x34D0). They are DIFFERENT registers.
 * NOTE: 0x0dc2 is NOT DWORD-aligned! Linux writes this as a BYTE offset. */
#define AMDBC250_REG_GRBM_GFX_CNTL         (AMDBC250_GC_BASE + 0x00000DC2)  /* 0x2022 */
#define AMDBC250_GRBM_GFX_CNTL_ME_SHIFT    16
#define AMDBC250_GRBM_GFX_CNTL_PIPE_SHIFT  8
#define AMDBC250_GRBM_GFX_CNTL_QUEUE_SHIFT 0
#define AMDBC250_GRBM_GFX_CNTL_VMID_SHIFT  26
#define AMDBC250_GRBM_GFX_CNTL_KIQ_VAL     (1 << 16)  /* ME=1, PIPE=0, QUEUE=0, VMID=0 */

/* GRBM_GFX_INDEX bit fields (Linux soc15 layout from soc15.h):
 *   bit 31:   SE_BROADCAST
 *   bit 30:   QUEUE_BROADCAST
 *   bit 29:   PIPE_BROADCAST
 *   bits 28-24: SE_INDEX + reserved
 *   bit 26:   INSTANCE_BROADCAST
 *   bits 25-24: INSTANCE_INDEX
 *   bits 23-20: reserved
 *   bits 19-16: MEID (ME index, 4 bits)
 *   bits 15-12: SAID (Shader Array ID)
 *   bits 11-8:  PIPEID (pipe index, 4 bits)
 *   bits 7-4:   reserved
 *   bits 3-0:   QUEUEID (queue index, 4 bits)
 */
#define AMDBC250_GRBM_GFX_INDEX_MEID_SHIFT        16
#define AMDBC250_GRBM_GFX_INDEX_PIPEID_SHIFT       8
#define AMDBC250_GRBM_GFX_INDEX_QUEUEID_SHIFT      0
#define AMDBC250_GRBM_GFX_INDEX_INSTANCE_SHIFT     24
#define AMDBC250_GRBM_GFX_INDEX_SAID_SHIFT        12
#define AMDBC250_GRBM_GFX_INDEX_SEID_SHIFT         24
#define AMDBC250_GRBM_GFX_INDEX_INSTANCE_BROADCAST (1 << 26)
#define AMDBC250_GRBM_GFX_INDEX_PIPE_BROADCAST     (1 << 29)
#define AMDBC250_GRBM_GFX_INDEX_QUEUE_BROADCAST    (1 << 30)
#define AMDBC250_GRBM_GFX_INDEX_SE_BROADCAST       (1 << 31)

/* GRBM_GFX_INDEX broadcast reset value (Linux DEFAULT_GRBM_GFX_INDEX = 0xE0000000) */
#define AMDBC250_GRBM_GFX_INDEX_BROADCAST_VAL \
    (AMDBC250_GRBM_GFX_INDEX_SE_BROADCAST | \
     AMDBC250_GRBM_GFX_INDEX_QUEUE_BROADCAST | \
     AMDBC250_GRBM_GFX_INDEX_PIPE_BROADCAST | \
     AMDBC250_GRBM_GFX_INDEX_INSTANCE_BROADCAST)

/* KIQ select: ME=1, PIPE=0, QUEUE=0.
 * NOTE: No broadcast flags! PSP driver confirmed KIQ registers only
 * respond to plain ME=1 (0x00010000), not with SE_BROADCAST or INSTANCE_BROADCAST. */
#define AMDBC250_GRBM_GFX_INDEX_KIQ_VAL \
    (1 << AMDBC250_GRBM_GFX_INDEX_MEID_SHIFT)

/* GFX queue select: ME=0, PIPE=0, QUEUE=0 */
#define AMDBC250_GRBM_GFX_INDEX_GFX_VAL  0

/* --- RLC / Scheduler (Sienna_Cichlid override: mm=0x4CA1, BASE_IDX=1) --- */
/* From Linux gfx_v10_0.c: #define mmRLC_CP_SCHEDULERS_Sienna_Cichlid 0x4ca1 BASE_IDX=1
 * BAR5 = GC_BASE_SEG1(0xA000) + 0x4CA1 = 0xECA1 (theoretical Sienna_Cichlid offset)
 * EMPIRICALLY FOUND at 0xECAA returns 0x002000E4 (kiq-hqd-init.c).
 * Test tools successfully written 0xA0 at 0xECA8 (kiq-rlc-test.c).
 * Both 0xECA1, 0xECA8, and 0xECAA may be aliases for the same register.
 * 0xECA8 is used in test tools (kiq-rlc-test.c, ib-direct-test.c).
 * 0xECA1 is the canonical Linux definition (may not be aligned to 4 bytes).
 * Value format: bit7=enable, bits5:6=ME, bits3:4=pipe, bits0:2=queue */
#define AMDBC250_REG_RLC_CP_SCHEDULERS      (0x0000ECA8)  /* empirically confirmed writable */
#define AMDBC250_REG_RLC_CP_SCHEDULERS_LEGACY (AMDBC250_GC_BASE_SEG1 + 0x00004CA1)  /* 0xECA1 ??? Linux mmRLC_CP_SCHEDULERS, NOT 4-byte aligned, read-only */
#define AMDBC250_RLC_CP_SCHEDULERS_ENABLE   0x80
#define AMDBC250_RLC_CP_SCHEDULERS_ME_SHIFT 5
#define AMDBC250_RLC_CP_SCHEDULERS_PIPE_SHIFT 3
#define AMDBC250_RLC_CP_SCHEDULERS_KIQ_VAL  (AMDBC250_RLC_CP_SCHEDULERS_ENABLE | (1 << 5))

/* --- CP_MEC_CNTL (Linux mmCP_MEC_CNTL = 0x0e2d for Navi10 / GFX10.1) --- */
/* From Linux gc_10_1_0_offset.h: mmCP_MEC_CNTL = 0x0e2d (Navi10 / GFX10.1)
 * BC-250 is GFX10.1.3 (NOT Sienna_Cichlid / GFX10.3), so uses 0x0e2d.
 * BAR5 = GC_BASE(0x1260) + 0x0e2d*4 = 0x1260 + 0x38B4 = 0x4B14
 * Bit fields: MEC_ME1_HALT=bit28, MEC_ME2_HALT=bit29 */
#define AMDBC250_REG_CP_MEC_CNTL_GC         (AMDBC250_GC_BASE + 0x000038B4)  /* 0x4B14 */
#define AMDBC250_CP_MEC_ME1_HALT            (1 << 28)
#define AMDBC250_CP_MEC_ME2_HALT            (1 << 29)

/* --- GRBM Status (GC_BASE + 0x2000 = 0x3260, confirmed) --- */
#define AMDBC250_REG_GRBM_STATUS            (AMDBC250_GC_BASE + 0x00002000)  /* 0x3260 */
#define AMDBC250_REG_GRBM_STATUS2           (AMDBC250_GC_BASE + 0x0000200C)  /* 0x326C */
#define AMDBC250_REG_GRBM_SOFT_RESET        (AMDBC250_GC_BASE + 0x00002018)  /* 0x3278 */

/* GRBM_STATUS bit fields */
#define GRBM_STATUS__GUI_ACTIVE             (1 << 31)
#define GRBM_STATUS__ME_BUSY                (1 << 16)
#define GRBM_STATUS__PFP_BUSY               (1 << 15)
#define GRBM_STATUS__CE_BUSY                (1 << 17)
#define GRBM_STATUS__CP_COHERENCY_BUSY      (1 << 28)
#define GRBM_STATUS__CB_BUSY                (1 << 14)
#define GRBM_STATUS__DB_BUSY                (1 << 13)
#define GRBM_STATUS__TA_BUSY                (1 << 12)
#define GRBM_STATUS__GDS_BUSY               (1 << 11)
#define GRBM_STATUS__BCI_BUSY               (1 << 10)
#define GRBM_STATUS__IA_BUSY                (1 << 9)
#define GRBM_STATUS__WD_BUSY                (1 << 8)
#define GRBM_STATUS__RLC_BUSY               (1 << 27)

/* --- CC (Compute Cores) Registers --- */
#define AMDBC250_REG_CC_GC_SHADER_ARRAY_CONFIG  (AMDBC250_GC_BASE + 0x00002004)  /* 0x3264 */
#define AMDBC250_REG_CC_GC_SHADER_RATE_CONFIG   (AMDBC250_GC_BASE + 0x00002010)  /* 0x3270 */

/* --- SPI (Shader Processor Input) Registers --- */
#define AMDBC250_REG_SPI_PG_ENABLE_STATIC_WGP_MASK (AMDBC250_GC_BASE + 0x0000229C)  /* 0x34FC */
#define AMDBC250_REG_RLC_PG_ALWAYS_ON_WGP_MASK  (AMDBC250_GC_BASE + 0x00002B04)     /* 0x3D64 */

/* --- KIQ (Kernel Interface Queue, BC-250: shift by GC_BASE=0x1260) --- */
/* KIQ_BASE_LO at 0xE060 is WRITABLE ??? only writable BASE register found on BC-250.
 * KIQ_CNTL at 0xE068 is READ-ONLY (writes silently ignored, reads 0).
 * KIQ_RPTR/WPTR at 0xE06C/0xE078 are WRITABLE.
 * KIQ_VMID at 0xE07C is writable.
 * KIQ_ACTIVE at 0xE080 is writable.
 * Native NBIO offsets (0xCE00+) are all read-only. */
#define AMDBC250_REG_CP_KIQ_BASE_LO      (AMDBC250_GC_BASE + 0x0000CE00)  /* 0xE060, WRITABLE */
#define AMDBC250_REG_CP_KIQ_BASE_HI      (AMDBC250_GC_BASE + 0x0000CE04)  /* 0xE064 */
#define AMDBC250_REG_CP_KIQ_CNTL         (AMDBC250_GC_BASE + 0x0000CE08)  /* 0xE068, READONLY=0 */
#define AMDBC250_REG_CP_KIQ_RPTR         (AMDBC250_GC_BASE + 0x0000CE0C)  /* 0xE06C, WRITABLE */
#define AMDBC250_REG_CP_KIQ_PQ_CTL       (AMDBC250_GC_BASE + 0x0000CE10)  /* 0xE070, READONLY=0x81818181 */
#define AMDBC250_REG_CP_KIQ_DOORBELL     (AMDBC250_GC_BASE + 0x0000CE14)  /* 0xE074, WRITABLE */
#define AMDBC250_REG_CP_KIQ_WPTR         (AMDBC250_GC_BASE + 0x0000CE18)  /* 0xE078, WRITABLE */
#define AMDBC250_REG_CP_KIQ_VMID         (AMDBC250_GC_BASE + 0x0000CE1C)  /* 0xE07C, WRITABLE */
#define AMDBC250_REG_CP_KIQ_ACTIVE       (AMDBC250_GC_BASE + 0x0000CE20)  /* 0xE080, WRITABLE */

/* --- Interrupt Handler (IH) ??? GFX10 style --- */
#define AMDBC250_REG_IH_RB_BASE_LO          0x00003800  /* IH ring base low  */
#define AMDBC250_REG_IH_RB_BASE_HI          0x00003804  /* IH ring base high */
#define AMDBC250_REG_IH_RB_CNTL             0x00003808  /* IH ring control   */
#define AMDBC250_REG_IH_RB_RPTR             0x00003810  /* IH read pointer   */
#define AMDBC250_REG_IH_RB_WPTR             0x00003814  /* IH write pointer  */
#define AMDBC250_REG_IH_RB_WPTR_POLL_CNTL   0x00003818  /* WPTR poll control */
#define AMDBC250_REG_IH_CNTL                0x00003820  /* IH control        */

/* --- Memory Controller (MC) ??? GFX10 --- */
#define AMDBC250_REG_MC_VM_FB_OFFSET        0x00000000  /* FB offset         */
#define AMDBC250_REG_MC_VM_FB_LOCATION_BASE 0x00000520  /* FB location base  */
#define AMDBC250_REG_MC_VM_FB_LOCATION_TOP  0x00000524  /* FB location top   */
#define AMDBC250_REG_MC_VM_AGP_BASE         0x00000528  /* AGP base          */
#define AMDBC250_REG_MC_VM_AGP_TOP          0x0000052C  /* AGP top           */
#define AMDBC250_REG_MC_VM_AGP_BOT          0x00000530  /* AGP bottom        */
#define AMDBC250_REG_MC_VM_AGP_CNTL         0x00000534  /* AGP control       */
#define AMDBC250_REG_MC_VM_SYSTEM_APERTURE_LOW_ADDR  0x00000540
#define AMDBC250_REG_MC_VM_SYSTEM_APERTURE_HIGH_ADDR  0x00000544
#define AMDBC250_REG_MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR 0x00000548

/* --- GCVM (GFX Hub GPU Virtual Memory) ??? GC_BASE-shifted --- */
/* Formula: BAR5_offset = GC_BASE(0x1260) + Linux_DWORD_offset * 4 */
/* CRITICAL: These are GFX Hub registers, NOT MMHUB registers! */
/* The MMHUB VM block at 0x1B400-0x1B600 is DEAD on BC-250. */
#define AMDBC250_REG_GCVM_L2_CNTL                       0x00000B360
#define AMDBC250_REG_GCVM_L2_CNTL2                      0x00000B364
#define AMDBC250_REG_GCVM_L2_CNTL3                      0x00000B368
#define AMDBC250_REG_GCVM_L2_CNTL4                      0x00000B36C

#define AMDBC250_REG_GCVM_CONTEXT0_CNTL                 0x00000B460
#define AMDBC250_REG_GCVM_CONTEXT0_PT_BASE_LO           0x000006C8C  /* Linux offset, verified WRITABLE */
#define AMDBC250_REG_GCVM_CONTEXT0_PT_BASE_HI           0x000006C90  /* Linux offset, verified WRITABLE */

/* NOTE: 0x0B608/0x0B60C are NOT PT_BASE (hardware-locked, reads 0). Correct PT_BASE is at 0x6C8C/0x6C90. */

/* TLB entries (Context0 page table ??? WRITABLE, format unknown) */
#define AMDBC250_REG_GCVM_CTX0_TLB_ENTRY_0              0x00000B408
#define AMDBC250_REG_GCVM_CTX0_TLB_ENTRY_19             0x00000B454

/* TLB configuration (WRITABLE, format unknown) */
#define AMDBC250_REG_GCVM_CTX0_CFG_0                    0x00000B4C0
#define AMDBC250_REG_GCVM_CTX0_CFG_5                    0x00000B4D4

/* GCVM Invalidate ??? verified working offsets:
 * REQ at 0x6C0C, ACK at 0x6C10.
 * Protocol: write 1 to ACK (clear), write 1 to REQ (request), poll ACK bit 0.
 * NOTE: hw.h previously had 0x0B51C/0x0B520 which are WRONG (dead registers). */
#define AMDBC250_REG_GCVM_INVALIDATE_ENG0_REQ            0x000006C0C
#define AMDBC250_REG_GCVM_INVALIDATE_ENG0_ACK            0x000006C10

/* --- MMHUB VM (Memory Hub) ??? WRONG on BC-250, DO NOT USE --- */
/* These are MMHUB MMEA registers (memory controller), NOT VM registers */
/* MMHUB VM block at 0x1B400-0x1B600 is DEAD (0xFFFFFFFF / 0x0) */
/* Kept for reference only ??? do not use in active code */
#if 0
#define AMDBC250_REG_MMHUB_VM_CONTEXT0_CNTL              0x00001A00  /* MMEA, NOT VM */
#define AMDBC250_REG_MMHUB_VM_PT_BASE_LO                 0x00001A04  /* MMEA, NOT VM */
#endif

/* --- HDP (Host Data Path) ??? CRITICAL for coherency --- */
#define AMDBC250_REG_HDP_MEM_COHERENCY_FLUSH_CNTL   0x000012A0  /* FLUSH!    */
#define AMDBC250_REG_HDP_DEBUG0                     0x000012B0  /* Invalidate */
#define AMDBC250_REG_HDP_NONSURFACE_INFO            0x000012C0
#define AMDBC250_REG_HDP_NONSURFACE_SIZE            0x000012C4
#define AMDBC250_REG_HDP_NONSURFACE_BASE            0x000012C8

/* --- Display Controller (DCN 2.1 for GFX10 / Navi) --- */
#define AMDBC250_REG_HUBPREQ0_DCSURF_PRIMARY_SURFACE_ADDRESS  0x00005080
#define AMDBC250_REG_HUBPREQ0_DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH 0x00005084
#define AMDBC250_REG_HUBPREQ0_DCSURF_FLIP_CONTROL     0x00005088
#define AMDBC250_REG_HUBPREQ0_DCSURF_SURFACE_PITCH     0x0000508C
#define AMDBC250_REG_HUBPREQ0_DCSURF_SURFACE_DIMENSIONS  0x00005090
#define AMDBC250_REG_HUBP0_DCSURF_SURFACE_CONFIG        0x00005094
#define AMDBC250_REG_HUBPREQ0_DCSURF_TILING_CONFIG    0x00005098
#define AMDBC250_REG_HUBPREQ0_DCSURF_PRI_VIEWPORT_START  0x0000509C
#define AMDBC250_REG_HUBPREQ0_DCSURF_PRI_VIEWPORT_DIMENSION 0x000050A0

/* --- OTG (Output Timing Generator) ??? DCN 2.1 --- */
#define AMDBC250_REG_OTG0_OTG_CONTROL                 0x00006000  /* OTG ctrl */
#define AMDBC250_REG_OTG0_OTG_INTERLACE_CONTROL       0x00006004
#define AMDBC250_REG_OTG0_OTG_CRTC_V_TOTAL            0x00006010  /* V total  */
#define AMDBC250_REG_OTG0_OTG_CRTC_H_TOTAL            0x00006014  /* H total  */
#define AMDBC250_REG_OTG0_OTG_CRTC_V_BLANK_START_END  0x00006018  /* V blank  */
#define AMDBC250_REG_OTG0_OTG_CRTC_H_BLANK_START_END  0x0000601C  /* H blank  */
#define AMDBC250_REG_OTG0_OTG_CRTC_V_SYNC_START_END   0x00006020  /* V sync   */
#define AMDBC250_REG_OTG0_OTG_CRTC_H_SYNC_START_END   0x00006024  /* H sync   */
#define AMDBC250_REG_OTG0_OTG_CRTC_STATUS             0x00006028  /* Status   */

/* --- DMCUB (Display Microcontroller Unit) ??? GFX10 uses DMCUB --- */
#define AMDBC250_REG_DMCUB_SCRATCH0               0x00007000
#define AMDBC250_REG_DMCUB_SCRATCH1               0x00007004
#define AMDBC250_REG_DMCUB_INBOX0_RPTR            0x00007010
#define AMDBC250_REG_DMCUB_INBOX0_WPTR            0x00007014
#define AMDBC250_REG_DMCUB_INBOX0_BASE_ADDR       0x00007018
#define AMDBC250_REG_DMCUB_INBOX0_SIZE            0x0000701C

/* --- SMU (System Management Unit) ??? GFX10 power management ---
 *
 * CORRECTED offsets for BC-250 (Cyan Skillfish):
 * MP1_BASE__INST0_SEG0 = 0x16000 (byte offset in BAR5)
 * Each C2PMSG register: BAR5 = MP1_BASE + mmMP1_SMN_C2PMSG_n * 4
 * mm register values from mp_11_0_8_offset.h
 *
 * Linux equivalent: SOC15_REG_OFFSET(MP1, 0, mmMP1_SMN_C2PMSG_n)
 */
#define AMDBC250_REG_MP1_SMN_C2PMSG_33            0x00016984  /* C2P msg 33  (0x16000 + 0x0261*4) */
#define AMDBC250_REG_MP1_SMN_C2PMSG_66            0x00016A08  /* C2P msg 66  ??? message ID (0x16000 + 0x0282*4) */
#define AMDBC250_REG_MP1_SMN_C2PMSG_82            0x00016A48  /* C2P msg 82  ??? argument (0x16000 + 0x0292*4) */
#define AMDBC250_REG_MP1_SMN_C2PMSG_83            0x00016A4C  /* C2P msg 83 (0x16000 + 0x0293*4) */
#define AMDBC250_REG_MP1_SMN_C2PMSG_90            0x00016A68  /* C2P msg 90  ??? response status (0x16000 + 0x029A*4) */

/* --- Thermal Sensor ??? GFX10 ---
 *
 * BC-250 verified offsets:
 * THM_BASE = 0x8000 (confirmed via write-back test ??? register at 0x8000 is writable)
 * Linux thm_11_0_2_offset.h suggests 0x16600 but this is WRONG on BC-250 P4.00G BIOS.
 * Hardware test: 0x8000 returns 0x18 (writable), 0x8008 returns temperature.
 */
#define AMDBC250_REG_THM_THERMAL_CTRL             0x00008000  /* THM control (writable, verified) */
#define AMDBC250_REG_THM_CURRENT_TEMP             0x00008008  /* THM current temp (read-only) */
#define AMDBC250_REG_THM_THERMAL_INT_ENA          0x00008050  /* THM interrupt enable (separate from CTRL, Linux offset 0x14*4) */

/* --- GB (Graphics Backend) Address Config ??? GFX10 --- */
#define AMDBC250_REG_GB_ADDR_CONFIG               0x00009800  /* Addr config   */
#define AMDBC250_REG_GB_ADDR_CONFIG_READ          0x00009804  /* Addr config r */

/*===========================================================================
  Register Bit Fields ??? GFX10 (RDNA2 / Cyan Skillfish)
===========================================================================*/

/* CP_ME_CNTL bits (GFX10) */
#define CP_ME_CNTL__ME_HALT                       (1 << 28)
#define CP_ME_CNTL__PFP_HALT                      (1 << 30)
#define CP_ME_CNTL__CE_HALT                       (1 << 29)

/* CP_GFX_RING0_CNTL bits */
#define CP_RING0_CNTL__RB_BUFSZ_MASK              0x000000FF
#define CP_RING0_CNTL__RB_BLKSZ_MASK              0x0000FF00
#define CP_RING0_CNTL__RB_BLKSZ_SHIFT             8
#define CP_RING0_CNTL__RPTR_WRITEBACK_ENABLE      (1 << 22)

/* IH_CNTL bits */
#define IH_CNTL__ENABLE_INTR                      (1 << 0)
#define IH_CNTL__RPTR_REARM                       (1 << 1)

/* HDP coherency ??? CRITICAL! */
#define HDP_MEM_COHERENCY_FLUSH_CNTL__FLUSH_CACHE (1 << 0)
#define HDP_DEBUG0__INVALIDATE_CACHE              (1 << 0)

/* OTG_CONTROL bits */
#define OTG_CNTL__ENABLE                          (1 << 0)
#define OTG_CNTL__CRTC_DISP_READ_REQUEST_DISABLE  (1 << 24)

/*===========================================================================
  PM4 Command Packet Format ??? GFX10 (RDNA2)
  
  Based on: GFX10 PM4 Programming Reference
  (Reverse-engineered by open-source community from Mesa/AMDGPU)
===========================================================================*/

/* PM4 packet types */
#define PM4_TYPE_0                                0       /* Type 0: reg write */
#define PM4_TYPE_2                                2       /* Type 2: NOP/pad   */
#define PM4_TYPE_3                                3       /* Type 3: executive */

/* PM4 Type 0: Write consecutive registers */
#define PM4_TYPE0_HDR(base_reg, count) \
    (((count - 1) << 16) | ((base_reg) >> 2))

/* PM4 Type 2: NOP (padding) */
#define PM4_TYPE2_NOP                             0x80000000

/* PM4 Type 3: Executive commands (GFX10 opcodes) */
#define PM4_TYPE3_HDR(opcode, count) \
    ((3 << 30) | (((count) - 1) << 16) | ((opcode) << 8))

/* GFX10 PM4 opcodes */
#define IT_NOP                                    0x10    /* No-operation         */
#define IT_DRAW_INDEX_AUTO                        0x2D    /* Draw auto (no index) */
#define IT_DRAW_INDEX_2                           0x27    /* Draw indexed         */
#define IT_DRAW_INDIRECT                          0x28    /* Draw indirect        */
#define IT_DRAW_INDIRECT_MULTI                    0x2C    /* Draw indirect multi  */
#define IT_DISPATCH_DIRECT                        0x15    /* Compute dispatch     */
#define IT_DISPATCH_INDIRECT                      0x16    /* Compute dispatch ind */
#define IT_INDIRECT_BUFFER                        0x3F    /* Indirect buffer      */
#define IT_EVENT_WRITE                            0x46    /* Event write          */
#define IT_EVENT_WRITE_EOP                        0x47    /* Event @ end-of-pipe  */
#define IT_EVENT_WRITE_EOS                        0x48    /* Event @ end-of-shader*/
#define IT_RELEASE_MEM                            0x49    /* Release memory       */
#define IT_PFP_SYNC_ME                            0x42    /* PFP sync ME          */
#define IT_SURFACE_SYNC                           0x43    /* Surface cache sync   */
#define IT_WAIT_REG_MEM                           0x3C    /* Wait reg/mem value   */
#define IT_WRITE_DATA                             0x37    /* Write data           */
#define IT_COPY_DATA                              0x40    /* Copy data            */
#define IT_SET_CONFIG_REG                         0x68    /* Set config reg       */
#define IT_SET_CONTEXT_REG                        0x69    /* Set context reg      */
#define IT_SET_SH_REG                             0x76    /* Set SH register      */
#define IT_SET_UCONFIG_REG                        0x77    /* Set UCONFIG reg      */

/* Event types for IT_EVENT_WRITE */
#define EVENT_TYPE_PIXEL_PIPE_SYNC              0x08
#define EVENT_TYPE_CACHE_FLUSH                  0x09
#define EVENT_TYPE_FLUSH_AND_INV_CB             0x0E
#define EVENT_TYPE_FLUSH_AND_INV_DB             0x0F
#define EVENT_TYPE_CS_PARTIAL_FLUSH             0x40
#define EVENT_TYPE_VS_PARTIAL_FLUSH             0x44
#define EVENT_TYPE_PS_PARTIAL_FLUSH             0x45
#define EVENT_TYPE_BOTTOM_OF_PIPE               0x3A
#define EVENT_TYPE_EOP                          0x46

/* Release_mem packet fields */
#define RELEASE_MEM__EVENT_TYPE__RELEASE_MEM        0x20
#define RELEASE_MEM__DEST_SEL__MEM              0x02
#define RELEASE_MEM__INT_SEL__SEND_DATA_ONLY    0x02
#define RELEASE_MEM__DATA_SEL__DATA_64          0x03

/*===========================================================================
  Interrupt Handler Constants ??? GFX10
===========================================================================*/

/* IH client IDs (GFX10) */
#define IH_CLIENTID_GFX                           0x09    /* Graphics engine   */
#define IH_CLIENTID_SDMA                          0x0D    /* System DMA        */
#define IH_CLIENTID_IH                            0x01    /* IH itself         */
#define IH_CLIENTID_VMC                           0x0B    /* Virtual memory    */
#define IH_CLIENTID_DCE                           0x08    /* Display controller */
#define IH_CLIENTID_OSS                           0x0A    /* OSS (System Mgmt) */

/* IH ring entry size: 4 DWORDs (16 bytes) */
#define IH_ENTRY_SIZE_BYTES                       16
#define IH_RING_SIZE_BYTES                        (256 * 1024)  /* 256 KB ring */

/*===========================================================================
  SDMA (System DMA) Engine ??? GFX10
===========================================================================*/

#define AMDBC250_REG_SDMA0_GFX_RB_BASE_LO         0x0000E000
#define AMDBC250_REG_SDMA0_GFX_RB_BASE_HI         0x0000E004
#define AMDBC250_REG_SDMA0_GFX_RB_CNTL           0x0000E008
#define AMDBC250_REG_SDMA0_GFX_RB_RPTR           0x0000E00C
#define AMDBC250_REG_SDMA0_GFX_RB_WPTR           0x0000E010
#define AMDBC250_REG_SDMA0_GFX_RB_WPTR_POLL      0x0000E014
#define AMDBC250_REG_SDMA0_CNTL                  0x0000E018

/* SDMA opcodes (GFX10) */
#define SDMA_OP_NOP                               0x00
#define SDMA_OP_COPY_LINEAR                       0x01
#define SDMA_OP_COPY_TILED                        0x02
#define SDMA_OP_FILL                              0x03
#define SDMA_OP_FENCE                             0x04
#define SDMA_OP_TRAP                              0x05
#define SDMA_OP_POLL_REGMEM                       0x06
#define SDMA_OP_CONST_WRITE                       0x07

/*===========================================================================
  Ray Tracing Accelerator ??? GFX1013
  
  BC-250 has dedicated RT cores (early generation).
  Performance is poor compared to RDNA3 RT.
===========================================================================*/

#define AMDBC250_REG_RT_ACCEL_CNTL               0x0000D000
#define AMDBC250_REG_RT_ACCEL_STATUS             0x0000D004
#define AMDBC250_REG_RT_BVH_ADDR_LO              0x0000D008
#define AMDBC250_REG_RT_BVH_ADDR_HI              0x0000D00C
#define AMDBC250_REG_RT_RAY_ADDR_LO              0x0000D010
#define AMDBC250_REG_RT_RAY_ADDR_HI              0x0000D014

/* RT packet opcodes (GFX10.1.3 specific) */
#define IT_TRACE_RAY                              0x5D    /* Trace ray (RT)    */
#define IT_INTERSECT_BBOX                         0x5E    /* Intersect AABB    */
#define IT_INTERSECT_TRIANGLE                     0x5F    /* Intersect triangle*/

/*===========================================================================
  Memory Alignment Requirements ??? GFX10
===========================================================================*/

#define AMDBC250_RING_ALIGNMENT                   4096    /* 4 KB              */
#define AMDBC250_FENCE_ALIGNMENT                  256     /* 256-byte          */
#define AMDBC250_PAGE_TABLE_ALIGNMENT             65536   /* 64 KB (GFX10)     */
#define AMDBC250_COMMAND_BUFFER_ALIGNMENT         256     /* 256-byte          */
#define AMDBC250_TEXTURE_ALIGNMENT                256     /* 256-byte          */

/*===========================================================================
  Timeout Values (microseconds)
===========================================================================*/

#define AMDBC250_INIT_TIMEOUT_US                  500000  /* 500ms init        */
#define AMDBC250_CP_TIMEOUT_US                    100000  /* 100ms CP          */
#define AMDBC250_FENCE_TIMEOUT_US                 5000000 /* 5s fence          */
#define AMDBC250_SMU_TIMEOUT_US                   100000  /* 100ms SMU         */
#define AMDBC250_DISPLAY_TIMEOUT_US               100000  /* 100ms display     */

/*===========================================================================
  GART (Graphics Aperture Remapping Table)
===========================================================================*/

#define AMDBC250_GART_NUM_ENTRIES                  16384   /* 16K entries       */
#define AMDBC250_GART_ENTRY_SIZE                   8       /* 8 bytes per entry */
#define AMDBC250_GART_APERTURE_BASE                0x0000000100000000ULL /* 1TB  */

/*===========================================================================
  GPU Virtual Memory Constants
===========================================================================*/

#define AMDBC250_MAX_VMIDS                        16      /* VMID 0-15         */
#define AMDBC250_MAX_VM_CONTEXTS                  16      /* Max VM contexts   */

/* PTE flags ??? MUST match Linux amdgpu_vm.h GFX10 format */
#define AMDBC250_PTE_VALID                         (1ULL << 0)
#define AMDBC250_PTE_SYSTEM                        (1ULL << 1)
#define AMDBC250_PTE_SNOOP                         (1ULL << 2)
#define AMDBC250_PTE_READABLE                      (1ULL << 5)
#define AMDBC250_PTE_WRITABLE                      (1ULL << 6)
#define AMDBC250_PTE_EXECUTABLE                    (1ULL << 3)  /* non-standard */
#define AMDBC250_PTE_VRAM                          (1ULL << 7)  /* non-standard, moved from bit6 */

/* VM access flags */
#define AMDBC250_VM_READ                           0x1
#define AMDBC250_VM_WRITE                          0x2
#define AMDBC250_VM_EXECUTE                        0x4
#define AMDBC250_VM_SYSTEM                         0x8
#define AMDBC250_VM_SNOOP                          0x10

/*===========================================================================
  GPU Virtual Memory (GFX10 supports 4-level page tables)
===========================================================================*/

#define AMDBC250_VM_LEVELS                        4       /* 4-level (GFX10)   */
#define AMDBC250_VM_BLOCK_SIZE                    9       /* 9-bit blocks      */
#define AMDBC250_VM_PAGE_SIZE                     4096    /* 4 KB pages        */
#define AMDBC250_VM_MAX_ADDRESS                   0x7FFFFFFF000ULL /* 128 TB   */

/* VM context IDs */
#define AMDBC250_VMID_SYSTEM                      0       /* System/Kernel     */
#define AMDBC250_VMID_MIN_USER                    1       /* Min user VMID     */
#define AMDBC250_VMID_MAX_USER                    15      /* Max VMID          */

/*===========================================================================
  Display Configuration ??? DCN 2.1
===========================================================================*/

#define AMDBC250_NUM_DISPLAY_PIPES                4       /* DCN 2.1: 4 pipes  */
#define AMDBC250_MAX_CRTCS                        4       /* 4 CRTCs           */
#define AMDBC250_MAX_DISPLAY_WIDTH                7680    /* 8K max            */
#define AMDBC250_MAX_DISPLAY_HEIGHT               4320    /* 8K max            */
#define AMDBC250_MAX_PIXEL_CLOCK_KHZ              1200000 /* 1.2 GHz max       */

/* Supported output types */
#define AMDBC250_OUTPUT_DISPLAYPORT               (1 << 0)  /* DP 1.4          */
#define AMDBC250_OUTPUT_HDMI                      (1 << 1)  /* HDMI 2.1        */
#define AMDBC250_OUTPUT_DVI                       (1 << 2)  /* DVI-D           */
#define AMDBC250_OUTPUT_VGA                       (1 << 3)  /* VGA (via DAC)   */

/*===========================================================================
  Known Hardware Quirks (from Linux driver & community)
===========================================================================*/

/* BC-250 specific workarounds */
#define AMDBC250_QUIRK_BROKEN_COMPUTE_QUEUE       TRUE    /* HW flaw, disable  */
#define AMDBC250_QUIRK_NEEDS_NOHIZ                TRUE    /* Fixes Z-buffer    */
#define AMDBC250_QUIRK_VRAM_BIOS_CONFIGURABLE     TRUE    /* VRAM split in BIOS */
#define AMDBC250_QUIRK_VCN_FIRMWARE_BLOCKED       TRUE    /* Sony blocks VCN   */
#define AMDBC250_QUIRK_STATIC_CLOCK_WITHOUT_GOV   1500    /* MHz w/o governor  */

/* Golden register sequences (MUST be programmed at init) */
/* These are hardware workarounds/errata from AMD */
#define AMDBC250_HAS_GOLDEN_REGS                  TRUE

/*===========================================================================
  Firmware Loading Functions (amdbc250_dream_fw_load.c)
  
  BC-250 uses DIRECT firmware loading (AMDGPU_FW_LOAD_DIRECT).
  Firmware is uploaded to IP block registers via MMIO, not through PSP.
===========================================================================*/

/* Load all CP firmware during initialization */
NTSTATUS
DreamV3LoadAllFirmware(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    );

/* Load a single firmware blob (called from LOAD_CP_FW IOCTL) */
NTSTATUS
DreamV3LoadSingleFirmware(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt,
    _In_ UINT32 FwType,
    _In_ const UINT8 *FwBlob,
    _In_ UINT32 FwSize
    );

/* Halt/unhalt all CP engines */
VOID
DreamV3HaltAllEngines(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    );

VOID
DreamV3UnhaltAllEngines(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    );

/* Golden register programming (amdbc250_dream_golden.c) */
NTSTATUS
DreamV3ProgramGoldenSettings(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    );

/* HDP register initialization */
NTSTATUS
DreamV3InitHdpRegisters(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    );

/* RLC initialization */
NTSTATUS
DreamV3InitRlc(
    _In_ PDREAM_V3_DEVICE_EXTENSION DevExt
    );

#endif /* _AMDBC250_DREAM_V3_HW_H_ */
