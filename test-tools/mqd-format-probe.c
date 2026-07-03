#include <windows.h>
#include <stdio.h>
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
    ((UINT32*)buf)[0] = (UINT32)(pa & 0xFFFFFFFF);
    ((UINT32*)buf)[1] = (UINT32)(pa >> 32);
    ((UINT32*)buf)[2] = size;
    memcpy(buf + 12, data, size);
    DWORD br = 0;
    DeviceIoControl(gH, 0x80000C10, buf, 12 + size, NULL, 0, &br, NULL);
}
static ULONG ReadPhys(UINT64 pa, void* data, ULONG size) {
    UCHAR inbuf[24], outbuf[4096];
    ((UINT32*)inbuf)[0] = (UINT32)(pa & 0xFFFFFFFF);
    ((UINT32*)inbuf)[1] = (UINT32)(pa >> 32);
    ((UINT32*)inbuf)[2] = size;
    DWORD br = 0;
    DeviceIoControl(gH, 0x80000C14, inbuf, 12, outbuf, sizeof(outbuf), &br, NULL);
    if (br > 0) memcpy(data, outbuf, min(br, size));
    return br;
}

int main(void) {
    gH = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL);
    if (gH == INVALID_HANDLE_VALUE) { printf("FAIL: open\n"); return 1; }

    UINT8 initBuf[32] = {0};
    *(UINT64*)(initBuf+0)=0xFE800000; *(UINT32*)(initBuf+8)=0x80000;
    *(UINT32*)(initBuf+12)=1; *(UINT64*)(initBuf+16)=0xC0000000;
    *(UINT32*)(initBuf+24)=0x20000000;
    DWORD br=0; DeviceIoControl(gH, 0x80000B80, initBuf, 32, NULL, 0, &br, NULL);

    printf("=== MQD Format Probe ===\n\n");

    /* Read initial PGM_LO */
    UINT32 pgmLoInit = R(0x8110);
    printf("Initial PGM_LO (0x8110) = 0x%08X\n", pgmLoInit);
    printf("Initial CP_MQD_BASE_ADDR (0x9104) = 0x%08X\n", R(0x9104));
    printf("Initial CP_MQD_BASE_ADDR_HI (0x9108) = 0x%08X\n", R(0x9108));
    printf("Initial CP_HQD_ACTIVE (0x910C) = 0x%08X\n", R(0x910C));

    /* Read PGM_LO multiple times to verify stability */
    printf("\nPGM_LO reads (10x, 20ms apart):\n");
    for (int i = 0; i < 10; i++) {
        printf("  t=%d: 0x%08X\n", i*20, R(0x8110));
        Sleep(20);
    }

    /* Test: write PGM_LO via MQD at different offsets to find correct format */
    printf("\n=== MQD Field Probe ===\n");
    UINT64 mqdPhys = 0xC0200000ULL;
    UINT32 testPattern = 0xA5A5A5A5;
    UINT32 mqd[64] = {0}; /* 256 bytes of MQD */

    /* We'll probe each 4-byte offset in the MQD to see which one maps to PGM_LO */
    printf("Probing MQD offsets for PGM_LO mapping:\n");
    printf("(writing test pattern at each offset, loading MQD, checking PGM_LO)\n\n");

    /* Array of MQD offsets to test (DWORDS, 0-63) */
    UINT32 offsetsToTest[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60};
    UINT32 origMqdBase = R(0x9104);
    UINT32 origHqdActive = R(0x910C);

    for (int oi = 0; oi < sizeof(offsetsToTest)/sizeof(offsetsToTest[0]); oi++) {
        UINT32 off = offsetsToTest[oi]; /* DWORD offset into MQD */
        UINT64 byteOff = (UINT64)off * 4;

        /* Clear MQD and write test pattern at this offset */
        memset(mqd, 0, sizeof(mqd));
        mqd[off] = testPattern;
        WritePhys(mqdPhys, mqd, sizeof(mqd));

        /* Write MQD base (low) */
        W(0x9104, (UINT32)(mqdPhys & 0xFFFFFFFF));

        /* Activate HQD to load MQD */
        W(0x910C, 0); /* deactivate first */
        W(0x910C, 1); /* activate */
        Sleep(1); /* small delay for MQD load */

        /* Read PGM_LO */
        UINT32 pgmLo = R(0x8110);
        if (pgmLo != pgmLoInit) {
            printf("  MQD DWORD[%u] (byte 0x%02llX): PGM_LO=0x%08X (diff=0x%08X)\n",
                off, byteOff, pgmLo, pgmLo ^ pgmLoInit);
        }
    }

    /* Reset MQD */
    W(0x910C, 0);
    W(0x9104, origMqdBase);

    /* Now test: what if we put PGM_LO in the MQD and it loads? */
    printf("\n=== Direct MQD PGM_LO load test ===\n");
    UINT32 testPgmLo = 0xDEAD0000;

    /* Try each DWORD offset again but with PGM_LO-specific value */
    memset(mqd, 0, sizeof(mqd));

    /* Write our desired PGM_LO value into MQD at a guessed offset */
    /* GFX10 MQD layout (Linux v10_compute_mqd) typically has PGM_LO at DWORD 28 (0x70) */
    mqd[28] = testPgmLo; /* offset 0x70 */
    WritePhys(mqdPhys, mqd, sizeof(mqd));

    W(0x9104, (UINT32)(mqdPhys & 0xFFFFFFFF));
    W(0x910C, 0);
    W(0x910C, 1);
    Sleep(1);
    UINT32 pgmAfter = R(0x8110);
    printf("  MQD[28]=0x%08X at offset 0x70: PGM_LO=0x%08X (expected 0x%08X)\n",
        testPgmLo, pgmAfter, (pgmAfter == testPgmLo) ? testPgmLo : pgmLoInit);

    /* Try offset 60 (0xF0) */
    mqd[60] = testPgmLo;
    WritePhys(mqdPhys, mqd, sizeof(mqd));
    W(0x910C, 0);
    W(0x910C, 1);
    Sleep(1);
    pgmAfter = R(0x8110);
    printf("  MQD[60]=0x%08X at offset 0xF0: PGM_LO=0x%08X\n", testPgmLo, pgmAfter);

    /* Try offset 0 with PGM_LO */
    mqd[0] = testPgmLo;
    WritePhys(mqdPhys, mqd, sizeof(mqd));
    W(0x910C, 0);
    W(0x910C, 1);
    Sleep(1);
    pgmAfter = R(0x8110);
    printf("  MQD[0]=0x%08X at offset 0x00: PGM_LO=0x%08X\n", testPgmLo, pgmAfter);

    /* Restore */
    W(0x910C, 0);
    W(0x9104, origMqdBase);

    /* Test: does PGM_LO change when we write to different MQD base? */
    printf("\n=== MQD Base Address Effect ===\n");

    /* Note: CP_MQD_BASE_ADDR_HI is RO=0xFF11EFE0
     * What if the actual MQD base is formed differently? */
    UINT32 hiVal = R(0x9108);
    printf("  CP_MQD_BASE_ADDR_HI = 0x%08X (RO)\n", hiVal);

    /* The effective MQD base might be: (HI field) << 16 | (LO field)
     * If HI=0xFF11EFE0, the upper bits might encode the instance/config
     * and only the LO field selects the physical page? */

    CloseHandle(gH);
    printf("\nDone\n");
    return 0;
}
