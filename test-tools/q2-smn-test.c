/* q2-smn-test.c — Test SMU Queue 2 (SMN_INDEX/DATA) for feature control */
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

/* Queue 2 registers (SMN_MP1_SMN_INDEX/DATA/ARGRSP) */
#define Q2_CMD  0x03B10528  /* SMN_MP1_SMN_INDEX */
#define Q2_RSP  0x03B10564  /* SMN_MP1_SMN_DATA */
#define Q2_ARG  0x03B10998  /* SMN_MP1_SMN_ARGRSP (arg + result) */

/* Queue 0 registers (C2PMSG mailbox) */
#define Q0_CMD  0x03B10A08  /* C2PMSG_66 */
#define Q0_RSP  0x03B10A68  /* C2PMSG_90 */
#define Q0_ARG  0x03B10A48  /* C2PMSG_82 */

static int q2Send(int msg, uint32_t arg, uint32_t argHigh) {
    /* Protocol from Bc250Mailbox:
       1. Write 0 to RSP (ack)
       2. Write arg to ARG
       3. Write arg_high to ARG+4
       4. Write msg to CMD (triggers SMU)
       5. Poll RSP until done */
    smnW(Q2_RSP, 0);
    smnW(Q2_ARG, arg);
    smnW(Q2_ARG+4, argHigh);
    smnW(Q2_CMD, msg);
    for(int i=0;i<1500;i++){
        uint32_t st=smnR(Q2_RSP);
        if(st==0x01) return smnR(Q2_ARG); /* OK */
        if(st==0xFF) return -1;           /* FAILED */
        if(st==0xFE) return -2;           /* UNKNOWN_CMD */
        if(st==0xFD) return -3;           /* REJECTED_PREREQ */
        if(st!=0)    return -((int)st);   /* Other error */
        Sleep(1);
    }
    return -100; /* TIMEOUT */
}

static int q0Send(int msg, uint32_t arg) {
    uint32_t c=smnR(Q0_RSP);
    if(c==1) smnW(Q0_RSP,0);
    else if(c!=0) smnW(Q0_RSP,0);
    smnW(Q0_ARG,arg);
    smnW(Q0_CMD,msg);
    for(int i=0;i<1500;i++){
        c=smnR(Q0_RSP);
        if(c==1){Sleep(5);return smnR(Q0_ARG);}
        if(c!=0) return -((int)c);
        Sleep(1);
    }
    return -100;
}

int main() {
    setvbuf(stdout,NULL,_IONBF,0);
    h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE){printf("FAIL CreateFile gle=%lu\n",GetLastError());return 1;}
    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br=0;
    ZeroMemory(&ih,sizeof(ih)); ih.MmioPhysicalBase=0xFE800000ULL; ih.MmioSize=0x80000; ih.Flags=AMDBC250_INIT_FLAG_NBIO_MAP;
    if(!DeviceIoControl(h,IOCTL_AMDBC250_INIT_HARDWARE,&ih,sizeof(ih),&ih,sizeof(ih),&br,NULL)){printf("INIT fail\n");return 1;}
    printf("INIT OK\n\n");

    /* Step 1: Verify Queue 0 works */
    printf("=== Queue 0 sanity ===\n");
    int r=q0Send(0x01,0); /* TestMessage */
    printf("TestMessage = %d (0x%08X)\n",r,r);

    /* Step 2: Test Queue 2 */
    printf("\n=== Queue 2 probe ===\n");
    r=q2Send(0x03,0,0); /* q2_0x03 returns constant 23 */
    printf("q2_0x03 = %d (expected 23)\n",r);

    /* Try a few more Queue 2 messages */
    int r2=q2Send(0x07,0,0);
    printf("q2_0x07 = %d\n",r2);
    int r3=q2Send(0x08,0,0);
    printf("q2_0x08 = %d\n",r3);
    int r4=q2Send(0x09,0,0);
    printf("q2_0x09 = %d\n",r4);

    /* Step 3: Disable GFXOFF via Queue 2 message 0x06 */
    /* GFXOFF = feature bit 2. mask = (1<<2) = 4 */
    printf("\n=== Disable GFXOFF via Queue 2 msg 0x06 ===\n");
    uint32_t before=smuMsg0(0x37,0);
    printf("Before: GfxFreq=%u MHz ActiveWgp=%u Features=0x%08X\n",
        before/100, smuMsg0(0x1E,0), smuMsg0(0x3D,0));
    /* Also try Enable/Disable features */
    int de=q2Send(0x06,4,0); /* disable_smu_features, mask=bit2 */
    printf("disable_smu_features(bit2=GFXOFF): %d\n",de);
    Sleep(100);
    uint32_t f=smuMsg0(0x37,0);
    uint32_t w=smuMsg0(0x1E,0);
    uint32_t fe=smuMsg0(0x3D,0);
    printf("After: GfxFreq=%u MHz ActiveWgp=%u Features=0x%08X\n",f/100,w,fe);
    if(fe&4) printf("  GFXOFF still enabled\n");
    else printf("  GFXOFF DISABLED!\n");

    /* Try re-enabling! */
    int en=q2Send(0x05,4,0); /* enable_smu_features, mask=bit2 */
    printf("enable_smu_features(bit2=GFXOFF): %d\n",en);
    Sleep(100);
    printf("Features after re-enable: 0x%08X\n",smuMsg0(0x3D,0));

    /* Step 4: Try to cancel GFXOFF again and set voltage+freq */
    printf("\n=== Try full wake sequence ===\n");
    printf("Step 4a: Unforce existing overrides\n");
    q0Send(0x3A,0); /* unforce_gfx_freq */
    q0Send(0x3C,0); /* unforce_gfx_vid */
    Sleep(50);
    
    printf("Step 4b: Disable GFXOFF\n");
    q2Send(0x06,4,0);
    Sleep(100);

    printf("Step 4c: Set GFX VID to 850 mV (VID=%d)\n",
        (int)((1.55-0.85)/0.00625+0.5));
    q0Send(0x3B,(int)((1.55-0.85)/0.00625+0.5));
    Sleep(50);

    printf("Step 4d: Set GFX freq to 1175 MHz (safe point)\n");
    q0Send(0x39,1175);
    Sleep(200);

    f=smuMsg0(0x37,0);
    w=smuMsg0(0x1E,0);
    printf("Result: GfxFreq=%u MHz ActiveWgp=%u\n",f/100,w);
    
    if(f/100>15) printf("*** GFX IS AWAKE! ***\n");
    else printf("GFX still sleeping\n");

    /* Step 5: Cleanup */
    printf("\n=== Cleanup ===\n");
    q0Send(0x3A,0); /* unforce_gfx_freq */
    q0Send(0x3C,0); /* unforce_gfx_vid */
    q2Send(0x05,4,0); /* re-enable GFXOFF */
    printf("Features=0x%08X\n",smuMsg0(0x3D,0));

    CloseHandle(h);
    printf("DONE\n");
    return 0;
}