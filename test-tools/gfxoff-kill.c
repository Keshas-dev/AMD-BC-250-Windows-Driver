/* gfxoff-kill.c — disable GFXOFF + try compute wake, GRBM_STATUS monitoring */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE h;

static BOOL W32(uint32_t o, uint32_t v) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=v; return DeviceIoControl(h,IOCTL_AMDBC250_WRITE_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL); }
static uint32_t R32(uint32_t o) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=0; if(DeviceIoControl(h,IOCTL_AMDBC250_READ_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL)) return r.Value; return 0xFFFFFFFF; }
static void smnW(uint32_t a,uint32_t v){W32(0x38,a);W32(0x3C,v);}
static uint32_t smnR(uint32_t a){W32(0x38,a);R32(0x38);return R32(0x3C);}

/* All queues: return 1=OK, -1=FAIL(0xFF), -2=UNKNOWN(0xFE), -3=REJECTED(0xFD), -4=BUSY(0xFC), -100=TIMEOUT */

static int q0(uint32_t msg,uint32_t arg){
    smnW(0x03B10A68,0); smnW(0x03B10A48,arg); smnW(0x03B10A08,msg);
    for(int i=0;i<500;i++){ uint32_t st=smnR(0x03B10A68); if(st==1) return 1; if(st==0xFF) return -1; if(st==0xFE) return -2; if(st==0xFD) return -3; if(st==0xFC) return -4; Sleep(1); }
    return -100;
}
static uint32_t q0_arg(){ return smnR(0x03B10A48); }

static int q2(uint32_t msg,uint32_t arg,uint32_t arg_h){
    smnW(0x03B10564,0); smnW(0x03B10998,arg); smnW(0x03B1099C,arg_h); smnW(0x03B10528,msg);
    for(int i=0;i<500;i++){ uint32_t st=smnR(0x03B10564); if(st==1) return 1; if(st==0xFF) return -1; if(st==0xFE) return -2; if(st==0xFD) return -3; if(st==0xFC) return -4; Sleep(1); }
    smnW(0x03B10564,0); return -100;
}
static uint32_t q2_arg(){ return smnR(0x03B10998); }

static int q3(uint32_t msg,uint32_t arg){
    smnW(0x03B10A80,0); smnW(0x03B10A88,arg); smnW(0x03B10A20,msg);
    for(int i=0;i<500;i++){ uint32_t st=smnR(0x03B10A80); if(st==1) return 1; if(st==0xFF) return -1; if(st==0xFE) return -2; if(st==0xFD) return -3; if(st==0xFC) return -4; Sleep(1); }
    return -100;
}
static uint32_t q3_arg(){ return smnR(0x03B10A88); }

