/* gcvm-pt-test.c — KIQ ring test with SAFE register writes (no CONTEXT0_CNTL modification) */
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

static void dump_kiq(void) {
    printf("\n=== KIQ dump ===\n");
    for(int off=0xE060;off<=0E098;off+=4)
        printf("  [0x%04X]=0x%08X\n",off,R(off));
    printf("GRBM_GFX_INDEX=0x%08X GRBM_GFX_CNTL=0x%08X\n", R(0x34D0), R(0x2022));
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

    /* Pre-setup state */
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
        printf("  PtPhys[0]=0x%08X%08X [1]=0x%08X%08X [2]=0x%08X%08X\n",
            p[3],p[2], p[5],p[4], p[7],p[6]);
        printf("  InvStatus=%lu PtBase0=0x%08X%08X\n",
            p[10], p[12], p[11]);
    }
    if(ringPA==0){printf("No ring\n");goto end;}

    /* After PT_SETUP state */
    printf("\n=== After PT_SETUP ===\n");
    dump_kiq();

    /* Read current CONTEXT0_CNTL — do NOT modify! */
    ULONG ctx0 = R(0x0B460);
    printf("\nCONTEXT0_CNTL=0x%08X (PT=%u DEFAULT_PAGE=%u)\n",
        ctx0, ctx0&1, (ctx0>>1)&3);
    printf("(NOT modifying — keeping BIOS PT_ENABLE=1)\n");

    /* Read L2_CNTL */
    ULONG l2 = R(0x0B360);
    printf("GCVM_L2_CNTL=0x%08X (bit8 sys_aperture=%u)\n", l2, (l2>>8)&1);

    /* Check ring: read NOP at offset 0 */
    printf("\nRing at PA 0x%08X: ", ringPA);
    /* We can't read system RAM from MMIO — read KIQ registers to verify setup */
    ULONG kBaseLo = R(0xE060);
    ULONG kBaseHi = R(0xE064);
    printf("KIQ_BASE=0x%08X%08X (expected=0x%08X)\n", kBaseHi, kBaseLo, ringPA);
    if (kBaseLo != ringPA) {
        printf("WARNING: KIQ_BASE doesn't match ringPA!\n");
    }

    /* Approach 1: Set KIQ_ACTIVE, WPTR, poll RPTR (no GRBM select) */
    printf("\n=== Approach 1: Simple KIQ kick ===\n");

    /* Read current ME_CNTL */
    ULONG meCntl = R(0x4A74);
    printf("ME_CNTL=0x%08X (ME_HALT=%u PFP_HALT=%u CE_HALT=%u)\n",
        meCntl, (meCntl>>28)&1, (meCntl>>30)&1, (meCntl>>29)&1);

    /* Ensure MEC is unhalted */
    ULONG mecMe1 = R(0x7A00);
    printf("MEC_ME1_CNTL=0x%08X (halt=%u)\n", mecMe1, mecMe1 & 1);

    /* Set KIQ_ACTIVE=1 */
    W(0xE080, 1);
    printf("KIQ_ACTIVE=%u\n", R(0xE080));

    /* Kick: WPTR=1 (NOP at ring offset 0 has its DWORD, CP expects WPTR pointing past it) */
    printf("--- Kick WPTR=1 ---\n");
    W(0xE078, 1);
    printf("WPTR=1 RPTR=%u\n", R(0xE06C));

    /* Poll */
    for(int i=0;i<300;i++){
        ULONG rptr=R(0xE06C);
        ULONG scr=R(0x32D4);
        if(rptr!=0){printf(">>> RPTR=%u after %dms (SCRATCH=0x%08X)\n",rptr,i*10,scr);break;}
        Sleep(10);
        if(i==299) printf(">>> RPTR=0 after 3s (no progress)\n");
    }

    /* Approach 2: Try with GRBM_GFX_CNTL (Linux register) */
    printf("\n=== Approach 2: KIQ with GRBM_GFX_CNTL ME select ===\n");
    W(0x2022, 1 << 16);  /* GRBM_GFX_CNTL ME=1 */
    printf("GRBM_GFX_CNTL=0x%08X\n", R(0x2022));

    W(0xE080, 1);  /* KIQ_ACTIVE=1 */
    W(0xE078, 4);  /* WPTR=4 */
    printf("WPTR=4 RPTR=%u\n", R(0xE06C));
    for(int i=0;i<200;i++){
        ULONG rptr=R(0xE06C);
        ULONG scr=R(0x32D4);
        if(rptr!=0){printf(">>> RPTR=%u after %dms (SCRATCH=0x%08X)\n",rptr,i*10,scr);break;}
        Sleep(10);
        if(i==199) printf(">>> RPTR=0 after 2s\n");
    }

    /* Approach 3: Try with GRBM_GFX_INDEX (our standard register) */
    printf("\n=== Approach 3: KIQ with GRBM_GFX_INDEX ME=1 ===\n");
    W(0x34D0, 0x00010000);  /* ME=1, PIPE=0, QUEUE=0 */
    printf("GRBM_GFX_INDEX=0x%08X\n", R(0x34D0));

    W(0xE080, 1);
    W(0xE078, 4);
    printf("WPTR=4 RPTR=%u\n", R(0xE06C));
    for(int i=0;i<200;i++){
        ULONG rptr=R(0xE06C);
        ULONG scr=R(0x32D4);
        if(rptr!=0){printf(">>> RPTR=%u after %dms (SCRATCH=0x%08X)\n",rptr,i*10,scr);break;}
        Sleep(10);
        if(i==199) printf(">>> RPTR=0 after 2s\n");
    }

    /* Approach 4: Reset GRBM to broadcast, unhalt ME, try again */
    printf("\n=== Approach 4: Unhalt ME, broadcast GRBM ===\n");
    W(0x34D0, 0xE0000000);  /* broadcast GRBM */
    W(0x4A74, 0);            /* unhalt ME+PFP+CE (all firmware loaded?) */
    printf("ME_CNTL=0x%08X\n", R(0x4A74));

    W(0xE080, 1);
    W(0xE078, 4);
    printf("WPTR=4 RPTR=%u\n", R(0xE06C));
    for(int i=0;i<200;i++){
        ULONG rptr=R(0xE06C);
        ULONG scr=R(0x32D4);
        if(rptr!=0){printf(">>> RPTR=%u after %dms (SCRATCH=0x%08X)\n",rptr,i*10,scr);break;}
        Sleep(10);
        if(i==199) printf(">>> RPTR=0 after 2s\n");
    }

    /* Final state dump */
    printf("\n=== Final State ===\n");
    dump_kiq();

end:
    if(hGpu!=INVALID_HANDLE_VALUE)CloseHandle(hGpu);
    return 0;
}
