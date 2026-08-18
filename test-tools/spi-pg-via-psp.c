/* spi-pg-via-psp.c - write SPI_PG through PSP driver (bypasses SOS lock) */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE h;
static BOOL W32(uint32_t o, uint32_t v) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=v; return DeviceIoControl(h,IOCTL_AMDBC250_WRITE_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL); }
static uint32_t R32(uint32_t o) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=0; if(DeviceIoControl(h,IOCTL_AMDBC250_READ_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL)) return r.Value; return 0xFFFFFFFF; }

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE){printf("FAIL: open GPU driver err=%lu\n",GetLastError());return 1;}

    AMDBC250_IOCTL_INIT_HARDWARE ih;
    DWORD br=0;
    memset(&ih,0,sizeof(ih));
    ih.MmioPhysicalBase=0xFE800000ULL;
    ih.MmioSize=0x80000;
    ih.Flags=AMDBC250_INIT_FLAG_NBIO_MAP;
    if(!DeviceIoControl(h,IOCTL_AMDBC250_INIT_HARDWARE,&ih,sizeof(ih),&ih,sizeof(ih),&br,NULL)){printf("INIT fail\n");return 1;}
    printf("INIT OK (GPU driver)\n");

    printf("Baseline SPI_PG=0x%08X via GPU driver\n", R32(0x5C3C));

    /* Now try writing through PSP driver */
    HANDLE hPsp = CreateFileA("\\\\.\\AmdBcPsp",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(hPsp==INVALID_HANDLE_VALUE){
        printf("FAIL: open PSP driver err=%lu\n",GetLastError());
        CloseHandle(h);
        return 1;
    }
    printf("PSP driver opened!\n");

    /* Initialize PSP HW (maps GPU BAR5 through PSP PCIe) */
    DWORD pspBr=0;
    if(!DeviceIoControl(hPsp,IOCTL_AMDBC250_INIT_HARDWARE,&ih,sizeof(ih),&ih,sizeof(ih),&pspBr,NULL)){
        printf("PSP INIT_HW failed err=%lu\n",GetLastError());
    } else {
        printf("PSP INIT_HW OK\n");
    }

    /* Read SPI_PG through PSP */
    AMDBC250_IOCTL_REG_ACCESS ra;
    DWORD rb;
    ra.RegisterOffset=0x5C3C; ra.Value=0;
    if(DeviceIoControl(hPsp,IOCTL_AMDBC250_READ_REG,&ra,sizeof(ra),&ra,sizeof(ra),&rb,NULL)){
        printf("SPI_PG via PSP = 0x%08X\n",ra.Value);
    }

    /* Try writing SPI_PG through PSP */
    ra.RegisterOffset=0x5C3C; ra.Value=0x1F;
    if(DeviceIoControl(hPsp,IOCTL_AMDBC250_WRITE_REG,&ra,sizeof(ra),&ra,sizeof(ra),&rb,NULL)){
        printf("SPI_PG write via PSP: OK\n");
    } else {
        printf("SPI_PG write via PSP: FAIL err=%lu\n",GetLastError());
    }

    /* Read back through PSP */
    ra.RegisterOffset=0x5C3C; ra.Value=0;
    if(DeviceIoControl(hPsp,IOCTL_AMDBC250_READ_REG,&ra,sizeof(ra),&ra,sizeof(ra),&rb,NULL)){
        printf("SPI_PG after write via PSP = 0x%08X %s\n",ra.Value,(ra.Value==0x1F)?"*** SUCCESS! ***":"");
    }

    /* Read back through GPU driver (to check if PSP write affected shared register) */
    printf("SPI_PG via GPU driver = 0x%08X\n", R32(0x5C3C));

    CloseHandle(hPsp);
    CloseHandle(h);
    return 0;
}
