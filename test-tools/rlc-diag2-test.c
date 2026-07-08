/* rlc-diag2-test.c — Targeted RLC/KIQ probe */
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
static void P(const char *name, ULONG off) {
    ULONG v = R(off);
    if (v == 0xFFFFFFFF) { printf("  %-30s [0x%04X] = DEAD\n", name, off); return; }
    W(off, v ^ 0xFFFFFFFF);
    ULONG v2 = R(off);
    W(off, v);
    printf("  %-30s [0x%04X] = 0x%08X  [%s]\n", name, off, v,
        (v2 == (v ^ 0xFFFFFFFF)) ? "WR" : (v2 != v) ? "W1C" : "RO");
}

int main(void) {
    hGpu=CreateFileW(L"\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
    if(hGpu==INVALID_HANDLE_VALUE){printf("FAIL err=%lu\n",GetLastError());return 1;}

    UCHAR init[32]={0};
    *(UINT64*)(init+0)=0xFE800000ULL;*(UINT32*)(init+8)=0x00080000;
    *(UINT32*)(init+12)=1;*(UINT64*)(init+16)=0xC0000000ULL;*(UINT32*)(init+24)=0x20000000;
    DWORD br=0;
    if(!DeviceIoControl(hGpu,IOCTL_GPU_INIT,init,32,NULL,0,&br,NULL))
        {printf("GPU init FAIL\n");CloseHandle(hGpu);return 1;}
    printf("GPU init OK\n\n");

    /* Check KIQ registers in detail */
    printf("=== KIQ Registers (0xE060-0xE080) ===\n");
    P("KIQ_BASE_LO",   0xE060);
    P("KIQ_BASE_HI",   0xE064);
    P("KIQ_CNTL/SIZE", 0xE068);
    P("KIQ_RPTR",      0xE06C);
    P("KIQ_PQ_CTL",    0xE070);
    P("KIQ_DOORBELL",  0xE074);
    P("KIQ_WPTR",      0xE078);
    P("KIQ_VMID",      0xE07C);
    P("KIQ_ACTIVE",    0xE080);

    /* Check RLC registers at 0x4B20 in detail */
    printf("\n=== RLC_CNTL-like at 0x4B20 ===\n");
    ULONG rlc20 = R(0x4B20);
    printf("  [0x4B20] = 0x%08X\n", rlc20);
    /* Try writing specific bit patterns (careful: may affect RLC state) */
    W(0x4B20, 0xFFFFFFFF);
    printf("  After write FFFFFFFF: %08X\n", R(0x4B20));
    W(0x4B20, rlc20); /* restore */
    printf("  After restore: %08X\n", R(0x4B20));

    /* CP_HQD_PQ_CONTROL (PSP driver uses 0x90F0, Linux says 0x9148) */
    printf("\n=== CP_HQD_PQ_CONTROL ===\n");
    P("CP_HQD_PQ_CONTROL(PSP=0x90F0)", 0x90F0);
    P("CP_HQD_PQ_CONTROL(Linux=0x9148)", 0x9148);

    /* Check CP_HQD_PQ_BASE/CONTROL/WPTR (ring addresses) */
    printf("\n=== CP_HQD Ring State ===\n");
    printf("  CP_MQD_BASE_ADDR      = 0x%08X\n", R(0x9104));
    printf("  CP_HQD_ACTIVE         = 0x%08X\n", R(0x910C));
    printf("  CP_HQD_VMID           = 0x%08X\n", R(0x9110));
    printf("  CP_HQD_PQ_BASE        = 0x%08X\n", R(0x9124));
    printf("  CP_HQD_PQ_CONTROL     = 0x%08X\n", R(0x9148));
    printf("  CP_HQD_PQ_WPTR_LO     = 0x%08X\n", R(0x91DC));

    /* Check GRBM_STATUS */
    printf("\n=== GRBM Status ===\n");
    ULONG grbm = R(0x3260);
    printf("  GRBM_STATUS  = 0x%08X\n", grbm);
    printf("    bits: GUI_ACTIVE=%d IA_BUSY=%d WD_BUSY=%d ME_BUSY=%d\n",
        (grbm>>31)&1, (grbm>>12)&1, (grbm>>11)&1, (grbm>>0)&1);
    printf("  GRBM_STATUS2 = 0x%08X\n", R(0x3264));

    /* Check if COMPUTE dispatch registers are working after RLC_CP_SCHEDULERS=0xFF */
    printf("\n=== COMPUTE dispatch check ===\n");
    printf("  DISPATCH_INITIATOR = 0x%08X\n", R(0x80E0));
    printf("  PGM_LO             = 0x%08X\n", R(0x8110));
    printf("  PGM_HI             = 0x%08X\n", R(0x8114));
    printf("  PGM_RSRC1          = 0x%08X\n", R(0x8128));
    printf("  PGM_RSRC2          = 0x%08X\n", R(0x812C));
    printf("  DIM_X              = 0x%08X\n", R(0x80E4));

    /* Try DISPATCH_INITIATOR trigger (W1C) */
    printf("\n=== DISPATCH write test ===\n");
    printf("  Writing DISPATCH_INITIATOR=1...\n");
    W(0x80E0, 1);
    Sleep(10);
    ULONG diAfter = R(0x80E0);
    printf("  After dispatch: DISPATCH_INITIATOR=%08X PGM_LO=%08X\n", diAfter, R(0x8110));
    printf("  CP_STATUS=%08X GRBM_STATUS=%08X\n", R(0x426C), R(0x3260));

    CloseHandle(hGpu);
    printf("\nDone.\n");
    return 0;
}
