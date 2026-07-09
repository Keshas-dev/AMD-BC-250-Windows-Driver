#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE h;
static BOOL W32(uint32_t o, uint32_t v) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=v; return DeviceIoControl(h,IOCTL_AMDBC250_WRITE_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL); }
static uint32_t R32(uint32_t o) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=0; if(DeviceIoControl(h,IOCTL_AMDBC250_READ_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL)) return r.Value; return 0xFFFFFFFF; }
static uint32_t SmnRead(uint32_t a){W32(0x38,a);R32(0x38);return R32(0x3C);}
static BOOL SmnWrite(uint32_t a,uint32_t v){if(!W32(0x38,a))return FALSE;return W32(0x3C,v);}
#define C2PMSG_66 0x03B10A08
#define C2PMSG_82 0x03B10A48
#define C2PMSG_90 0x03B10A68
#define C2PMSG_80 0x03B10A80  /* Q3 control */
#define C2PMSG_88 0x03B10A88  /* Q3 arg */
#define C2PMSG_528 0x03B10528 /* Q2 control */
#define C2PMSG_998 0x03B10998 /* Q2 arg */
#define C2PMSG_564 0x03B10564 /* Q2 rsp */

static int WaitC2p(uint32_t rspAddr, uint32_t exp, int ms){for(int i=0;i<ms;i++){if(SmnRead(rspAddr)==exp)return 1;Sleep(1);}return 0;}

static uint32_t SmuQ0(uint16_t msg,uint32_t arg){
    SmnWrite(C2PMSG_90,0); SmnWrite(C2PMSG_82,arg); SmnWrite(C2PMSG_66,msg);
    if(!WaitC2p(C2PMSG_90,1,200))return 0xFFFFFFFF; return SmnRead(C2PMSG_82);
}
static uint32_t SmuQ2(uint16_t msg,uint32_t arg,uint32_t argH){
    SmnWrite(C2PMSG_564,0); SmnWrite(C2PMSG_998,arg); SmnWrite(C2PMSG_998+4,argH); SmnWrite(C2PMSG_528,msg);
    if(!WaitC2p(C2PMSG_564,1,200))return 0xFFFFFFFF; return SmnRead(C2PMSG_998);
}
static uint32_t SmuQ3(uint16_t msg,uint32_t arg){
    SmnWrite(C2PMSG_80,0); SmnWrite(C2PMSG_88,arg); SmnWrite(C2PMSG_80+4,msg);
    if(!WaitC2p(C2PMSG_80,1,200))return 0xFFFFFFFF; return SmnRead(C2PMSG_88);
}

static int mv_to_vid(int mv){ return (int)((1.55 - (double)mv/1000.0) / 0.00625 + 0.5); }

int main(){
    setvbuf(stdout,NULL,_IONBF,0);
    h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE){printf("FAIL gle=%lu\n",GetLastError());return 1;}
    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br=0;
    ZeroMemory(&ih,sizeof(ih)); ih.MmioPhysicalBase=0xFE800000ULL; ih.MmioSize=0x80000; ih.Flags=AMDBC250_INIT_FLAG_NBIO_MAP;
    if(!DeviceIoControl(h,IOCTL_AMDBC250_INIT_HARDWARE,&ih,sizeof(ih),&ih,sizeof(ih),&br,NULL)){printf("INIT fail\n");return 1;}

    printf("=== Governor Full Sequence Test ===\n\n");
    printf("Initial: Freq=%u WGP=%u Feat=0x%08X\n", SmuQ0(0x37,0), SmuQ0(0x1E,0), SmuQ0(0x3D,0));

    /* Full governor sequence from cyan-skillfish-governor */
    printf("\n--- Step 1: Q3 SetMaxTemp (0x8C) 80C ---\n");
    SmuQ3(0x8C, 80);

    printf("\n--- Step 2: Q0 UnforceFreq (0x3A) ---\n");
    SmuQ0(0x3A, 0);

    printf("\n--- Step 3: Q0 UnforceVid (0x3C) ---\n");
    SmuQ0(0x3C, 0);

    printf("\n--- Step 4: Q3 PerfProfile (0x1E) Profile=3 (high) ---\n");
    SmuQ3(0x1E, 3);
    Sleep(100);

    printf("\n--- Step 5: Q0 ForceVid (0x3B) 850mV ---\n");
    SmuQ0(0x3B, mv_to_vid(850));
    Sleep(100);

    printf("\n--- Step 6: Q0 ForceFreq (0x39) 1200MHz ---\n");
    SmuQ0(0x39, 1200);
    Sleep(500);

    printf("\nAfter force: Freq=%u WGP=%u Feat=0x%08X\n", SmuQ0(0x37,0), SmuQ0(0x1E,0), SmuQ0(0x3D,0));

    /* Try Q2 disable GFXOFF+CG+PG (0x06/0x05) */
    printf("\n--- Step 7: Q2 DisableSmuFeatures (0x06) mask=0x1C (GFXOFF|CG|PG) ---\n");
    SmuQ2(0x06, 0x1C, 0);
    Sleep(200);
    printf("  Features after: 0x%08X\n", SmuQ0(0x3D,0));
    printf("  Freq=%u WGP=%u\n", SmuQ0(0x37,0), SmuQ0(0x1E,0));

    /* Try Q0 RequestActiveWgp (0x18) - may wake GFX */
    printf("\n--- Step 8: Q0 RequestActiveWgp (0x18) ---\n");
    SmuQ0(0x18, 0);
    Sleep(200);
    printf("  Freq=%u WGP=%u\n", SmuQ0(0x37,0), SmuQ0(0x1E,0));

    /* Try Q0 QueryGfxclk (0x0F) which sometimes triggers ramp */
    printf("\n--- Step 9: Q0 QueryGfxclk (0x0F) ---\n");
    SmuQ0(0x0F, 0);
    Sleep(200);
    printf("  Freq=%u WGP=%u\n", SmuQ0(0x37,0), SmuQ0(0x1E,0));

    /* Check GRBM_STATUS after all this */
    printf("\n--- GRBM_STATUS (0x3260) ---\n");
    printf("  0x%08X\n", R32(0x3260));

    /* Cleanup */
    printf("\n--- Cleanup ---\n");
    SmuQ0(0x3A, 0);  // unforce freq
    SmuQ0(0x3C, 0);  // unforce vid
    SmuQ2(0x05, 0x1C, 0);  // re-enable features
    Sleep(200);
    printf("Final: Freq=%u WGP=%u Feat=0x%08X\n", SmuQ0(0x37,0), SmuQ0(0x1E,0), SmuQ0(0x3D,0));

    CloseHandle(h);
    printf("\nDone.\n");
    return 0;
}