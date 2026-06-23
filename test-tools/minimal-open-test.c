/* minimal-open-test.c — Just open device and read GPU_ID */
/* NO writes at all, NO SCRATCH, NO init */

#include <windows.h>
#include <stdio.h>

#define IOCTL_GPU_READ  0x80000B88

int main(void) {
    HANDLE h = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
                            0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("FAIL: Cannot open (err=%lu)\n", GetLastError());
        return 1;
    }
    printf("Device opened OK\n");

    /* Read GPU_ID at offset 0 */
    UCHAR buf[8] = {0};
    *(ULONG*)(buf+0) = 0;           /* offset */
    *(ULONG*)(buf+4) = 0xDEADBEEF;  /* output placeholder */
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_GPU_READ, buf, 8, buf, 8, &br, NULL);
    ULONG val = *(ULONG*)(buf+4);
    printf("GPU_ID read: %s (val=0x%08X, br=%lu)\n",
           ok ? "OK" : "FAILED", val, br);

    CloseHandle(h);
    printf("Done\n");
    return 0;
}