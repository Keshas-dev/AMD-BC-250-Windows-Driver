/*
 * Copyright © 2026 AMD BC-250 "Dream Drivers" Project
 *
 * SPDX-License-Identifier: MIT
 *
 * BC-250 buffer management for RADV.
 *
 * Replaces radv_amdgpu_bo.c (Linux DRM GEM) with KMD IOCTL calls:
 *   buffer_create  -> IOCTL_AMDBC250_ALLOC_VIDMEM (0x80000840)
 *   buffer_destroy -> IOCTL_AMDBC250_FREE_VIDMEM  (0x80000844)
 *   buffer_map     -> IOCTL_AMDBC250_MAP_VIDMEM   (0x80000848)
 *   buffer_unmap   -> IOCTL_AMDBC250_UNMAP_VIDMEM (0x8000084c)
 *
 * Domain mapping:
 *   RADEON_DOMAIN_VRAM     -> SegmentId 0
 *   RADEON_DOMAIN_GTT      -> SegmentId 1
 *   RADEON_DOMAIN_VRAM_GTT -> SegmentId 0 (prefer VRAM)
 */

#include "radv_bc250_winsys.h"

/* RADV headers (from Mesa src/amd/vulkan/). When building inside
 * Mesa, these resolve via include paths. Standalone builds must
 * add mesa src/include paths. */
#ifdef RADV_BC250_STANDALONE
/* Standalone struct definitions live in radv_bc250_winsys.h. */
#include <stdlib.h>
#include <string.h>
#else
#include "radv_radeon_winsys.h"
#include "util/u_memory.h"
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include <stdio.h>
#endif

static bool
bc250_ioctl(HANDLE kmd, uint32_t code, const void *in, uint32_t in_size, void *out, uint32_t out_size)
{
#ifdef _WIN32
   DWORD returned = 0;
   return DeviceIoControl(kmd, code, (LPVOID)in, in_size, out, out_size, &returned, NULL);
#else
   (void)kmd; (void)code; (void)in; (void)in_size; (void)out; (void)out_size;
   return false;
#endif
}

static VkResult
radv_bc250_bo_create(struct radeon_winsys *rws, uint64_t size, unsigned alignment,
                     enum radeon_bo_domain domain, enum radeon_bo_flag flags,
                     unsigned priority, uint64_t address, struct radeon_winsys_bo **out_bo)
{
   struct radv_bc250_winsys *ws = radv_bc250_winsys(rws);
   struct radv_bc250_winsys_bo *bo;
   struct bc250_alloc_vidmem_in in = {0};
   struct bc250_alloc_vidmem_out out = {0};

   (void)address; /* KMD assigns GPU VA */

   if (flags & RADEON_FLAG_VIRTUAL) {
      /* Sparse/virtual buffers: no backing store yet. Record the
       * request; real pages are bound via buffer_virtual_bind. */
      bo = calloc(1, sizeof(*bo));
      if (!bo)
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      bo->base.va = 0;
      bo->base.size = size;
      bo->base.is_virtual = true;
      bo->base.initial_domain = domain;
      bo->priority = priority;
      *out_bo = &bo->base;
      return VK_SUCCESS;
   }

   bo = calloc(1, sizeof(*bo));
   if (!bo)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   in.Size = size;
   in.Alignment = alignment ? alignment : 4096;
   in.Flags = 0;
   if (flags & RADEON_FLAG_CPU_ACCESS)
      in.Flags |= 0x1; /* CPU-visible hint */
   in.SegmentId = (domain & RADEON_DOMAIN_VRAM) ? 0 : 1;

   if (!bc250_ioctl(ws->kmd, BC250_IOCTL_ALLOC_VIDMEM, &in, sizeof(in), &out, sizeof(out))) {
      free(bo);
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;
   }

   if (!out.GpuVirtualAddress || !out.Handle) {
      free(bo);
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;
   }

   bo->base.va = out.GpuVirtualAddress;
   bo->base.size = size;
   bo->base.is_local = (in.SegmentId == 0);
   bo->base.initial_domain = domain;
   bo->alloc_handle = out.Handle;
   bo->priority = priority;

   if (in.SegmentId == 0)
      ws->allocated_vram += size;
   else
      ws->allocated_gtt += size;

   *out_bo = &bo->base;
   return VK_SUCCESS;
}

