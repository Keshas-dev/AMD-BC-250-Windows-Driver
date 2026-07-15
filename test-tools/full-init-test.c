#include <windows.h>
#include <stdio.h>
#include <memory.h>

#define IOCTL_INIT_HW 0x80000B80

/* Triggers FULL hardware init (DreamV3HwInitialize) by passing Flags=0.
 * NBIO_MAP mode passes Flags=AMDBC250_INIT_FLAG_NBIO_MAP (1) and skips it.
 * TDR RISK: full init touches rings/firmware/RLC. If it hangs, reboot and
 * read the driver's Step_HwInit registry marker to see the last-entered step. */
int main(void)
{
    HANDLE h = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { printf("open failed (admin? driver loaded?): %lu\n", GetLastError()); return 1; }

    UINT8 buf[32] = {0};
    *(UINT64*)(buf+0)  = 0xFE800000ULL;   /* MmioPhysicalBase */
    *(UINT32*)(buf+8)  = 0x00080000;      /* MmioSize */
    *(UINT32*)(buf+12) = 0;               /* Flags=0 => FULL INIT */
    *(UINT64*)(buf+16) = 0;               /* FbPhysicalBase */
    *(UINT32*)(buf+24) = 0;               /* FbSize=0 => skip 512MB VRAM map */

    DWORD br = 0;
    printf("Calling INIT_HARDWARE Flags=0 (FULL INIT) — TDR risk!\n");
    fflush(stdout);
    BOOL ok = DeviceIoControl(h, IOCTL_INIT_HW, buf, sizeof(buf), NULL, 0, &br, NULL);
    if (ok) printf("INIT_HARDWARE returned SUCCESS — full init completed, no TDR.\n");
    else    printf("INIT_HARDWARE failed/blocked: %lu\n", GetLastError());
    CloseHandle(h);
    return 0;
}
