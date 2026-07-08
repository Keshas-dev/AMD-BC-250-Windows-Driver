/* wake-gfx-v2.c — Wake GFX using force_gfx_vid + force_gfx_freq (from bc250-collective) */
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
static uint32_t smnR(uint32_t a) { W32(0x38,a); R32(0x38); return R32(0x3C); }
static void smnW(uint32_t a, uint32_t v) { W32(0x38,a); W32(0x3C,v); }

/* Queue 0 C2PMSG mailbox */
#define Q0_CMD  0x03B10A08
#define Q0_RSP  0x03B10A68
#define Q0_ARG  0x03B10A48

static int q0Send(int msg, uint32_t arg) {
    uint32_t c=smnR(Q0_RSP);
    if(c==1) smnW(Q0_RSP,0);
    else if(c!=0) smnW(Q0_RSP,0);
    smnW(Q0_ARG,arg);
    smnW(Q0_CMD,msg);
    for(int i=0;i<1500;i++){
        c=smnR(Q0_RSP);
        if(c==1){Sleep(2);return (int)smnR(Q0_ARG);}
        if(c!=0) return -((int)c);
        Sleep(1);
    }
    return -100;
}

/* Queue 3 C2PMSG mailbox */
#define Q3_CMD  0x03B10A20
#define Q3_RSP  0x03B10A80
#define Q3_ARG  0x03B10A88

static int q3Send(int msg, uint32_t arg) {
    uint32_t c=smnR(Q3_RSP);
    if(c==1) smnW(Q3_RSP,0);
    else if(c!=0) smnW(Q3_RSP,0);
    smnW(Q3_ARG,arg);
    smnW(Q3_CMD,msg);
    for(int i=0;i<1500;i++){
        c=smnR(Q3_RSP);
        if(c==1){Sleep(2);return (int)smnR(Q3_ARG);}
        if(c!=0) return -((int)c);
        Sleep(1);
    }
    return -100;
}

/* Queue 2 SMN_INDEX/DATA/ARGRSP */
#define Q2_RSP  0x03B10564
#define Q2_ARG  0x03B10998
#define Q2_CMD  0x03B10528

static int q2Send(int msg, uint32_t arg, uint32_t argHigh) {
    smnW(Q2_RSP,0);
    smnW(Q2_ARG,arg);
    smnW(Q2_ARG+4,argHigh);
    smnW(Q2_CMD,msg);
    for(int i=0;i<1500;i++){
        uint32_t st=smnR(Q2_RSP);
        if(st==1) return (int)smnR(Q2_ARG);
        if(st==0xFF) return -1; if(st==0xFE) return -2; if(st==0xFD) return -3;
        if(st!=0) return -((int)st);
        Sleep(1);
    }
    return -100;
}

/* mV <-> VID conversion (bc250-collective codec.py formulas, integer math)
   vid = round((1.55 - mV/1000) / 0.00625) = ((1550 - mV) * 4 + 12) / 25
   mV  = round((-vid * 0.00625 + 1.55) * 1000) = (31010 - vid * 125) / 20 */
static int mvToVid(int mv) { return ((1550-mv)*4+12)/25; }
static int vidToMv(int vid) { return (31010-vid*125)/20; }

static void state() {
    printf("  Freq=%d MHz  Vid=%d mV(%d)  Wgp=%d  Feat=0x%08X  Vers=0x%08X\n",
        q0Send(0x37,0), vidToMv(q0Send(0x38,0)), q0Send(0x38,0),
        q0Send(0x1E,0), q0Send(0x3D,0), q0Send(0x02,0));
}

int main() {
    setvbuf(stdout,NULL,_IONBF,0);
    h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE){printf("FAIL gle=%lu\n",GetLastError());return 1;}
    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br=0;
    ZeroMemory(&ih,sizeof(ih)); ih.MmioPhysicalBase=0xFE800000ULL; ih.MmioSize=0x80000; ih.Flags=AMDBC250_INIT_FLAG_NBIO_MAP;
    if(!DeviceIoControl(h,IOCTL_AMDBC250_INIT_HARDWARE,&ih,sizeof(ih),&ih,sizeof(ih),&br,NULL)){printf("INIT fail\n");return 1;}
    printf("INIT OK\n\nInitial state: "); state();

    /* Step 1: Cleanup stale overrides (governor init) */
    printf("\n=== Cleanup ===\n");
    q0Send(0x3A,0); /* unforce_gfx_freq */
    q0Send(0x3C,0); /* unforce_gfx_vid */
    printf("After cleanup: "); state();

    /* Step 2: Try Queue 3 perf profile + Queue 0 voltage/freq (governor sequence) */
    printf("\n=== Governor sequence: Q3-set-perf + Q0-vid+freq ===\n");

    /* Safe point 1: 1175 MHz @ 850 mV, perf_profile=3 */
    int targetMhz=1175;
    int targetMv=850;
    int perProf=3;

    printf("Target: %d MHz @ %d mV (VID=%d), perf_profile=%d\n",targetMhz,targetMv,mvToVid(targetMv),perProf);

    /* Queue 3: Set perf profile index */
    printf("Q3[0x1E] set_perf_profile(%d): ",perProf);
    int r3=q3Send(0x1E,perProf);
    printf("%d\n",r3);
    Sleep(20);

    /* Queue 0: Force voltage */
    int vid=mvToVid(targetMv);
    printf("Q0[0x3B] force_gfx_vid(%d mV -> VID=%d): ",targetMv,vid);
    int r1=q0Send(0x3B,vid);
    printf("%d\n",r1);
    Sleep(50);

    /* Queue 0: Force frequency */
    printf("Q0[0x39] force_gfx_freq(%d MHz): ",targetMhz);
    int r2=q0Send(0x39,targetMhz);
    printf("%d\n",r2);
    Sleep(200);

    printf("After force: "); state();

    /* Step 3: Try lower safe point */
    printf("\n=== Lower safe point: 800 MHz @ 800 mV, perf_profile=1 ===\n");
    q3Send(0x1E,1);
    q0Send(0x3B,mvToVid(800));
    q0Send(0x39,800);
    Sleep(200);
    printf("After lower: "); state();

    /* Step 4: Try Queue 2 disable GFXOFF */
    printf("\n=== Queue 2: disable_smu_features(GFXOFF=bit2) ===\n");
    int q2r=q2Send(6,4,0); /* disable: mask=bit2 */
    printf("  Q2[0x06] disable(GFXOFF): %d\n",q2r);
    Sleep(50);
    printf("  After Q2: "); state();

    /* Re-try freq after disabling GFXOFF */
    printf("\n=== Re-try freq after Q2 ===\n");
    q0Send(0x39,800);
    Sleep(200);
    printf("  After re-force: "); state();

    /* Step 5: Try even higher voltage with moderate freq */
    printf("\n=== Safe point: 1000 MHz @ 800 mV perf=3 ===\n");
    q3Send(0x1E,3);
    q0Send(0x3B,mvToVid(800));
    q0Send(0x39,1000);
    Sleep(200);
    printf("  After 1000: "); state();

    /* Cleanup */
    printf("\n=== Cleanup ===\n");
    q0Send(0x3A,0);
    q0Send(0x3C,0);
    /* Re-enable GFXOFF */
    q2Send(5,4,0);
    printf("Final: "); state();

    CloseHandle(h);
    printf("DONE\n");
    return 0;
}