static void
radv_bc250_bo_destroy(struct radeon_winsys *rws, struct radeon_winsys_bo *bo)
{
   struct radv_bc250_winsys *ws = radv_bc250_winsys(rws);
   struct radv_bc250_winsys_bo *bbo = radv_bc250_winsys_bo(bo);
   uint64_t handle = bbo->alloc_handle;

   if (bbo->cpu_map) {
      /* Unmap first (KMD tracks mapping per handle). */
      bc250_ioctl(ws->kmd, BC250_IOCTL_UNMAP_VIDMEM, &handle, sizeof(handle), NULL, 0);
      bbo->cpu_map = NULL;
   }

   if (handle)
      bc250_ioctl(ws->kmd, BC250_IOCTL_FREE_VIDMEM, &handle, sizeof(handle), NULL, 0);

   if (bo->is_local)
      ws->allocated_vram -= bo->size;
   else
      ws->allocated_gtt -= bo->size;

   free(bbo);
}

static void *
radv_bc250_bo_map(struct radeon_winsys *rws, struct radeon_winsys_bo *bo, bool use_fixed_addr, void *fixed_addr)
{
   struct radv_bc250_winsys *ws = radv_bc250_winsys(rws);
   struct radv_bc250_winsys_bo *bbo = radv_bc250_winsys_bo(bo);
   struct bc250_map_vidmem_in in = {0};
   struct bc250_map_vidmem_out out = {0};

   (void)use_fixed_addr;
   (void)fixed_addr;

   if (bbo->cpu_map)
      return bbo->cpu_map;

   in.Handle = bbo->alloc_handle;
   in.Offset = 0;
   in.Size = bo->size;

   if (!bc250_ioctl(ws->kmd, BC250_IOCTL_MAP_VIDMEM, &in, sizeof(in), &out, sizeof(out)))
      return NULL;

   if (!out.CpuAddress)
      return NULL;

   bbo->cpu_map = (void *)(uintptr_t)out.CpuAddress;
   return bbo->cpu_map;
}

static void
radv_bc250_bo_unmap(struct radeon_winsys *rws, struct radeon_winsys_bo *bo, bool replace)
{
   struct radv_bc250_winsys *ws = radv_bc250_winsys(rws);
   struct radv_bc250_winsys_bo *bbo = radv_bc250_winsys_bo(bo);
   uint64_t handle = bbo->alloc_handle;

   (void)replace;

   if (!bbo->cpu_map)
      return;

   bc250_ioctl(ws->kmd, BC250_IOCTL_UNMAP_VIDMEM, &handle, sizeof(handle), NULL, 0);
   bbo->cpu_map = NULL;
}

static void
radv_bc250_bo_set_metadata(struct radeon_winsys *rws, struct radeon_winsys_bo *bo,
                            struct radeon_bo_metadata *md)
{
   (void)rws; (void)bo; (void)md;
   /* BC-250 KMD does not consume tiling metadata; RADV keeps its
    * own copy. No-op by design. */
}

static void
radv_bc250_bo_get_metadata(struct radeon_winsys *rws, struct radeon_winsys_bo *bo,
                            struct radeon_bo_metadata *md)
{
   (void)rws; (void)bo;
   memset(md, 0, sizeof(*md));
}

static VkResult
radv_bc250_bo_make_resident(struct radeon_winsys *rws, struct radeon_winsys_bo *bo, bool resident)
{
   (void)rws; (void)bo; (void)resident;
   /* BC-250 KMD pins allocations at create time (contiguous memory);
    * residency is implicit. No-op by design. */
   return VK_SUCCESS;
}

static bool
radv_bc250_bo_wait_idle(struct radeon_winsys *rws, struct radeon_winsys_bo *bo)
{
   (void)rws; (void)bo;
   /* No per-BO fences on BC-250; queue-level fences only.
    * Report idle so callers proceed. */
   return true;
}

void
radv_bc250_bo_init_functions(struct radv_bc250_winsys *ws)
{
   ws->base.buffer_create = radv_bc250_bo_create;
   ws->base.buffer_destroy = radv_bc250_bo_destroy;
   ws->base.buffer_map = radv_bc250_bo_map;
   ws->base.buffer_unmap = radv_bc250_bo_unmap;
   ws->base.buffer_set_metadata = radv_bc250_bo_set_metadata;
   ws->base.buffer_get_metadata = radv_bc250_bo_get_metadata;
   ws->base.buffer_make_resident = radv_bc250_bo_make_resident;
   ws->base.bo_wait_for_idle = radv_bc250_bo_wait_idle;
}
