#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>

int main(void) {
    HANDLE h = CreateFileW(L"\\\\.\\AMDBC250DreamV43",
        GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("Driver open FAILED err=%lu\n", GetLastError());
        return 1;
    }
    printf("Driver opened OK\n");

    /* INIT_HARDWARE with NBIO_MAP only — safe, no GPU alive test */
    UCHAR initIn[32] = {0};
    DWORD br = 0;
    *(UINT64*)(initIn + 0)  = 0xFE800000ULL;  /* BAR5 PA */
    *(UINT32*)(initIn + 8)  = 0x00080000;      /* 512KB */
    *(UINT32*)(initIn + 12) = 1;               /* NBIO_MAP flag */
    *(UINT64*)(initIn + 16) = 0xC0000000ULL;   /* FB PA */
    *(UINT32*)(initIn + 24) = 0x10000000;      /* 256MB */

    BOOL ok = DeviceIoControl(h, 0x80000B80, initIn, sizeof(initIn), NULL, 0, &br, NULL);
    printf("INIT_HARDWARE NBIO_MAP: %s (err=%lu)\n", ok ? "OK" : "FAIL", ok ? 0 : GetLastError());

    if (!ok) {
        CloseHandle(h);
        return 1;
    }

    /* Quick read test */
    UINT32 regs[] = { 0x3260, 0x3264, 0x32D4, 0x34FC, 0x0000, 0x0F20, 0x16600, 0x16800, 0xC060 };
    const char *names[] = { "GRBM_STATUS", "CC_CONFIG", "SCRATCH", "SPI_WGP", "GPU_ID", "HDP", "THM", "SMUIO", "CP_ME_CNTL" };
    for (int i = 0; i < 9; i++) {
        UINT32 ra[2] = { regs[i], 0 };
        DeviceIoControl(h, 0x80000B88, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
        printf("  [0x%05X] %-12s = 0x%08X\n", regs[i], names[i], ra[1]);
    }

    CloseHandle(h);
    return 0;
}
