/* vram-query.c — read-only VRAM info probe (GET_VRAM_INFO 0x80000804). */
#include <windows.h>
#include <stdio.h>

#define IOCTL_AMDBC250_GET_VRAM_INFO  0x80000804

int main(void)
{
    HANDLE h;
    unsigned long long info[4] = {0};
    DWORD br = 0;
    BOOL ok;

    h = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("CreateFile FAILED gle=%lu\n", GetLastError());
        return 1;
    }
    ok = DeviceIoControl(h, IOCTL_AMDBC250_GET_VRAM_INFO, NULL, 0,
                         info, sizeof(info), &br, NULL);
    if (!ok) {
        printf("GET_VRAM_INFO FAILED gle=%lu\n", GetLastError());
        CloseHandle(h);
        return 1;
    }
    printf("TotalVramBytes   = %llu MB (0x%llX)\n", info[0] / (1024*1024), info[0]);
    printf("VisibleVramBytes = %llu MB (0x%llX)\n", info[1] / (1024*1024), info[1]);
    printf("VRAM base lo     = 0x%08X hi = 0x%08X\n", (unsigned)info[2], (unsigned)info[3]);
    CloseHandle(h);
    return 0;
}
