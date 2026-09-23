#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"
int main(void) {
    HANDLE h = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { printf("open fail %lu\n", GetLastError()); return 1; }
    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br = 0;
    memset(&ih, 0, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL;
    ih.MmioSize = 0x80000;
    ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
    printf("INIT size=%u IOCTL=0x%08X PA=0x%llX Size=0x%X Flags=0x%X\n",
        (unsigned)sizeof(ih), (unsigned)IOCTL_AMDBC250_INIT_HARDWARE,
        ih.MmioPhysicalBase, ih.MmioSize, ih.Flags);
    if (!DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), NULL, 0, &br, NULL)) {
        printf("INIT fail err=%lu\n", GetLastError());
        CloseHandle(h); return 2;
    }
    printf("INIT OK br=%lu\n", br);
    AMDBC250_IOCTL_REG_ACCESS r; br = 0;
    r.RegisterOffset = 0x0000; r.Value = 0;
    if (DeviceIoControl(h, IOCTL_AMDBC250_READ_REG, &r, sizeof(r), &r, sizeof(r), &br, NULL))
        printf("GPU_ID=0x%08X\n", r.Value);
    else printf("READ_REG fail err=%lu\n", GetLastError());
    r.RegisterOffset = 0x3260; r.Value = 0;
    if (DeviceIoControl(h, IOCTL_AMDBC250_READ_REG, &r, sizeof(r), &r, sizeof(r), &br, NULL))
        printf("GRBM_STATUS=0x%08X\n", r.Value);
    CloseHandle(h);
    return 0;
}
