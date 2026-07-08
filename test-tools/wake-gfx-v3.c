/* wake-gfx-v3.c — SAFE: only disable GFXOFF via Queue 2, no force_freq/voltage */
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

/* Queue 0 C2PMSG */
static int q0Send(int msg, uint32_t arg) {
    uint32_t c=smnR(0x03B10A68);
    if(c==1) smnW(0x03B10A68,0); else if(c!=0) smnW(0x03B10A68,0);
    smnW(0x03B10A48,arg); smnW(0x03B10A08,msg);
    for(int i=0;i<1500;i++){
        c=smnR(0x03B10A68);
        if(c==1){Sleep(2);return (int)smnR(0x03B10A48);}
        if(c!=0) return -((int)c); Sleep(1);
    }
    return -100;
}

/* Queue 2 SMN_INDEX/DATA/ARGRSP — with proper cleanup */
static int q2Send(int msg, uint32_t arg, uint32_t argHigh) {
    /* Clear any stale response state */
    uint32_t st=smnR(0x03B10564);
    if(st!=0) smnW(0x03B10564,0);
    smnW(0x03B10998,arg); smnW(0x03B1099C,argHigh);
    smnW(0x03B10528,msg);
    for(int i=0;i<2000;i++){
        st=smnR(0x03B10564);
        if(st==1) return (int)smnR(0x03B10998);
        if(st==0xFF) return -1;
        if(st==0xFE) return -2;
        if(st==0xFD) return -3;
        if(st!=0&&st!=0xFC) smnW(0x03B10564,0); /* clear unexpected */
        Sleep(1);
    }
    /* Timeout — try to clear */
    smnW(0x03B10564,0);
    return -100;
}

static int mvToVid(int mv) { return ((1550-mv)*4+12)/25; }
static int vidToMv(int vid) { return (31010-vid*125)/20; }

int main() {
    setvbuf(stdout,NULL,_IONBF,0);
    h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE){printf("FAIL gle=%lu\n",GetLastError());return 1;}
    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br=0;
    ZeroMemory(&ih,sizeof(ih)); ih.MmioPhysicalBase=0xFE800000ULL; ih.MmioSize=0x80000; ih.Flags=AMDBC250_INIT_FLAG_NBIO_MAP;
    if(!DeviceIoControl(h,IOCTL_AMDBC250_INIT_HARDWARE,&ih,sizeof(ih),&ih,sizeof(ih),&br,NULL)){printf("INIT fail\n");return 1;}
    printf("INIT OK\n\n");

    /* Test Queue 2 */
    printf("=== Queue 2 sanity ===\n");
    int q2test=q2Send(3,0,0);
    printf("q2_0x03 = %d (expect 23)\n",q2test);
    if(q2test!=23){printf("Queue 2 NOT available!\n");CloseHandle(h);return 1;}
    printf("Queue 2 WORKS!\n\n");

    /* Current state */
    int freq0=q0Send(0x37,0);
    int wgp0=q0Send(0x1E,0);
    uint32_t feat0=(uint32_t)q0Send(0x3D,0);
    printf("Initial: Freq=%d MHz  Wgp=%d  Feat=0x%08X\n",freq0,wgp0,feat0);
    printf("  GFXOFF=%s\n",(feat0&4)?"ON":"OFF");

    /* Step 1: Unforce any stale overrides FIRST (governor init) */
    printf("\n=== Step 1: Cleanup stale overrides ===\n");
    q0Send(0x3A,0); /* unforce_gfx_freq */
    q0Send(0x3C,0); /* unforce_gfx_vid */
    printf("Done\n");

    /* Step 2: Disable GFXOFF via Queue 2 */
    printf("\n=== Step 2: disable_smu_features(GFXOFF=bit2) ===\n");
    int r=q2Send(6,4,0);
    printf("Result: %d (0=OK)\n",r);
    Sleep(150);

    uint32_t feat1=(uint32_t)q0Send(0x3D,0);
    int freq1=q0Send(0x37,0);
    int wgp1=q0Send(0x1E,0);
    printf("After disable: Freq=%d MHz  Wgp=%d  Feat=0x%08X\n",freq1,wgp1,feat1);
    printf("  GFXOFF=%s\n",(feat1&4)?"ON":"OFF");

    /* Step 3: Try disabling more power features (bit 0 = GFXCLK DPM) */
    printf("\n=== Step 3: Try combinations ===\n");

    /* Re-enable GFXOFF, then disable + others */
    r=q2Send(5,4,0); /* re-enable GFXOFF */
    printf("Re-enable GFXOFF: %d\n",r);
    Sleep(50);

    /* Disable multiple bits together */
    r=q2Send(6,4|0x20|0x800|0x2000,0); /* GFXOFF + bit5 + bit11 + bit13 */
    printf("Disable GFXOFF+bit5+bit11+bit13: %d\n",r);
    Sleep(150);

    uint32_t feat2=(uint32_t)q0Send(0x3D,0);
    int freq2=q0Send(0x37,0);
    printf("After multi-disable: Freq=%d MHz  Feat=0x%08X  GFXOFF=%s\n",
        freq2,feat2,(feat2&4)?"ON":"OFF");

    /* Step 4: Re-enable all features */
    printf("\n=== Step 4: Re-enable everything ===\n");
    r=q2Send(5,0xFFFFFFFF,0);
    printf("enable: %d\n",r);
    Sleep(100);

    uint32_t feat3=(uint32_t)q0Send(0x3D,0);
    int freq3=q0Send(0x37,0);
    printf("Restored: Freq=%d MHz  Feat=0x%08X  GFXOFF=%s\n",
        freq3,feat3,(feat3&4)?"ON":"OFF");

    /* Step 5: GC register check */
    printf("\n=== GC Register check ===\n");
    printf("GRBM_STATUS      [0x3260] = 0x%08X\n",R32(0x3260));
    printf("CP_SCRATCH[0]    [0x32D4] = 0x%08X\n",R32(0x32D4));
    printf("SPI_PG_ENABLE    [0x34FC] = 0x%08X\n",R32(0x34FC));
    printf("KIQ_BASE_LO      [0xE060] = 0x%08X\n",R32(0xE060));
    printf("KIQ_CNTL         [0xE068] = 0x%08X\n",R32(0xE068));
    printf("KIQ_SIZE         [0xE070] = 0x%08X\n",R32(0xE070));
    printf("CP_HQD_ACTIVE    [0x910C] = 0x%08X\n",R32(0x910C));
    printf("COMPUTE_PGM_LO   [0x8110] = 0x%08X\n",R32(0x8110));

    CloseHandle(h);
    printf("\nDONE\n");
    return 0;
}