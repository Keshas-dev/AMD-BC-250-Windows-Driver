#include <windows.h>
#include <stdio.h>
#include <memory.h>

#pragma pack(push, 1)
typedef struct { UINT32 Off; UINT32 Val; } REG_IO;
#pragma pack(pop)

static HANDLE gH;

static UINT32 R(UINT32 off) {
    REG_IO in={off,0}, out={0}; DWORD br=0;
    DeviceIoControl(gH, 0x80000B88, &in, 8, &out, 8, &br, NULL);
    return out.Val;
}
static void W(UINT32 off, UINT32 val) {
    REG_IO in={off,val}, out={0}; DWORD br=0;
    DeviceIoControl(gH, 0x80000B8C, &in, 8, &out, 8, &br, NULL);
}
static void WritePhys(UINT64 pa, const void* data, ULONG size) {
    UCHAR buf[4096 + 12];
    ((PULONG)buf)[0] = (ULONG)(pa & 0xFFFFFFFF);
    ((PULONG)buf)[1] = (ULONG)(pa >> 32);
    ((PULONG)buf)[2] = size;
    memcpy(buf + 12, data, size);
    DWORD br = 0;
    DeviceIoControl(gH, 0x80000C10, buf, 12 + size, NULL, 0, &br, NULL);
}
static ULONG ReadPhys(UINT64 pa, void* data, ULONG size) {
    UCHAR inbuf[24], outbuf[4096];
    ((PULONG)inbuf)[0] = (ULONG)(pa & 0xFFFFFFFF);
    ((PULONG)inbuf)[1] = (ULONG)(pa >> 32);
    ((PULONG)inbuf)[2] = size;
    DWORD br = 0;
    DeviceIoControl(gH, 0x80000C14, inbuf, 12, outbuf, sizeof(outbuf), &br, NULL);
    if (br > 0) memcpy(data, outbuf, min(br, size));
    return br;
}

#define SHADER_ADDR  0xC0100000ULL
#define FENCE_ADDR   0xC0600000ULL

static UINT8 fwBuf[512 * 1024];  /* 512KB max firmware */
static UINT32 fwSize = 0;
static UINT64 ringPa = 0;

static int LoadFirmware(const char* path) {
    HANDLE f = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    DWORD read = 0;
    ReadFile(f, fwBuf, sizeof(fwBuf), &read, NULL);
    fwSize = read;
    CloseHandle(f);
    return (fwSize > 44) ? 1 : 0;
}

static int CallGcvmSetup(void) {
    UCHAR ptBuf[256] = {0};
    DWORD br = 0;
    BOOL ptOk = DeviceIoControl(gH, 0x8000098C, NULL, 0, ptBuf, sizeof(ptBuf), &br, NULL);
    if (!ptOk) { printf("  IOCTL failed: err=%d\n", GetLastError()); return 0; }
    ULONG* pt = (ULONG*)ptBuf;
    printf("  Result=0x%08X CtxCntlBefore=0x%08X depth=0x%X\n",
        pt[9], pt[0], pt[0] & 6);
    if (pt[9] != 0xCAFEBABE) { printf("  Bad result 0x%08X\n", pt[9]); return 0; }
    ringPa = ((ULONG64)pt[2] << 32) | pt[1];
    printf("  RingPA=0x%016llX Phys[0]=0x%08X Phys[1]=0x%08X Phys[2]=0x%08X\n",
        ringPa, pt[10], pt[11], pt[12]);
    /* Enable GCVM if not already */
    UINT32 cntl = R(0xB460);
    if (!(cntl & 1)) {
        W(0xB460, cntl | 1);
        W(0x6C10, 1); W(0x6C0C, 1); Sleep(10);
        printf("  GCVM enabled\n");
    }
    return (ringPa != 0) ? 1 : 0;
}

