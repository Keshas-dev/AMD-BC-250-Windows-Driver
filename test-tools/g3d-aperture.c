/*
 * g3d-aperture - test whether BAR0 / BAR2 carry a writable copy of the GC block.
 *
 * Established: the GFX ring registers are read-only through BAR5, but full
 * 32-bit writes work elsewhere (GRBM_GFX_INDEX 0x34D0, SCRATCH 0x32D4). So there
 * is no global ACL. The remaining explanation is that BAR5 is simply the wrong
 * aperture for the CP/ring block on this APU.
 *
 * The GPU function exposes four BARs and the driver has only ever mapped one:
 *   BAR0 0xC0000000  (64-bit prefetchable)   never mapped
 *   BAR2 0xD0000000  (64-bit prefetchable)   never mapped
 *   BAR4 0x0000EF00  (I/O)                    never mapped
 *   BAR5 0xFE800000  (512K MEM)               the only one in use
 *
 * INIT_HARDWARE accepts an arbitrary physical base and repoints the driver's
 * MMIO window at it, so each aperture can be tried without a driver change.
 *
 * This is read-mostly: it reads every register it prints, and the single write
 * is a canary against the ring block, restored immediately. The driver's window
 * is returned to BAR5 on the way out.
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "..\inc\amdbc250_ioctl.h"

#define GPU_ID      0x0000
#define GRBM_GFX_INDEX 0x34D0
#define SCRATCH     0x32D4
#define RB0_BASE_LO 0x89E0
#define RB0_CNTL    0x89E4
#define RB0_WPTR    0x8A30
#define RB0_BASE_HI 0x8BA4

static HANDLE g_hDev = INVALID_HANDLE_VALUE;

static uint32_t ReadReg(uint32_t off) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD ret = 0;
    r.RegisterOffset = off; r.Value = 0;
    if (DeviceIoControl(g_hDev, IOCTL_AMDBC250_READ_REG, &r, sizeof(r),
                        &r, sizeof(r), &ret, NULL)) return r.Value;
    return 0xFFFFFFFF;
}
static BOOL WriteReg(uint32_t off, uint32_t v) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD ret = 0;
    r.RegisterOffset = off; r.Value = v;
    return DeviceIoControl(g_hDev, IOCTL_AMDBC250_WRITE_REG, &r, sizeof(r),
                           &r, sizeof(r), &ret, NULL);
}

/* Repoint the driver's MMIO window at pa. */
static BOOL MapAperture(uint64_t pa, uint32_t size) {
    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br = 0;
    memset(&ih, 0, sizeof(ih));
    ih.MmioPhysicalBase = pa;
    ih.MmioSize = size;
    ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
    if (!DeviceIoControl(g_hDev, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih),
                         NULL, 0, &br, NULL)) {
        printf("    INIT_HARDWARE PA=0x%llX FAILED err=%lu\n", pa, GetLastError());
        return FALSE;
    }
    return TRUE;
}

static void ProbeAperture(uint64_t pa, uint32_t size) {
    printf("\n================ APERTURE 0x%08llX  size 0x%X ================\n", pa, size);
    if (!MapAperture(pa, size)) return;

    uint32_t id = ReadReg(GPU_ID);
    uint32_t grbm = ReadReg(GRBM_GFX_INDEX);
    uint32_t scr = ReadReg(SCRATCH);

    printf("  GPU_ID          0x%08X  %s\n", id,
           id == 0x9FFF9700 ? "<== same GC block as BAR5"
         : (id == 0xFFFFFFFF ? "(unmapped / 0xFFFFFFFF)" : "(different device)"));
    printf("  GRBM_GFX_INDEX  0x%08X\n", grbm);
    printf("  SCRATCH_REG0    0x%08X\n", scr);

    if (id == 0xFFFFFFFF) {
        printf("  -> 0xFFFFFFFF everywhere: not a register window.\n");
        return;
    }

    printf("  --- GFX ring block ---\n");
    uint32_t b0 = ReadReg(RB0_BASE_LO), c0 = ReadReg(RB0_CNTL);
    uint32_t w0 = ReadReg(RB0_WPTR), h0 = ReadReg(RB0_BASE_HI);
    printf("  BASE_LO 0x%08X  CNTL 0x%08X  WPTR 0x%08X  BASE_HI 0x%08X\n",
           b0, c0, w0, h0);

    printf("  --- canary write ---\n");
    const uint32_t P = 0xA5C35E7Du;
    uint32_t ob = ReadReg(RB0_BASE_LO), oc = ReadReg(RB0_CNTL);
    uint32_t ow = ReadReg(RB0_WPTR), oh = ReadReg(RB0_BASE_HI);

    WriteReg(RB0_BASE_LO, P);
    WriteReg(RB0_CNTL,  0x00030001u);
    WriteReg(RB0_WPTR,  P);
    uint32_t nb = ReadReg(RB0_BASE_LO), nc = ReadReg(RB0_CNTL), nw = ReadReg(RB0_WPTR);
    printf("  wrote BASE=0x%08X CNTL=0x00030001 WPTR=0x%08X\n", P, P);
    printf("  got   BASE=0x%08X CNTL=0x%08X WPTR=0x%08X\n", nb, nc, nw);
    if (nb == P || nc == 0x00030001u || nw == P)
        printf("  >>> SOMETHING IS WRITABLE HERE <<<\n");
    else
        printf("  -> still read-only\n");

    WriteReg(RB0_BASE_LO, ob);
    WriteReg(RB0_CNTL,  oc);
    WriteReg(RB0_WPTR,  ow);
    WriteReg(RB0_BASE_HI, oh);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);

    g_hDev = CreateFileA("\\\\.\\AMDBC250DreamV43",
        GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, 0, NULL);
    if (g_hDev == INVALID_HANDLE_VALUE) {
        printf("FAIL: cannot open GPU device (err=%lu)\n", GetLastError());
        return 1;
    }

    /* Baseline first: the aperture we always used, for comparison. */
    ProbeAperture(0xFE800000ull, 0x80000);

    if (argc > 1) {
        /* Explicit single aperture, e.g. g3d-aperture.exe 0xD0000000 */
        ProbeAperture(strtoull(argv[1], NULL, 0), 0x1000);
    } else {
        ProbeAperture(0xC0000000ull, 0x1000);   /* BAR0 */
        ProbeAperture(0xD0000000ull, 0x1000);   /* BAR2 */
    }

    printf("\n=== restoring BAR5 window ===\n");
    MapAperture(0xFE800000ull, 0x80000);
    printf("  GPU_ID now 0x%08X %s\n", ReadReg(GPU_ID),
           ReadReg(GPU_ID) == 0x9FFF9700 ? "OK, back on BAR5" : "!! NOT RESTORED !!");

    printf("\nDone.\n");
    CloseHandle(g_hDev);
    return 0;
}