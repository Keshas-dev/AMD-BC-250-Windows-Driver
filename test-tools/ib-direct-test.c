/* ib-direct-test.c — Direct PM4 submission via indirect buffer registers */
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

int main(void){
    hGpu=CreateFileW(L"\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
    if(hGpu==INVALID_HANDLE_VALUE){printf("FAIL err=%lu\n",GetLastError());return 1;}
    UCHAR init[32]={0};*(UINT64*)init=0xFE800000ULL;*(UINT32*)(init+8)=0x00080000;
    *(UINT32*)(init+12)=1;*(UINT64*)(init+16)=0xC0000000ULL;*(UINT32*)(init+24)=0x20000000;
    DWORD br=0;DeviceIoControl(hGpu,IOCTL_GPU_INIT,init,sizeof(init),NULL,0,&br,NULL);

    /* Restore GRBM to broadcast */
    W(0x34D0,0xE0000000);

    /* Load MEC firmware (needed for CP to process PM4) */
    HANDLE mecFw = CreateFileA("C:\\AMD-BC-250\\AMD-BC-250-PSP-Windows-Driver\\output\\cyan_skillfish2_mec.bin",
        GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (mecFw == INVALID_HANDLE_VALUE) {
        printf("MEC firmware file not found\n");
    } else {
        DWORD fwSize = GetFileSize(mecFw, NULL);
        UINT32 totalSize = 16 + fwSize;
        UCHAR *fwb = (UCHAR*)malloc(totalSize);
        memset(fwb, 0, 16);
        *(UINT32*)(fwb+0) = 4; /* type=4 = MEC */
        *(UINT32*)(fwb+4) = fwSize;
        DWORD brFw = 0;
        ReadFile(mecFw, fwb+16, fwSize, &brFw, NULL);
        CloseHandle(mecFw);
        UINT32 fwOut[4] = {0};
        BOOL fwOk = DeviceIoControl(hGpu, IOCTL_CP_FW, fwb, totalSize, fwOut, sizeof(fwOut), &br, NULL);
        printf("MEC firmware loaded (type=4): %s Result=0x%08X\n", fwOk ? "OK" : "FAIL", fwOut[0]);
        free(fwb);
        if (!fwOk) { CloseHandle(hGpu); return 1; }
    }

    /* Setup identity page tables */
    UCHAR ptb[256]={0};*(UINT32*)(ptb+0)=0;
    if(!DeviceIoControl(hGpu,IOCTL_GCVM_PT,ptb,256,ptb+236,20,&br,NULL)){
        printf("PT_SETUP FAILED\n"); CloseHandle(hGpu); return 1;
    }
    ULONG ringAddr = *(ULONG*)(ptb+236); /* ring buffer PA */
    printf("PT_SETUP OK. Ring buffer PA = 0x%08X\n", ringAddr);

    /* Save scratch */
    ULONG scratchSave = R(0x32D4);
    printf("SCRATCH before = 0x%08X\n", scratchSave);

    /* === PHASE 1: Probe IB registers one more time with small values === */
    printf("\n=== Phase 1: IB register write tests ===\n");

    /* Test 0x3BAC (IB? BASE_LO) */
    ULONG orig_ac=R(0x3BAC), orig_b0=R(0x3BB0), orig_c0=R(0x3BC0);
    W(0x3BAC, ringAddr);
    ULONG read_ac=R(0x3BAC);
    printf("[0x3BAC] wrote 0x%08X, read 0x%08X - %s\n",
        ringAddr, read_ac, (read_ac==ringAddr?"WRITABLE":"MASKED"));

    W(0x3BB0, 0x00000000);
    ULONG read_b0=R(0x3BB0);
    printf("[0x3BB0] wrote 0x00000000, read 0x%08X\n", read_b0);

    W(0x3BC0, 0x00000020); /* 32 dwords */
    ULONG read_c0=R(0x3BC0);
    printf("[0x3BC0] wrote 0x00000020, read 0x%08X - %s\n",
        read_c0, (read_c0==0x20?"WRITABLE":"MASKED"));

    /* Restore if readback failed */
    if(read_ac!=ringAddr)W(0x3BAC, orig_ac);
    if(read_c0!=0x20)W(0x3BC0, orig_c0);

    /* === PHASE 2: Write PM4 into ring buffer (use our buffer or the ring) === */
    printf("\n=== Phase 2: Setting up IB buffer ===\n");

    /* The ring address. We need to write PM4 packets to this address.
     * Since this is physical address in system RAM, we need to write it
     * via a different mechanism. Let's use the kernel driver's buffer
     * that was allocated by PT_SETUP.
     * 
     * Actually, we can't directly write to physical memory from user mode.
     * But we can use the PT_SETUP's ring which already has NOPs.
     * 
     * Let's just try the IB approach with whatever's in the ring buffer.
     * The ring should have NOPs from PT_SETUP.
     */

    /* === PHASE 3: Set IB registers and trigger === */
    printf("\n=== Phase 3: Setting IB registers ===\n");

    /* Write IB buffer address */
    W(0x3BAC, ringAddr);
    ULONG confirm_ac=R(0x3BAC);
    printf("IB_BASE_LO (0x3BAC) = 0x%08X\n", confirm_ac);

    W(0x3BB0, 0x00000000);
    ULONG confirm_b0=R(0x3BB0);
    printf("IB_BASE_HI (0x3BB0) = 0x%08X\n", confirm_b0);

    /* Try 0x3BC0 as size/control - write 32 dwords */
    W(0x3BC0, 0x00000020);
    ULONG confirm_c0=R(0x3BC0);
    printf("IB_BUFSZ (0x3BC0) = 0x%08X\n", confirm_c0);

    /* Try enabling via 0x3BC0 with bit 0 (ENABLE) */
    W(0x3BC0, 0x00000021); /* 32 dwords + enable bit 0 */
    ULONG c1=R(0x3BC0);
    printf("IB_CNTL with EN=1 (0x3BC0) = 0x%08X\n", c1);

    /* Check if IB started - poll scratch */
    for(int i=0;i<50;i++){
        ULONG s=R(0x32D4);
        if(s!=scratchSave){
            printf("SCRATCH CHANGED! 0x%08X -> 0x%08X (iter %d)\n",scratchSave,s,i);
            break;
        }
        Sleep(50);
    }

    ULONG scratchAfter = R(0x32D4);
    printf("SCRATCH after poll = 0x%08X %s\n",scratchAfter,
        (scratchAfter!=scratchSave?"CHANGED!":"unchanged"));

    /* === PHASE 4: Try with RLC_CP_SCHEDULERS === */
    printf("\n=== Phase 4: Trigger via RLC_CP_SCHEDULERS ===\n");

    /* Restore state first */
    W(0x3BAC, ringAddr);
    W(0x3BB0, 0x00000000);
    W(0x3BC0, 0x00000020);

    /* Fire RLC scheduler */
    W(0xECA8, 0xA0);
    Sleep(100);

    ULONG scratchRlc = R(0x32D4);
    printf("SCRATCH after RLC trigger = 0x%08X %s\n", scratchRlc,
        (scratchRlc!=scratchAfter?"CHANGED!":"unchanged"));

    /* Try different combinations */
    printf("\n=== Phase 5: Try GRBM select ME=1 ===\n");
    W(0x34D0, 0x00000001); /* ME=1 (MEC) */
    W(0x3BAC, ringAddr);
    W(0x3BB0, 0x00000000);
    W(0x3BC0, 0x00000020);
    W(0xECA8, 0xA0);
    Sleep(100);
    W(0x34D0, 0xE0000000); /* restore broadcast */

    ULONG scratchMe1 = R(0x32D4);
    printf("SCRATCH after ME=1 + RLC = 0x%08X %s\n", scratchMe1,
        (scratchMe1!=scratchRlc?"CHANGED!":"unchanged"));

    /* === Phase 6: Reset everything === */
    W(0x3BAC, orig_ac);
    W(0x3BB0, orig_b0);
    W(0x3BC0, orig_c0);

    printf("\n=== Summary ===\n");
    printf("SCRATCH: 0x%08X -> 0x%08X (saved 0x%08X)\n", scratchSave, scratchMe1, scratchSave);
    if(scratchMe1 != scratchSave){
        printf("*** SOMETHING CHANGED SCRATCH! ***\n");
    } else {
        printf("SCRATCH unchanged. IB execution did NOT occur.\n");
    }

    CloseHandle(hGpu);
    return 0;
}
