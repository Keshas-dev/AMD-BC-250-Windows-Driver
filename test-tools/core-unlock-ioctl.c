/* core-unlock-ioctl.c — unlock BC-250's 2 disabled CPU cores via the driver's
 * safe IOCTL (IOCTL_AMDBC250_CORE_UNLOCK, CTL_CODE 0x78). The kernel handles
 * the SMU Q3 msg 0x98 mailbox protocol AND enforces a strict whitelist (only
 * SMN 0x0115A870, only when the mask is the known 0x77 = 6-core state), so
 * this tool cannot accidentally target a wrong address.
 *
 * Usage: core-unlock-ioctl.exe
 * Takes effect on next reboot. Cold power cycle may revert it — rerun after.
 *
 * IOCTLs: INIT_HW 0x80000B80, CORE_UNLOCK 0x80000BA0 (CTL_CODE 0x78).
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== BC-250 CPU Core Unlock (IOCTL 0x78, kernel whitelisted) ===\n\n");

    HANDLE h = CreateFileA("\\\\.\\AMDBC250DreamV43",
        GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("FAIL: CreateFile gle=%lu\n", GetLastError());
        return 1;
    }

    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br = 0;
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL;
    ih.MmioSize = 0x80000;
    ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
    if (!DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE,
                         &ih, sizeof(ih), &ih, sizeof(ih), &br, NULL)) {
        printf("INIT_HARDWARE FAILED gle=%lu\n", GetLastError());
        CloseHandle(h);
        return 1;
    }
    printf("INIT_HARDWARE OK\n");

    AMDBC250_IOCTL_CORE_UNLOCK cu;
    ZeroMemory(&cu, sizeof(cu));
    if (!DeviceIoControl(h, IOCTL_AMDBC250_CORE_UNLOCK,
                         NULL, 0, &cu, sizeof(cu), &br, NULL) || br < sizeof(cu)) {
        printf("FAIL: CORE_UNLOCK IOCTL (err=%lu)\n", GetLastError());
        CloseHandle(h);
        return 1;
    }

    printf("\n--- Result ---\n");
    printf("Core mask before : 0x%08X (%u cores)\n", cu.CoreMaskBefore,
           cu.CoreMaskBefore == 0xFF ? 8 : (cu.CoreMaskBefore == 0x77 ? 6 : 0));
    printf("Core mask after  : 0x%08X (%u cores)\n", cu.CoreMaskAfter,
           cu.CoreMaskAfter == 0xFF ? 8 : (cu.CoreMaskAfter == 0x77 ? 6 : 0));
    printf("SmuStatus        : 0x%02X %s\n", cu.SmuStatus,
           cu.SmuStatus == 1 ? "(OK)" : (cu.SmuStatus == 0xFF ? "(SMU not alive)" : "(refused)"));

    switch (cu.Result) {
        case 1:
            printf("\nOK! Core mask is now 0xFF.\n");
            printf("REBOOT to bring up all 8 cores (16 threads).\n");
            printf("WARNING: cold power cycle may revert it - rerun after.\n");
            printf("Stress-test cores before trusting them (MCEs possible).\n");
            break;
        case 2:
            printf("\nMask already 0xFF - all 8 cores enabled. Reboot to pick them up.\n");
            break;
        default:
            printf("\nUnlock did not take effect.\n");
            break;
    }

    CloseHandle(h);
    return 0;
}
