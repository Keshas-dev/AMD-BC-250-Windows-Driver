/* wgp-halt-probe.c — engine-halt + SPI_PG probe via IOCTL 0x80000BEC.
 * Driver halts ME/MEC, tries per-bank SPI_PG=0x1F, restores everything.
 * No SMU, no rings, no VM, no display regs. Readouts decide. */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "..\inc\amdbc250_ioctl.h"

int main(void) {
    HANDLE h;
    AMDBC250_IOCTL_WGP_HALT_PROBE p;
    DWORD br = 0;
    AMDBC250_IOCTL_INIT_HARDWARE ih;
    int i;
    static const char *names[4] = {"SE0/SH0", "SE0/SH1", "SE1/SH0", "SE1/SH1"};

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== WGP halt-probe (IOCTL 0x80000BEC) ===\n\n");

    h = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("FAIL open gle=%lu\n", GetLastError());
        return 1;
    }
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL;
    ih.MmioSize = 0x80000;
    ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
    ih.FbPhysicalBase = 0xC0000000ULL;
    ih.FbSize = 0x10000000;
    DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &br, NULL);

    ZeroMemory(&p, sizeof(p));
    p.Magic = 0x57475000;
    if (!DeviceIoControl(h, IOCTL_AMDBC250_WGP_HALT_PROBE, &p, sizeof(p),
                         &p, sizeof(p), &br, NULL)) {
        printf("FAIL ioctl gle=%lu\n", GetLastError());
        CloseHandle(h);
        return 1;
    }
    printf("ME_CNTL : before=0x%08X halted=0x%08X after=0x%08X %s\n",
           p.MeCntlBefore, p.MeCntlHalted, p.MeCntlAfter,
           (p.MeCntlBefore == p.MeCntlAfter) ? "[restored]" : "*** NOT RESTORED ***");
    printf("MEC_CNTL: before=0x%08X halted=0x%08X after=0x%08X %s\n",
           p.MecCntlBefore, p.MecCntlHalted, p.MecCntlAfter,
           (p.MecCntlBefore == p.MecCntlAfter) ? "[restored]" : "*** NOT RESTORED ***");
    printf("GRBM    : before=0x%08X after=0x%08X %s\n",
           p.GrbmBefore, p.GrbmAfter,
           (p.GrbmBefore == p.GrbmAfter) ? "[restored]" : "*** NOT RESTORED ***");
    for (i = 0; i < 4; i++) {
        printf("%s: SPI_PG before=0x%08X after=0x%08X %s\n", names[i],
               p.SpiBefore[i], p.SpiAfter[i],
               (p.SpiAfter[i] == 0x1F) ? "*** STUCK ***" : "[locked]");
    }
    printf("\nBanks stuck: %u/4\n", p.BanksStuck);
    CloseHandle(h);
    return 0;
}
