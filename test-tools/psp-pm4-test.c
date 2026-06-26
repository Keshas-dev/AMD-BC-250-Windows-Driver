/* psp-pm4-test.c — su teisinga struktūra */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

#define IOCTL_GPU_INIT  0x80000B80
#define IOCTL_GPU_READ  0x80000B88
#define IOCTL_SEND_PM4  0x80000B84

#pragma pack(push, 8)
typedef struct {
    uint32_t Commands[64];
    uint32_t CommandCount;
    uint32_t Padding;
    uint64_t FenceValue;
    uint32_t QueueType;
    uint32_t Padding2;
} SEND_PM4;
#pragma pack(pop)

static HANDLE hGpu;
static uint32_t R(uint32_t off){
    uint8_t b[8]={0};*(uint32_t*)b=off;*(uint32_t*)(b+4)=0xBAD0C0DE;
    DWORD br=0;DeviceIoControl(hGpu,IOCTL_GPU_READ,b,8,b,8,&br,NULL);
    return *(uint32_t*)(b+4);
}

int main(){
    printf("sizeof(SEND_PM4)=%llu\n",(ULONGLONG)sizeof(SEND_PM4));

    hGpu=CreateFileW(L"\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,
        0,NULL,OPEN_EXISTING,0,NULL);
    if(hGpu==INVALID_HANDLE_VALUE){printf("FAIL err=%lu\n",GetLastError());return 1;}

    uint8_t init[32]={0};
    *(uint64_t*)init=0xFE800000ULL;
    *(uint32_t*)(init+8)=0x00080000;
    *(uint32_t*)(init+12)=1;
    *(uint64_t*)(init+16)=0xC0000000ULL;
    *(uint32_t*)(init+24)=0x20000000;
    DWORD br=0;
    DeviceIoControl(hGpu,IOCTL_GPU_INIT,init,sizeof(init),NULL,0,&br,NULL);
    printf("Init OK\n");

    uint32_t scratchBefore=R(0x32D4);
    printf("SCRATCH before=0x%08X\n",scratchBefore);

    SEND_PM4 pm4={0};
    pm4.CommandCount=5;
    pm4.Commands[0]=0xC0370003;
    pm4.Commands[1]=0x10100000;
    pm4.Commands[2]=0x000032D4;
    pm4.Commands[3]=0x00000000;
    pm4.Commands[4]=0xCAFEBABE;

    printf("Calling SEND_PM4...\n");
    BOOL ok=DeviceIoControl(hGpu,IOCTL_SEND_PM4,&pm4,sizeof(pm4),&pm4,sizeof(pm4),&br,NULL);
    printf("SEND_PM4: ok=%d err=%lu br=%lu\n",ok,GetLastError(),br);

    Sleep(200);
    uint32_t scratchAfter=R(0x32D4);
    printf("SCRATCH after=0x%08X\n",scratchAfter);
    if(scratchAfter==0xCAFEBABE)printf("*** PM4 EXECUTED! ***\n");
    else if(scratchAfter!=scratchBefore)printf("SCRATCH changed to 0x%08X\n",scratchAfter);
    else printf("SCRATCH unchanged\n");

    CloseHandle(hGpu);
    return 0;
}
