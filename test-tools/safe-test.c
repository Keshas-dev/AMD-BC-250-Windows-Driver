/* safe-test.c — Probe KIQ/HQD register writability + NBIO test */
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
static void WNR(ULONG off, ULONG v) { /* Write no readback delay */
    UCHAR buf[8]={0}; *(ULONG*)(buf+0)=off; *(ULONG*)(buf+4)=v;
    DWORD br=0; DeviceIoControl(hGpu,IOCTL_GPU_WRITE,buf,8,NULL,0,&br,NULL);
}

int main(void) {
    hGpu=CreateFileW(L"\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
    if(hGpu==INVALID_HANDLE_VALUE){printf("FAIL err=%lu\n",GetLastError());return 1;}

    /* Test write + readback for KIQ registers */
    printf("=== KIQ Register Writes (0xE060+) ===\n");
    
    /* Save original values */
    ULONG orig[20];
    for(int off=0xE060; off<=0xE098; off+=4)
        orig[(off-0xE060)/4] = R(off);
    
    /* SAFETY: Deactivate KIQ first to avoid GPU trying to process garbage */
    W(0xE080, 0);
    Sleep(10);
    
    /* Write distinct pattern to each */
    for(int off=0xE060; off<=0xE098; off+=4)
        W(off, 0xA0000000 | ((off-0xE060)/4));
    
    /* Read back */
    for(int off=0xE060; off<=0xE098; off+=4) {
        ULONG v = R(off);
        ULONG origV = orig[(off-0xE060)/4];
        printf("  [0x%04X] wrote 0xA%01X_ ret=0x%08X (orig=0x%08X)%s\n",
            off, (off-0xE060)/4, v, origV,
            (v == (0xA0000000 | ((off-0xE060)/4))) ? " STUCK" : "");
    }

    /* Restore originals (also deactivates KIQ since orig[0xE080]=0) */
    for(int off=0xE060; off<=0xE098; off+=4)
        W(off, orig[(off-0xE060)/4]);

    /* Now test HQD range under ME=1 */
    printf("\n=== HQD Register Writes (ME=1) ===\n");
    WNR(0x34D0, 0x00010000); /* GRBM_GFX_INDEX = ME=1 */
    Sleep(2);
    
    /* Save */
    ULONG hqdOrig[40];
    for(int off=0xDAC0; off<0xDBFF; off+=4)
        hqdOrig[(off-0xDAC0)/4] = R(off);
    
    /* Write 0xA to each */
    for(int off=0xDAC0; off<0xDBFF; off+=4)
        W(off, 0xAAAA5555);
    
    /* Read back */
    int stuckCount = 0;
    for(int off=0xDAC0; off<0xDBFF; off+=4) {
        ULONG v = R(off);
        ULONG origV = hqdOrig[(off-0xDAC0)/4];
        if(v == origV) {
            stuckCount++;
            if(stuckCount <= 5)
                printf("  [0x%04X] WROTE=0xAAAA5555 GOT=0x%08X (orig=0x%08X) NO EFFECT\n", off, v, origV);
        }
    }
    printf("  %d/%d registers unchanged (NBIO-blocked)\n", stuckCount, (0xDBFF-0xDAC0)/4+1);
    
    /* Now write via KIQ ACTIVE at 0xE080 */
    printf("\n=== KIQ_ACTIVE (0xE080) writability ===\n");
    ULONG actBefore = R(0xE080);
    W(0xE080, 0);
    ULONG actAfter0 = R(0xE080);
    W(0xE080, 1);
    ULONG actAfter1 = R(0xE080);
    printf("  before=%u after(0)=%u after(1)=%u\n", actBefore, actAfter0, actAfter1);
    
    /* Restore GRBM */
    WNR(0x34D0, 0xE0000000);
    
    CloseHandle(hGpu);
    printf("\nDone.\n");
    return 0;
}
