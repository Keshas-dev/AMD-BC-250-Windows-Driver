/* ib-cntl-probe.c — Tiesioginis 0x3BC0 (CP_IB1_CNTL) bitų testas */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#define IOCTL_GPU_INIT  0x80000B80
#define IOCTL_GPU_READ  0x80000B88
#define IOCTL_GPU_WRITE 0x80000B8C

static HANDLE h;
static uint32_t R(uint32_t off){
    uint8_t b[8]={0};*(uint32_t*)b=off;*(uint32_t*)(b+4)=0xBAD0C0DE;
    DWORD br=0;DeviceIoControl(h,IOCTL_GPU_READ,b,8,b,8,&br,NULL);
    return *(uint32_t*)(b+4);}
static void W(uint32_t off,uint32_t v){
    uint8_t b[8]={0};*(uint32_t*)b=off;*(uint32_t*)(b+4)=v;
    DWORD br=0;DeviceIoControl(h,IOCTL_GPU_WRITE,b,8,NULL,0,&br,NULL);Sleep(5);}

int main(){
    h=CreateFileW(L"\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,
        0,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE){printf("FAIL err=%lu\n",GetLastError());return 1;}

    uint8_t init[32]={0};
    *(uint64_t*)init=0xFE800000ULL;*(uint32_t*)(init+8)=0x00080000;
    *(uint32_t*)(init+12)=1;*(uint64_t*)(init+16)=0xC0000000ULL;
    *(uint32_t*)(init+24)=0x20000000;
    DWORD br=0;DeviceIoControl(h,IOCTL_GPU_INIT,init,sizeof(init),NULL,0,&br,NULL);

    /* Test 0x3BC0 writable bits */
    uint32_t test_vals[]={1,2,3,4,8,0x10,0x20,0x40,0x80,0x100,0x1000,0x10000,0x100000,0x1000000};
    printf("Testing 0x3BC0 (CP_IB1_CNTL) writable bits:\n");
    printf("Value    Readback Status\n");
    printf("------   -------- ------\n");
    for(int i=0;i<sizeof(test_vals)/4;i++){
        uint32_t orig=R(0x3BC0);
        W(0x3BC0,test_vals[i]);
        uint32_t rb=R(0x3BC0);
        W(0x3BC0,orig);
        printf("0x%08X 0x%08X %s\n",test_vals[i],rb,
            (rb==test_vals[i]?"WRITABLE":"masked"));
    }

    /* Test 0x3BAC/0x3BB0 with IB enable (bit 0 of 0x3BC0) */
    printf("\nTesting IB_ENABLE (0x3BC0=1) with zero address:\n");
    uint32_t sav_ac=R(0x3BAC),sav_b0=R(0x3BB0),sav_c0=R(0x3BC0);
    W(0x3BAC,0);
    W(0x3BB0,0);
    W(0x3BC0,1);  /* IB_ENABLE=1 */
    uint32_t rb=R(0x3BC0);
    printf("0x3BC0 after writing 1: 0x%08X - %s\n",rb,(rb==1?"WRITABLE":"masked"));

    W(0x3BC0,0);  /* Clear enable */
    /* Restore */
    W(0x3BAC,sav_ac);W(0x3BB0,sav_b0);W(0x3BC0,sav_c0);
    CloseHandle(h);
    printf("Done\n");
    return 0;
}
