/* rlc-scan.c — RLC register range scan (0xEC00-0xEF00) + write test */
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
    Sleep(1);
}

int main(void) {
    hGpu=CreateFileW(L"\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
    if(hGpu==INVALID_HANDLE_VALUE){printf("FAIL err=%lu\n",GetLastError());return 1;}

    /* GPU init */
    UCHAR init[32]={0};
    *(UINT64*)(init+0)=0xFE800000ULL;*(UINT32*)(init+8)=0x00080000;
    *(UINT32*)(init+12)=1;*(UINT64*)(init+16)=0xC0000000ULL;*(UINT32*)(init+24)=0x20000000;
    DWORD br=0;
    if(!DeviceIoControl(hGpu,IOCTL_GPU_INIT,init,sizeof(init),NULL,0,&br,NULL))
        {printf("GPU init FAIL\n");CloseHandle(hGpu);return 1;}
    printf("GPU init OK\n\n");

    /* Phase 1: Read RLC range 0xEC00-0xEF00 (4-byte aligned) */
    printf("=== RLC Range Read (0xEC00-0xEF00) ===\n");
    printf("Offset     Value      Notes\n");
    printf("--------   --------   -----\n");
    ULONG prevVal = 0, prevOff = 0;
    int sameCount = 0;
    for(int off=0xEC00; off<=0xEF00; off+=4) {
        ULONG v = R(off);
        if(v == 0xFFFFFFFF) continue;  /* dead register */
        if(v == prevVal && off == prevOff + 4) {
            sameCount++;
            if(sameCount <= 3)
                printf("[0x%04X]  0x%08X   (same pattern continues)\n", off, v);
        } else {
            if(sameCount > 3)
                printf("         ... (%d more with same value 0x%08X)\n", sameCount-3, prevVal);
            sameCount = 0;
            const char *note = "";
            if(off == 0xECA0 || off == 0xECA4 || off == 0xECA8 || off == 0xECAC)
                note = " <-- RLC_CP_SCHEDULERS candidate";
            printf("[0x%04X]  0x%08X%s\n", off, v, note);
        }
        prevVal = v;
        prevOff = off;
    }
    if(sameCount > 3)
        printf("         ... (%d more with same value 0x%08X)\n", sameCount-3, prevVal);

    /* Phase 2: Write test on promising registers */
    printf("\n=== Write Test (safe pattern) ===\n");
    printf("Offset     Before     After      Writable?\n");
    printf("--------   --------   --------   --------\n");

    /* Candidates for write test: registers that returned non-0xFFFFFFFF, non-0 */
    ULONG candidates[] = {
        0xECA0, 0xECA4, 0xECA8, 0xECAC,
        0xECB0, 0xECB4, 0xECB8, 0xECBC,
        0xECC0, 0xECC4,
        0xECE0, 0xECE4, 0xECE8, 0xECEC,
        0xED00, 0xED04, 0xED08,
        0xEE00, 0xEE04, 0xEE10, 0xEE14
    };

    for(int i=0; i<sizeof(candidates)/sizeof(candidates[0]); i++) {
        ULONG off = candidates[i];
        ULONG before = R(off);
        if(before == 0xFFFFFFFF || before == 0) continue;
        /* Write inverted pattern (safe: won't harm) */
        W(off, before ^ 0xFFFFFFFF);
        ULONG after = R(off);
        W(off, before);  /* restore */
        printf("[0x%04X]  0x%08X   0x%08X   %s\n", off, before, after,
            (after == (before ^ 0xFFFFFFFF)) ? "YES" : "NO");
    }

    /* Phase 3: Try RLC_CP_SCHEDULERS at 0xECA8 and nearby with KIQ value */
    printf("\n=== RLC_CP_SCHEDULERS test ===\n");
    ULONG rlcOffsets[] = {0xECA0, 0xECA4, 0xECA8, 0xECAC, 0xECB0};
    for(int i=0; i<5; i++) {
        ULONG off = rlcOffsets[i];
        ULONG before = R(off);
        /* Write RLC_SCHEDULERS_KIQ = 0xA0 */
        W(off, 0xA0);
        ULONG after = R(off);
        W(off, before);  /* restore */
        printf("[0x%04X]  0x%08X -> 0xA0 -> 0x%08X %s\n", off, before, after,
            (after == 0xA0) ? "STUCK!" : "");
    }

    /* Phase 4: Scan for BAR5 size (check max accessible offset) */
    printf("\n=== BAR5 size probe ===\n");
    ULONG testOffsets[] = {0x80000, 0x100000, 0x180000, 0x1D000, 0x1D284, 0x200000};
    for(int i=0; i<6; i++) {
        ULONG v = R(testOffsets[i]);
        printf("[0x%06X] = 0x%08X %s\n", testOffsets[i], v,
            (v == 0xFFFFFFFF) ? "(DEAD)" : "(ALIVE!)");
    }

    CloseHandle(hGpu);
    printf("\nDone.\n");
    return 0;
}
