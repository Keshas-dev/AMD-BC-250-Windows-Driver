#include <windows.h>
#include <stdio.h>
#include <memory.h>

#define IOCTL_INIT_HW            0x80000B80
#define IOCTL_READ_REG           0x80000B88
#define IOCTL_WRITE_REG          0x80000B8C
#define IOCTL_WRITE_PHYSICAL_MEM 0x80000C10

typedef struct { UINT32 Off; UINT32 Val; } REG_IO;

static HANDLE gH = INVALID_HANDLE_VALUE;
static UINT32 R(UINT32 off) {
    REG_IO in={off,0}, out={0}; DWORD br=0;
    DeviceIoControl(gH, IOCTL_READ_REG, &in, sizeof(in), &out, sizeof(out), &br, NULL);
    return out.Val;
}
static void W(UINT32 off, UINT32 val) {
    REG_IO in={off,val}, out={0}; DWORD br=0;
    DeviceIoControl(gH, IOCTL_WRITE_REG, &in, sizeof(in), &out, sizeof(out), &br, NULL);
}
static void WritePhys(UINT64 pa, const void* data, ULONG size) {
    UCHAR buf[4096 + 12];
    ((PULONG)buf)[0] = (ULONG)(pa & 0xFFFFFFFF);
    ((PULONG)buf)[1] = (ULONG)(pa >> 32);
    ((PULONG)buf)[2] = size;
    memcpy(buf + 12, data, size);
    DWORD br = 0;
    DeviceIoControl(gH, IOCTL_WRITE_PHYSICAL_MEM, buf, 12 + size, NULL, 0, &br, NULL);
}

