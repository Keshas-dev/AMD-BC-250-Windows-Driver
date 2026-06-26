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
    DeviceIoControl(h,0x80000B80,init,sizeof(init),NULL,0,&br,NULL);
    printf("INIT_HARDWARE OK\n");
    UINT32 ra[2];
    ra[0]=0xECA8; DeviceIoControl(h,0x80000B88,ra,sizeof(ra),ra,sizeof(ra),&br,NULL);
    printf("RLC_CP_SCHEDULERS[0xECA8] = 0x%08X\n", ra[1]);
    ra[0]=0xECA1; DeviceIoControl(h,0x80000B88,ra,sizeof(ra),ra,sizeof(ra),&br,NULL);
    printf("RLC_CP_SCHEDULERS[0xECA1] = 0x%08X\n", ra[1]);
    ra[0]=0x32D4; DeviceIoControl(h,0x80000B88,ra,sizeof(ra),ra,sizeof(ra),&br,NULL);
    printf("SCRATCH[0x32D4] = 0x%08X\n", ra[1]);
    /* Write 0xA0 to 0xECA8 and check SCRATCH */
    ra[0]=0xECA8; ra[1]=0xA0;
    DeviceIoControl(h,0x80000B8C,ra,sizeof(ra),NULL,0,&br,NULL);
    printf("Wrote 0xA0 to RLC[0xECA8]\n");
    Sleep(100);
    ra[0]=0x32D4; DeviceIoControl(h,0x80000B88,ra,sizeof(ra),ra,sizeof(ra),&br,NULL);
    printf("SCRATCH after: 0x%08X\n", ra[1]);
    ra[0]=0xECA8; DeviceIoControl(h,0x80000B88,ra,sizeof(ra),ra,sizeof(ra),&br,NULL);
    printf("RLC[0xECA8] readback: 0x%08X\n", ra[1]);
    CloseHandle(h);
    return 0;
}