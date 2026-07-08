/* governor-sequence.c — exact governor init + change_freq sequence via Queue 0 + Queue 3 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE h;

static BOOL W32(uint32_t o, uint32_t v) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=v;
    return DeviceIoControl(h,IOCTL_AMDBC250_WRITE_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL);
}
static uint32_t R32(uint32_t o) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=0;
    if(DeviceIoControl(h,IOCTL_AMDBC250_READ_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL)) return r.Value;
    return 0xFFFFFFFF;
}
static void smnW(uint32_t a,uint32_t v){W32(0x38,a);W32(0x3C,v);}
static uint32_t smnR(uint32_t a){W32(0x38,a);R32(0x38);return R32(0x3C);}

/* All queues return: 1=OK, -1=FAIL(0xFF), -2=UNKNOWN(0xFE), -3=REJECTED(0xFD), -4=BUSY(0xFC), -100=TIMEOUT */

/* Queue 0: cmd=0x03B10A08 rsp=0x03B10A68 arg=0x03B10A48 */
static int q0(uint32_t msg,uint32_t arg){
    smnW(0x03B10A68,0); smnW(0x03B10A48,arg); smnW(0x03B10A08,msg);
    for(int i=0;i<500;i++){
        uint32_t st=smnR(0x03B10A68);
        if(st==1) return 1; if(st==0xFF) return -1; if(st==0xFE) return -2;
        if(st==0xFD) return -3; if(st==0xFC) return -4; Sleep(1);
    }
    return -100;
}
static uint32_t q0_arg(void){ return smnR(0x03B10A48); }

/* Queue 3: cmd=0x03B10A20 rsp=0x03B10A80 arg=0x03B10A88 */
static int q3(uint32_t msg,uint32_t arg){
    smnW(0x03B10A80,0); smnW(0x03B10A88,arg); smnW(0x03B10A20,msg);
    for(int i=0;i<500;i++){
        uint32_t st=smnR(0x03B10A80);
        if(st==1) return 1; if(st==0xFF) return -1; if(st==0xFE) return -2;
        if(st==0xFD) return -3; if(st==0xFC) return -4; Sleep(1);
    }
    return -100;
}
static uint32_t q3_arg(void){ return smnR(0x03B10A88); }

static int mv_to_vid(int mv){
    return (int)((1.55 - (double)mv/1000.0) / 0.00625 + 0.5);
}

int main(){
    setvbuf(stdout,NULL,_IONBF,0);
    h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE){printf("FAIL gle=%lu\n",GetLastError());return 1;}
    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br=0;
    ZeroMemory(&ih,sizeof(ih)); ih.MmioPhysicalBase=0xFE800000ULL; ih.MmioSize=0x80000; ih.Flags=AMDBC250_INIT_FLAG_NBIO_MAP;
    if(!DeviceIoControl(h,IOCTL_AMDBC250_INIT_HARDWARE,&ih,sizeof(ih),&ih,sizeof(ih),&br,NULL)){printf("INIT fail\n");return 1;}
    printf("=== Governor Init Sequence ===\n");

    /* 1. Test message via Queue 3 */
    int r=q3(1,123); uint32_t tst=q3_arg();
    printf("Q3 test(123): status=%d resp=%u (expect 124)\n",r,tst);
    if(r!=1||tst!=124){printf("Q3 dead, try Q0: "); r=q0(1,123); uint32_t q0tst=q0_arg(); printf("status=%d resp=%u\n",r,q0tst);}
    if(r!=1){printf("SMU not responding\n");CloseHandle(h);return 1;}

    /* 2. Before state */
    r=q0(0x37,0); uint32_t freq=q0_arg();
    r=q0(0x1E,0); uint32_t wgp=q0_arg();
    r=q0(0x3D,0); uint32_t feat=q0_arg();
    r=q0(0x38,0); int vid_cur=q0_arg(); /* -2 likely means not supported, ignore */
    printf("Before: Freq=%u Wgp=%u Feat=0x%08X GFXOFF=%s\n",freq,wgp,feat,(feat&4)?"ON":"OFF");

    /* 3. Governor init — unforce, set temperature */
    printf("\n--- Governor init ---\n");
    r=q3(0x8C,80);     printf("set_gpu_max_temp(80C)=%d\n",r);
    r=q0(0x3A,0);      printf("unforce_gfx_freq=%d\n",r);
    r=q0(0x3C,0);      printf("unforce_gfx_vid=%d (expect failures ok)\n",r);

    /* 4. change_freq: profile -> voltage -> freq */
    int target_mhz=1175; int target_mv=850;
    int vid=mv_to_vid(target_mv);
    printf("\n--- change_freq(%d MHz, %d mV, VID=%d) ---\n",target_mhz,target_mv,vid);

    r=q3(0x1E,3);      printf("set_perf_profile(3)=%d\n",r);
    if(r!=1){printf("FAIL: profile\n");CloseHandle(h);return 1;}
    Sleep(50);

    r=q0(0x3B,vid);    printf("force_gfx_vid(%dmV->VID=%d)=%d\n",target_mv,vid,r);
    if(r!=1){printf("FAIL: voltage\n");CloseHandle(h);return 1;}
    Sleep(50);

    r=q0(0x39,target_mhz); printf("force_gfx_freq(%dMHz)=%d\n",target_mhz,r);
    if(r!=1){
        printf("trying lower freq...\n");
        r=q0(0x39,1000); printf("force_gfx_freq(1000MHz)=%d\n",r);
        if(r!=1){printf("FAIL: even 1000MHz failed\n");CloseHandle(h);return 1;}}
    Sleep(200);

    /* 5. After state */
    r=q0(0x37,0); uint32_t freq2=q0_arg();
    r=q0(0x1E,0); uint32_t wgp2=q0_arg();
    r=q0(0x3D,0); uint32_t feat2=q0_arg();
    printf("After:  Freq=%u (%+d) Wgp=%u Feat=0x%08X GFXOFF=%s\n",
        freq2,(int)freq2-(int)freq,wgp2,feat2,(feat2&4)?"ON":"OFF");

    if(freq2>freq) printf("*** FREQ ROSE from %u to %u! GFX WAKING! ***\n",freq,freq2);
    else if(freq2>=target_mhz-50) printf("*** GFX WAKE SUCCESS! %u MHz ***\n",freq2);
    else printf("Freq unchanged at %u\n",freq2);

    if(wgp2>0) printf("*** WGPs ACTIVE: %u ***\n",wgp2);
    if(!(feat2&4)) printf("*** GFXOFF DISABLED! ***\n");

    /* 6. Restore */
    printf("\n--- Restore ---\n");
    q0(0x3A,0); q0(0x3C,0);
    r=q0(0x37,0); uint32_t freq3=q0_arg();
    printf("Final Freq=%u\n",freq3);

    CloseHandle(h);
    return 0;
}