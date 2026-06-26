/* ib-test-app.c — IB test with MEC firmware load */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

#define IOCTL_GPU_INIT  0x80000B80
#define IOCTL_CP_FW     0x80000BD4
#define IOCTL_GPU_KIQ_TEST 0x80000BD0

typedef struct {
    uint32_t Result, ScratchBefore, ScratchAfter;
    uint32_t MmioMapped, RingAllocated, HqdProgrammed, Pm4Submitted;
    uint32_t UseIB;
} KIQ_TEST;

typedef struct {
    uint32_t FwType;
    uint32_t FwSize;
    uint32_t Result;
    uint32_t UcodeVersion;
    uint8_t  Data[262144]; /* max 256KB */
} FW_LOAD;

int main(){
    HANDLE h=CreateFileW(L"\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,
        0,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE){printf("FAIL err=%lu\n",GetLastError());return 1;}
    printf("Device opened OK\n");

    /* Init BAR5 */
    uint8_t init[32]={0};
    *(uint64_t*)init=0xFE800000ULL;
    *(uint32_t*)(init+8)=0x00080000;
    *(uint32_t*)(init+12)=1;
    *(uint64_t*)(init+16)=0xC0000000ULL;
    *(uint32_t*)(init+24)=0x20000000;
    DWORD br=0;
    DeviceIoControl(h,IOCTL_GPU_INIT,init,sizeof(init),NULL,0,&br,NULL);
    printf("Init done\n");

    /* Load MEC firmware */
    HANDLE fm=fopen("..\\firmware\\cyan_skillfish2_mec.bin","rb");
    if(!fm){printf("FW file not found\n");CloseHandle(h);return 1;}
    FW_LOAD fw={0};
    fw.FwType=4; /* MEC */
    fw.FwSize=(uint32_t)fread(fw.Data,1,sizeof(fw.Data),fm);
    fclose(fm);
    printf("MEC firmware read: %u bytes\n",fw.FwSize);

    BOOL ok=DeviceIoControl(h,IOCTL_CP_FW,&fw,sizeof(fw),&fw,sizeof(fw),&br,NULL);
    printf("FW load: ok=%d err=%lu result=0x%08X version=0x%08X\n",
        ok,GetLastError(),fw.Result,fw.UcodeVersion);

    /* IB test */
    KIQ_TEST t={0};
    t.UseIB=1;
    ok=DeviceIoControl(h,IOCTL_GPU_KIQ_TEST,&t,sizeof(t),&t,sizeof(t),&br,NULL);
    printf("\nIB test: ok=%d err=%lu bytes=%lu\n",ok,GetLastError(),br);
    if(ok){
        printf("Result=0x%08X\n",t.Result);
        printf("RingAllocated=%d HqdProgrammed=%d Pm4Submitted=%d\n",
            t.RingAllocated,t.HqdProgrammed,t.Pm4Submitted);
        printf("ScratchBefore=0x%08X ScratchAfter=0x%08X\n",
            t.ScratchBefore,t.ScratchAfter);
        if(t.ScratchAfter==0xCAFEBABE)printf("*** PM4 EXECUTED! ***\n");
        else if(t.ScratchAfter!=t.ScratchBefore)
            printf("SCRATCH changed to 0x%08X\n",t.ScratchAfter);
        else printf("SCRATCH unchanged\n");
    }
    CloseHandle(h);
    return 0;
}
