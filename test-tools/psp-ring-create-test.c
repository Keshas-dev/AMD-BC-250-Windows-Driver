/* psp-ring-create-test.c - test PSP ring creation via IOCTL and direct BAR5 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"
#pragma warning(disable: 4996)

static HANDLE h;
static BOOL W32(uint32_t o, uint32_t v) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=v; return DeviceIoControl(h,IOCTL_AMDBC250_WRITE_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL); }
static uint32_t R32(uint32_t o) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=0; if(DeviceIoControl(h,IOCTL_AMDBC250_READ_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL)) return r.Value; return 0xFFFFFFFF; }

static void smnW(uint32_t a,uint32_t v){W32(0x38,a);W32(0x3C,v);}
static uint32_t smnR(uint32_t a){W32(0x38,a);R32(0x38);return R32(0x3C);}

/* MP0 C2PMSG registers via SMN */
#define C2PMSG_64  0x1A350  /* via SMN: 0x38=addr, 0x3C=data */
static uint32_t c2pmsg64_read(void){ return smnR(0x03B10A68); }

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE){printf("open FAIL gle=%lu\n",GetLastError());return 1;}

    AMDBC250_IOCTL_INIT_HARDWARE ih;
    DWORD br=0;
    memset(&ih,0,sizeof(ih));
    ih.MmioPhysicalBase=0xFE800000ULL;
    ih.MmioSize=0x80000;
    ih.Flags=AMDBC250_INIT_FLAG_NBIO_MAP;
    if(!DeviceIoControl(h,IOCTL_AMDBC250_INIT_HARDWARE,&ih,sizeof(ih),&ih,sizeof(ih),&br,NULL)){printf("INIT fail\n");return 1;}
    printf("INIT OK\n");

    /* Check SOS alive via C2PMSG_81 */
    uint32_t sol = smnR(0x03B10A48);  /* C2PMSG_81 */
    printf("C2PMSG_81 (SOS) = 0x%08X %s\n", sol, (sol & 0x80000000) ? "ALIVE" : "dead");

    /* Check C2PMSG_64 TOS ready flag */
    uint32_t c2p64 = c2pmsg64_read();
    printf("C2PMSG_64 = 0x%08X (bit31=%d) %s\n", c2p64, (c2p64>>31)&1, (c2p64 & 0x80000000)?"TOS READY!":"not ready");

    /* Try PSP ring create manually */
    printf("\n=== Manual PSP Ring Create ===\n");
    printf("Step 1: Wait TOS ready (C2PMSG_64 bit31)...\n");
    int timeout=50;
    while(timeout-->0){
        c2p64 = c2pmsg64_read();
        if(c2p64 & 0x80000000){printf("  TOS READY after %d polls!\n",50-timeout);break;}
        Sleep(100);
    }
    if(timeout<=0){printf("  TOS ready flag NEVER set (timeout) — SOS lacks TOS component\n");}

    /* Read SPI_PG */
    uint32_t spi = R32(0x5C3C);
    printf("\nSPI_PG (0x5C3C) = 0x%08X\n", spi);

    /* Try direct SPI write again */
    printf("\n=== Direct SPI write ===\n");
    W32(0x5C3C, 0x1F);
    spi = R32(0x5C3C);
    printf("SPI_PG after write 0x1F: 0x%08X %s\n", spi, (spi==0x1F)?"UNLOCKED!":"locked");

    printf("\n=== DONE ===\n");
    CloseHandle(h);
    return 0;
}
