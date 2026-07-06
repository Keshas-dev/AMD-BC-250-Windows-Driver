#include <windows.h>
#include <stdio.h>
#include "..\inc\amdbc250_ioctl.h"

int main() {
    HANDLE hDev = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hDev == INVALID_HANDLE_VALUE) {
        printf("FAIL gle=%lu\n", GetLastError());
        return 1;
    }
    printf("OK\n");

    AMDBC250_IOCTL_RESOURCE_BARS r; DWORD ret = 0;
    ZeroMemory(&r, sizeof(r));
    BOOL ok = DeviceIoControl(hDev, IOCTL_AMDBC250_GET_RESOURCE_BARS, NULL, 0, &r, sizeof(r), &ret, NULL);
    printf("ok=%d gle=%lu\n", ok, GetLastError());
    if (ok) {
        printf("  DeviceStarted=%u\n", r.DeviceStarted);
        printf("  MmioMapped=%u\n", r.MmioMapped);
        printf("  MmioPhysicalBase=0x%llx\n", r.MmioPhysicalBase);
        printf("  MmioSize=%u (0x%x)\n", r.MmioSize, r.MmioSize);
        printf("  MmioVirtualBase=0x%llx\n", r.MmioVirtualBase);
        printf("  FbPhysicalBase=0x%llx\n", r.FbPhysicalBase);
        printf("  FbSize=%u (0x%x)\n", r.FbSize, r.FbSize);
    }
    CloseHandle(hDev);
    return 0;
}
