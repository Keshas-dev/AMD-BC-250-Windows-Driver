#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include "..\inc\amdbc250_ioctl.h"

static FILE *L;

static void logline(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    if (L) {
        va_start(ap, fmt);
        vfprintf(L, fmt, ap);
        va_end(ap);
        fflush(L);
    }
    fflush(stdout);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    L = fopen("C:\\AMD-BC-250\\safe-probe.log", "w");
    if (!L) L = fopen("safe-probe.log", "w");
    logline("STEP1 log opened\n");

    HANDLE h = CreateFileA("\\\\.\\AMDBC250DreamV43",
        GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        logline("STEP2 CreateFile FAIL gle=%lu\n", GetLastError());
        return 1;
    }
    logline("STEP2 CreateFile OK\n");

    DWORD br = 0;
    ULONG caps[8] = {0};
    BOOL ok = DeviceIoControl(h, IOCTL_AMDBC250_GET_CAPS, NULL, 0, caps, sizeof(caps), &br, NULL);
    logline("STEP3 GET_CAPS ok=%d br=%lu gle=%lu v0=%u v2=%u\n",
            (int)ok, br, GetLastError(), caps[0], caps[2]);

    AMDBC250_IOCTL_HW_STATUS hs;
    memset(&hs, 0, sizeof(hs));
    ok = DeviceIoControl(h, IOCTL_AMDBC250_GET_HW_STATUS, NULL, 0, &hs, sizeof(hs), &br, NULL);
    logline("STEP4 GET_HW_STATUS ok=%d br=%lu gle=%lu mmio=%u rings=%u\n",
            (int)ok, br, GetLastError(), hs.MmioMapped, hs.RingsInitialized);

    CloseHandle(h);
    logline("STEP5 closed OK\n");
    if (L) fclose(L);
    return 0;
}
