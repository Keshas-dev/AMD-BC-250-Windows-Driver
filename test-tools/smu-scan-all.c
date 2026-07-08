/* smu-scan-all.c — phase 2: test unknown messages WITH parameters */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE h;
static BOOL W(uint32_t o, uint32_t v) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=v;
    return DeviceIoControl(h,IOCTL_AMDBC250_WRITE_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL);
}
static uint32_t R(uint32_t o) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=0;
    if(DeviceIoControl(h,IOCTL_AMDBC250_READ_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL)) return r.Value;
    return 0xFFFFFFFF;
}
static uint32_t S(uint32_t a) { W(0x38,a); R(0x38); return R(0x3C); }
static void Sw(uint32_t a, uint32_t v) { W(0x38,a); W(0x3C,v); }

static uint32_t smuSend(uint16_t msg, uint32_t param) {
    uint32_t c=S(0x03B10A68);
    if(c==1) Sw(0x03B10A68,0);
    else if(c!=0) Sw(0x03B10A68,0);
    Sw(0x03B10A48,param);
    Sw(0x03B10A08,msg);
    for(int i=0;i<1500;i++){
        c=S(0x03B10A68);
        if(c==1){Sleep(5);return S(0x03B10A48);}
        if(c!=0) return 0xFFFF0000|(c&0xFF);
        Sleep(1);
    }
    return 0xFFFFFFFF;
}

int main() {
    setvbuf(stdout,NULL,_IONBF,0);

    h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE){printf("FAIL gle=%lu\n",GetLastError());return 1;}
    printf("OK\n");

    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br=0;
    ZeroMemory(&ih,sizeof(ih)); ih.MmioPhysicalBase=0xFE800000ULL; ih.MmioSize=0x80000; ih.Flags=AMDBC250_INIT_FLAG_NBIO_MAP;
    if(!DeviceIoControl(h,IOCTL_AMDBC250_INIT_HARDWARE,&ih,sizeof(ih),&ih,sizeof(ih),&br,NULL)){printf("INIT fail\n");return 1;}
    printf("INIT OK\n\n");

    printf("=== Phase 2: New messages with PARAMS ===\n\n");

    /* Test each new responder with different params to see if they're SET msgs */
    struct {uint16_t msg; const char* name; uint32_t params[4]; int nparam;} tests[]={
        {0x0B, "Msg0x0B",        {0,1,100,1000}, 4},
        {0x11, "Msg0x11",        {0,1,100,1000}, 4},
        {0x16, "Msg0x16",        {0,1,100,1000}, 4},
        {0x1C, "Msg0x1C",        {0,1,100,1000}, 4},
        {0x1D, "Msg0x1D",        {0,1,100,1000}, 4},
        {0x2C, "Msg0x2C",        {0,1,100,1000}, 4},
        {0x2F, "Msg0x2F",        {0,1,100,1000}, 4},
        {0x30, "Msg0x30",        {0,1,100,1000}, 4},
        {0x31, "Msg0x31",        {0,1,100,1000}, 4},
        {0x34, "Msg0x34",        {0,1,100,1000}, 4},
        /* SetDriverDramAddr known to work — skip repeat */
        /* SetSoftMin/SoftMax known no-effect — skip repeat */
    };

    for(int t=0;t<sizeof(tests)/sizeof(tests[0]);t++){
        printf("--- %s (0x%02X) ---\n",tests[t].name,tests[t].msg);
        for(int p=0;p<tests[t].nparam;p++){
            uint32_t param=tests[t].params[p];
            uint32_t r=smuSend(tests[t].msg,param);
            printf("  param=%7u: ",param);
            if(r==0xFFFFFFFF) printf("TIMEOUT\n");
            else if((r&0xFFFF0000)==0xFFFF0000) printf("BUSY(c90=0x%02X)\n",r&0xFF);
            else printf("0x%08X (%d)\n",r,r);
            Sleep(10);
        }
        printf("\n");
    }

    /* Check the high-value test: what happens if we send large params to 0x0B? */
    printf("=== 0x0B with large params (frequency-like) ===\n");
    uint32_t bigParams[]={50000,100000,150000};
    const char* bpNames[]={"500MHz","1000MHz","1500MHz"};
    for(int i=0;i<3;i++){
        printf("  0x0B(%s): ",bpNames[i]);
        uint32_t r=smuSend(0x0B,bigParams[i]);
        printf("0x%08X\n",r);
        Sleep(100);
        printf("  -> GfxFreq=%u MHz\n",smuSend(0x37,0)/100);
    }

    /* Quick final state */
    printf("\n=== Final ===\n");
    printf("TestMessage=0x%08X\n",smuSend(0x01,0));
    printf("Features=0x%08X\n",smuSend(0x3D,0));
    printf("GfxFreq=%u MHz\n",smuSend(0x37,0)/100);
    printf("0x11=0x%08X 0x2F=0x%08X\n",smuSend(0x11,0),smuSend(0x2F,0));

    CloseHandle(h);
    printf("DONE\n");
    return 0;
}
