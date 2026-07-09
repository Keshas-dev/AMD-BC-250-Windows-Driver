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

static int WaitC2p90(uint32_t exp,int ms){for(int i=0;i<ms;i++){if(SmnRead(C2PMSG_90)==exp)return 1;Sleep(1);}return 0;}

static uint32_t SmuQuery(uint16_t msg){
    uint32_t c90=SmnRead(C2PMSG_90); if(c90==1)SmnWrite(C2PMSG_90,0);
    SmnWrite(C2PMSG_82,0); SmnWrite(C2PMSG_66,msg);
    if(!WaitC2p90(1,200))return 0xFFFFFFFF; return SmnRead(C2PMSG_82);
}
static uint32_t SmuQueryParam(uint16_t msg,uint32_t param){
    uint32_t c90=SmnRead(C2PMSG_90); if(c90==1)SmnWrite(C2PMSG_90,0);
    SmnWrite(C2PMSG_82,param); SmnWrite(C2PMSG_66,msg);
    if(!WaitC2p90(1,200))return 0xFFFFFFFF; return SmnRead(C2PMSG_82);
}

int main(){
    setvbuf(stdout,NULL,_IONBF,0);
    h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE){printf("FAIL gle=%lu\n",GetLastError());return 1;}
    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br=0;
    ZeroMemory(&ih,sizeof(ih)); ih.MmioPhysicalBase=0xFE800000ULL; ih.MmioSize=0x80000; ih.Flags=AMDBC250_INIT_FLAG_NBIO_MAP;
    if(!DeviceIoControl(h,IOCTL_AMDBC250_INIT_HARDWARE,&ih,sizeof(ih),&ih,sizeof(ih),&br,NULL)){printf("INIT fail\n");return 1;}

    printf("=== SMU DPM Table Tests (short timeouts) ===\n\n");

    printf("FW_FLAGS: 0x%08X\n", SmnRead(0x03B10024));
    printf("PUB_CTRL: 0x%08X\n", SmnRead(0x03B10B14));
    printf("TestMessage: 0x%08X\n", SmuQuery(0x01));
    printf("GetSmuVersion: 0x%08X\n", SmuQuery(0x02));
    printf("GetDriverIfVersion: 0x%08X\n", SmuQuery(0x03));
    printf("Features: 0x%08X\n", SmuQuery(0x3D));
    printf("GfxFreq: %u MHz\n", SmuQuery(0x37));
    printf("ActiveWgp: %u\n", SmuQuery(0x1E));

    /* Message 0x06 - try only table 0 (PPTABLE) first */
    printf("\n--- TransferTableSmu2Dram (0x06) table 0 ---\n");
    uint32_t r = SmuQueryParam(0x06, 0);
    printf("  Table 0 (PPTABLE): 0x%08X\n", r);

    /* Message 0x07 - try table 0 */
    printf("\n--- TransferTableDram2Smu (0x07) table 0 ---\n");
    r = SmuQueryParam(0x07, 0);
    printf("  Table 0: 0x%08X\n", r);

    /* Key messages */
    printf("\n--- Key messages ---\n");
    struct { uint16_t msg; const char* name; uint32_t param; } targets[] = {
        {0x0C, "QueryCorePstate", 0},
        {0x0F, "QueryGfxclk", 0},
        {0x13, "QueryDfPstate", 0},
        {0x1E, "QueryActiveWgp", 0},
        {0x35, "SetSoftMinCclk", 20000},
        {0x36, "SetSoftMaxCclk", 40000},
        {0x19, "SetMinDeepSleepGfxclkFreq", 0},
        {0x1A, "SetMaxDeepSleepDfllGfxDiv", 1},
    };
    for(int i=0;i<sizeof(targets)/sizeof(targets[0]);i++){
        uint32_t r = targets[i].param ? SmuQueryParam(targets[i].msg, targets[i].param) : SmuQuery(targets[i].msg);
        printf("  Msg 0x%02X (%s): 0x%08X\n", targets[i].msg, targets[i].name, r);
    }

    /* Force freq test */
    printf("\n--- Force freq test ---\n");
    SmuQuery(0x3A);  // unforce
    SmuQuery(0x3C);  // unforce vid
    SmuQueryParam(0x35, 20000);  // soft min 200MHz
    SmuQueryParam(0x36, 80000);  // soft max 800MHz
    Sleep(200);
    printf("  GfxFreq after soft limits: %u MHz\n", SmuQuery(0x37));
    printf("  ActiveWgp: %u\n", SmuQuery(0x1E));

    CloseHandle(h);
    printf("\nDone.\n");
    return 0;
}