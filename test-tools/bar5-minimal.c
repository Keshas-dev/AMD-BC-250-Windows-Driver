#include <windows.h>
#include <stdio.h>
#include "..\inc\amdbc250_ioctl.h"

int main() {
    HANDLE hDev = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hDev == INVALID_HANDLE_VALUE) {
        printf("CreateFile FAIL: %lu\n", GetLastError());
        return 1;
    }
    printf("CreateFile OK\n");

    AMDBC250_IOCTL_REG_ACCESS r; DWORD ret = 0;
    r.RegisterOffset = 0x2000; r.Value = 0;
    BOOL ok = DeviceIoControl(hDev, IOCTL_AMDBC250_READ_REG, &r, sizeof(r), &r, sizeof(r), &ret, NULL);
    printf("READ_REG(0x2000): ok=%d gle=%lu val=0x%08X returned=%lu\n", ok, GetLastError(), r.Value, ret);

    /* Also test a write IOCTL to check if device responds */
    r.RegisterOffset = 0; r.Value = 0;
    ok = DeviceIoControl(hDev, IOCTL_AMDBC250_GET_HW_STATUS, NULL, 0, &r, sizeof(r), &ret, NULL);
    printf("GET_HW_STATUS: ok=%d gle=%lu returned=%lu\n", ok, GetLastError(), ret);

    CloseHandle(hDev);
    printf("DONE\n");
    return 0;
}
