/*
 * g3d-ring-realbase - canary CP_RB0_BASE_LO with a real, PSP-proven PA.
 *
 * The first canary wrote 0x12345678 and only bits [7:0] survived. That value is
 * not a plausible ring address: not page-aligned, not backed by memory. If the
 * DF ACL filters writes by whether the value looks like a legitimate driver ring
 * base, garbage gets rejected while a real address would be accepted.
 *
 * The address used here (default 0x7E515000) is the same contiguous buffer
 * PSP_RING_INIT hands out - PSP has already written to it, so it is provably
 * real, page-aligned and below 4 GB. Pass a different one as argv[1] if needed.
 *
 * If this sticks, a real GFX ring can be built. If it does not, the address
 * field is genuinely locked and no host path exists.
 *
 * CNTL is never touched, so the ring stays disabled and nothing can execute.
 * Every register is restored to its original value.
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

#define RB0_BASE_LO  0x89E0
#define RB0_BASE_HI  0x8BA4
#define RB0_CNTL     0x89E4
#define RB0_SIZE     0x89E8

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

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);

    uint32_t pa = (argc > 1) ? (uint32_t)strtoul(argv[1], NULL, 0)
                             : 0x7E515000u;
    printf("Using candidate PA 0x%08X  (%s 4KB aligned)\n",
           pa, (pa & 0xFFF) ? "NOT" : "IS");

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

    uint32_t origLo = ReadReg(RB0_BASE_LO);
    uint32_t origHi = ReadReg(RB0_BASE_HI);
    uint32_t origSz = ReadReg(RB0_SIZE);
    uint32_t cntl   = ReadReg(RB0_CNTL);

    printf("\n=== BEFORE ===\n");
    printf("  BASE_LO 0x%08X  BASE_HI 0x%08X  CNTL 0x%08X  SIZE 0x%08X\n",
           origLo, origHi, cntl, origSz);
    if (cntl != 0) {
        printf("ABORT: ring CNTL non-zero, ring may be in use.\n");
        CloseHandle(g_hDev);
        return 2;
    }

    /* gfx_v10_0.c: rb_addr = ring->gpu_addr >> 8; WREG32(mmCP_RB0_BASE, rb_addr);
     * The >>8 shift is mandatory, not optional. If the register is only
     * implemented to hold values that decode back onto real memory, this is the
     * one encoding that can pass. Earlier trials covered raw and >>2, never >>8. */
    const uint32_t cand[6] = {
        pa,
        pa >> 2,
        pa >> 4,
        pa >> 8,        /* what Linux actually writes */
        pa >> 12,
        pa >> 16,
    };
    const char *cname[6] = {
        "raw PA", "PA>>2", "PA>>4",
        "PA>>8   <== Linux encoding",
        "PA>>12", "PA>>16",
    };

    printf("\n=== TRIALS ===\n");
    for (int i = 0; i < 6; i++) {
        WriteReg(RB0_BASE_LO, 0);
        WriteReg(RB0_BASE_HI, 0);
        WriteReg(RB0_BASE_LO, cand[i]);
        uint32_t lo = ReadReg(RB0_BASE_LO);
        const char *v = (lo == cand[i] && cand[i] != 0)
                          ? "STUCK  <-- address field WRITABLE"
                      : (lo == 0) ? "rejected (reads 0)"
                                   : "partial";
        printf("  %-30s wrote 0x%08X -> LO=0x%08X  %s\n", cname[i], cand[i], lo, v);
    }

    /* With the correct encoding in place, verify the HI half separately. */
    printf("\n=== BASE_HI trial (>>8 encoding) ===\n");
    WriteReg(RB0_BASE_LO, 0);
    WriteReg(RB0_BASE_HI, 0);
    WriteReg(RB0_BASE_LO, pa >> 8);
    WriteReg(RB0_BASE_HI, (uint32_t)(pa >> 40));
    printf("  LO=0x%08X HI=0x%08X (wrote LO=0x%08X HI=0x%08X)\n",
           ReadReg(RB0_BASE_LO), ReadReg(RB0_BASE_HI), pa >> 8,
           (uint32_t)(pa >> 40));

    printf("\n=== RING SIZE trial ===\n");
    WriteReg(RB0_SIZE, 0x00010000);
    uint32_t sz = ReadReg(RB0_SIZE);
    printf("  SIZE wrote 0x00010000 -> 0x%08X  %s\n", sz,
           sz == 0x00010000 ? "STUCK <-- SIZE WRITABLE" : "rejected");

    printf("\n=== RESTORE ===\n");
    WriteReg(RB0_BASE_LO, origLo);
    WriteReg(RB0_BASE_HI, origHi);
    WriteReg(RB0_SIZE, origSz);
    printf("  BASE_LO 0x%08X  BASE_HI 0x%08X  SIZE 0x%08X\n",
           ReadReg(RB0_BASE_LO), ReadReg(RB0_BASE_HI), ReadReg(RB0_SIZE));

    printf("\nDone.\n");
    CloseHandle(g_hDev);
    return 0;
}