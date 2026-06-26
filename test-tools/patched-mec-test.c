/* patched-mec-test.c v2 — Load patched MEC firmware + KIQ test via driver IOCTL
 *
 * Uses driver's KIQ_NOP_TEST handler (0x80000BD0) which properly:
 * - Allocates 4KB contiguous DMA ring buffer
 * - Writes valid PM4 IT_WRITE_DATA packets
 * - Programs KIQ_BASE/RPTR/WPTR
 * - Unhalts engines, polls RPTR
 * - Restores state on cleanup
 *
 * Our test: load patched MEC firmware, then call KIQ_NOP_TEST.
 * If RPTR advances -> firmware patch WORKED (KIQ_SIZE check was firmware-mediated).
 * If RPTR stays 0  -> KIQ_SIZE=0 is HARDWARE-level block.
 */
#include <windows.h>
#include <stdio.h>

#define IOCTL_GPU_INIT       0x80000B80
#define IOCTL_LOAD_CP_FW     0x80000BD4
#define IOCTL_GPU_KIQ_TEST   0x80000BD0

static HANDLE hGpu;

typedef struct {
    UINT32 Result;
    UINT32 ScratchBefore;
    UINT32 ScratchAfter;
    UINT32 MmioMapped;
    UINT32 RingAllocated;
    UINT32 HqdProgrammed;
    UINT32 Pm4Submitted;
    UINT32 RingWptr;
} GPU_KIQ_TEST_OUT;

#define FW_BASE "C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\firmware\\"

static void PrintRegs(const char *label) {
    UCHAR buf[8] = {0};
    DWORD br = 0;
    printf("--- %s ---\n", label);
    /* KIQ regs */
    *(ULONG*)(buf+0)=0xE060; *(ULONG*)(buf+4)=0; DeviceIoControl(hGpu,0x80000B88,buf,8,buf,8,&br,NULL);
    ULONG baseLo = *(ULONG*)(buf+4);
    *(ULONG*)(buf+0)=0xE064; DeviceIoControl(hGpu,0x80000B88,buf,8,buf,8,&br,NULL);
    ULONG baseHi = *(ULONG*)(buf+4);
    *(ULONG*)(buf+0)=0xE068; DeviceIoControl(hGpu,0x80000B88,buf,8,buf,8,&br,NULL);
    ULONG size = *(ULONG*)(buf+4);
    *(ULONG*)(buf+0)=0xE06C; DeviceIoControl(hGpu,0x80000B88,buf,8,buf,8,&br,NULL);
    ULONG rptr = *(ULONG*)(buf+4);
    *(ULONG*)(buf+0)=0xE078; DeviceIoControl(hGpu,0x80000B88,buf,8,buf,8,&br,NULL);
    ULONG wptr = *(ULONG*)(buf+4);
    *(ULONG*)(buf+0)=0xE080; DeviceIoControl(hGpu,0x80000B88,buf,8,buf,8,&br,NULL);
    ULONG active = *(ULONG*)(buf+4);
    printf("KIQ: BASE=0x%08X_%08X SIZE=0x%08X ACTIVE=0x%08X RPTR=%lu WPTR=%lu\n",
        baseHi, baseLo, size, active, rptr, wptr);
    /* engine state */
    *(ULONG*)(buf+0)=0x4A74; DeviceIoControl(hGpu,0x80000B88,buf,8,buf,8,&br,NULL);
    ULONG meCntl = *(ULONG*)(buf+4);
    *(ULONG*)(buf+0)=0x7A00; DeviceIoControl(hGpu,0x80000B88,buf,8,buf,8,&br,NULL);
    ULONG mecCntl = *(ULONG*)(buf+4);
    *(ULONG*)(buf+0)=0x32D4; DeviceIoControl(hGpu,0x80000B88,buf,8,buf,8,&br,NULL);
    ULONG scratch = *(ULONG*)(buf+4);
    *(ULONG*)(buf+0)=0x3260; DeviceIoControl(hGpu,0x80000B88,buf,8,buf,8,&br,NULL);
    ULONG grbm = *(ULONG*)(buf+4);
    printf("ME_CNTL=0x%08X MEC_ME1=0x%08X SCRATCH=0x%08X GRBM=0x%08X\n",
        meCntl, mecCntl, scratch, grbm);
}

