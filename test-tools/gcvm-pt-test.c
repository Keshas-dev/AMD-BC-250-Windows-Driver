/* gcvm-pt-test.c — KIQ ring test with SAFE register writes only */
#include <windows.h>
#include <stdio.h>

#define IOCTL_GPU_READ    0x80000B88
#define IOCTL_GPU_WRITE   0x80000B8C
#define IOCTL_GPU_INIT    0x80000B80
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

static void dump_kiq(void) {
    printf("\n=== KIQ dump ===\n");
    for(int off=0xE060;off<=0xE098;off+=4)
        printf("  [0x%04X]=0x%08X\n",off,R(off));
    printf("GRBM_GFX_INDEX=0x%08X\n", R(0x34D0));
    printf("MEC_ME1_CNTL=0x%08X ME_CNTL=0x%08X\n", R(0x7A00), R(0x4A74));
    printf("GRBM_STATUS=0x%08X SCRATCH=0x%08X\n", R(0x3260), R(0x32D4));
}

int main(void) {
    printf("=== KIQ ring test (SAFE mode) ===\n");

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

    /* State dump before PT_SETUP */
    printf("\n=== Before PT_SETUP ===\n");
    dump_kiq();

    /* PT_SETUP to allocate ring and setup KIQ_BASE */
    ULONG ringPA=0;
    {
        UCHAR buf[512]={0}; DWORD br=0;
        if(!DeviceIoControl(hGpu,IOCTL_GCVM_PT_SETUP,NULL,0,buf,sizeof(buf),&br,NULL))
            {printf("PT_SETUP FAIL\n");goto end;}
        ULONG *p=(ULONG*)buf;
        ringPA = p[1];
        printf("\nPT_SETUP: result=0x%08X ringPA=0x%08X\n", p[9], ringPA);
        printf("  CtxCntl before=0x%08X\n", p[0]);
    }
    if(ringPA==0){printf("No ring\n");goto end;}

    /* State dump after PT_SETUP */
    printf("\n=== After PT_SETUP ===\n");
    dump_kiq();

    /* Read key registers (read-only, safe) */
    ULONG ctx0 = R(0x0B460);
    printf("\nCONTEXT0_CNTL=0x%08X (PT=%u DEFAULT_PAGE=%u)\n",
        ctx0, ctx0&1, (ctx0>>1)&3);

    ULONG l2 = R(0x0B360);
    printf("GCVM_L2_CNTL=0x%08X (bit8 sys_aperture=%u)\n", l2, (l2>>8)&1);

    ULONG kBaseLo = R(0xE060);
    ULONG kBaseHi = R(0xE064);
    printf("KIQ_BASE=0x%08X%08X (expected=0x%08X)\n", kBaseHi, kBaseLo, ringPA);

    ULONG meCntl = R(0x4A74);
    printf("ME_CNTL=0x%08X (ME_HALT=%u PFP_HALT=%u CE_HALT=%u)\n",
        meCntl, (meCntl>>28)&1, (meCntl>>30)&1, (meCntl>>29)&1);

    ULONG mecMe1 = R(0x7A00);
    printf("MEC_ME1_CNTL=0x%08X (halt=%u)\n", mecMe1, mecMe1 & 1);

    /* Deactivate KIQ first (safe guard) */
    W(0xE080, 0);
    Sleep(10);

    /* Set KIQ_ACTIVE=1, then kick WPTR, poll RPTR */
    printf("\n=== KIQ kick (WPTR=1) ===\n");
    W(0xE080, 1);
    printf("KIQ_ACTIVE=%u\n", R(0xE080));
    W(0xE078, 1);
    printf("WPTR=1 RPTR=%u\n", R(0xE06C));

    for(int i=0;i<300;i++){
        ULONG rptr=R(0xE06C);
        ULONG scr=R(0x32D4);
        if(rptr!=0){printf(">>> RPTR=%u after %dms (SCRATCH=0x%08X)\n",rptr,i*10,scr);break;}
        Sleep(10);
        if(i==299) printf(">>> RPTR=0 after 3s\n");
    }

    /* Try with WPTR=4 */
    printf("\n=== KIQ kick (WPTR=4) ===\n");
    W(0xE078, 4);
    printf("WPTR=4 RPTR=%u\n", R(0xE06C));
    for(int i=0;i<200;i++){
        ULONG rptr=R(0xE06C);
        if(rptr!=0){printf(">>> RPTR=%u after %dms\n",rptr,i*10);break;}
        Sleep(10);
        if(i==199) printf(">>> RPTR=0 after 2s\n");
    }

    /* Try GRBM_GFX_INDEX ME=1 select */
    printf("\n=== KIQ with GRBM_GFX_INDEX ME=1 ===\n");
    W(0x34D0, 0x00010000);
    printf("GRBM_GFX_INDEX=0x%08X\n", R(0x34D0));
    W(0xE080, 1);
    W(0xE078, 4);
    printf("WPTR=4 RPTR=%u\n", R(0xE06C));
    for(int i=0;i<200;i++){
        ULONG rptr=R(0xE06C);
        if(rptr!=0){printf(">>> RPTR=%u after %dms\n",rptr,i*10);break;}
        Sleep(10);
        if(i==199) printf(">>> RPTR=0 after 2s\n");
    }

    /* Try with RLC_CP_SCHEDULERS at correct offset 0xECA8 */
    printf("\n=== RLC_CP_SCHEDULERS(0xECA8=0xA0) + KIQ kick ===\n");
    ULONG rlcBefore = R(0xECA8);
    printf("RLC_CP_SCHEDULERS before=0x%08X\n", rlcBefore);
    W(0xECA8, 0xA0);  /* ENABLE=1, ME=1 */
    printf("RLC_CP_SCHEDULERS after=0x%08X\n", R(0xECA8));

    W(0xE080, 1);
    W(0xE078, 1);
    printf("WPTR=1 RPTR=%u\n", R(0xE06C));
    for(int i=0;i<300;i++){
        ULONG rptr=R(0xE06C);
        ULONG scr=R(0x32D4);
        if(rptr!=0){printf(">>> RPTR=%u after %dms (SCRATCH=0x%08X)\n",rptr,i*10,scr);break;}
        Sleep(10);
        if(i==299) printf(">>> RPTR=0 after 3s\n");
    }

    /* Restore RLC */
    if(rlcBefore != R(0xECA8)) W(0xECA8, rlcBefore);

    printf("\n=== Final State ===\n");
    dump_kiq();

end:
    if(hGpu!=INVALID_HANDLE_VALUE)CloseHandle(hGpu);
    return 0;
}
