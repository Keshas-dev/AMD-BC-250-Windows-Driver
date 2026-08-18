/* smn-spi-pg-scan.c - comprehensive SMN scan for SPI_PG */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE h;
static BOOL W32(uint32_t o, uint32_t v) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=v; return DeviceIoControl(h,IOCTL_AMDBC250_WRITE_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL); }
static uint32_t R32(uint32_t o) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=0; if(DeviceIoControl(h,IOCTL_AMDBC250_READ_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL)) return r.Value; return 0xFFFFFFFF; }

static void smnW(uint32_t a,uint32_t v){W32(0x38,a);W32(0x3C,v);}
static uint32_t smnR(uint32_t a){W32(0x38,a);R32(0x38);return R32(0x3C);}

static int smu98(uint32_t smnAddr){
    smnW(0x03B10A80,0); smnW(0x03B10A88,smnAddr); smnW(0x03B10A20,0x98);
    for(int i=0;i<200;i++){uint32_t st=smnR(0x03B10A80);if(st==1)return 1;if(st==0xFF)return -1;if(st==0xFE)return -2;if(st==0xFD)return -3;if(st==0xFC)return -4;Sleep(1);}return -100;
}

int main(void){
    h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE){printf("FAIL: open err=%lu\n",GetLastError());return 1;}
    printf("=== Comprehensive SMN scan for SPI_PG ===\n");
    printf("Baseline SPI_PG (BAR5) = 0x%08X\n\n", R32(0x5C3C));

    /* Scan all promising SMN ranges */
    uint32_t ranges[][2] = {
        {0x02400000, 0x02410000},  /* GC high window */
        {0x01100000, 0x01200000},  /* Core/PSP window */
        {0x00000000, 0x00100000},  /* Low SMN (dangerous) */
        {0x03B10000, 0x03B20000},  /* SMU mailbox extended */
        {0x04000000, 0x04010000},  /* High SMU */
    };
    int nranges = 5;
    int found = 0;

    for(int ri=0; ri<nranges; ri++){
        uint32_t lo = ranges[ri][0];
        uint32_t hi = ranges[ri][1];
        printf("=== Scanning 0x%08X-0x%08X ===\n", lo, hi);
        for(uint32_t addr=lo; addr<hi; addr+=0x100){
            uint32_t val = smnR(addr);
            if(val == 0xFFFFFFFF) continue;  /* dead */
            if(val == 0x00000000){
                /* Matches SPI_PG stock value! Try write */
                int r = smu98(addr);
                if(r == 1){
                    uint32_t after = smnR(addr);
                    printf("  *** 0x%08X: read=0x%08X SMU98=OK after=0x%08X <- POSSIBLE SPI_PG!\n", addr, val, after);
                    found++;
                }
            } else if(val == 0x00000007 || val == 0x0000001F){
                /* Matches SPI_PG unlocked value! */
                printf("  *** 0x%08X: read=0x%08X <- LOOKS LIKE SPI_PG UNLOCKED!\n", addr, val);
                found++;
            }
            if(found > 10) goto done;
        }
    }
done:
    printf("\n=== Found %d candidates ===\n", found);
    CloseHandle(h);
    return 0;
}