static int LoadFW(const char *path, ULONG type) {
    HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if(hFile == INVALID_HANDLE_VALUE) return 0;
    DWORD sz = GetFileSize(hFile, NULL);
    if(sz < 44) { CloseHandle(hFile); return 0; }

    ULONG bufSz = 16 + sz;
    UCHAR *buf = (UCHAR*)malloc(bufSz);
    if(!buf) { CloseHandle(hFile); return 0; }

    *(ULONG*)(buf+0) = type;
    *(ULONG*)(buf+4) = sz;
    *(ULONG*)(buf+8) = 0;
    *(ULONG*)(buf+12) = 0;

    DWORD read; ReadFile(hFile, buf+16, sz, &read, NULL);
    CloseHandle(hFile);

    DWORD br=0;
    BOOL ok = DeviceIoControl(hGpu, IOCTL_LOAD_CP_FW, buf, bufSz, buf, bufSz, &br, NULL);
    ULONG result = *(ULONG*)(buf+8);
    ULONG ver = *(ULONG*)(buf+12);
    free(buf);

    if(!ok || result != 1) {
        printf("  FW load FAIL: ok=%d result=0x%08X ver=0x%X (path=%s)\n", ok, result, ver, path);
        return 0;
    }
    printf("  FW loaded: type=%lu ver=0x%X\n", type, ver);
    return 1;
}

