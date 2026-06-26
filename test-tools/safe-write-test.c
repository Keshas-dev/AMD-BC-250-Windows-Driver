#include <windows.h>
#include <stdio.h>
int main() {
    HANDLE h = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
    if (h == INVALID_HANDLE_VALUE) { printf("No driver\n"); return 1; }
    UCHAR init[32]={0}; DWORD br;
    *(unsigned __int64*)(init+0)=0xFE800000ULL;
    *(unsigned*)(init+8)=0x00080000;
    *(unsigned*)(init+12)=1;
    *(unsigned __int64*)(init+16)=0xC0000000ULL;
    *(unsigned*)(init+24)=0x10000000;
    if (!DeviceIoControl(h,0x80000B80,init,sizeof(init),NULL,0,&br,NULL)) {
        printf("INIT FAILED\n"); CloseHandle(h); return 1;
    }
    printf("INIT_HARDWARE OK\n");
    UINT32 ra[2];
    ra[0]=0x32D4; DeviceIoControl(h,0x80000B88,ra,sizeof(ra),ra,sizeof(ra),&br,NULL);
    printf("SCRATCH before = 0x%08X\n", ra[1]);
    ra[0]=0x32D4; ra[1]=0x12345678;
    if (!DeviceIoControl(h,0x80000B8C,ra,8,NULL,0,&br,NULL)) {
        printf("WRITE FAILED\n"); CloseHandle(h); return 1;
    }
    printf("Wrote 0x12345678 to SCRATCH\n");
    Sleep(50);
    ra[0]=0x32D4; DeviceIoControl(h,0x80000B88,ra,sizeof(ra),ra,sizeof(ra),&br,NULL);
    printf("SCRATCH after  = 0x%08X\n", ra[1]);
    /* Restore */
    ra[0]=0x32D4; ra[1]=0x4D585042;
    DeviceIoControl(h,0x80000B8C,ra,8,NULL,0,&br,NULL);
    printf("Restored SCRATCH\n");
    CloseHandle(h);
    printf("Done - safe exit\n");
    return 0;
}
