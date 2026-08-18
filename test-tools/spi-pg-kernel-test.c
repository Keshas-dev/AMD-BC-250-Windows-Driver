/* spi-pg-kernel-test.c - try writing SPI_PG from kernel context via driver IOCTL loop */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE h;
static BOOL W32(uint32_t o, uint32_t v) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=v; return DeviceIoControl(h,IOCTL_AMDBC250_WRITE_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL); }
static uint32_t R32(uint32_t o) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=0; if(DeviceIoControl(h,IOCTL_AMDBC250_READ_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL)) return r.Value; return 0xFFFFFFFF; }

static void smnW(uint32_t a,uint32_t v){W32(0x38,a);W32(0x3C,v);}
static uint32_t smnR(uint32_t a){W32(0x38,a);R32(0x38);return R32(0x3C);}

static int q0(uint32_t msg,uint32_t arg){
    smnW(0x03B10A68,0); smnW(0x03B10A48,arg); smnW(0x03B10A08,msg);
    for(int i=0;i<500;i++){uint32_t st=smnR(0x03B10A68);if(st==1)return 1;if(st==0xFF)return -1;if(st==0xFE)return -2;if(st==0xFD)return -3;if(st==0xFC)return -4;Sleep(1);}return -100;
}
static uint32_t q0_arg(){return smnR(0x03B10A48);}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE){printf("FAIL: open err=%lu\n",GetLastError());return 1;}

    AMDBC250_IOCTL_INIT_HARDWARE ih;
    DWORD br=0;
    memset(&ih,0,sizeof(ih));
    ih.MmioPhysicalBase=0xFE800000ULL;
    ih.MmioSize=0x80000;
    ih.Flags=AMDBC250_INIT_FLAG_NBIO_MAP;
    if(!DeviceIoControl(h,IOCTL_AMDBC250_INIT_HARDWARE,&ih,sizeof(ih),&ih,sizeof(ih),&br,NULL)){printf("INIT fail\n");return 1;}
    printf("INIT OK\n");

    /* Query baseline */
    int r=q0(0x1E,0); uint32_t baseWgp=q0_arg();
    printf("Baseline: Wgp=%u SPI_PG=0x%08X RLC_PG=0x%08X GRBM=0x%08X\n", baseWgp, R32(0x5C3C), R32(0x3D64), R32(0x2000));

    /* Test 1: Write CC + SPI + RLC together */
    printf("\n=== Test 1: Write CC=0xFFE00000 + SPI=0x1F + RLC=0x1F ===\n");
    W32(0x9C1C, 0xFFE00000);
    W32(0x5C3C, 0x1F);
    W32(0x3D64, 0x1F);
    printf("After write: SPI_PG=0x%08X RLC_PG=0x%08X CC=0x%08X\n", R32(0x5C3C), R32(0x3D64), R32(0x9C1C));
    r=q0(0x1E,0); printf("QueryActiveWgp=%u\n",q0_arg());

    /* Test 2: Continuous write loop (maybe we can catch a window) */
    printf("\n=== Test 2: Continuous write x100 ===\n");
    int stuck = 0;
    for(int i=0; i<100; i++){
        W32(0x5C3C, 0x1F);
        uint32_t rb = R32(0x5C3C);
        if(rb == 0x1F){printf("  [%d] SPI_PG=0x1F STUCK!\n",i); stuck++;}}
    if(stuck==0) printf("  SPI_PG never stuck (always 0x%08X)\n", R32(0x5C3C));

    /* Test 3: Write with different values */
    printf("\n=== Test 3: Different SPI values ===\n");
    uint32_t vals[] = {0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF, 0x1FF, 0xFFFF, 0xFFFFFFFF};
    for(int i=0; i<9; i++){
        W32(0x5C3C, vals[i]);
        uint32_t rb = R32(0x5C3C);
        printf("  Wrote 0x%08X → readback 0x%08X %s\n", vals[i], rb, (rb==vals[i])?"STUCK!":"");
    }

    /* Test 4: Read RLC_PG (maybe it's not 0xFFFFFFFF now) */
    printf("\n=== Test 4: RLC_PG after writes ===\n");
    printf("RLC_PG_ALWAYS_ON (0x3D64) = 0x%08X\n", R32(0x3D64));

    /* Test 5: QueryActiveWgp after all tests */
    printf("\n=== Test 5: Final state ===\n");
    r=q0(0x1E,0); printf("QueryActiveWgp = %u\n", q0_arg());
    printf("SPI_PG = 0x%08X\n", R32(0x5C3C));
    printf("GRBM_STATUS = 0x%08X\n", R32(0x2000));

    printf("\n=== DONE ===\n");
    CloseHandle(h);
    return 0;
}
