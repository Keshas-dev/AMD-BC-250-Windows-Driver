#include <windows.h>
#include <stdio.h>
#include <memory.h>

typedef struct { UINT32 Off; UINT32 Val; } REG_IO;
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
static void WaitMs(DWORD ms) { Sleep(ms); }

#define SHADER_ADDR  0xC0100000ULL
#define FENCE_ADDR   0xC0600000ULL

int main(void) {
    gH = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (gH == INVALID_HANDLE_VALUE) { printf("ERR opening device\n"); return 1; }

    UINT8 initBuf[32] = {0};
    *(UINT64*)(initBuf+0)  = 0xFE800000ULL;
    *(UINT32*)(initBuf+8)  = 0x00080000;
    *(UINT32*)(initBuf+12) = 1;
    *(UINT64*)(initBuf+16) = 0xC0000000ULL;
    *(UINT32*)(initBuf+24) = 0x20000000;
    DWORD br = 0;
    DeviceIoControl(gH, 0x80000B80, initBuf, 32, NULL, 0, &br, NULL);

    /* Write shader: s_endpgm (harmless) */
    UINT32 shader[16] = {0};
    shader[0] = 0xBF9F0000;
    shader[1] = 0xBF9F0000;
    WritePhys(SHADER_ADDR, shader, 64);

    /* Init fence */
    UINT64 zero = 0;
    WritePhys(FENCE_ADDR, &zero, 8);
    printf("Fence at 0x%08X = %llu\n", (UINT32)FENCE_ADDR, 0ULL);

    /* GCVM OFF */
    W(0xB460, R(0xB460) & ~1);
    W(0x34FC, 0xFFFFFFFF);
    W(0x9C1C, R(0x9C1C) | 0x1F000000);

    /* Unhalt ME if halted */
    UINT32 meCntl = R(0x4A74);
    if (meCntl & (1<<28)) { W(0x4A74, meCntl & ~(1<<28)); printf("ME unhalted\n"); }
    else printf("ME already unhalted\n");

    /* Read many status areas */
    printf("\n=== PRE-DISPATCH STATUS DUMP ===\n");
    printf("GRBM_STATUS(0x3260)       = 0x%08X\n", R(0x3260));
    printf("GRBM_STATUS2(0x3268)      = 0x%08X\n", R(0x3268));
    printf("GRBM_STATUS_SE0(0x326C)   = 0x%08X\n", R(0x326C));
    printf("GRBM_STATUS_SE1(0x3270)   = 0x%08X\n", R(0x3270));
    printf("GRBM_STATUS_SE2(0x3274)   = 0x%08X\n", R(0x3274));
    printf("GRBM_STATUS_SE3(0x3278)   = 0x%08X\n", R(0x3278));

    /* Set PGM via MMIO */
    W(0x8110, (UINT32)(SHADER_ADDR >> 8));
    W(0x8114, 0);
    printf("PGM_LO(0x8110) = 0x%08X\n", R(0x8110));
    printf("PGM_HI(0x8114) = 0x%08X\n", R(0x8114));

    /* ===== EXPERIMENT 1: Probe 0xDC60 register thoroughly ===== */
    printf("\n=== EXPERIMENT 1: 0xDC60 register behavior ===\n");
    printf("Initial:        0xDC60 = 0x%08X\n", R(0xDC60));
    
    /* Read it multiple times without writing */
    printf("Read x3 (no write): ");
    for (int i = 0; i < 3; i++) {
        printf("0x%08X ", R(0xDC60));
    }
    printf("\n");

    /* Write different values and observe */
    UINT32 testWrites[] = {0x00000000, 0x00000001, 0x00000003, 0xDEADBEEF, 0x00010000, 0xFFFFFFFF, 0x00000080, 0x00000200};
    for (int i = 0; i < 8; i++) {
        UINT32 old = R(0xDC60);
        W(0xDC60, testWrites[i]);
        WaitMs(5);
        UINT32 after = R(0xDC60);
        printf("W(0xDC60, 0x%08X): %08X → %08X\n", testWrites[i], old, after);
        /* Check GRBM after each write */
        UINT32 gs = R(0x3260);
        if (gs != 0) printf("  → GRBM changed! 0x%08X\n", gs);
    }

    /* ===== EXPERIMENT 2: Try 0xDC60 as DISPATCH trigger ===== */
    printf("\n=== EXPERIMENT 2: 0xDC60 as dispatch trigger ===\n");
    for (int try = 0; try < 5; try++) {
        W(0xDC60, 0x0003);  /* COMPUTE_SHADER_EN + VALID */
        WaitMs(200);
        UINT64 fence = 0;
        ReadPhys(FENCE_ADDR, &fence, 8);
        printf("Try %d: 0xDC60=0x%08X  GRBM=0x%08X  FENCE=%llu\n",
            try, R(0xDC60), R(0x3260), fence);
        if (fence != 0) { printf("*** FENCE CHANGED! ***\n"); break; }
    }

    /* ===== EXPERIMENT 3: MQD in SYSTEM RAM ===== */
    printf("\n=== EXPERIMENT 3: MQD in system RAM ===\n");
    /* Read the WRITE_PHYSICAL_MEM reply to find system RAM address */
    /* Actually, we need to allocate system physical RAM contiguous.
     * The driver's IOCTL 0x80000C1C allocates physical memory.
     * Let's try it. */
    UINT8 allocBuf[32] = {0};
    UINT64 sysMqdAddr = 0;
    *(UINT32*)(allocBuf+0) = 4096;  /* size */
    *(UINT32*)(allocBuf+4) = 0;     /* flags */
    UINT8 allocOut[16] = {0};
    br = 0;
    BOOL allocOk = DeviceIoControl(gH, CTL_CODE(FILE_DEVICE_UNKNOWN, 0xC1C, METHOD_BUFFERED, FILE_ANY_ACCESS),
        allocBuf, 8, &allocOut[0], 16, &br, NULL);
    if (allocOk && br >= 8) {
        sysMqdAddr = *(UINT64*)&allocOut[0];
        printf("Allocated system physical page at 0x%016llX\n", sysMqdAddr);
    } else {
        /* Try raw 0x80000C1C */
        br = 0;
        allocOk = DeviceIoControl(gH, 0x80000C1C, allocBuf, 8, &allocOut[0], 16, &br, NULL);
        if (allocOk && br >= 8) {
            sysMqdAddr = *(UINT64*)&allocOut[0];
            printf("Allocated (raw) system physical page at 0x%016llX\n", sysMqdAddr);
        } else {
            printf("Can't allocate - IOCTL failed (br=%u, err=%d)\n", br, GetLastError());
            /* Use a known system RAM address? Not safe. Skip. */
        }
    }

    if (sysMqdAddr != 0 && sysMqdAddr < 0x100000000ULL) {
        /* Build MQD at system RAM address */
        UINT32 mqd[512] = {0};  /* v10_compute_mqd = 512 DWORDs (2KB) */

        /* Region 1: COMPUTE_PGM_LO/HI */
        mqd[0] = (UINT32)(SHADER_ADDR >> 8);        /* COMPUTE_PGM_LO */
        mqd[1] = 0x00000000;                        /* COMPUTE_PGM_HI */
        mqd[2] = 0x00000000;                        /* COMPUTE_PGM_RSRC1 = 0 = NO VGPR/SGPR */
        mqd[3] = 0x00000000;                        /* COMPUTE_PGM_RSRC2 = 0 = 1 thread/threadgroup */
        mqd[4] = 0x00000000;                        /* COMPUTE_DIM_X = 1 (default 0 means 1) */
        mqd[5] = 0x00000000;                        /* COMPUTE_DIM_Y = 1 */
        mqd[6] = 0x00000000;                        /* COMPUTE_DIM_Z = 1 */
        mqd[7] = 0x00000001;                        /* COMPUTE_DISPATCH_INITIATOR (just VALID) */
        /* ... rest of Region 1 stays 0 (minimal config) */

        /* Region 2: CP_HQD state for ring processing */
        mqd[128 + 0] = (UINT32)(sysMqdAddr);                /* CP_HQD_PQ_BASE_LO */
        mqd[128 + 1] = (UINT32)(sysMqdAddr >> 32);          /* CP_HQD_PQ_BASE_HI */
        mqd[128 + 2] = 0x00001000;                           /* CP_HQD_PQ_CONTROL: size=4096 */
        mqd[128 + 3] = (UINT32)sysMqdAddr;                  /* CP_HQD_PQ_WPTR_LO */
        mqd[128 + 4] = 0;                                    /* CP_HQD_PQ_WPTR_HI */
        mqd[128 + 5] = 0;                                    /* CP_HQD_PQ_RPTR */
        mqd[128 + 6] = 1;                                    /* CP_HQD_ACTIVE */
        mqd[128 + 7] = 0;                                    /* CP_HQD_VMID = 0 */

        WritePhys(sysMqdAddr, mqd, 2048);
        printf("MQD written to system RAM @ 0x%016llX\n", sysMqdAddr);

        /* Set CP_MQD_BASE_ADDR */
        W(0x9104, (UINT32)sysMqdAddr);
        W(0x9108, (UINT32)(sysMqdAddr >> 32));
        printf("CP_MQD_BASE(0x9104) = 0x%08X\n", R(0x9104));
        printf("CP_MQD_BASE_HI(0x9108) = 0x%08X\n", R(0x9108));

        /* Activate */
        W(0x910C, 1);
        WaitMs(50);
        printf("CP_HQD_ACTIVE = 0x%08X\n", R(0x910C));

        /* Check if PGM loaded */
        printf("PGM_LO after MQD activate = 0x%08X\n", R(0x8110));
        printf("FENCE = %llu\n", (ULONG64)R((UINT32)FENCE_ADDR));

        /* Try dispatch via 0x80E0 after MQD active */
        W(0x80E0, 0x0003);
        WaitMs(200);
        UINT64 fence2 = 0;
        ReadPhys(FENCE_ADDR, &fence2, 8);
        printf("After dispatch: 0x80E0=0x%08X GRBM=0x%08X FENCE=%llu\n",
            R(0x80E0), R(0x3260), fence2);

        /* Try dispatch via 0xDC60 after MQD */
        W(0xDC60, 0x00000003);
        WaitMs(200);
        ReadPhys(FENCE_ADDR, &fence2, 8);
        printf("After 0xDC60 dispatch: 0xDC60=0x%08X GRBM=0x%08X FENCE=%llu\n",
            R(0xDC60), R(0x3260), fence2);
    }

    /* ===== EXPERIMENT 4: Try both HW (0xDC60) and GRBM_STATUS polling ===== */
    printf("\n=== EXPERIMENT 4: Polling dispatch ===\n");
    W(0x8110, (UINT32)(SHADER_ADDR >> 8));

    /* Continuous dispatch pulses while checking status */
    for (int i = 0; i < 10; i++) {
        W(0x80E0, 0x0001);  /* just COMPUTE_SHADER_EN, no VALID */
        WaitMs(10);
        UINT32 gs = R(0x3260);
        UINT32 dc60 = R(0xDC60);
        UINT32 gs2 = R(0x3268);
        if (gs != 0 || gs2 != 0) {
            printf("Pulse %d: GRBM=0x%08X GRBM2=0x%08X DC60=0x%08X\n", i, gs, gs2, dc60);
        }
    }
    printf("No activity detected\n");

    /* ===== EXPERIMENT 5: Read nearby registers to understand 0xDC60 ===== */
    printf("\n=== EXPERIMENT 5: 0xDC60 neighborhood (GC_BASE shift) ===\n");
    printf("0xDC00 = 0x%08X\n", R(0xDC00));
    printf("0xDC40 = 0x%08X\n", R(0xDC40));
    printf("0xDC60 = 0x%08X\n", R(0xDC60));
    printf("0xDC64 = 0x%08X\n", R(0xDC64));
    printf("0xDC68 = 0x%08X\n", R(0xDC68));
    printf("0xDC6C = 0x%08X\n", R(0xDC6C));
    printf("0xDC70 = 0x%08X\n", R(0xDC70));
    printf("0xDC74 = 0x%08X\n", R(0xDC74));
    printf("0xDC78 = 0x%08X\n", R(0xDC78));
    printf("0xDC7C = 0x%08X\n", R(0xDC7C));
    printf("0xDC80 = 0x%08X\n", R(0xDC80));
    printf("0xDCA0 = 0x%08X\n", R(0xDCA0));
    printf("0xDCC0 = 0x%08X\n", R(0xDCC0));
    printf("0xDCE0 = 0x%08X\n", R(0xDCE0));
    printf("0xDD00 = 0x%08X\n", R(0xDD00));
    printf("0xDD20 = 0x%08X\n", R(0xDD20));
    printf("0xDD40 = 0x%08X\n", R(0xDD40));
    printf("0xDD60 = 0x%08X\n", R(0xDD60));
    printf("0xDD80 = 0x%08X\n", R(0xDD80));
    printf("0xDDA0 = 0x%08X\n", R(0xDDA0));
    printf("0xDDC0 = 0x%08X\n", R(0xDDC0));
    printf("0xDDE0 = 0x%08X\n", R(0xDDE0));
    printf("0xDE00 = 0x%08X\n", R(0xDE00));
    printf("0xDF00 = 0x%08X\n", R(0xDF00));
    printf("0xE000 = 0x%08X\n", R(0xE000));

    /* Also read the COMPUTE block neighborhood */
    printf("\n=== 0x80E0 neighborhood ===\n");
    printf("0x8080 = 0x%08X\n", R(0x8080));
    printf("0x80A0 = 0x%08X\n", R(0x80A0));
    printf("0x80C0 = 0x%08X\n", R(0x80C0));
    printf("0x80E0 = 0x%08X\n", R(0x80E0));
    printf("0x8100 = 0x%08X\n", R(0x8100));
    printf("0x8120 = 0x%08X\n", R(0x8120));
    printf("0x8140 = 0x%08X\n", R(0x8140));
    printf("0x8160 = 0x%08X\n", R(0x8160));
    printf("0x8180 = 0x%08X\n", R(0x8180));
    printf("0x81A0 = 0x%08X\n", R(0x81A0));
    printf("0x81C0 = 0x%08X\n", R(0x81C0));
    printf("0x81E0 = 0x%08X\n", R(0x81E0));
    printf("0x8200 = 0x%08X\n", R(0x8200));

    /* Read GCVM neighbors */
    printf("\n=== GCVM area ===\n");
    printf("0xB460 = 0x%08X\n", R(0xB460));
    printf("0xB464 = 0x%08X\n", R(0xB464));
    printf("0xB468 = 0x%08X\n", R(0xB468));
    printf("0xB46C = 0x%08X\n", R(0xB46C));

    CloseHandle(gH);
    return 0;
}