int main(void) {
    printf("=== WIDE SEG1 SCAN v5 ===\n\n");
    gH = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (gH == INVALID_HANDLE_VALUE) { printf("ERROR\n"); return 1; }

    UINT8 initBuf[32] = {0};
    *(UINT64*)(initBuf+0)  = 0xFE800000ULL;
    *(UINT32*)(initBuf+8)  = 0x00080000;
    *(UINT32*)(initBuf+12) = 1;
    *(UINT64*)(initBuf+16) = 0xC0000000ULL;
    *(UINT32*)(initBuf+24) = 0x20000000;
    DWORD br = 0;
    DeviceIoControl(gH, IOCTL_INIT_HW, initBuf, sizeof(initBuf), NULL, 0, &br, NULL);

    /* Write shader to VRAM */
    UINT32 shader[] = { 0xBF9F0000, 0x00000000 };
    WritePhys(0xC0100000, shader, 8);

    /* Write MQD to VRAM */
    UINT32 mqd[512] = {0};
    mqd[0]  = 0xC0310800;  /* header */
    mqd[1]  = 0x80000000;   /* dispatch_initiator */
    mqd[2]  = 1; mqd[3] = 1; mqd[4] = 1;  /* dims */
    mqd[8]  = 32; mqd[9] = 1; mqd[10] = 1;  /* num_threads */
    mqd[11] = 1;  /* pipelinestat_enable */
    mqd[13] = (UINT32)(0xC0100000ULL >> 8) & 0xFFFF;  /* pgm_lo */
    mqd[14] = (UINT32)(0xC0100000ULL >> 24) & 0xFFFF;  /* pgm_hi */
    mqd[20] = 0x00000000;  /* pgm_rsrc1 */
    mqd[21] = 0x00000000;  /* pgm_rsrc2 */
    mqd[22] = 0;  /* vmid */
    mqd[23] = 0x000000FF;  /* resource_limits */
    mqd[24] = 0xFFFFFFFF;  /* thr_mgmt_se0 */
    mqd[25] = 0xFFFFFFFF;  /* thr_mgmt_se1 */
    mqd[33] = 0x3;  /* misc */
    WritePhys(0xC0200000, mqd, sizeof(mqd));

    /* Enable WGPs, disable GCVM */
    UINT32 savedIdx = R(0x34D0);
    UINT32 savedGcvm = R(0xB460);
    UINT32 oldSpi = R(0x34FC);
    W(0xB460, savedGcvm & ~1);
    W(0x34FC, 0xFFFFFFFF);
    W(0x9C1C, 0xFFE00000);
    W(0x3D64, 0xFFFFFFFF);
    printf("  Config: GCVM=OFF, SPI_PG=MAX\n");

    /* SCAN 1: Find MQD header pattern 0xC0310800 in SEG1 + GC_BASE range */
    printf("\n--- Scanning for MQD header (0xC0310800) ---\n");
    UINT32 scanRanges[] = {0xB260, 0xD000, 0xE260};  /* SEG1 start, ring start, SEG1 end */
    UINT32 scanEnds[]   = {0xCF00, 0xE25F, 0xEFFF};
    int found = 0;
    for (int r = 0; r < 3; r++) {
        for (UINT32 off = scanRanges[r]; off <= scanEnds[r]; off += 4) {
            UINT32 v = R(off);
            if (v == 0xC0310800) {
                printf("  FOUND header at 0x%04X!\n", off);
                found++;
            }
        }
    }
    if (!found) printf("  Header 0xC0310800 not found in scanned ranges\n");

    /* SCAN 2: Find writable COMPUTE registers by searching for PGM_LO value */
    printf("\n--- Scanning for PGM_LO value 0x1000 ---\n");
    found = 0;
    for (UINT32 off = 0xB260; off <= 0xEFFF; off += 4) {
        UINT32 v = R(off);
        if (v == 0x00001000 && off != 0xDC70) {
            printf("  Found PGM_LO=0x1000 at 0x%04X (not DC70)\n", off);
            found++;
        }
    }
    if (!found) printf("  No duplicate PGM_LO found\n");

    /* SCAN 3: Single-pass scan of 0xDD00-0xDFFF (extended compute area) */
    printf("\n--- Extended COMPUTE scan [0xDD00-0xDFFC] ---\n");
    int count = 0;
    for (UINT32 off = 0xDD00; off <= 0xDFFC; off += 4) {
        UINT32 v = R(off);
        if (v != 0) {
            printf("  0x%04X = 0x%08X\n", off, v);
            count++;
        }
    }
    if (count == 0) printf("  All zero (no registers)\n");

    /* SCAN 4: Try alternate COMPUTE base at different SEG1 offsets */
    printf("\n--- Checking alternate offsets ---\n");
    /* Linux mmCOMPUTE_DISPATCH_INITIATOR = 0x0A00 (byte offset from SEG1).
     * Possible COMPUTE base: GC_BASE + SEG1 + 0x0000 = 0xB260 */
    UINT32 altBases[] = {0xB260, 0xB270, 0xB280, 0xB300, 0xB400, 0xB500, 0xBA00, 0xC000};
    UINT32 knownVals[] = {0x00001000, 0xBF9F0000};  /* PGM_LO value, shader value */
    for (int b = 0; b < 8; b++) {
        UINT32 base = altBases[b];
        for (int k = 0; k < 2; k++) {
            UINT32 v = R(base + 0x10);  /* PGM_LO would be at +0x10 from base */
            if (v == knownVals[k] && base + 0x10 != 0xDC70) {
                printf("  Possible COMPUTE base at 0x%04X (PGM_LO=0x%08X)\n", base, v);
            }
        }
    }

    /* SCAN 5: Check if any register contains our shader address components */
    /* PGM_LO = (0xC0100000 >> 8) & 0xFFFF = 0x1000 */
    /* PGM_HI = (0xC0100000 >> 24) & 0xFFFF = 0xC0 */
    printf("\n--- Look for 0x000000C0 (PGM_HI) duplicates ---\n");
    for (UINT32 off = 0xB260; off <= 0xEFFF; off += 4) {
        UINT32 v = R(off);
        if (v == 0x000000C0 && off != 0xDC74) {
            printf("  Found PGM_HI=0xC0 at 0x%04X\n", off);
        }
    }

    /* Restore */
    W(0x34D0, savedIdx);
    W(0xB460, savedGcvm);
    W(0x34FC, oldSpi);
    CloseHandle(gH);
    printf("\n=== Done ===\n");
    return 0;
}
