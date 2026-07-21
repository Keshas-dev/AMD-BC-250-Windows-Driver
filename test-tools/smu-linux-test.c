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
#define C2PMSG_80 0x03B10A80
#define C2PMSG_88 0x03B10A88

static int WaitC2p(uint32_t rspAddr, uint32_t exp, int ms){for(int i=0;i<ms;i++){if(SmnRead(rspAddr)==exp)return 1;Sleep(1);}return 0;}
static uint32_t SmuQ0(uint16_t msg,uint32_t arg){SmnWrite(C2PMSG_90,0);SmnWrite(C2PMSG_82,arg);SmnWrite(C2PMSG_66,msg);if(!WaitC2p(C2PMSG_90,1,200))return 0xFFFFFFFF;return SmnRead(C2PMSG_82);}
static uint32_t SmuQ3(uint16_t msg,uint32_t arg){SmnWrite(C2PMSG_80,0);SmnWrite(C2PMSG_88,arg);SmnWrite(C2PMSG_80+4,msg);if(!WaitC2p(C2PMSG_80,1,200))return 0xFFFFFFFF;return SmnRead(C2PMSG_88);}
static int mv_to_vid(int mv){ return (int)((1.55 - (double)mv/1000.0) / 0.00625 + 0.5); }

int main(){
    setvbuf(stdout,NULL,_IONBF,0);
    h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE){printf("FAIL gle=%lu\n",GetLastError());return 1;}
    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br=0;
    ZeroMemory(&ih,sizeof(ih)); ih.MmioPhysicalBase=0xFE800000ULL; ih.MmioSize=0x80000; ih.Flags=AMDBC250_INIT_FLAG_NBIO_MAP;
    if(!DeviceIoControl(h,IOCTL_AMDBC250_INIT_HARDWARE,&ih,sizeof(ih),&ih,sizeof(ih),&br,NULL)){printf("INIT fail\n");return 1;}

    uint32_t tmp;

    /* ==================== TEST 1: RequestGfxclk vs ForceGfxFreq ==================== */
    printf("=== TEST 1: RequestGfxclk (0x0E) vs ForceGfxFreq (0x39) ===\n\n");

    /* Safe sequence: Q3 max temp -> Q0 unforce -> Q3 profile -> Q0 forceVid -> Q0 test */
    SmuQ3(0x8C, 80);
    SmuQ0(0x3A, 0);  /* UnForce */
    SmuQ0(0x3C, 0);  /* UnForceVid */
    SmuQ3(0x1E, 3);  /* Profile 3 high */

    /* First set voltage so freq change doesn't crash */
    SmuQ0(0x3B, mv_to_vid(900));
    Sleep(50);
    printf("Initial: Freq=%u MHz Features=0x%08X\n", SmuQ0(0x37,0), SmuQ0(0x3D,0));

    /* Test 1a: RequestGfxclk (0x0E) - Linux OD path */
    printf("\n--- RequestGfxclk(0x0E) 1200MHz ---\n");
    tmp = SmuQ0(0x0E, 1200);
    Sleep(500);
    printf("  Response: 0x%08X  Freq=%u MHz\n", tmp, SmuQ0(0x37,0));

    printf("\n--- RequestGfxclk(0x0E) 800MHz ---\n");
    tmp = SmuQ0(0x0E, 800);
    Sleep(500);
    printf("  Response: 0x%08X  Freq=%u MHz\n", tmp, SmuQ0(0x37,0));

    /* Restore */
    SmuQ0(0x3A, 0); SmuQ0(0x3C, 0);

    /* Test 1b: ForceGfxFreq (0x39) - for comparison */
    printf("\n--- ForceGfxFreq(0x39) 1200MHz (with voltage) ---\n");
    SmuQ0(0x3B, mv_to_vid(900));
    Sleep(50);
    tmp = SmuQ0(0x39, 1200);
    Sleep(500);
    printf("  Response: 0x%08X  Freq=%u MHz\n", tmp, SmuQ0(0x37,0));

    printf("\n--- ForceGfxFreq(0x39) 800MHz ---\n");
    tmp = SmuQ0(0x39, 800);
    Sleep(500);
    printf("  Response: 0x%08X  Freq=%u MHz\n", tmp, SmuQ0(0x37,0));

    /* Cleanup */
    SmuQ0(0x3A, 0); SmuQ0(0x3C, 0);
    Sleep(200);
    printf("\nAfter cleanup: Freq=%u MHz\n", SmuQ0(0x37,0));

    /* ==================== TEST 2: SMN Temperature Scan ==================== */
    printf("\n\n=== TEST 2: SMN Temperature Probe ===\n");
    printf("Method: read SMN around known thermal ranges\n");
    printf("Temp decode: raw/100 = degC for GFX, raw/256*1000 = mV for voltage\n\n");

    uint32_t addrs[] = {
        /* Known MP1 ranges */
        0x03B10020, 0x03B10024, 0x03B10028, 0x03B1002C,
        0x03B10030, 0x03B10034, 0x03B10038, 0x03B1003C,
        /* Extended thermal area */
        0x03B10040, 0x03B10044, 0x03B10048, 0x03B1004C,
        0x03B10050, 0x03B10054, 0x03B10058, 0x03B1005C,
        0x03B10060, 0x03B10064, 0x03B10068, 0x03B1006C,
        /* SMU metrics block start */
        0x03B10080, 0x03B10084, 0x03B10088, 0x03B1008C,
        0x03B10090, 0x03B10094, 0x03B10098, 0x03B1009C,
        /* More */
        0x03B100A0, 0x03B100A4, 0x03B100A8, 0x03B100AC,
        0x03B100B0, 0x03B100B4, 0x03B100B8, 0x03B100BC,
        /* Temperature-related from ThmBlock */
        0x03B10100, 0x03B10104, 0x03B10108, 0x03B1010C,
        0x03B10110, 0x03B10114, 0x03B10118, 0x03B1011C,
        /* CPU thermal */
        0x03B10200, 0x03B10204, 0x03B10208, 0x03B1020C,
        /* NBIF/PCIe temp */
        0x03B10300, 0x03B10304, 0x03B10308, 0x03B1030C,
        /* FUSE area temps */
        0x03B17400, 0x03B17404, 0x03B17408, 0x03B1740C,
        0x03B17410, 0x03B17414, 0x03B17418, 0x03B1741C,
    };

    for (int i = 0; i < sizeof(addrs)/sizeof(addrs[0]); i++) {
        uint32_t v = SmnRead(addrs[i]);
        if (v != 0 && v != 0xFFFFFFFF) {
            printf("SMN[0x%08X] = 0x%08X", addrs[i], v);
            if (v < 0xFFFF && v > 100) printf(" (~%u.%02uC)", v/100, v%100);
            if (addrs[i] == 0x03B10028) printf(" MEM TEMP");
            printf("\n");
        }
    }

    /* Dump full SMN run at 0x03B10000-0x03B10100 for complete view */
    printf("\n--- SMU SMN Dump 0x03B10000-0x03B100FF ---\n");
    for (uint32_t a = 0x03B10000; a <= 0x03B100FF; a += 4) {
        uint32_t v = SmnRead(a);
        if (v != 0 && v != 0xFFFFFFFF)
            printf("SMN[0x%08X] = 0x%08X\n", a, v);
    }

    printf("\n--- THM Block 0x03B16600-0x03B1667F ---\n");
    for (uint32_t a = 0x03B16600; a <= 0x03B1667F; a += 4) {
        uint32_t v = SmnRead(a);
        if (v != 0 && v != 0xFFFFFFFF)
            printf("SMN[0x%08X] = 0x%08X\n", a, v);
    }

    CloseHandle(h);
    printf("\nDone.\n");
    return 0;
}
