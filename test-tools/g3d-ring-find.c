/*
 * g3d-ring-find - find the real CP_RB0_BASE address. READ-ONLY.
 *
 * Measured: writing 0x12345678 to 0x89E0 read back 0x00000078. Only bits [7:0]
 * survived. A ring base needs >= 4KB granularity, so 0x89E0 is some other live
 * register with an 8-bit field, not CP_RB0_BASE.
 *
 * The arithmetic 0x1260 + mm*4 assumed BASE_IDX=0. In Linux
 * gc_10_1_0_offset.h most CP registers carry BASE_IDX=1, which means the BAR5
 * address is GC_BASE + 0xA000 + mm*4 instead. This probe just reads the
 * candidate addresses and reports what is actually there.
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE g_hDev = INVALID_HANDLE_VALUE;

static uint32_t ReadReg(uint32_t off) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD ret = 0;
    r.RegisterOffset = off; r.Value = 0;
    if (DeviceIoControl(g_hDev, IOCTL_AMDBC250_READ_REG, &r, sizeof(r),
                        &r, sizeof(r), &ret, NULL)) return r.Value;
    return 0xFFFFFFFF;
}

static void Probe(const char *label, uint32_t off) {
    printf("  %-34s 0x%05X = 0x%08X\n", label, off, ReadReg(off));
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);

    g_hDev = CreateFileA("\\\\.\\AMDBC250DreamV43",
        GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, 0, NULL);
    if (g_hDev == INVALID_HANDLE_VALUE) {
        printf("FAIL: cannot open GPU device (err=%lu)\n", GetLastError());
        return 1;
    }
    {
        AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br = 0;
        memset(&ih, 0, sizeof(ih));
        ih.MmioPhysicalBase = 0xFE800000ULL;
        ih.MmioSize = 0x80000;
        ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
        if (!DeviceIoControl(g_hDev, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih),
                             NULL, 0, &br, NULL)) {
            printf("FAIL: INIT_HARDWARE (err=%lu)\n", GetLastError());
            return 1;
        }
    }

    /* mmCP_RB0_BASE = 0x1DE0 -> *4 = 0x7780 */
    const uint32_t off7780 = 0x7780;

    printf("\n=== CP_RB0_BASE candidates (mm*4 = 0x7780) ===\n");
    Probe("raw mm (no GC_BASE)",           0x0000 + off7780);
    Probe("BASE_IDX=0  0x1260+0x7780",     0x1260 + off7780);
    Probe("BASE_IDX=1  0x1260+0xA000+...", 0x1260 + 0xA000 + off7780);
    Probe("bare 0xA000+0x7780",            0xA000 + off7780);
    Probe("high SOC 0x02402C00+0x7780",    0x02402C00 + off7780);

    printf("\n=== neighbours of BASE_IDX=1 candidate (RB0 block) ===\n");
    Probe("BASE_HI  mm=0x1E51",            0x1260 + 0xA000 + 0x1E51 * 4);
    Probe("CNTL     mm=0x1DE1",            0x1260 + 0xA000 + 0x1DE1 * 4);
    Probe("RPTR_ADDR mm=0x1DE3",           0x1260 + 0xA000 + 0x1DE3 * 4);
    Probe("WPTR     mm=0x1DF4",            0x1260 + 0xA000 + 0x1DF4 * 4);
    Probe("SIZE     mm=0x1DE5",            0x1260 + 0xA000 + 0x1DE5 * 4);

    printf("\n=== neighbours of BASE_IDX=0 candidate (current, wrong) ===\n");
    Probe("CNTL     mm=0x1DE1",            0x1260 + 0x1DE1 * 4);
    Probe("RPTR_ADDR mm=0x1DE3",           0x1260 + 0x1DE3 * 4);
    Probe("WPTR     mm=0x1DF4",            0x1260 + 0x1DF4 * 4);

    printf("\n=== a known-good anchor to validate the SEG1 theory ===\n");
    printf("  (CP_ME_CNTL / CP_MEC_CNTL are believed to be at BASE_IDX=0 today)\n");
    Probe("CP_ME_CNTL  (claimed 0x4A74)",  0x4A74);
    Probe("CP_ME_CNTL  SEG1 form",         0x1260 + 0xA000 + 0x0F56 * 4);
    Probe("CP_MEC_CNTL (claimed 0x4B14)",  0x4B14);
    Probe("CP_MEC_CNTL SEG1 form",         0x1260 + 0xA000 + 0x0E2D * 4);

    printf("\nDone (read-only).\n");
    CloseHandle(g_hDev);
    return 0;
}