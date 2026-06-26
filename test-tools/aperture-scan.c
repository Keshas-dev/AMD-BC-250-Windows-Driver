/* aperture-scan.c — Find GPU system aperture + 0xECA8 test with safe ring */
#include <windows.h>
#include <stdio.h>

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

int main(void) {
    hGpu=CreateFileW(L"\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
    if(hGpu==INVALID_HANDLE_VALUE){printf("FAIL err=%lu\n",GetLastError());return 1;}

    UCHAR init[32]={0};
    *(UINT64*)(init+0)=0xFE800000ULL;*(UINT32*)(init+8)=0x00080000;
    *(UINT32*)(init+12)=1;*(UINT64*)(init+16)=0xC0000000ULL;*(UINT32*)(init+24)=0x20000000;
    DWORD br=0;
    if(!DeviceIoControl(hGpu,IOCTL_GPU_INIT,init,sizeof(init),NULL,0,&br,NULL))
        {printf("GPU init FAIL\n");CloseHandle(hGpu);return 1;}
    printf("GPU init OK\n\n");

    /* Phase 1: Read BAR2 VRAM signature */
    printf("=== BAR2/VRAM signature check ===\n");
    for(ULONG off=0xE0000000; off<0xE0100000; off+=0x100000) {
        printf("BAR2[0x%08X] WARNING: reading VRAM... %s\n", off, "SKIPPED (would map outside 512KB)");
    }
    /* Can't read VRAM via these IOCTLs — only BAR5. Need driver support. */

    /* Phase 2: Read GCVM aperture registers */
    printf("\n=== GCVM L2/System Aperture Registers ===\n");
    ULONG gcvmRegs[] = {
        0x0B300, 0x0B304, 0x0B308, 0x0B30C, 0x0B310, 0x0B314, 0x0B318, 0x0B31C,
        0x0B320, 0x0B324, 0x0B328, 0x0B32C, 0x0B330, 0x0B334, 0x0B338, 0x0B33C,
        0x0B340, 0x0B344, 0x0B348, 0x0B34C, 0x0B350, 0x0B354, 0x0B358, 0x0B35C,
        0x0B360, 0x0B364, 0x0B368, 0x0B36C, 0x0B370, 0x0B374, 0x0B378, 0x0B37C,
        0x0B380, 0x0B384, 0x0B388, 0x0B38C, 0x0B390, 0x0B394, 0x0B398, 0x0B39C,
        0x0B3A0, 0x0B3A4, 0x0B3A8, 0x0B3AC, 0x0B3B0, 0x0B3B4, 0x0B3B8, 0x0B3BC,
        0x0B3C0, 0x0B3C4, 0x0B3C8, 0x0B3CC, 0x0B3D0, 0x0B3D4, 0x0B3D8, 0x0B3DC,
        0x0B3E0, 0x0B3E4, 0x0B3E8, 0x0B3EC, 0x0B3F0, 0x0B3F4, 0x0B3F8, 0x0B3FC,
    };
    for(int i=0; i<sizeof(gcvmRegs)/sizeof(gcvmRegs[0]); i+=4) {
        ULONG off = gcvmRegs[i];
        ULONG v0 = R(off), v1 = R(off+4), v2 = R(off+8), v3 = R(off+12);
        printf("[0x%04X]=0x%08X  [0x%04X]=0x%08X  [0x%04X]=0x%08X  [0x%04X]=0x%08X\n",
            off, v0, off+4, v1, off+8, v2, off+12, v3);
    }

    /* Phase 3: Scan CP fence/doorbell range 0x3AD8-0x3AEC */
    printf("\n=== CP Fence/Doorbell Registers (0x3AD8-0x3AEC) ===\n");
    for(ULONG off=0x3AD8; off<=0x3AEC; off+=4) {
        ULONG v = R(off);
        printf("[0x%04X] = 0x%08X", off, v);
        if(v != 0 && v != 0xFFFFFFFF) {
            printf("  (addr range 0x%08X-0x%08X)", v, v + 0x1000);
        }
        printf("\n");
    }

    /* Phase 4: Look for aperture_base/size registers */
    printf("\n=== Extended aperture scan 0xB400-0xB600 ===\n");
    for(ULONG off=0xB400; off<=0xB600; off+=4) {
        ULONG v = R(off);
        if(v != 0 && v != 0xFFFFFFFF)
            printf("[0x%04X] = 0x%08X\n", off, v);
    }

    /* Phase 5: Context0 page table entry registers */
    printf("\n=== Context0 PT Range (0xB408-0xB4AC) ===\n");
    for(ULONG off=0xB408; off<=0xB4AC; off+=4) {
        ULONG v = R(off);
        if(v != 0 && v != 0xFFFFFFFF)
            printf("[0x%04X] = 0x%08X\n", off, v);
    }

    /* Phase 6: RLC_CP_SCHEDULERS safe test (KIQ_ACTIVE=0 first!) */
    printf("\n=== RLC_CP_SCHEDULERS safe write test ===\n");
    /* First disable KIQ to ensure no scheduling while we probe */
    W(0xE080, 0);
    Sleep(10);
    printf("KIQ_ACTIVE=0\n");

    ULONG rlcBefore = R(0xECA8);
    printf("RLC_CP_SCHEDULERS before=0x%08X\n", rlcBefore);
    printf("ME_CNTL before=0x%08X\n", R(0x4A64));

    /* Write RLC_CP_SCHEDULERS = 0 to disable, then read back */
    W(0xECA8, 0);
    printf("RLC_CP_SCHEDULERS after zero=0x%08X\n", R(0xECA8));

    /* Now write 0xA0 and check if MEC state changes */
    W(0xECA8, 0xA0);
    printf("RLC_CP_SCHEDULERS after 0xA0=0x%08X\n", R(0xECA8));
    printf("ME_CNTL after 0xA0=0x%08X\n", R(0x4A64));

    /* Check GRBM_STATUS for any engine activity */
    for(int i=0; i<10; i++) {
        ULONG stat = R(0x3260);
        ULONG scr = R(0x32D4);
        printf("GRBM_STATUS=0x%08X SCRATCH=0x%08X (i=%d)\n", stat, scr, i);
        if(stat != 0) { printf(">>> Engine active!\n"); break; }
        if(scr != 0) { printf(">>> Scratch changed!\n"); break; }
        Sleep(50);
    }

    /* Disable RLC and restore */
    W(0xECA8, 0);
    if(rlcBefore != 0) W(0xECA8, rlcBefore);
    printf("Restored RLC=0x%08X\n", R(0xECA8));

    CloseHandle(hGpu);
    printf("\nDone.\n");
    return 0;
}