int main(void) {
    gH = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (gH == INVALID_HANDLE_VALUE) { printf("ERR: open device\n"); return 1; }
    printf("Device opened\n");

    /* Init BAR5 + VRAM */
    UINT8 initBuf[32] = {0};
    *(UINT64*)(initBuf+0)=0xFE800000; *(UINT32*)(initBuf+8)=0x80000;
    *(UINT32*)(initBuf+12)=1; *(UINT64*)(initBuf+16)=0xC0000000;
    *(UINT32*)(initBuf+24)=0x20000000;
    DWORD br=0; DeviceIoControl(gH, 0x80000B80, initBuf, 32, NULL, 0, &br, NULL);

    /* Write shader + fence */
    UINT32 shader[4] = {0xBF9F0000, 0xBF9F0000};
    WritePhys(SHADER_ADDR, shader, 16);
    UINT64 zero = 0;
    WritePhys(FENCE_ADDR, &zero, 8);

    /* === STEP 1: Load patched MEC firmware === */
    printf("\n=== STEP 1: LOAD PATCHED MEC FIRMWARE ===\n");
    if (!LoadFirmware("..\\firmware\\cyan_skillfish2_mec_patched.bin")) {
        printf("ERR: load firmware file\n"); CloseHandle(gH); return 1;
    }
    printf("Firmware size: %u bytes\n", fwSize);

    /* Build IOCTL input: header + firmware blob */
    UINT8* ioBuf = (UINT8*)malloc(sizeof(UINT32)*4 + fwSize);
    ((UINT32*)ioBuf)[0] = 4;  /* FwType = MEC */
    ((UINT32*)ioBuf)[1] = fwSize;
    ((UINT32*)ioBuf)[2] = 0;  /* Result (out) */
    ((UINT32*)ioBuf)[3] = 0;  /* UcodeVersion (out) */
    memcpy(ioBuf + 16, fwBuf, fwSize);

    DWORD outBytes = 0;
    UINT8 outBuf[32] = {0};
    BOOL loadOk = DeviceIoControl(gH, 0x80000BD4, ioBuf, 16 + fwSize, outBuf, 16, &outBytes, NULL);
    UINT32* out = (UINT32*)outBuf;
    printf("LOAD_CP_FW: %s (err=%d) Result=0x%08X UcodeVer=0x%08X\n",
        loadOk ? "OK" : "FAILED", GetLastError(), out[2], out[3]);
    free(ioBuf);
    if (!loadOk) { CloseHandle(gH); return 1; }

    /* Check MEC halt status */
    printf("CP_MEC_CNTL(0x4B14)=0x%08X\n", R(0x4B14));

    /* === STEP 2: GCVM SETUP === */
    printf("\n=== STEP 2: GCVM PAGE TABLES ===\n");
    if (!CallGcvmSetup()) { printf("ERR: GCVM setup\n"); CloseHandle(gH); return 1; }
    printf("Ring PA=0x%016llX\n", ringPa);

    /* === STEP 3: Set up KIQ ring registers === */
    printf("\n=== STEP 3: KIQ RING SETUP ===\n");
    printf("Before: BASE=0x%08X_%08X SIZE=0x%08X RPTR=0x%08X WPTR=0x%08X\n",
        R(0xE064), R(0xE060), R(0xE068), R(0xE06C), R(0xE078));

    W(0xE060, (UINT32)(ringPa & 0xFFFFFFFF));  /* KIQ_BASE_LO */
    W(0xE064, (UINT32)(ringPa >> 32));          /* KIQ_BASE_HI */
    /* KIQ_CNTL is read-only, leave it */
    W(0xE078, 0);  /* KIQ_WPTR = 0 */
    W(0xE07C, 0);  /* KIQ_VMID = 0 */
    W(0xE080, 1);  /* KIQ_ACTIVE = 1 */
    Sleep(10);

    printf("After:  BASE=0x%08X_%08X SIZE=0x%08X RPTR=0x%08X WPTR=0x%08X VMID=0x%08X ACTIVE=0x%08X\n",
        R(0xE064), R(0xE060), R(0xE068), R(0xE06C), R(0xE078), R(0xE07C), R(0xE080));

    /* RLC scheduler enable for KIQ queue */
    W(0xECA8, 0xA0);  /* ENABLE + ME1 + PIPE0 + QUEUE0 (KIQ) */
    W(0x34D0, 0xE0000000);  /* broadcast */

    /* === STEP 4: Write MQD with compute PGM and ring config === */
    printf("\n=== STEP 4: MQD ===\n");
    UINT32 mqd[512] = {0};
    /* Region 1 */
    mqd[1] = (UINT32)(SHADER_ADDR >> 8);  /* PGM_LO */
    mqd[2] = 0;                             /* PGM_HI */
    mqd[3] = (2 << 0) | (1 << 6);          /* RSRC1: 8 VGPR, 16 SGPR */
    mqd[4] = (63 << 0);                     /* RSRC2: 64 threads */

    /* Region 2 at DW128 */
    mqd[128 + 0] = (UINT32)(ringPa & 0xFFFFFFFF);   /* CP_HQD_PQ_BASE_LO */
    mqd[128 + 1] = (UINT32)(ringPa >> 32);           /* CP_HQD_PQ_BASE_HI */
    mqd[128 + 2] = 0x00001000;                       /* PQ_CONTROL: queue_size=4096 */
    mqd[128 + 3] = 0;                                 /* PQ_WPTR_LO */
    mqd[128 + 4] = 0;                                 /* PQ_WPTR_HI */
    mqd[128 + 5] = 0;                                 /* PQ_RPTR */
    mqd[128 + 6] = 1;                                 /* CP_HQD_ACTIVE (set inside MQD) */
    mqd[128 + 7] = 0;                                 /* VMID=0 */

    WritePhys(ringPa, mqd, 2048);
    printf("MQD written to PA 0x%016llX (256 DWORDS)\n", ringPa);

    /* Set MQD_BASE and activate */
    W(0x9104, (UINT32)(ringPa & 0xFFFFFFFF));
    W(0x910C, 1);  /* CP_HQD_ACTIVE = 1 */
    Sleep(50);
    printf("CP_HQD_ACTIVE(0x910C)=0x%08X PGM_LO(0x8110)=0x%08X\n",
        R(0x910C), R(0x8110));

    /* === STEP 5: Write PM4 to ring buffer === */
    printf("\n=== STEP 5: PM4 DISPATCH ===\n");

    /* PM4 packets for compute dispatch */
    UINT32 pm4[32] = {0};
    int dw = 0;

    /* Method 1: IT_DISPATCH_DIRECT (0x15) */
    pm4[dw++] = 0xC0150004;  /* PM4 type3, op=0x15, count=4 */
    pm4[dw++] = 1;           /* DIM_X */
    pm4[dw++] = 1;           /* DIM_Y */
    pm4[dw++] = 1;           /* DIM_Z */
    pm4[dw++] = 0x00000003;  /* DISPATCH_INITIATOR: VALID + ENABLE */

    /* Method 2: IT_COMPUTE (0x2E) - MEC specific */
    pm4[dw++] = 0xC02E0004;  /* PM4 type3, op=0x2E, count=4 */
    pm4[dw++] = 0x00000001;  /* DISPATCH_INITIATOR */
    pm4[dw++] = 1;           /* DIM_X */
    pm4[dw++] = 1;           /* DIM_Y */
    pm4[dw++] = 1;           /* DIM_Z */

    /* NOP padding */
    pm4[dw++] = 0xC0001000;  /* IT_NOP */
    pm4[dw++] = 0xC0001000;

    /* Method 3: SET_SH_REG + dispatch */
    /* SET_SH_REG: op=0x76, count=10 regs starting at COMPUTE_PGM_LO (mm=0x1BAC) */
    pm4[dw++] = 0x0076000A | (3 << 30);  /* PM4 type3, op=0x76, count=10 */
    pm4[dw++] = 0x1BAC;                  /* base register mm offset (COMPUTE_PGM_LO) */
    pm4[dw++] = (UINT32)(SHADER_ADDR >> 8); /* PGM_LO */
    pm4[dw++] = 0;                          /* PGM_HI */
    pm4[dw++] = (2 << 0) | (1 << 6);       /* RSRC1 */
    pm4[dw++] = (63 << 0);                  /* RSRC2 */
    pm4[dw++] = 1;                          /* DIM_X */
    pm4[dw++] = 1;                          /* DIM_Y */
    pm4[dw++] = 1;                          /* DIM_Z */
    pm4[dw++] = 0;                          /* START_X */
    pm4[dw++] = 0;                          /* START_Y */
    pm4[dw++] = 0;                          /* START_Z */
    /* Then dispatch */
    pm4[dw++] = 0xC0150004;
    pm4[dw++] = 1;
    pm4[dw++] = 1;
    pm4[dw++] = 1;
    pm4[dw++] = 0x00000003;

    /* NOP pad to end (ring must end with NOP to avoid wrapping) */
    pm4[dw++] = 0xC0001000;

    UINT32 pm4Bytes = dw * 4;
    WritePhys(ringPa, pm4, pm4Bytes);
    printf("Written %u DWORDS (%u bytes) to ring\n", dw, pm4Bytes);

    /* Verify ring content */
    UINT32 verify[8];
    ReadPhys(ringPa, verify, 32);
    printf("Ring[0..7]: 0x%08X 0x%08X 0x%08X 0x%08X ...\n",
        verify[0], verify[1], verify[2], verify[3]);

    /* === STEP 6: Update WPTR to trigger MEC processing === */
    printf("\n=== STEP 6: RING PROCESSING ===\n");
    printf("Before: RPTR=0x%08X WPTR=0x%08X GRBM=0x%08X\n",
        R(0xE06C), R(0xE078), R(0x3260));

    /* Write WPTR = number of bytes written */
    W(0xE078, pm4Bytes);  /* KIQ_WPTR */
    Sleep(100);

    printf("After:  RPTR=0x%08X WPTR=0x%08X GRBM=0x%08X\n",
        R(0xE06C), R(0xE078), R(0x3260));

    /* Poll for RPTR advancement (MEC firmware processing) */
    printf("\nPolling RPTR for 2 seconds...\n");
    UINT32 rptr_initial = R(0xE06C);
    UINT32 rptr = rptr_initial;
    for (int i = 0; i < 20; i++) {
        rptr = R(0xE06C);
        UINT32 wptr = R(0xE078);
        UINT32 grbm = R(0x3260);
        UINT32 grbm2 = R(0x3268);
        UINT32 fence_dw = 0;
        ReadPhys(FENCE_ADDR, &fence_dw, 4);
        if (rptr != rptr_initial || grbm != 0 || fence_dw != 0) {
            printf("  Poll %d: RPTR=0x%08X WPTR=0x%08X GRBM=0x%08X GRBM2=0x%08X FENCE=0x%08X *** ACTIVITY! ***\n",
                i, rptr, wptr, grbm, grbm2, fence_dw);
            if (fence_dw != 0 || grbm != 0) break;
        }
        Sleep(100);
    }
    if (rptr == rptr_initial) {
        printf("  NO ACTIVITY after 2s. RPTR still 0x%08X\n", rptr_initial);
    }

    /* === STEP 7: Try with WPTR=4 (small increment, just PM4 NOP) === */
    printf("\n=== STEP 7: TRY WPTR=4 ===\n");
    UINT32 nop = 0xC0001000;  /* IT_NOP */
    WritePhys(ringPa, &nop, 4);

    W(0xE078, 4);
    Sleep(200);

    UINT32 rptr2 = R(0xE06C);
    UINT32 wptr2 = R(0xE078);
    printf("RPTR=0x%08X WPTR=0x%08X GRBM=0x%08X\n", rptr2, wptr2, R(0x3260));
    if (rptr2 != rptr) printf("*** RPTR MOVED! ***\n");

    /* === STEP 8: Read all KIQ/MEC related registers === */
    printf("\n=== FINAL STATUS ===\n");
    printf("GRBM_STATUS(0x3260)=0x%08X\n", R(0x3260));
    printf("GRBM_STATUS2(0x3268)=0x%08X\n", R(0x3268));
    printf("CP_ME_CNTL(0x4A74)=0x%08X\n", R(0x4A74));
    printf("CP_MEC_CNTL(0x4B14)=0x%08X\n", R(0x4B14));
    printf("MEC_ME1_CNTL(0x7A00)=0x%08X\n", R(0x7A00));
    printf("SCRATCH(0x32D4)=0x%08X\n", R(0x32D4));

    UINT64 fenceFinal = 0;
    ReadPhys(FENCE_ADDR, &fenceFinal, 8);
    printf("FENCE = %llu\n", fenceFinal);

    CloseHandle(gH);
    return 0;
}
