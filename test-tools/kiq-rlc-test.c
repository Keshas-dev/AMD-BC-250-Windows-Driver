/* kiq-rlc-test.c — Load MEC firmware + PT_SETUP + KIQ+RLC test */
#include <windows.h>
#include <stdio.h>

#define IOCTL_GPU_READ       0x80000B88
#define IOCTL_GPU_WRITE      0x80000B8C
#define IOCTL_GPU_INIT       0x80000B80
#define IOCTL_GPU_GCVM_PT    0x8000098C
#define IOCTL_LOAD_CP_FW     0x80000BD4

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

/* Load a firmware blob via LOAD_CP_FW IOCTL */
static int LoadFirmware(const char *filePath, ULONG fwType, ULONG *versionOut) {
    HANDLE hFile = CreateFileA(filePath, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if(hFile == INVALID_HANDLE_VALUE) {
        printf("  FW file open FAIL: %s (err=%lu)\n", filePath, GetLastError());
        return 0;
    }
    DWORD fwSize = GetFileSize(hFile, NULL);
    if(fwSize < 44) { CloseHandle(hFile); return 0; }

    /* Allocate IOCTL buffer: header (16 bytes) + firmware blob */
    ULONG bufSize = 16 + fwSize;
    UCHAR *buf = (UCHAR*)malloc(bufSize);
    if(!buf) { CloseHandle(hFile); return 0; }

    /* Fill header */
    *(ULONG*)(buf+0) = fwType;     /* FwType */
    *(ULONG*)(buf+4) = fwSize;     /* FwSize */
    *(ULONG*)(buf+8) = 0;          /* Result (OUT) */
    *(ULONG*)(buf+12) = 0;         /* UcodeVersion (OUT) */

    /* Read firmware blob after header */
    DWORD read;
    ReadFile(hFile, buf+16, fwSize, &read, NULL);
    CloseHandle(hFile);

    /* Send IOCTL */
    DWORD br=0;
    BOOL ok = DeviceIoControl(hGpu, IOCTL_LOAD_CP_FW, buf, bufSize, buf, bufSize, &br, NULL);
    ULONG result = *(ULONG*)(buf+8);
    ULONG ucodeVer = *(ULONG*)(buf+12);
    free(buf);

    if(!ok || result != 1) {
        printf("  FW load FAIL: ok=%d result=%lu ver=%lu\n", ok, result, ucodeVer);
        return 0;
    }
    if(versionOut) *versionOut = ucodeVer;
    return 1;
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

    /* Phase 0: Load CP firmware */
    printf("\n=== LOAD CP FIRMWARE ===\n");
    ULONG meVer=0, mecVer=0;

    if(LoadFirmware("..\\firmware\\cyan_skillfish2_mec.bin", 4, &mecVer))
        printf("  MEC firmware loaded, version=0x%X\n", mecVer);
    else
        printf("  MEC firmware FAILED — continuing anyway\n");

    if(LoadFirmware("..\\firmware\\cyan_skillfish2_me.bin", 1, &meVer))
        printf("  ME firmware loaded, version=0x%X\n", meVer);
    else
        printf("  ME firmware FAILED — continuing anyway\n");

    /* Phase 1: PT_SETUP */
    printf("\n=== PT_SETUP ===\n");
    UCHAR ptOut[256]={0};
    if(!DeviceIoControl(hGpu,IOCTL_GPU_GCVM_PT,NULL,0,ptOut,sizeof(ptOut),&br,NULL)) {
        printf("PT_SETUP FAIL err=%lu\n", GetLastError());
        CloseHandle(hGpu); return 1;
    }
    ULONG *p = (ULONG*)ptOut;
    printf("CtxCntlBefore=0x%08X RingBaseLo=0x%08X RingBaseHi=0x%08X\n",
        p[0], p[1], p[2]);
    printf("Result=0x%08X InvStatus=%u\n", p[9], p[16]);

    if(p[9] != 0xCAFEBABE) { printf("PT_SETUP FAILED 0x%08X\n", p[9]); CloseHandle(hGpu); return 1; }
    UINT64 ringPa = ((UINT64)p[2] << 32) | p[1];
    printf("Ring PA = 0x%llx\n\n", ringPa);

    /* Phase 2: BEFORE state */
    printf("=== BEFORE ===\n");
    ULONG rlcBefore = R(0xECA8);
    ULONG mecCntl   = R(0x7A00);    /* MEC_ME1_CNTL */
    printf("RLC_CP_SCHEDULERS=0x%08X\n", rlcBefore);
    printf("KIQ_BASE=0x%08X_%08X  SIZE=0x%08X  PQ_CTL=0x%08X  ACTIVE=0x%08X\n",
        R(0xE064), R(0xE060), R(0xE068), R(0xE070), R(0xE080));
    printf("ME_CNTL(0x4A74)=0x%08X  MEC_ME1_CNTL=0x%08X  CTX0_CNTL=0x%08X  GRBM=0x%08X\n",
        R(0x4A74), mecCntl, R(0xB460), R(0x3260));
    printf("RPTR=%u  WPTR=%u  SCRATCH=0x%08X\n\n", R(0xE06C), R(0xE078), R(0x32D4));

    /* Phase 3: HALT MEC + DISABLE everything */
    printf("=== HALT MEC + DISABLE ===\n");
    W(0xE080, 0);       /* KIQ_ACTIVE=0 */
    W(0xECA8, 0);       /* RLC=0 */
    W(0x7A00, 1);       /* MEC_ME1_HALT */
    W(0x4A74, 0x70000000); /* ME_HALT|PFP_HALT|CE_HALT */
    Sleep(10);
    printf("MEC_ME1_CNTL=0x%08X ME_CNTL=0x%08X\n", R(0x7A00), R(0x4A74));

    /* Phase 4: RLC alone (KIQ disabled) — observe if MEC wakes up */
    printf("\n=== RLC=0xA0 (KIQ disabled) ===\n");
    W(0xECA8, 0xA0);
    printf("RLC=0x%08X\n", R(0xECA8));
    for(int i=0;i<5;i++){
        printf("  t=%d: MEC_ME1=%08X ME_CNTL=%08X SCR=%08X STAT=%08X\n",
            i*10, R(0x7A00), R(0x4A74), R(0x32D4), R(0x3260));
        Sleep(10);
    }

    /* Phase 5: KIQ activate (MEC still halted, RLC enabled) */
    printf("\n=== RLC=0xA0 + MEC unhalt + KIQ_ACTIVE=1 ===\n");
    W(0x7A00, 0);       /* Unhalt MEC */
    W(0x4A74, 0);       /* Unhalt all ME/PFP/CE */
    W(0xE080, 1);       /* KIQ_ACTIVE=1 */
    printf("MEC_ME1=0x%08X ME_CNTL=0x%08X\n", R(0x7A00), R(0x4A74));

    /* Write WPTR=1 and poll RPTR */
    W(0xE078, 1);
    printf("WPTR=1 initial RPTR=%u\n", R(0xE06C));

    int advanced = 0;
    for(int i=0;i<500;i++){
        ULONG rpt=R(0xE06C);
        ULONG scr=R(0x32D4);
        ULONG stat=R(0x3260);
        if(rpt!=0){
            printf(">>> RPTR=%u after %dms (SCR=0x%08X STAT=0x%08X)\n",rpt,i*10,scr,stat);
            advanced=1; break;
        }
        if(i%100==0) printf("  i=%d: RPTR=%u SCR=0x%08X STAT=0x%08X\n", i, rpt, scr, stat);
        Sleep(10);
    }
    if(!advanced) printf(">>> RPTR=0 after 5s\n");

    /* Phase 6: After state */
    printf("\n=== AFTER ===\n");
    printf("GRBM_STATUS=0x%08X ME_CNTL=0x%08X MEC_ME1=0x%08X\n",
        R(0x3260), R(0x4A74), R(0x7A00));
    printf("KIQ: BASE=0x%08X RPTR=%u WPTR=%u ACTIVE=0x%08X\n",
        R(0xE060), R(0xE06C), R(0xE078), R(0xE080));
    printf("RLC=0x%08X SCRATCH=0x%08X\n", R(0xECA8), R(0x32D4));

    /* Phase 7: Cleanup */
    printf("\n=== CLEANUP ===\n");
    W(0xE080, 0);
    W(0xECA8, 0);
    W(0x7A00, 1);   /* halt MEC */
    W(0x4A74, 0x70000000);
    if(rlcBefore != 0) W(0xECA8, rlcBefore);
    printf("Restored: RLC=0x%08X MEC_ME1=0x%08X\n", R(0xECA8), R(0x7A00));

    CloseHandle(hGpu);
    printf("\nDone.\n");
    return 0;
}