int main(){
    setvbuf(stdout,NULL,_IONBF,0);
    h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE){printf("FAIL gle=%lu\n",GetLastError());return 1;}
    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br=0;
    ZeroMemory(&ih,sizeof(ih)); ih.MmioPhysicalBase=0xFE800000ULL; ih.MmioSize=0x80000; ih.Flags=AMDBC250_INIT_FLAG_NBIO_MAP;
    if(!DeviceIoControl(h,IOCTL_AMDBC250_INIT_HARDWARE,&ih,sizeof(ih),&ih,sizeof(ih),&br,NULL)){printf("INIT fail\n");return 1;}

    printf("=== GFXOFF KILL + GRBM_STATUS ===\n\n");

    /* 1. Verify SMU alive via Q3 test msg */
    int r=q3(1,123); uint32_t tst=q3_arg();
    printf("SMU test: status=%d resp=%u\n",r,tst);
    if(r!=1){printf("SMU dead\n");CloseHandle(h);return 1;}

    /* 2. Register peek — GRBM_STATUS aliases */
    printf("\n=== GRBM_STATUS register probes ===\n");
    uint32_t grbm_2000=R32(0x2000);   /* plain BAR5 offset */
    uint32_t grbm_2004=R32(0x2004);   /* what governor reads */
    uint32_t grbm_3260=R32(0x3260);   /* GC_BASE + 0x800*4 */
    uint32_t grbm_3264=R32(0x3264);   /* GC_BASE + 0x801*4 */
    printf("BAR5+0x2000=0x%08X  0x2004=0x%08X\n",grbm_2000,grbm_2004);
    printf("BAR5+0x3260=0x%08X  0x3264=0x%08X\n",grbm_3260,grbm_3264);
    printf("GUI_ACTIVE@0x2004 bit31=%u\n",(grbm_2004>>31)&1);

    /* 3. Before state */
    r=q0(0x37,0); uint32_t freq=q0_arg();
    r=q0(0x1E,0); uint32_t wgp=q0_arg();
    r=q0(0x3D,0); uint32_t feat=q0_arg();
    printf("\nBefore: Freq=%u Wgp=%u Feat=0x%08X GFXOFF=%s\n",freq,wgp,feat,(feat&4)?"ON":"OFF");

    /* 4. Try Queue 2 test + disable GFXOFF */
    printf("\n=== Queue 2: disable GFXOFF ===\n");
    r=q2(3,0,0); printf("Q2 test msg=0x03 -> %d (expect 1)\n",r);
    if(r==1){ uint32_t c=q2_arg(); printf("  Q2_0x03 constant = %u (expect 23)\n",c); }

    r=q2(4,0,0); /* device name chunk index 0 */
    uint32_t devname=q2_arg();
    printf("Q2 device_name[0]=0x%08X ('%c%c%c%c')\n",devname,
        (char)(devname&0xFF),(char)((devname>>8)&0xFF),(char)((devname>>16)&0xFF),(char)((devname>>24)&0xFF));

    /* Disable GFXOFF: mask_low=bit2=4, mask_high=0 */
    printf("Sending disable_smu_features(mask=4) for GFXOFF...\n");
    r=q2(6,4,0);
    printf("Q2 disable GFXOFF -> %d\n",r);
    Sleep(200);

    /* Check if features changed */
    r=q0(0x3D,0); uint32_t feat2=q0_arg();
    r=q0(0x37,0); uint32_t freq2=q0_arg();
    r=q0(0x1E,0); uint32_t wgp2=q0_arg();
    uint32_t grbm2=R32(0x2004);
    printf("After disable: Feat=0x%08X GFXOFF=%s Freq=%u Wgp=%u GUI_ACTIVE=%u\n",
        feat2,(feat2&4)?"ON":"OFF",freq2,wgp2,(grbm2>>31)&1);

    if(!(feat2&4)) printf("*** GFXOFF DISABLED! ***\n");
    if(wgp2>0) printf("*** WGPs ACTIVE: %u ***\n",wgp2);

    /* 5. Also try force_freq @ 2000 MHz with GFXOFF off */
    if(!(feat2&4)){
        printf("\n=== GFXOFF off, try force 2000 MHz ===\n");
        q3(0x1E,3); Sleep(50);
        int vid=(int)((1.55-850.0/1000.0)/0.00625+0.5); /* 850 mV */
        q0(0x3B,vid); Sleep(50);
        r=q0(0x39,2000); printf("force 2000MHz -> %d\n",r); Sleep(200);
        r=q0(0x37,0); uint32_t freq3=q0_arg();
        r=q0(0x1E,0); uint32_t wgp3=q0_arg();
        uint32_t grbm3=R32(0x2004);
        printf("After force: Freq=%u Wgp=%u GUI_ACTIVE=%u\n",freq3,wgp3,(grbm3>>31)&1);
    }

    /* 6. Restore GFXOFF + unforce */
    printf("\n=== Restore ===\n");
    q2(5,4,0); /* enable GFXOFF */
    q0(0x3A,0); q0(0x3C,0);
    Sleep(100);
    r=q0(0x37,0); uint32_t final_freq=q0_arg(); printf("Final Freq=%u\n",final_freq);
    r=q0(0x3D,0); uint32_t final_feat=q0_arg(); printf("Final Feat=0x%08X GFXOFF=%s\n",final_feat,(final_feat&4)?"ON":"OFF");

    CloseHandle(h);
    return 0;
}