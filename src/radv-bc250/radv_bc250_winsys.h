/*
 * Copyright © 2026 AMD BC-250 "Dream Drivers" Project
 *
 * SPDX-License-Identifier: MIT
 *
 * BC-250 winsys backend for RADV.
 *
 * Implements the RADV radeon_winsys interface (radv_radeon_winsys.h)
 * on top of the BC-250 KMD IOCTL surface (\\.\AMDBC250DreamV43)
 * instead of Linux DRM/amdgpu.
 *
 * IOCTL mapping:
 *   buffer_create  -> IOCTL_AMDBC250_ALLOC_VIDMEM  (0x80000840)
 *   buffer_destroy -> IOCTL_AMDBC250_FREE_VIDMEM   (0x80000844)
 *   buffer_map     -> IOCTL_AMDBC250_MAP_VIDMEM    (0x80000848)
 *   buffer_unmap   -> IOCTL_AMDBC250_UNMAP_VIDMEM  (0x8000084C)
 *   cs_submit      -> IOCTL_AMDBC250_SUBMIT_COMMANDS (0x80000880)
 *   fence wait     -> IOCTL_AMDBC250_WAIT_FENCE    (0x80000884)
 *   fence signal   -> IOCTL_AMDBC250_SIGNAL_FENCE  (0x80000888)
 *   read_regs      -> IOCTL_AMDBC250_READ_REG      (0x80000B88)
 *   telemetry      -> IOCTL_AMDBC250_GET_SMU_TELEMETRY (0x80000B9C)
 *
 * NOTE: BC-250 WGPs are SOS-gated on Windows (SPI_PG 0x5C3C = 0).
 * Submissions return VK_SUCCESS but the GPU does not execute
 * shader work until WGPs are unlocked (EFI route only).
 * This backend is complete and correct; execution is gated
 * by hardware state, not by this code.
 */

#ifndef RADV_BC250_WINSYS_H
#define RADV_BC250_WINSYS_H

#ifdef _WIN32
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>
#else
#include <stdint.h>
#include <stdbool.h>
typedef void *HANDLE;
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#endif

/* Forward declarations from RADV (radv_radeon_winsys.h). */
struct radeon_winsys;
struct radeon_winsys_bo;
struct radeon_winsys_ctx;
struct radeon_info;

#ifdef RADV_BC250_STANDALONE
/* Full local definitions so standalone builds (without Mesa headers)
 * can type-check the backend. Function-pointer signatures mirror
 * radv_radeon_winsys.h but use simplified parameter types. */
struct radeon_winsys_bo {
   uint64_t va;
   uint64_t size;
   bool is_local;
   bool vram_no_cpu_access;
   bool use_global_list;
   bool gfx12_allow_dcc;
   bool is_virtual;
   int initial_domain;
   uint64_t obj_id;
};
struct radeon_bo_metadata { uint32_t size_metadata; uint32_t metadata[64]; };
struct ac_cmdbuf { uint32_t *buf; unsigned cdw; unsigned max_dw; struct radeon_winsys_bo *bo; };
struct radv_winsys_submit_info { int ip_type; int queue_index; unsigned cs_count; struct ac_cmdbuf **cs_array; };
struct vk_sync_wait { void *p; };
struct vk_sync_signal { int dummy; };
struct radeon_winsys_ctx { void *p; };
struct radeon_info { int dummy; };
typedef int VkResult;
#define VK_SUCCESS 0
#define VK_ERROR_OUT_OF_HOST_MEMORY -1
#define VK_ERROR_OUT_OF_DEVICE_MEMORY -2
#define VK_ERROR_DEVICE_LOST -3
#define VK_ERROR_INITIALIZATION_FAILED -4
enum radeon_bo_domain { RADEON_DOMAIN_GTT = 2, RADEON_DOMAIN_VRAM = 4, RADEON_DOMAIN_VRAM_GTT = 6 };
enum radeon_bo_flag { RADEON_FLAG_CPU_ACCESS = 2, RADEON_FLAG_NO_CPU_ACCESS = 4, RADEON_FLAG_VIRTUAL = 8 };
struct radeon_winsys {
   void (*destroy)(struct radeon_winsys *ws);
   uint64_t (*query_value)(struct radeon_winsys *ws, int value);
   bool (*read_registers)(struct radeon_winsys *ws, unsigned reg_offset, unsigned num_registers, uint32_t *out);
   VkResult (*buffer_create)(struct radeon_winsys *ws, uint64_t size, unsigned alignment, enum radeon_bo_domain domain, enum radeon_bo_flag flags, unsigned priority, uint64_t address, struct radeon_winsys_bo **out_bo);
   void (*buffer_destroy)(struct radeon_winsys *ws, struct radeon_winsys_bo *bo);
   void *(*buffer_map)(struct radeon_winsys *ws, struct radeon_winsys_bo *bo, bool use_fixed_addr, void *fixed_addr);
   void (*buffer_unmap)(struct radeon_winsys *ws, struct radeon_winsys_bo *bo, bool replace);
   void (*buffer_set_metadata)(struct radeon_winsys *ws, struct radeon_winsys_bo *bo, struct radeon_bo_metadata *md);
   void (*buffer_get_metadata)(struct radeon_winsys *ws, struct radeon_winsys_bo *bo, struct radeon_bo_metadata *md);
   VkResult (*buffer_make_resident)(struct radeon_winsys *ws, struct radeon_winsys_bo *bo, bool resident);
   bool (*bo_wait_for_idle)(struct radeon_winsys *ws, struct radeon_winsys_bo *bo);
   struct ac_cmdbuf *(*cs_create)(struct radeon_winsys *ws, int ip_type, bool is_secondary);
   void (*cs_destroy)(struct ac_cmdbuf *cs);
   void (*cs_reset)(struct ac_cmdbuf *cs);
   void (*cs_grow)(struct ac_cmdbuf *cs, size_t min_size);
   VkResult (*cs_finalize)(struct ac_cmdbuf *cs);
   VkResult (*cs_submit)(struct radeon_winsys_ctx *ctx, const struct radv_winsys_submit_info *submit, uint32_t wait_count, const struct vk_sync_wait *waits, uint32_t signal_count, const struct vk_sync_signal *signals);
   void (*cs_add_buffer)(struct ac_cmdbuf *cs, struct radeon_winsys_bo *bo);
   void (*cs_execute_ib)(struct ac_cmdbuf *cs, struct radeon_winsys_bo *bo, const uint64_t va, const uint32_t cdw, const bool predicate);
   VkResult (*ctx_create)(struct radeon_winsys *ws, int priority, struct radeon_winsys_ctx **out_ctx);
   void (*ctx_destroy)(struct radeon_winsys_ctx *ctx);
   bool (*ctx_wait_idle)(struct radeon_winsys_ctx *ctx, int ip_type, int ring_index);
};
#endif

