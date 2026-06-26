/* sdma-probe.c — Test SDMA registers + scan 0xE000-0xE05C for writable ring config */
#include <windows.h>
#include <stdio.h>
#define IOCTL_GPU_READ  0x80000B88
#define IOCTL_GPU_WRITE 0x80000B8C
#define IOCTL_GPU_INIT  0x80000B80
static HANDLE hGpu;
static ULONG R(ULONG off){UCHAR b[8]={0};*(ULONG*)b=off;*(ULONG*)(b+4)=0xBAD0C0DE;DWORD br=0;
    if(!DeviceIoControl(hGpu,IOCTL_GPU_READ,b,8,b,8,&br,NULL)||br<8)return 0xBAD0C0DE;return *(ULONG*)(b+4);}
static void W(ULONG off,ULONG v){UCHAR b[8]={0};*(ULONG*)b=off;*(ULONG*)(b+4)=v;
    DWORD br=0;DeviceIoControl(hGpu,IOCTL_GPU_WRITE,b,8,NULL,0,&br,NULL);Sleep(2);}

int main(void){
    hGpu=CreateFileW(L"\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
    if(hGpu==INVALID_HANDLE_VALUE){printf("FAIL err=%lu\n",GetLastError());return 1;}
    UCHAR init[32]={0};*(UINT64*)init=0xFE800000ULL;*(UINT32*)(init+8)=0x00080000;
    *(UINT32*)(init+12)=1;*(UINT64*)(init+16)=0xC0000000ULL;*(UINT32*)(init+24)=0x20000000;
    DWORD br=0;DeviceIoControl(hGpu,IOCTL_GPU_INIT,init,sizeof(init),NULL,0,&br,NULL);

    /* Restore GRBM broadcast first to fix display */
    W(0x34D0,0xE0000000);
    printf("GRBM restored to broadcast\n");

    /* Scan SDMA + KIQ range 0xE000-0xE0FF — write test every 4 bytes */
    printf("\n=== Write probe 0xE000-0xE0FF ===\n");
    printf("Offset   Before    After     Status\n");
    printf("------   --------  --------  ------\n");
    ULONG saved[0x40];
    for(int off=0xE000;off<=0xE0FC;off+=4){
        saved[(off-0xE000)/4]=R(off);
        W(off,0xA5A5A5A5);
        ULONG a=R(off);
        W(off,saved[(off-0xE000)/4]);
        char st[32]="READ-ONLY";
        if(a==0xA5A5A5A5)strcpy(st,"WRITABLE");
        else if(a==0xFFFFFFFF)strcpy(st,"DEAD");
        printf("[0x%04X] 0x%08X 0x%08X %s\n",off,saved[(off-0xE000)/4],a,st);
    }

    /* Check if SDMA registers at 0xE000-0xE018 are writable */
    printf("\n=== SDMA Register Summary ===\n");
    for(int off=0xE000;off<=0xE018;off+=4){
        ULONG v=R(off);
        W(off,0x5A5A5A5A);ULONG a=R(off);W(off,v);
        printf("[0x%04X] %s\n",off,(a==0x5A5A5A5A)?"WRITABLE":"READ-ONLY");
    }

    CloseHandle(hGpu);
    printf("\nDone. Reboot recommended.\n");
    return 0;
}