int main(int argc, char *argv[]) {
    const char *mecName = (argc > 1) ? argv[1]
        : "cyan_skillfish2_mec_patched.bin";
    /* also allow loading ORIGINAL for control test */
    int useOriginal = 0;
    if(argc > 2 && strcmp(argv[2], "original") == 0) useOriginal = 1;
    if(useOriginal) mecName = "cyan_skillfish2_mec.bin";
    char mecPath[512];
    snprintf(mecPath, sizeof(mecPath), "%s%s", FW_BASE, mecName);

    printf("=== Patched MEC Firmware KIQ Test v2 ===\n");
    printf("MEC: %s (original=%d)\n\n", mecPath, useOriginal);

    hGpu = CreateFileW(L"\\\\.\\AMDBC250DreamV43",
        GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if(hGpu == INVALID_HANDLE_VALUE) {
        printf("FAIL: cannot open driver (err=%lu)\n", GetLastError());
        return 1;
    }
    printf("Driver opened OK\n");

    /* Phase 1: INIT_HARDWARE */
    printf("\n=== INIT_HARDWARE ===\n");
    UCHAR initBuf[32] = {0};
    *(UINT64*)(initBuf+0)  = 0xFE800000ULL;
    *(UINT32*)(initBuf+8)  = 0x00080000ULL;
    *(UINT32*)(initBuf+12) = 1;               /* NBIO_MAP */
    *(UINT64*)(initBuf+16) = 0xC0000000ULL;
    *(UINT32*)(initBuf+24) = 0x20000000ULL;
    DWORD br = 0;
    if(!DeviceIoControl(hGpu, IOCTL_GPU_INIT, initBuf, sizeof(initBuf), NULL, 0, &br, NULL)) {
        printf("INIT_HARDWARE FAIL (err=%lu)\n", GetLastError());
        CloseHandle(hGpu); return 1;
    }
    printf("INIT_HARDWARE OK\n");

    /* Phase 2: Check GPU */
    printf("\n=== CHECK GPU ===\n");
    UCHAR regBuf[8] = {0};
    *(ULONG*)(regBuf+0)=0xC100; DeviceIoControl(hGpu,0x80000B88,regBuf,8,regBuf,8,&br,NULL);
    ULONG nbio = *(ULONG*)(regBuf+4);
    *(ULONG*)(regBuf+0)=0x3840; DeviceIoControl(hGpu,0x80000B88,regBuf,8,regBuf,8,&br,NULL);
    ULONG gpuId = *(ULONG*)(regBuf+4);
    printf("GPU_ID=0x%08X NBIO_ID=0x%08X\n", gpuId, nbio);

    /* Phase 3: Load ME+PFP+CE firmware */
    printf("\n=== LOAD ME+PFP+CE FIRMWARE ===\n");
    char fwPath[512];
    snprintf(fwPath, sizeof(fwPath), "%s%s", FW_BASE, "cyan_skillfish2_me.bin");
    int meOk = LoadFW(fwPath, 1);
    snprintf(fwPath, sizeof(fwPath), "%s%s", FW_BASE, "cyan_skillfish2_pfp.bin");
    int pfpOk = LoadFW(fwPath, 2);
    snprintf(fwPath, sizeof(fwPath), "%s%s", FW_BASE, "cyan_skillfish2_ce.bin");
    int ceOk = LoadFW(fwPath, 3);
    printf("ME=%s PFP=%s CE=%s\n", meOk?"OK":"FAIL", pfpOk?"OK":"FAIL", ceOk?"OK":"FAIL");
    if(!meOk) { printf("ME firmware required!\n"); CloseHandle(hGpu); return 1; }

    /* Phase 4: Load PATCHED (or original) MEC firmware */
    printf("\n=== LOAD MEC FIRMWARE ===\n");
    int mecOk = LoadFW(mecPath, 4);
    printf("MEC=%s\n", mecOk?"OK":"FAIL");
    if(!mecOk) { printf("MEC firmware required!\n"); CloseHandle(hGpu); return 1; }

    /* Phase 5: State before KIQ test */
    PrintRegs("BEFORE KIQ TEST");

    /* Phase 6: Run driver's KIQ_NOP_TEST IOCTL
     * This handler does the proper ring allocation, PM4 write, KIQ setup, and polling.
     * We just call it and read the result.
     */
    printf("\n=== GPU KIQ TEST (via driver IOCTL) ===\n");
    GPU_KIQ_TEST_OUT kiqOut = {0};
    BOOL kiqOk = DeviceIoControl(hGpu, IOCTL_GPU_KIQ_TEST,
        NULL, 0, &kiqOut, sizeof(kiqOut), &br, NULL);

    printf("KIQ_TEST IOCTL: %s (err=%lu)\n", kiqOk ? "OK" : "FAIL", kiqOk ? 0 : GetLastError());
    printf("  Result=0x%08X (1=success)\n", kiqOut.Result);
    printf("  ScratchBefore=0x%08X ScratchAfter=0x%08X\n",
        kiqOut.ScratchBefore, kiqOut.ScratchAfter);
    printf("  MmioMapped=%u RingAllocated=%u HqdProgrammed=%u Pm4Submitted=%u\n",
        kiqOut.MmioMapped, kiqOut.RingAllocated, kiqOut.HqdProgrammed, kiqOut.Pm4Submitted);
    printf("  RingWptr=%u\n", kiqOut.RingWptr);

    /* Phase 7: State after KIQ test */
    PrintRegs("AFTER KIQ TEST");

    /* Result */
    printf("\n=== RESULT ===\n");
    int pass = (kiqOk && kiqOut.Result == 1);
    if(pass) {
        printf("SUCCESS: KIQ ring test PASSED!\n");
        if(!useOriginal)
            printf("  -> Patched MEC firmware WORKS (KIQ_SIZE check was firmware-mediated)\n");
        else
            printf("  -> ORIGINAL MEC firmware works too (control OK)\n");
        /* Check if SCRATCH was modified by PM4 */
        if(kiqOut.ScratchBefore != kiqOut.ScratchAfter)
            printf("  -> PM4 IT_WRITE_DATA executed! SCRATCH: 0x%08X -> 0x%08X\n",
                kiqOut.ScratchBefore, kiqOut.ScratchAfter);
    } else {
        printf("FAILURE: KIQ ring test FAILED (Result=0x%08X)\n", kiqOut.Result);
        printf("  -> KIQ_SIZE=0 is HARDWARE-level block, NOT firmware-mediated\n");
        printf("  -> Software PM4 executor is the only viable path\n");
    }

    CloseHandle(hGpu);
    printf("\nDone.\n");
    return pass ? 0 : 1;
}
