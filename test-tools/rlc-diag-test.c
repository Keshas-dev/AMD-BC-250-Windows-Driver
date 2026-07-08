/* rlc-diag-test.c — Probe RLC registers at multiple candidate offsets */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

#define IOCTL_GPU_READ  0x80000B88
#define IOCTL_GPU_WRITE 0x80000B8C
#define IOCTL_GPU_INIT  0x80000B80

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
    Sleep(2);
}
static void Probe(const char *name, ULONG off) {
    ULONG v = R(off);
    if (v == 0xFFFFFFFF) return; /* skip dead */
    /* Write test: write ~v, read back, restore */
    W(off, v ^ 0xFFFFFFFF);
    ULONG v2 = R(off);
    W(off, v);
    printf("  %-32s [0x%04X] = 0x%08X  %s\n", name, off, v,
        (v2 == (v ^ 0xFFFFFFFF)) ? "WRITABLE" : (v2 != v) ? "W1C?" : "RO");
}

int main(void) {
    hGpu=CreateFileW(L"\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
    if(hGpu==INVALID_HANDLE_VALUE){printf("FAIL err=%lu\n",GetLastError());return 1;}

    /* Init HW */
    UCHAR init[32]={0};
    *(UINT64*)(init+0)=0xFE800000ULL;*(UINT32*)(init+8)=0x00080000;
    *(UINT32*)(init+12)=1;*(UINT64*)(init+16)=0xC0000000ULL;*(UINT32*)(init+24)=0x20000000;
    DWORD br=0;
    if(!DeviceIoControl(hGpu,IOCTL_GPU_INIT,init,sizeof(init),NULL,0,&br,NULL))
        {printf("GPU init FAIL\n");CloseHandle(hGpu);return 1;}
    printf("GPU init OK\n\n");

    /* === Phase 1: RLC register candidates at multiple offsets === */
    printf("=== Phase 1: RLC candidates at 0x3A00 range (driver RLC block) ===\n");
    Probe("RLC_CNTL (driver)",     0x3A00);
    Probe("RLC_CNTL2",             0x3A04);
    Probe("RLC_CNTL3",             0x3A08);
    Probe("RLC_MGCG_CTRL",         0x3A10);
    Probe("RLC_CLK_CNTL",          0x3A14);
    Probe("RLC_SPARE_BOOL",        0x3A1C);
    Probe("RLC_SMU_SAFE_MODE",     0x3A30);
    Probe("RLC_SMU_MSG_CNTL",      0x3A34);
    Probe("RLC_INT_CNTL",          0x3A40);
    Probe("RLC_INT_STATUS",        0x3A44);
    Probe("RLC_INT_CLEAR",         0x3A48);
    Probe("RLC_GPM_UCODE_ADDR",    0x3A4C);
    Probe("RLC_GPM_UCODE_DATA",    0x3A50);

    printf("\n=== Phase 2: RLC candidates at 0x4B00-0x4C20 range ===\n");
    Probe("RLC_CNTL (ring-probe)", 0x4B20);
    Probe("RLC_ ?",                0x4B24);
    Probe("RLC_ ?",                0x4B28);
    Probe("RLC_ ?",                0x4B2C);
    Probe("RLC_CNTL (Gemini)",     0x4C00);
    Probe("RLC_STATUS (Gemini)",   0x4C1C);
    /* Probe around 0x4C00 */
    for (ULONG o = 0x4BE0; o <= 0x4C20; o += 4) {
        ULONG v = R(o);
        if (v != 0xFFFFFFFF) printf("  [0x%04X] = 0x%08X  (near 0x4C00)\n", o, v);
    }

    printf("\n=== Phase 3: RLC_CP_SCHEDULERS and nearby (0xECA0-0xED00) ===\n");
    Probe("RLC_CP_SCHEDULERS",     0xECA8);
    /* Scan the RLC scheduler range */
    for (ULONG off = 0xECA0; off <= 0xED00; off += 4) {
        ULONG v = R(off);
        if (v != 0xFFFFFFFF) {
            ULONG v2 = R(off); /* read twice to check stability */
            printf("  [0x%04X] = 0x%08X (stable=%d)%s\n", off, v, v==v2,
                off==0xECA8 ? "  <-- RLC_CP_SCHEDULERS (known)" : "");
        }
    }

    /* === Phase 4: Check RLC state via CP_STATUS and GRBM === */
    printf("\n=== Phase 4: CP/ME/MEC/SDMA engine status ===\n");
    {
        ULONG gs = R(0x3260);
        ULONG cpSt = R(0x426C);
        ULONG mecInt = R(0x4270);
        ULONG cpInt = R(0x43A0);
        ULONG rlcSched = R(0xECA8);
        ULONG meCntl = R(0x4A74);
        ULONG mecCntl = R(0x4B14);
        printf("  GRBM_STATUS        = 0x%08X\n", gs);
        printf("  CP_STATUS          = 0x%08X\n", cpSt);
        printf("  MEC_INT_ST         = 0x%08X\n", mecInt);
        printf("  CP_INT_ST          = 0x%08X\n", cpInt);
        printf("  RLC_CP_SCHEDULERS  = 0x%08X\n", rlcSched);
        printf("  ME_CNTL (0x4A74)   = 0x%08X\n", meCntl);
        printf("  MEC_CNTL (0x4B14)  = 0x%08X\n", mecCntl);
        printf("  CP_HQD_ACTIVE      = 0x%08X\n", R(0x910C));
        printf("  CP_STATUS (0x426C) = 0x%08X  %s\n", cpSt,
            cpSt==0 ? "IDLE (RLC not scheduling)" : "ACTIVE!");
        printf("  GRBM bit31(SE)     = %d  bit30(TA) = %d\n",
            (gs>>31)&1, (gs>>30)&1);
    }

    /* === Phase 5: Try enabling RLC if disabled === */
    printf("\n=== Phase 5: Attempt RLC enable ===\n");
    /* Try writing to 0xECA8 with scheduler bits for compute/GFX */
    ULONG curRlc = R(0xECA8);
    printf("  Current RLC_CP_SCHEDULERS = 0x%08X\n", curRlc);
    /* RLC_SCHEDULERS bits: compute=0x80, gfx=0x20, kiq=0x100 */
    if ((curRlc & 0xA0) != 0xA0) {
        printf("  Missing compute (0x80) or gfx (0x20) bits. Setting...\n");
        W(0xECA8, curRlc | 0xA0);
        Sleep(10);
        printf("  After write: 0x%08X\n", R(0xECA8));
    }
    /* Also try 0x3A00 if it's alive */
    ULONG rlcCntl = R(0x3A00);
    if (rlcCntl != 0xFFFFFFFF) {
        printf("  RLC_CNTL(0x3A00) = 0x%08X  bit0=%s\n", rlcCntl,
            (rlcCntl & 1) ? "ENABLED" : "DISABLED");
        if (!(rlcCntl & 1)) {
            printf("  Attempting RLC_ENABLE...\n");
            W(0x3A00, rlcCntl | 1);
            Sleep(10);
            printf("  After: 0x%08X  bit0=%s\n", R(0x3A00),
                (R(0x3A00) & 1) ? "ENABLED!" : "still DISABLED");
        }
    }

    /* === Phase 6: Retry KIQ poll === */
    printf("\n=== Phase 6: KIQ ring poll test ===\n");
    printf("  KIQ_BASE=0x%08X_%08X SIZE=0x%08X\n", R(0xE064), R(0xE060), R(0xE068));
    printf("  KIQ_WPTR=%u KIQ_RPTR=%u\n", R(0xE078), R(0xE06C));
    ULONG scratchBefore = R(0x32D4);
    printf("  SCRATCH=0x%08X\n", scratchBefore);

    /* Write WPTR=1 to kick KIQ (if any KIQ processing can happen) */
    W(0xE078, 1);
    int advanced = 0;
    for (int i = 0; i < 200; i++) {
        ULONG rpt = R(0xE06C);
        if (rpt != 0) { printf("  RPTR advanced to %u after %dms!\n", rpt, i*10); advanced=1; break; }
        Sleep(10);
    }
    if (!advanced) printf("  RPTR stuck at 0 after 2s (RLC still not scheduling)\n");

    CloseHandle(hGpu);
    printf("\nDone.\n");
    return 0;
}
