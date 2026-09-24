/* mc-read.c — READ-ONLY dump of MC VM/GART state (is GART already programmed?).
 * If BIOS/POST left AGP/FB_LOCATION sane, no MC writes may be needed at all. */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE h = INVALID_HANDLE_VALUE;

static uint32_t rd(uint32_t off, int *ok) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD br = 0;
    ZeroMemory(&r, sizeof(r));
    r.RegisterOffset = off;
    if (!DeviceIoControl(h, IOCTL_AMDBC250_READ_REG, &r, sizeof(r),
                         &r, sizeof(r), &br, NULL)) {
        if (ok) *ok = 0;
        return 0xFFFFFFFF;
    }
    if (ok) *ok = 1;
    return r.Value;
}

int main(void) {
    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br = 0;
    int ok;
    uint32_t fb_lo, fb_top, agp_b, agp_t, agp_bot, agp_cntl, sa_lo, sa_hi, sa_def;
    uint32_t gpu, spi;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== MC VM/GART state (READ-ONLY) ===\n\n");

    h = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("FAIL: CreateFile gle=%lu\n", GetLastError());
        return 1;
    }
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL;
    ih.MmioSize = 0x80000;
    ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
    DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &br, NULL);

    gpu = rd(0x0000, &ok);
    printf("GPU_ID              = 0x%08X %s\n", gpu, ok ? "" : "(READ FAIL)");
    fb_lo  = rd(0x9520, NULL);
    fb_top = rd(0x9524, NULL);
    agp_b  = rd(0x9528, NULL);
    agp_t  = rd(0x952C, NULL);
    agp_bot= rd(0x9530, NULL);
    agp_cntl=rd(0x9534, NULL);
    sa_lo  = rd(0x9540, NULL);
    sa_hi  = rd(0x9544, NULL);
    sa_def = rd(0x9548, NULL);
    printf("FB_LOCATION  base   = 0x%08X top = 0x%08X\n", fb_lo, fb_top);
    printf("AGP          base   = 0x%08X top = 0x%08X bot = 0x%08X cntl = 0x%08X\n",
           agp_b, agp_t, agp_bot, agp_cntl);
    printf("SYS_APERTURE lo     = 0x%08X hi  = 0x%08X def = 0x%08X\n", sa_lo, sa_hi, sa_def);
    spi = rd(0x5C3C, NULL);
    printf("SPI_PG (baseline)   = 0x%08X\n", spi);

    /* verdict: is any aperture programmed (nonzero, non-FF)? */
    if (fb_lo != 0 && fb_lo != 0xFFFFFFFF)
        printf("\nVERDICT: FB_LOCATION programmed by BIOS/POST (GART may need no MC writes)\n");
    else
        printf("\nVERDICT: FB_LOCATION empty/unmapped (MC untouched since reset)\n");
    if ((agp_b != 0 && agp_b != 0xFFFFFFFF) || (agp_t != 0 && agp_t != 0xFFFFFFFF))
        printf("VERDICT: AGP aperture programmed (0x%08X-0x%08X)\n", agp_b, agp_t);
    else
        printf("VERDICT: AGP aperture NOT programmed\n");

    CloseHandle(h);
    printf("\nDone. No writes performed.\n");
    return 0;
}
