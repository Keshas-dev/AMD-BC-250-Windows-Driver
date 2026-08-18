/* unlock40cu-direct.c - call IOCTL_AMDBC250_UNLOCK_40CU directly */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#pragma warning(disable: 4996)

#define CTL_CODE_CUSTOM(m,fn,md,a) ((((m)&0xffff)<<16)|(((a)&0x3)<<14)|(((fn)&0xfff)<<2)|((md)&3))
#define AMDBG_CTL(m,fn,md,a) CTL_CODE_CUSTOM(m,fn,md,a)
#define IOCTL_AMDBC250_UNLOCK_40CU AMDBG_CTL(0x8000, 0x0980, METHOD_BUFFERED, FILE_ANY_ACCESS)

int main(void){
    HANDLE h = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if(h==INVALID_HANDLE_VALUE){printf("open FAIL gle=%lu\n",GetLastError());return 1;}

    DWORD base = 0x80000000;
    ULONG ioctl[] = { 0x80000980, 0x8000980, 0x800000980, base + 0x980, base + (0x60<<2) };
    ULONG inData = 1;
    ULONG out[8] = {0};
    DWORD br;

    for(int i=0; i<5; i++){
        ULONG code = ioctl[i];
        BOOL r = DeviceIoControl(h, code, &inData, sizeof(inData), out, sizeof(out), &br, NULL);
        printf("IOCTL 0x%08X -> %s (gle=%lu, br=%lu)\n", code, r?"OK":"FAIL", GetLastError(), br);
        if(r && br>0){
            printf("  out: 0x%08X 0x%08X 0x%08X 0x%08X\n", out[0], out[1], out[2], out[3]);
        }
    }
    CloseHandle(h);
    return 0;
}
