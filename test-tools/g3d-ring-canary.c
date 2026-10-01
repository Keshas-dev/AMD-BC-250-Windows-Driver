/*
 * g3d-ring-canary - tests whether CP_RB0_BASE_LO is writable on BC-250.
 *
 * Measured state (g3d-state-ro.exe, 2026-10-01): the whole GFX ring is
 * unprogrammed - BASE_LO/HI, CNTL, WPTR, RPTR are all 0, and KIQ is 0/0.
 * So there is no ring at all, not a ring locked at some BIOS value.
 *
 * That changes the risk profile versus the earlier white-screen incident: the
 * ring is disabled (CNTL=0) and there are no active WGPs (SPI_PG=0), so writing
 * the BASE register cannot make the CP execute anything. The register is also
 * currently 0, so restoring is exact.
 *
 * Only CP_RB0_BASE_LO is touched. Everything else is read-only.
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

#define RB0_BASE_LO  0x89E0
#define RB0_BASE_HI  0x8BA4
#define RB0_CNTL     0x89E4

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

    printf("=== BEFORE ===\n");
    uint32_t base0 = ReadReg(RB0_BASE_LO);
    uint32_t base1 = ReadReg(RB0_BASE_HI);
    uint32_t cntl  = ReadReg(RB0_CNTL);
    printf("  0x89E0 BASE_LO = 0x%08X\n", base0);
    printf("  0x8BA4 BASE_HI = 0x%08X\n", base1);
    printf("  0x89E4 CNTL    = 0x%08X%s\n", cntl,
           cntl ? "   (ring active - stopping!)" : "   (ring disabled)");

    if (cntl != 0) {
        printf("ABORT: ring CNTL is non-zero, ring may be in use.\n");
        CloseHandle(g_hDev);
        return 2;
    }

    const uint32_t CANARY = 0x12345678;
    printf("\n=== WRITE CANARY 0x%08X to 0x89E0 ===\n", CANARY);
    if (!WriteReg(RB0_BASE_LO, CANARY)) {
        printf("FAIL: WriteReg error=%lu\n", GetLastError());
        CloseHandle(g_hDev);
        return 1;
    }

    uint32_t after = ReadReg(RB0_BASE_LO);
    printf("\n=== READBACK ===\n");
    printf("  0x89E0 = 0x%08X\n", after);

    const char *verdict;
    if      (after == CANARY) verdict = "WRITABLE - ring base is NOT locked";
    else if (after == base0)  verdict = "RO - write ignored, value unchanged";
    else if (after == 0)      verdict = "W1C - write cleared it";
    else                      verdict = "PARTIAL - bits filtered by hardware";
    printf("\nVERDICT: %s\n", verdict);

    printf("\n=== RESTORE ===\n");
    WriteReg(RB0_BASE_LO, base0);
    uint32_t restored = ReadReg(RB0_BASE_LO);
    printf("  wrote back 0x%08X, now 0x%08X  %s\n",
           base0, restored, restored == base0 ? "OK" : "MISMATCH");

    printf("\nDone.\n");
    CloseHandle(g_hDev);
    return 0;
}