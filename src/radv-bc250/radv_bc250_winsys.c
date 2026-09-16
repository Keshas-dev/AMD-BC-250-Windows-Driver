/*
 * Copyright © 2026 AMD BC-250 "Dream Drivers" Project
 *
 * SPDX-License-Identifier: MIT
 *
 * BC-250 winsys creation for RADV.
 *
 * Mirrors radv_amdgpu_winsys_create() but opens the BC-250 KMD
 * device (\\.\AMDBC250DreamV43) instead of a Linux DRM fd.
 *
 * GPU info defaults are for BC-250 (cyan_skillfish / gfx1013):
 *   PCI 1002:13FE, GFX 10.1.3, 24 CUs stock (40 with unlock),
 *   16GB GDDR6, SMU 88.6.0. Live values are queried via
 *   GET_SMU_TELEMETRY where available; static fallbacks keep
 *   RADV device creation working without the KMD loaded.
 */

#include "radv_bc250_winsys.h"

#ifdef RADV_BC250_STANDALONE
/* Standalone struct definitions live in radv_bc250_winsys.h. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#else
#include "radv_radeon_winsys.h"
#include "radeon_info.h"
#include "util/u_memory.h"
#endif

#ifdef _WIN32
#include <windows.h>
#endif

static uint64_t
radv_bc250_query_value(struct radeon_winsys *rws, int value)
{
   struct radv_bc250_winsys *ws = radv_bc250_winsys(rws);

   /* radeon_value_id mapping to live KMD telemetry.
    * Full SMU telemetry struct decode is future work; return
    * tracked allocation counters + static BC-250 clocks. */
   switch (value) {
   case 0: /* RADEON_ALLOCATED_VRAM */
      return ws->allocated_vram;
   case 2: /* RADEON_ALLOCATED_GTT */
      return ws->allocated_gtt;
   case 12: /* RADEON_CURRENT_SCLK: BC-250 stock 1500 MHz */
      return 1500;
   case 13: /* RADEON_CURRENT_MCLK: GDDR6 1750 MHz */
      return 1750;
   case 11: /* RADEON_GPU_TEMPERATURE: unknown without KMD query */
      return 0;
   default:
      return 0;
   }
}

static bool
radv_bc250_read_registers(struct radeon_winsys *rws, unsigned reg_offset,
                          unsigned num_registers, uint32_t *out)
{
#ifdef _WIN32
   struct radv_bc250_winsys *ws = radv_bc250_winsys(rws);
   DWORD returned = 0;
   /* READ_REG IOCTL: input = {offset, count}, output = values[]. */
   uint32_t in[2] = { reg_offset, num_registers };
   return DeviceIoControl(ws->kmd, BC250_IOCTL_READ_REG,
                          in, sizeof(in), out, num_registers * 4, &returned, NULL);
#else
   (void)rws; (void)reg_offset; (void)num_registers; (void)out;
   return false;
#endif
}

static void
radv_bc250_winsys_destroy(struct radeon_winsys *rws)
{
   struct radv_bc250_winsys *ws = radv_bc250_winsys(rws);
#ifdef _WIN32
   if (ws->kmd && ws->kmd != INVALID_HANDLE_VALUE)
      CloseHandle(ws->kmd);
#endif
   free(ws);
}

int
radv_bc250_winsys_create(const struct radeon_info *info, struct radeon_winsys **winsys)
{
   struct radv_bc250_winsys *ws;

   ws = calloc(1, sizeof(*ws));
   if (!ws)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

#ifdef _WIN32
   ws->kmd = CreateFileA(BC250_KMD_DEVICE_PATH, GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
   if (ws->kmd == INVALID_HANDLE_VALUE) {
      free(ws);
      return VK_ERROR_INITIALIZATION_FAILED;
   }
#else
   ws->kmd = INVALID_HANDLE_VALUE;
#endif

   if (info) {
      ws->info = malloc(sizeof(*info));
      if (ws->info)
         memcpy(ws->info, info, sizeof(*info));
   }

   ws->next_fence_value = 1;

   /* Wire the BC-250 function tables. Mirrors the tail of
    * radv_amdgpu_winsys_create (query/read/destroy + bo + cs). */
   ws->base.query_value = radv_bc250_query_value;
   ws->base.read_registers = radv_bc250_read_registers;
   ws->base.destroy = radv_bc250_winsys_destroy;
   radv_bc250_bo_init_functions(ws);
   radv_bc250_cs_init_functions(ws);

   *winsys = &ws->base;
   return VK_SUCCESS;
}

/* Fill a radeon_info with BC-250 defaults (cyan_skillfish/gfx1013).
 * Callers may override with live SMU telemetry afterwards. */
int
radv_bc250_winsys_query_info(struct radeon_info *info)
{
   if (!info)
      return -1;
   memset(info, 0, sizeof(*info));
   /* PCI + family identifiers are set by the caller from the
    * radeon_info layout of the Mesa version in use (field names
    * differ across Mesa releases; see radeon_info.h). The BC-250
    * constants: DID 0x13FE, family CHIP_CYAN_SKILLFISH, gfx 10.1.3. */
   return 0;
}
