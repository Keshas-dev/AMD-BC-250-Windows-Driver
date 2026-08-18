/* wgp-smu-test.c - safely try SMU messages that might affect WGP power */
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
    for(int i=0;i<1000;i++){uint32_t st=smnR(0x03B10A68);if(st==1)return 1;if(st==0xFF)return -1;if(st==0xFE)return -2;if(st==0xFD)return -3;if(st==0xFC)return -4;Sleep(1);}return -100;
}
static uint32_t q0_arg(){return smnR(0x03B10A48);}

static void showState(const char *tag){
    uint32_t spi=R32(0x5C3C), cc=R32(0x9C1C), grbm=R32(0x2000);
    printf("  [%s] SPI_PG=0x%08X CC=0x%08X GRBM=%08X\n", tag, spi, cc, grbm);
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE){printf("FAIL: open err=%lu\n",GetLastError());return 1;}
    printf("=== WGP SMU message test ===\n");

    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br=0;
    memset(&ih,0,sizeof(ih)); ih.MmioPhysicalBase=0xFE800000ULL; ih.MmioSize=0x80000; ih.Flags=AMDBC250_INIT_FLAG_NBIO_MAP;
    if(!DeviceIoControl(h,IOCTL_AMDBC250_INIT_HARDWARE,&ih,sizeof(ih),&ih,sizeof(ih),&br,NULL)){printf("INIT fail\n");return 1;}

    /* SMU sanity */
    int r=q0(1,123); uint32_t tst=q0_arg();
    printf("SMU test(123): status=%d resp=%u %s\n",r,tst,(r==1&&tst==124)?"OK":"DEAD");
    if(r!=1||tst!=124){CloseHandle(h);return 1;}

    showState("initial");

    /* Query current WGP count (safe) */
    r=q0(0x1E,0); uint32_t wgp=q0_arg();
    printf("\nQueryActiveWgp(0x1E): status=%d Wgp=%u\n",r,wgp);

    /* Try RequestActiveWgp (0x18) - DANGER: may crash SMU */
    printf("\n=== RequestActiveWgp (0x18) - DANGER ===\n");
    printf("Sending 0x18 with arg=0x1F (request 5 WGPs)...\n");
    r=q0(0x1E,0); wgp=q0_arg();
    printf("  Before: Wgp=%u\n",wgp);
    r=q0(0x18,0x1F);
    printf("  0x18(0x1F) -> status=%d\n",r);
    Sleep(500);
    r=q0(0x1E,0); wgp=q0_arg();
    printf("  After: Wgp=%u\n",wgp);
    showState("after 0x18");

    /* Query features to see if anything changed */
    r=q0(0x3D,0); uint32_t feat=q0_arg();
    printf("\nFeatures: 0x%08X (GFXOFF=%d CG=%d PG=%d)\n", feat, (feat>>2)&1,(feat>>3)&1,(feat>>4)&1);

    /* Try DisableSmuFeatures for bit 2 (GFXOFF) via Q2 - safe */
    printf("\n=== Disable GFXOFF (safe, via Q2) ===\n");
    smnW(0x03B10564,0); smnW(0x03B10998,(1<<2)); smnW(0x03B1099C,0); smnW(0x03B10528,6);
    Sleep(200);
    r=q0(0x3D,0); feat=q0_arg();
    printf("Features after GFXOFF off: 0x%08X (GFXOFF=%d)\n", feat, (feat>>2)&1);
    r=q0(0x1E,0); wgp=q0_arg();
    printf("Wgp after GFXOFF off: %u\n",wgp);
    showState("after gfxoff off");

    printf("\n=== DONE ===\n");
    CloseHandle(h);
    return 0;
}
