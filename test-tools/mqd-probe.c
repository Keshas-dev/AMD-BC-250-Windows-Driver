/* mqd-probe.c — Probe MQD/HQD writability + try MQD-based KIQ */
#include <windows.h>
#include <stdio.h>

#define IOCTL_GPU_READ  0x80000B88
#define IOCTL_GPU_WRITE 0x80000B8C
#define IOCTL_GPU_INIT  0x80000B80
#define IOCTL_LOAD_CP_FW     0x80000BD4
#define IOCTL_GPU_GCVM_PT    0x8000098C

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

static int LoadFirmware(const char *filePath, ULONG fwType) {
    HANDLE hFile = CreateFileA(filePath, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if(hFile == INVALID_HANDLE_VALUE) return 0;
    DWORD fwSize = GetFileSize(hFile, NULL);
    if(fwSize < 44) { CloseHandle(hFile); return 0; }
    ULONG bufSize = 16 + fwSize;
    UCHAR *buf = (UCHAR*)malloc(bufSize);
    *(ULONG*)(buf+0) = fwType; *(ULONG*)(buf+4) = fwSize;
    *(ULONG*)(buf+8) = 0; *(ULONG*)(buf+12) = 0;
    DWORD read; ReadFile(hFile, buf+16, fwSize, &read, NULL); CloseHandle(hFile);
    DWORD br=0;
    BOOL ok = DeviceIoControl(hGpu, IOCTL_LOAD_CP_FW, buf, bufSize, buf, bufSize, &br, NULL);
    ULONG result = *(ULONG*)(buf+8);
    ULONG ver = *(ULONG*)(buf+12);
    free(buf);
    return (ok && result == 1) ? ver : 0;
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
    printf("GPU init OK\n");

    ULONG mecVer = LoadFirmware("..\\firmware\\cyan_skillfish2_mec.bin", 4);
    printf("MEC firmware version=0x%X\n", mecVer);

    UCHAR ptOut[256]={0};
    if(DeviceIoControl(hGpu,IOCTL_GPU_GCVM_PT,NULL,0,ptOut,sizeof(ptOut),&br,NULL)) {
        ULONG *p = (ULONG*)ptOut;
        if(p[9]==0xCAFEBABE) printf("PT_SETUP OK: Ring=0x%08X_%08X\n\n", p[2], p[1]);
    }

    /* === MQD PROBE === */
    printf("--- MQD/HQD probe ---\n");
    ULONG probe[]={0xDAB8,0xDABC,0xDAC0,0xDAC4,0xDAC8,0xDAD8,0xDADC,0xDAE0,
        0xDAE4,0xDAE8,0xDAEC,0xDAF0,0xDAF4,0xDAFC,0xDB00,0xDB90,0xDB94};
    for(int i=0;i<sizeof(probe)/sizeof(probe[0]);i++){
        ULONG b=R(probe[i]);W(probe[i],0x12345678);ULONG a=R(probe[i]);W(probe[i],b);
        printf(" [0x%04X] %s%s\n",probe[i],(a==0x12345678)?"WRITABLE":"READ-ONLY",
            (b==0xFFFFFFFF)?" DEAD":"");
    }

    /* === KIQ test with GRBM select === */
    printf("\n--- KIQ test with GRBM select ME=1 ---\n");

    /* Hold MEC halted */
    W(0xE080,0);W(0xECA8,0);

    /* Set GRBM to ME=1 (KIQ engine) */
    W(0x34D0,0x00010000);
    printf("GRBM_GFX_INDEX=0x%08X\n",R(0x34D0));

    /* Write KIQ registers under ME=1 select */
    W(0xE080,1); /* KIQ_ACTIVE */
    W(0xECA8,0xA0); /* RLC schedule ME=1 */
    printf("KIQ_ACTIVE=%u RLC=0x%08X\n",R(0xE080),R(0xECA8));

    /* Try WPTR=1 and check */
    W(0xE078,1);
    printf("WPTR=1 initial RPTR=%u\n",R(0xE06C));

    /* Poll under GRBM=ME=1 */
    for(int i=0;i<200;i++){
        ULONG r=R(0xE06C);if(r){printf(">>> RPTR=%u t=%dms\n",r,i*10);goto done;}
        Sleep(10);
    }
    printf(">>> RPTR=0 after 2s\n");

    /* Now try with GRBM=Broadcast */
    printf("\n--- KIQ test with GRBM broadcast ---\n");
    W(0x34D0,0xE0000000);
    W(0xE078,1);
    for(int i=0;i<200;i++){
        ULONG r=R(0xE06C);if(r){printf(">>> RPTR=%u t=%dms (broadcast)\n",r,i*10);goto done;}
        Sleep(10);
    }
    printf(">>> RPTR=0 after 2s (broadcast)\n");

    /* Try: set MEC_ME1_CNTL=0 explicitly + KIQ */
    printf("\n--- Explicit MEC unhalt + KIQ ---\n");
    W(0x7A00,0);
    W(0x4A74,0);
    W(0x34D0,0x00010000);
    W(0xE080,1);
    W(0xECA8,0xA0);
    W(0xE078,1);
    printf("MEC_ME1=0x%08X ME_CNTL=0x%08X\n",R(0x7A00),R(0x4A74));
    for(int i=0;i<200;i++){
        ULONG r=R(0xE06C);if(r){printf(">>> RPTR=%u t=%dms (MEC unhalt)\n",r,i*10);goto done;}
        Sleep(10);
    }
    printf(">>> RPTR=0 after 2s (MEC unhalt)\n");

    /* Check if MEC is actually running: try to send NEW SCRATCH value via PM4 */
    /* Actually the ring already has NOP. Write a different PM4 to ring and check scratch */
    /* But we can't write to ring from user mode... */

    /* Final: dump ring PA content through driver? Check if GRBM_STATUS changes with engine activity */
    printf("\n--- Final dump ---\n");
    printf("SCRATCH=0x%08X GRBM_STATUS=0x%08X GRBM_SE0=0x%08X\n",
        R(0x32D4),R(0x3260),R(0x3268));
    printf("ME_CNTL=0x%08X MEC_ME1=0x%08X RLC=0x%08X\n",
        R(0x4A74),R(0x7A00),R(0xECA8));
    printf("KIQ: BASE=0x%08X RPTR=%u WPTR=%u ACTIVE=0x%08X\n",
        R(0xE060),R(0xE06C),R(0xE078),R(0xE080));
    printf("KIQ_VMID=0x%08X\n",R(0xE07C));

    /* Check 0xE07C VMID bits - maybe needs bits 0-3 for VMID */
    /* Check if there's a KIQ_CNTL bit other than 0 that matters */
    /* Let's also check GRBM_GFX_INDEX readback */
    printf("GRBM_GFX_INDEX=0x%08X\n",R(0x34D0));

    /* Try one more: write VMID, then ACTIVE, then WPTR */
    printf("\n--- VMID + ACTIVE + WPTR sequence ---\n");
    W(0xE07C,0x00000000); /* VMID=0 */
    W(0xE080,1);          /* ACTIVE */
    W(0xE078,4);          /* WPTR=4 (aligned) */
    for(int i=0;i<200;i++){
        ULONG r=R(0xE06C);if(r){printf(">>> RPTR=%u t=%dms\n",r,i*10);goto done;}
        Sleep(10);
    }
    printf(">>> RPTR=0 after 2s (VMID=0)\n");

done:
    W(0xE080,0);W(0xECA8,0);
    CloseHandle(hGpu);
    printf("\nDone.\n");
    return 0;
}