/* BC-250 KMD device path. */
#define BC250_KMD_DEVICE_PATH "\\\\.\\AMDBC250DreamV43"

/* KMD IOCTL codes (from inc/amdbc250_ioctl.h, CTL_CODE_AMDBC250). */
#define BC250_IOCTL_ALLOC_VIDMEM     0x80000840u
#define BC250_IOCTL_FREE_VIDMEM      0x80000844u
#define BC250_IOCTL_MAP_VIDMEM       0x80000848u
#define BC250_IOCTL_UNMAP_VIDMEM     0x8000084cu
#define BC250_IOCTL_SUBMIT_COMMANDS  0x80000880u
#define BC250_IOCTL_WAIT_FENCE       0x80000884u
#define BC250_IOCTL_SIGNAL_FENCE     0x80000888u
#define BC250_IOCTL_READ_REG         0x80000B88u
#define BC250_IOCTL_GET_SMU_TELEM    0x80000B9cu
#define BC250_IOCTL_INIT_HARDWARE    0x80000B90u

/* ALLOC_VIDMEM (from inc/amdbc250_ioctl.h). */
struct bc250_alloc_vidmem_in {
   uint64_t Size;
   uint64_t Alignment;
   uint32_t Flags;
   uint32_t SegmentId; /* 0=VRAM, 1=System */
};

struct bc250_alloc_vidmem_out {
   uint64_t GpuVirtualAddress;
   uint64_t PhysicalAddress;
   uint64_t Handle;
};

/* MAP_VIDMEM. */
struct bc250_map_vidmem_in {
   uint64_t Handle;
   uint64_t Offset;
   uint64_t Size;
};

struct bc250_map_vidmem_out {
   uint64_t CpuAddress;
   uint64_t PhysicalAddress;
};

/* SUBMIT_COMMANDS. */
struct bc250_submit_in {
   uint64_t DmaBufferGpuVa;
   uint64_t DmaBufferSize;
   uint32_t FenceValue;
   uint32_t QueueType; /* 0=GFX, 1=Compute, 2=SDMA */
};

/* WAIT_FENCE. */
struct bc250_wait_fence_in {
   uint32_t FenceValue;
   uint32_t TimeoutMs;
};

/* BC-250 winsys object. Replaces radv_amdgpu_winsys;
 * holds a KMD HANDLE instead of a DRM fd. */
struct radv_bc250_winsys {
   struct radeon_winsys base;

   HANDLE kmd;               /* Handle to \\.\AMDBC250DreamV43 */
   struct radeon_info *info; /* GPU info (filled at create) */

   /* Allocation tracking (mirrors alloc_tracker). */
   uint64_t allocated_vram;
   uint64_t allocated_gtt;

   /* Fence counter for submissions. */
   uint32_t next_fence_value;

   /* Init state. */
   bool hw_initialized;
};

/* BC-250 BO. Replaces radv_amdgpu_winsys_bo;
 * holds our KMD allocation Handle instead of GEM handles. */
struct radv_bc250_winsys_bo {
   struct radeon_winsys_bo base;
   uint64_t alloc_handle; /* KMD allocation handle */
   void *cpu_map;         /* CPU mapping (from MAP_VIDMEM) */
   uint32_t priority;
};

static inline struct radv_bc250_winsys *
radv_bc250_winsys(struct radeon_winsys *ws)
{
   return (struct radv_bc250_winsys *)ws;
}

static inline struct radv_bc250_winsys_bo *
radv_bc250_winsys_bo(struct radeon_winsys_bo *bo)
{
   return (struct radv_bc250_winsys_bo *)bo;
}

/* Entry points (mirrors radv_amdgpu_winsys_create / query_info). */
int radv_bc250_winsys_create(const struct radeon_info *info, struct radeon_winsys **winsys);
int radv_bc250_winsys_query_info(struct radeon_info *info);

void radv_bc250_bo_init_functions(struct radv_bc250_winsys *ws);
void radv_bc250_cs_init_functions(struct radv_bc250_winsys *ws);

#endif /* RADV_BC250_WINSYS_H */
