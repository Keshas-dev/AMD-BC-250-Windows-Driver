#include <windows.h>
#include <stdio.h>

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

int main(void) {
    gH = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (gH == INVALID_HANDLE_VALUE) { printf("ERR open device\n"); return 1; }
    printf("Device opened OK\n");

    /* Init BAR5+VRAM (safe, clean state) */
    UINT8 initBuf[32] = {0};
    *(UINT64*)(initBuf+0)=0xFE800000; *(UINT32*)(initBuf+8)=0x80000;
    *(UINT32*)(initBuf+12)=1; *(UINT64*)(initBuf+16)=0xC0000000;
    *(UINT32*)(initBuf+24)=0x20000000;
    DWORD br=0; DeviceIoControl(gH, 0x80000B80, initBuf, 32, NULL, 0, &br, NULL);
    printf("BAR5+VRAM init done\n");

    /* ===== 1. GRBM Status ===== */
    printf("\n=== GRBM STATUS ===\n");
    printf("GRBM_STATUS(0x3260)=0x%08X\n", R(0x3260));
    printf("GRBM_STATUS2(0x3268)=0x%08X\n", R(0x3268));

    /* ===== 2. KIQ_SIZE writability test (CRITICAL) ===== */
    printf("\n=== KIQ_SIZE (0xE068) — CRITICAL TEST ===\n");
    UINT32 kiqSizeBefore = R(0xE068);
    printf("Initial value: 0x%08X\n", kiqSizeBefore);
    if (kiqSizeBefore == 0) {
        W(0xE068, 0x00001004);  /* try writing size=4096 + valid */
        UINT32 kiqSizeAfter = R(0xE068);
        printf("After write 0x1004: 0x%08X %s\n", kiqSizeAfter,
            (kiqSizeAfter != kiqSizeBefore) ? "*** WRITABLE! ***" : "(still read-only)");
    } else {
        printf("KIQ_SIZE is NOT zero! value=0x%08X\n", kiqSizeBefore);
    }

    /* ===== 3. KIQ_WPTR writability test ===== */
    printf("\n=== KIQ_WPTR (0xE078) ===\n");
    UINT32 wptrBefore = R(0xE078);
    printf("Initial: 0x%08X\n", wptrBefore);
    W(0xE078, 0x40);
    printf("After write 0x40: 0x%08X %s\n", R(0xE078),
        (R(0xE078) == 0x40) ? "*** WRITABLE! ***" : "read-only");
    W(0xE078, 0);  /* restore */

    /* ===== 4. COMPUTE register area ===== */
    printf("\n=== COMPUTE registers (0x80E0-0x8114) ===\n");
    for (UINT32 off = 0x80E0; off <= 0x8114; off += 4) {
        printf("  0x%04X = 0x%08X", off, R(off));
        if (off == 0) printf("");
        printf("\n");
    }

    /* ===== 5. DIM_X writability test ===== */
    printf("\n=== DIM_X (0x80E4) WRITABILITY TEST ===\n");
    UINT32 dimXBefore = R(0x80E4);
    printf("Initial: 0x%08X\n", dimXBefore);
    W(0x80E4, 1);
    UINT32 dimXAfter = R(0x80E4);
    printf("After write 1: 0x%08X %s\n", dimXAfter,
        (dimXAfter != dimXBefore) ? "*** WRITABLE! ***" : "read-only");

    /* ===== 6. DISPATCH_INITIATOR ===== */
    printf("\n=== DISPATCH_INITIATOR (0x80E0) ===\n");
    printf("Initial: 0x%08X\n", R(0x80E0));
    W(0x80E0, 0xFFFFFFFF);
    printf("After write 0xFFFFFFFF: 0x%08X (W1C: 0=%s)\n", R(0x80E0),
        (R(0x80E0) == 0) ? "yes, W1C" : "no");

    /* ===== 7. PGM_LO writability ===== */
    printf("\n=== PGM_LO (0x8110) WRITABILITY ===\n");
    UINT32 pgmLoBefore = R(0x8110);
    printf("Initial: 0x%08X\n", pgmLoBefore);
    W(0x8110, 0x00C01000);
    printf("After write: 0x%08X %s\n", R(0x8110),
        (R(0x8110) == 0x00C01000) ? "*** WRITABLE! ***" : "read-only");

    /* ===== 8. SDMA_WPTR writability ===== */
    printf("\n=== SDMA0_RB_WPTR (0xE010) ===\n");
    UINT32 sdmaWptrBefore = R(0xE010);
    printf("Initial: 0x%08X\n", sdmaWptrBefore);
    W(0xE010, 0x10);
    printf("After write 0x10: 0x%08X %s\n", R(0xE010),
        (R(0xE010) != sdmaWptrBefore) ? "*** WRITABLE! ***" : "read-only");

    /* ===== 9. CP_HQD registers ===== */
    printf("\n=== CP_HQD key registers ===\n");
    printf("CP_MQD_BASE(0x9104)=0x%08X\n", R(0x9104));
    printf("CP_MQD_BASE_HI(0x9108)=0x%08X\n", R(0x9108));
    printf("CP_HQD_ACTIVE(0x910C)=0x%08X\n", R(0x910C));
    printf("CP_HQD_PQ_BASE(0x9124)=0x%08X\n", R(0x9124));
    printf("CP_HQD_PQ_CNTL(0x9148)=0x%08X\n", R(0x9148));

    /* ===== 10. GCVM_CONTEXT0_CNTL ===== */
    printf("\n=== GCVM ===\n");
    printf("CONTEXT0_CNTL(0xB460)=0x%08X\n", R(0xB460));
    printf("PT_BASE_LO(0x6C8C)=0x%08X\n", R(0x6C8C));
    printf("PT_BASE_HI(0x6C90)=0x%08X\n", R(0x6C90));

    /* ===== 11. ME/ MEC halt status ===== */
    printf("\n=== HALT STATUS ===\n");
    printf("CP_ME_CNTL(0x4A74)=0x%08X (ME_HALT=%d PFP_HALT=%d CE_HALT=%d)\n",
        R(0x4A74), (R(0x4A74)>>28)&1, (R(0x4A74)>>30)&1, (R(0x4A74)>>29)&1);
    printf("CP_MEC_CNTL(0x4B14)=0x%08X (ME1_HALT=%d)\n",
        R(0x4B14), (R(0x4B14)>>28)&1);

    CloseHandle(gH);
    return 0;
}
