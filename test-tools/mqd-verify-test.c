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

    /* Test 1: Can we write to CP_RING0_BASE_LO? */
    printf("=== Test 1: GFX Ring (CP_RING0) writability ===\n");
    UINT32 origBaseLo = R(0xDA60);
    UINT32 origCntl = R(0xDA68);
    printf("Before: BASE_LO=0x%08X CNTL=0x%08X\n", origBaseLo, origCntl);

    UINT32 testVal = 0x12345678;
    W(0xDA60, testVal);
    UINT32 readBack = R(0xDA60);
    printf("After write 0x%08X: read=0x%08X %s\n", testVal, readBack,
        (readBack == testVal) ? "WRITABLE!" : "READ-ONLY");
    W(0xDA60, origBaseLo);

    /* Test 2: MQD write verification */
    printf("\n=== Test 2: MQD write verification ===\n");
    UINT8 ptBuf[256] = {0};
    if (!DeviceIoControl(gH, 0x8000098C, NULL, 0, ptBuf, sizeof(ptBuf), &br, NULL)) {
        printf("GCVM SETUP FAILED\n");
        CloseHandle(gH); return 1;
    }
    UINT32* pt = (UINT32*)ptBuf;
    if (pt[9] != 0xCAFEBABE) { printf("Bad GCVM\n"); CloseHandle(gH); return 1; }
    UINT64 ringPa = ((UINT64)pt[2] << 32) | pt[1];
    printf("ringPa = 0x%016llX\n", ringPa);

    /* Enable GCVM */
    UINT32 cntl = R(0xB460);
    if (!(cntl & 1)) { W(0xB460, cntl|1); W(0x6C10,1); W(0x6C0C,1); Sleep(10); }

    /* Write MQD with magic */
    UINT32 mqd[256] = {0};
    mqd[11] = 0xCAFEBABE;  /* PGM_LO */
    WritePhys(ringPa, mqd, 1024);

    /* Read back MQD */
    UINT32 check[4];
    ReadPhys(ringPa, check, 16);
    printf("MQD[0..3] at ringPa: 0x%08X 0x%08X 0x%08X 0x%08X\n",
        check[0], check[1], check[2], check[3]);
    printf("MQD[11] (PGM_LO slot)=0x%08X %s\n", check[11],
        (check[11] == 0xCAFEBABE) ? "MATCH!" : "MISMATCH");

    /* Set CP_HQD_ACTIVE and check PGM_LO reg */
    W(0x9104, (UINT32)(ringPa & 0xFFFFFFFF));
    W(0x910C, 1);
    Sleep(50);
    UINT32 pgmLo = R(0x8110);
    printf("After CP_HQD_ACTIVE: HW PGM_LO=0x%08X (expected 0xCAFEBABE)\n", pgmLo);
    printf("PGM_LO %s\n", (pgmLo == 0xCAFEBABE) ? "MATCH! MQD loads correctly!" : "MISMATCH - MQD not loading");

    /* Cleanup */
    W(0x910C, 0);

    printf("\n=== Test 3: Direct register write to CP_RING0 ===\n");
    printf("Writing CP_RING0_BASE=ringPa, CNTL=0x10, scracth test\n");
    W(0xDA60, (UINT32)(ringPa & 0xFFFFFFFF));
    W(0xDA64, (UINT32)(ringPa >> 32));
    W(0xDA68, 0x10);  /* size=16 dwords */
    W(0xDA78, 0);     /* WPTR = 0 */
    printf("BASE_LO=0x%08X BASE_HI=0x%08X CNTL=0x%08X\n",
        R(0xDA60), R(0xDA64), R(0xDA68));

    CloseHandle(gH);
    printf("\nDone\n");
    return 0;
}
