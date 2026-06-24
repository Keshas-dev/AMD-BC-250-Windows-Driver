/* gcvm-pt-test.c — KIQ with DEFAULT_PAGE=system (bypass page tables) */
#include <windows.h>
#include <stdio.h>

#define IOCTL_GPU_READ  0x80000B88
#define IOCTL_GPU_WRITE 0x80000B8C
#define IOCTL_GPU_INIT  0x80000B80
#define IOCTL_GCVM_PT_SETUP 0x8000098C

static HANDLE hGpu;

static ULONG R(ULONG off) {
    UCHAR buf[8]={0}; *(ULONG*)(buf+0)=off; *(ULONG*)(buf+4)=0xBAD0C0DE;
    DWORD br=0;
    if(!DeviceIoControl(hGpu,IOCTL_GPU_READ,buf,8,buf,8,&br,NULL)||br<8) return 0xBAD0C0DE;
    return *(ULONG*)(buf+4);
}
static void W(ULONG off, ULONG v) {
    UCHAR buf[8]={0}; *(ULONG*)(buf+0)=off; *(ULONG*)(buf+4)=v;
    DWORD br=0; DeviceIoControl(hGpu,IOCTL_GPU_WRITE,buf,8,NULL,0,&br,NULL);
    Sleep(1);
}

int main(void) {
    printf("=== KIQ with DEFAULT_PAGE=system ===\n");

    hGpu=CreateFileW(L"\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
    if(hGpu==INVALID_HANDLE_VALUE){printf("FAIL err=%lu\n",GetLastError());return 1;}

    /* GPU init */
    {
        UCHAR init[32]={0};
        *(UINT64*)(init+0)=0xFE800000ULL;*(UINT32*)(init+8)=0x00080000;
        *(UINT32*)(init+12)=1;*(UINT64*)(init+16)=0xC0000000ULL;*(UINT32*)(init+24)=0x20000000;
        DWORD br=0;
        if(!DeviceIoControl(hGpu,IOCTL_GPU_INIT,init,sizeof(init),NULL,0,&br,NULL))
            {printf("GPU init FAIL\n");goto end;}
    }
    printf("GPU init OK\n");

    /* PT_SETUP to allocate ring and KIQ_BASE */
    ULONG ringPA=0;
    {
        UCHAR buf[512]={0}; DWORD br=0;
        if(!DeviceIoControl(hGpu,IOCTL_GCVM_PT_SETUP,NULL,0,buf,sizeof(buf),&br,NULL))
            {printf("PT_SETUP FAIL\n");goto end;}
        ULONG *p=(ULONG*)buf;
        ringPA = p[1];
        printf("PT_SETUP: result=0x%08X ringPA=0x%08X\n",p[9],ringPA);
        printf("  CtxCntl before PT_SETUP=0x%08X\n", p[0]);
    }
    if(ringPA==0){printf("No ring\n");goto end;}

    /* Read CONTEXT0_CNTL */
    ULONG ctx0 = R(0x0B460);
    printf("CONTEXT0_CNTL before=0x%08X\n", ctx0);

    /* Read L2_CNTL */
    ULONG l2 = R(0x0B360);
    printf("GCVM_L2_CNTL=0x%08X (bit8=%u)\n", l2, (l2>>8)&1);

    /* Write CONTEXT0_CNTL = DEFAULT_PAGE=2 (system), PT_ENABLE=0 */
    /* DEFAULT_PAGE in bits 2:1 = 10b = 2 */
    W(0x0B460, 0x04);  /* bit1=1, bit0=0: page tables off, default=system */
    ULONG ctxAfter = R(0x0B460);
    printf("CONTEXT0_CNTL after=0x%08X (PT=%u DEFAULT=%u)\n", ctxAfter, ctxAfter&1, (ctxAfter>>1)&3);

    /* Set KIQ_ACTIVE=1 */
    W(0xE080, 1);
    printf("KIQ_ACTIVE=%u\n", R(0xE080));

    /* Write a WRITE_DATA command to the ring instead of NOP */
    /* The ringPA is in system memory. PT_SETUP writes a NOP (0xC0001000) there.
     * Let's try with NOP first, then with WRITE_DATA */
    
    /* Kick: WPTR=1 (past NOP header at ring offset 0) */
    printf("\n--- Kick (NOP) ---\n");
    W(0xE078, 1);
    printf("WPTR=1 RPTR=%u\n", R(0xE06C));

    /* Poll */
    for(int i=0;i<300;i++){
        ULONG rptr=R(0xE06C);
        ULONG scr=R(0x32D4);
        if(rptr!=0){printf(">>> RPTR=%u after %dms (SCRATCH=0x%08X)\n",rptr,i*10,scr);break;}
        Sleep(10);
        if(i==299) printf(">>> RPTR=0 after 3s\n");
    }

    /* Also try WPTR=4 (skip 4 DWORDS to ensure we're past any garbage) */
    printf("\n--- Kick WPTR=4 ---\n");
    W(0xE078, 4);
    printf("WPTR=4 RPTR=%u\n", R(0xE06C));
    for(int i=0;i<100;i++){
        ULONG rptr=R(0xE06C);
        if(rptr!=0){printf(">>> RPTR=%u after %dms!\n",rptr,i*10);break;}
        Sleep(10);
        if(i==99) printf(">>> RPTR=0 after 1s\n");
    }

    /* Read back all KIQ registers */
    printf("\n=== KIQ final ===\n");
    for(int off=0xE060;off<=0xE098;off+=4)
        printf("  [0x%04X]=0x%08X\n",off,R(off));
    printf("CONTEXT0_CNTL=0x%08X MEC_ME1_CNTL=0x%08X ME_CNTL=0x%08X\n",
        R(0x0B460), R(0x7A00), R(0x4A74));

end:
    if(hGpu!=INVALID_HANDLE_VALUE)CloseHandle(hGpu);
    return 0;
}
