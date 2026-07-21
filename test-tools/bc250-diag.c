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
    for(int i=0;i<500;i++){ uint32_t st=smnR(0x03B10A68); if(st==1) return 1; if(st==0xFF) return -1; Sleep(1); }
    return -100;
}
static uint32_t q0_arg(){ return smnR(0x03B10A48); }

#define LINE() printf("----------------------------------------\n")
int main(){
    LINE();
    printf("BC-250 Diagnostic Tool\n");
    LINE();

    h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE){printf("FAIL: Driver not found (gle=%lu)\n",GetLastError());return 1;}
    printf("[OK] Driver connected\n");

    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br=0;
    ZeroMemory(&ih,sizeof(ih)); ih.MmioPhysicalBase=0xFE800000ULL; ih.MmioSize=0x80000; ih.Flags=AMDBC250_INIT_FLAG_NBIO_MAP;
    if(!DeviceIoControl(h,IOCTL_AMDBC250_INIT_HARDWARE,&ih,sizeof(ih),&ih,sizeof(ih),&br,NULL)){printf("FAIL: INIT_HW (gle=%lu)\n",GetLastError());CloseHandle(h);return 1;}
    printf("[OK] BAR5 mapped (0x%llX/0x%X)\n",ih.MmioPhysicalBase,ih.MmioSize);

    /* GPU basic */
    uint32_t id=R32(0x0000), scr=R32(0x32D4);
    printf("\n  GPU_ID:      0x%08X",id);
    if((id&0xFFFF)==0x13FE) printf(" (1002:13FE BC-250)");
    printf("\n  SCRATCH0:    0x%08X (%c%c%c%c)\n",scr,scr&0xFF,(scr>>8)&0xFF,(scr>>16)&0xFF,(scr>>24)&0xFF);

    /* SMU alive test */
    smnW(0x03B10A80,0); smnW(0x03B10A88,123); smnW(0x03B10A20,1);
    int alive=0; for(int wi=0;wi<500;wi++){uint32_t st=smnR(0x03B10A80);if(st==1){alive=1;break;}if(st==0xFF)break;Sleep(1);}
    uint32_t tst=smnR(0x03B10A88);
    if(alive&&tst==124) printf("[OK] SMU alive\n");
    else {printf("[FAIL] SMU dead (%d,%u)\n",alive,tst);CloseHandle(h);return 1;}

    /* SMU version */
    q0(2,0); uint32_t ver=q0_arg();
    q0(3,0); uint32_t drv=q0_arg();
    printf("  SMU v%u.%u.%u (driver_if=%u)\n",(ver>>16)&0xFF,(ver>>8)&0xFF,ver&0xFF,drv);

    /* Telemetry */
    q0(0x37,0); uint32_t freq=q0_arg();
    q0(0x38,0); int vid=q0_arg();
    q0(0x1E,0); uint32_t wgp=q0_arg();
    q0(0x3D,0); uint32_t feat=q0_arg();
    q0(0x0F,0); uint32_t gfxclk=q0_arg();
    int mv=(vid>=0&&vid<=255)?(int)((-vid*0.00625+1.55)*1000+0.5):0;
    printf("  Frequency:   %u MHz (GFXCLK=%u MHz)\n",freq,gfxclk);
    printf("  Voltage:     VID=%d (%d mV)\n",vid,mv);
    printf("  WGPs:        %u\n",wgp);
    printf("  Features:    0x%08X",feat);
    printf(" (GFXCLK=%s, GFXOFF=%s, CG=%s, PG=%s)\n",
        (feat&1)?"ON":"OFF",(feat&4)?"ON":"OFF",
        (feat&8)?"ON":"OFF",(feat&16)?"ON":"OFF");

    /* GRBM */
    uint32_t grbm=R32(0x3260);
    printf("\n  GRBM_STATUS: 0x%08X (GUI=%d IA=%d WD=%d CB=%d)\n",
        grbm,(grbm>>31)&1,(grbm>>19)&1,(grbm>>18)&1,(grbm>>17)&1);

    /* Key registers */
    printf("  CC_ARRAY:    0x%08X\n",R32(0x9C1C));
    printf("  SPI_PG_EN:   0x%08X\n",R32(0x5C3C));
    printf("  RLC_PG:      0x%08X\n",R32(0x3D64));
    printf("  GRBM_IDX:    0x%08X\n",R32(0x34D0));

    /* SOS status (PSP C2PMSG_81 at 0x10614) */
    uint32_t c2p81=R32(0x10614);
    printf("  C2PMSG_81:   0x%08X (SOS %s)\n",c2p81,(c2p81&0xF0000010)?"alive":"dead");

    /* Temperatures via SMN */
    uint32_t memtemp=smnR(0x03B10028);
    printf("  Mem Temp:    0x%04X (%u C?)\n",memtemp,memtemp/100);
    uint32_t edge=smnR(0x03B10000);
    printf("  Edge Temp:   0x%08X%s\n",edge,edge==0xFFFFFFFF?" (dead)":"");

    /* SMU mailbox state */
    printf("\n  SMU Mailbox: C2PMSG_66=0x%08X _82=0x%08X _90=0x%08X\n",
        smnR(0x03B10A08),smnR(0x03B10A48),smnR(0x03B10A68));

    LINE();
    printf("Result: BC-250 v%u.%u.%u @ %u MHz %d mV WGPs=%u\n",
        (ver>>16)&0xFF,(ver>>8)&0xFF,ver&0xFF,freq,mv,wgp);
    LINE();
    CloseHandle(h);
    return 0;
}
