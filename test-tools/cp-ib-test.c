/* cp-ib-test.c — Probe CP_IB1 registers + Direct PM4 via Indirect Buffer */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#define IOCTL_GPU_READ  0x80000B88
#define IOCTL_GPU_WRITE 0x80000B8C
#define IOCTL_GPU_INIT  0x80000B80
#define IOCTL_GCVM_PT   0x8000098C
#define IOCTL_CP_FW     0x80000BD4
static HANDLE hGpu;
static ULONG R(ULONG off){UCHAR b[8]={0};*(ULONG*)b=off;*(ULONG*)(b+4)=0xBAD0C0DE;DWORD br=0;
    if(!DeviceIoControl(hGpu,IOCTL_GPU_READ,b,8,b,8,&br,NULL)||br<8)return 0xBAD0C0DE;return *(ULONG*)(b+4);}
static void W(ULONG off,ULONG v){UCHAR b[8]={0};*(ULONG*)b=off;*(ULONG*)(b+4)=v;
    DWORD br=0;DeviceIoControl(hGpu,IOCTL_GPU_WRITE,b,8,NULL,0,&br,NULL);Sleep(2);}
static int LoadFW(UINT32 type){
    const char *paths[] = {"", "cyan_skillfish2_me.bin", "cyan_skillfish2_pfp.bin", "cyan_skillfish2_ce.bin", "cyan_skillfish2_mec.bin"};
    char fwPath[256]="C:\\AMD-BC-250\\AMD-BC-250-PSP-Windows-Driver\\output\\";
    strcat(fwPath, paths[type]);
    HANDLE hFw = CreateFileA(fwPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFw == INVALID_HANDLE_VALUE) return -1;
    DWORD fwSize = GetFileSize(hFw, NULL);
    UINT32 total = 16 + fwSize;
    UCHAR *b = (UCHAR*)malloc(total);
    memset(b, 0, 16);
    *(UINT32*)(b+0) = type;
    *(UINT32*)(b+4) = fwSize;
    DWORD brFw = 0;
    ReadFile(hFw, b+16, fwSize, &brFw, NULL);
    CloseHandle(hFw);
    UINT32 out[4] = {0}; DWORD br=0;
    int result = DeviceIoControl(hGpu, IOCTL_CP_FW, b, total, out, sizeof(out), &br, NULL) && br>=4 ? out[1] : -1;
    free(b);
    return result;}
static int DoPT(void){
    UCHAR b[256]={0};*(UINT32*)(b+0)=0;DWORD br=0;
    return DeviceIoControl(hGpu,IOCTL_GCVM_PT,b,256,b+236,20,&br,NULL)&&br>=20?*(UINT32*)(b+236):-1;}

int main(void){
    hGpu=CreateFileW(L"\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
    if(hGpu==INVALID_HANDLE_VALUE){printf("FAIL err=%lu\n",GetLastError());return 1;}
    UCHAR init[32]={0};*(UINT64*)init=0xFE800000ULL;*(UINT32*)(init+8)=0x00080000;
    *(UINT32*)(init+12)=1;*(UINT64*)(init+16)=0xC0000000ULL;*(UINT32*)(init+24)=0x20000000;
    DWORD br=0;DeviceIoControl(hGpu,IOCTL_GPU_INIT,init,sizeof(init),NULL,0,&br,NULL);

    /* Restore GRBM broadcast */
    W(0x34D0,0xE0000000);

    /* === Step 1: Probe CP_IB1 register range 0x3B80-0x3C00 === */
    printf("=== Write probe 0x3B80-0x3C00 (expected CP_IB area) ===\n");
    printf("Offset   Before    After     Status\n");
    printf("------   --------  --------  ------\n");
    ULONG orig[0x40];
    for(int off=0x3B80;off<=0x3BFC;off+=4){
        int idx=(off-0x3B80)/4;
        orig[idx]=R(off);
        W(off,0xA5A5A5A5);
        ULONG a=R(off);
        W(off,orig[idx]); /* restore */
        char* st="READ-ONLY";
        if(a==0xA5A5A5A5)st="WRITABLE";
        else if(a==0xFFFFFFFF)st="DEAD";
        printf("[0x%04X] 0x%08X 0x%08X %s\n",off,orig[idx],a,st);
    }

    /* === Step 2: If IB registers writable, load firmware and try Direct IB === */
    /* Check 0x3BB4 (IB1_BASE_LO), 0x3BB8 (IB1_BASE_HI), 0x3BBC (IB1_BUFSZ) */
    ULONG v0=R(0x3BB4),v1=R(0x3BB8),v2=R(0x3BBC),v3=R(0x3BC0);
    printf("\n=== CP_IB1 candidate registers ===\n");
    printf("[0x3BB4]=0x%08X  [0x3BB8]=0x%08X  [0x3BBC]=0x%08X  [0x3BC0]=0x%08X\n",v0,v1,v2,v3);

    /* Try writing test values */
    W(0x3BB4,0x5A5A5A5A); ULONG t0=R(0x3BB4); W(0x3BB4,v0);
    W(0x3BB8,0xA5A5A5A5); ULONG t1=R(0x3BB8); W(0x3BB8,v1);
    W(0x3BBC,0x00000020); ULONG t2=R(0x3BBC); W(0x3BBC,v2);
    W(0x3BC0,0x00000020); ULONG t3=R(0x3BC0); W(0x3BC0,v3);
    printf("Write test:\n");
    printf("[0x3BB4] %s (wrote 0x5A5A5A5A, read 0x%08X)\n",(t0==0x5A5A5A5A?"WRITABLE":"READ-ONLY"),t0);
    printf("[0x3BB8] %s (wrote 0xA5A5A5A5, read 0x%08X)\n",(t1==0xA5A5A5A5?"WRITABLE":"READ-ONLY"),t1);
    printf("[0x3BBC] %s (wrote 0x20, read 0x%08X)\n",(t2==0x20?"WRITABLE":"READ-ONLY"),t2);
    printf("[0x3BC0] %s (wrote 0x20, read 0x%08X)\n",(t3==0x20?"WRITABLE":"READ-ONLY"),t3);

    /* Also test 0x3BA0-0x3BB0 area (might be CP_IB2 or other variants) */
    int writable_ib=-1;
    for(int off=0x3B80;off<=0x3C00;off+=4){
        ULONG save=R(off);
        W(off,0x12345678);
        ULONG test=R(off);
        W(off,save);
        if(test==0x12345678){
            printf("FOUND WRITABLE at 0x%04X!\n",off);
            if(writable_ib==-1)writable_ib=off;
        }
    }

    CloseHandle(hGpu);
    printf("\nDone.\n");
    return 0;
}
