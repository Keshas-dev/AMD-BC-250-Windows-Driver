#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>

int main(void) {
    HANDLE h = CreateFileW(L"\\\\.\\AMDBC250DreamV43",
        GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("ERROR: Cannot open driver (err=%lu)\n", GetLastError());
        return 1;
    }

    /* Safe 512KB BAR5 mapping */
    UCHAR in[32]={0}; DWORD br=0;
    *(UINT64*)(in+0)  = 0xFE800000ULL;
    *(UINT32*)(in+8)  = 0x00080000;   /* 512KB - SAFE */
    *(UINT32*)(in+12) = 1;
    *(UINT64*)(in+16) = 0xC0000000ULL;
    *(UINT32*)(in+24) = 0x10000000;
    if (!DeviceIoControl(h,0x80000B80,in,sizeof(in),NULL,0,&br,NULL)) {
        printf("ERROR: INIT_HARDWARE failed (err=%lu)\n", GetLastError());
        CloseHandle(h); return 1;
    }
    printf("GPU monitoring started - 512KB BAR5 mapped.\n");
    printf("Now run PSP driver in another terminal:\n");
    printf("  test-psp-driver.exe -i 0xFE800000 0x200000\n");
    printf("  test-psp-driver.exe -f cyan_skillfish2_sos_extracted.bin\n");
    printf("  test-psp-driver.exe -C 0x00000004\n");
    printf("  test-psp-driver.exe -C 0x00000008\n\n");

    for (int i = 0; i < 120; i++) { /* 2 minutes */
        UINT32 ra[2]={0x2004,0xDEADBEEF};
        DeviceIoControl(h,0x80000B88,ra,sizeof(ra),ra,sizeof(ra),&br,NULL);
        UINT32 grbm = ra[1];

        UINT32 gpu=0xFFFFFFFF; ra[0]=0x0000; ra[1]=0xDEADBEEF;
        DeviceIoControl(h,0x80000B88,ra,sizeof(ra),ra,sizeof(ra),&br,NULL);
        gpu = ra[1];

        printf("[%ds] GPU_ID=0x%08X  GRBM=0x%08X", i, gpu, grbm);
        if (grbm != 0xFFFFFFFF && grbm != 0x00000000) {
            printf("  *** NBIO UNLOCKED! ***\n");
            /* Check CP and CLK too */
            ra[0]=0x2000; ra[1]=0xDEADBEEF;
            DeviceIoControl(h,0x80000B88,ra,sizeof(ra),ra,sizeof(ra),&br,NULL);
            printf("      CP[0x2000]=0x%08X  CLK[0x0D00]=", ra[1]);
            ra[0]=0x0D00; ra[1]=0xDEADBEEF;
            DeviceIoControl(h,0x80000B88,ra,sizeof(ra),ra,sizeof(ra),&br,NULL);
            printf("0x%08X\n", ra[1]);
            break;
        }
        printf("\n");
        Sleep(1000);
    }
    printf("\nMonitoring complete.\n");
    CloseHandle(h);
    return 0;
}
