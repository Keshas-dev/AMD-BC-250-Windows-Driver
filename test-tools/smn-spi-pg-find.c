/* smn-spi-pg-find.c - find SPI_PG SMN alias in GC high window */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE h;
static BOOL W32(uint32_t o, uint32_t v) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=v; return DeviceIoControl(h,IOCTL_AMDBC250_WRITE_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL); }
static uint32_t R32(uint32_t o) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=0; if(DeviceIoControl(h,IOCTL_AMDBC250_READ_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL)) return r.Value; return 0xFFFFFFFF; }

static void smnW(uint32_t a,uint32_t v){W32(0x38,a);W32(0x3C,v);}
static uint32_t smnR(uint32_t a){W32(0x38,a);R32(0x38);return R32(0x3C);}

/* SMU Q3 msg 0x98: writes 0xFF to SMN address (if valid) */
static int smu98(uint32_t smnAddr){
    smnW(0x03B10A80,0); smnW(0x03B10A88,smnAddr); smnW(0x03B10A20,0x98);
    for(int i=0;i<500;i++){uint32_t st=smnR(0x03B10A80);if(st==1)return 1;if(st==0xFF)return -1;if(st==0xFE)return -2;if(st==0xFD)return -3;if(st==0xFC)return -4;Sleep(1);}return -100;
}

int main(void){
    h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE){printf("FAIL: open err=%lu\n",GetLastError());return 1;}
    printf("=== Find SPI_PG SMN alias ===\n");
    printf("Baseline SPI_PG (BAR5) = 0x%08X\n", R32(0x5C3C));

    /* Try GC high window 0x0240xxxx */
    printf("\n=== Scanning 0x02400000-0x02410000 (GC high window) ===\n");
    int found = 0;
    for(uint32_t addr = 0x02400000; addr < 0x02410000; addr += 0x1000){
        uint32_t val = smnR(addr);
        /* SPI_PG stock = 0x00000000, look for readable registers */
        if(val != 0xFFFFFFFF && val != 0){
            printf("  0x%08X: READ = 0x%08X\n", addr, val);
            /* Try SMU 0x98 write */
            int r = smu98(addr);
            if(r == 1){
                uint32_t after = smnR(addr);
                printf("    -> SMU 0x98 OK! After = 0x%08X\n", after);
                found++;
            }
            if(found > 5) break;
        }
    }

    /* Try specific candidate: 0x02405C3C (SPI_PG offset mapped to SMN) */
    printf("\n=== Try candidate 0x02405C3C ===\n");
    uint32_t candidate = 0x02405C3C;
    uint32_t before = smnR(candidate);
    printf("  Before: 0x%08X\n", before);
    int r = smu98(candidate);
    printf("  SMU 0x98 result: %d\n", r);
    uint32_t after = smnR(candidate);
    printf("  After: 0x%08X\n", after);

    /* Also try 0x0115C000 range (near core mask) */
    printf("\n=== Try 0x0115C000 range ===\n");
    for(uint32_t addr = 0x0115C000; addr < 0x01160000; addr += 0x1000){
        uint32_t val = smnR(addr);
        if(val == 0x00000000){
            printf("  0x%08X: READ = 0x00000000 (matches SPI_PG stock!)\n", addr);
            int r = smu98(addr);
            if(r == 1){
                uint32_t after = smnR(addr);
                printf("    -> SMU 0x98 OK! After = 0x%08X <- SPI_PG ALIAS?\n", after);
            }
            break;
        }
    }

    printf("\n=== DONE ===\n");
    CloseHandle(h);
    return 0;
}
