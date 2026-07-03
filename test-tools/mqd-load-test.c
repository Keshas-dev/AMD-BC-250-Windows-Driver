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

static void TlbFlush(void) {
    W(0x6C10, 1);  /* INV_REQ */
    W(0x6C0C, 1);  /* INV_ALL */
    Sleep(10);
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

    /* GCVM_PT_SETUP */
    UINT8 ptBuf[256] = {0};
    DeviceIoControl(gH, 0x8000098C, NULL, 0, ptBuf, sizeof(ptBuf), &br, NULL);
    UINT32* pt = (UINT32*)ptBuf;
    UINT64 ringPa = ((UINT64)pt[2] << 32) | pt[1];
    printf("ringPa = 0x%016llX\n", ringPa);

    UINT32 cntl = R(0xB460);
    if (!(cntl & 1)) { W(0xB460, cntl|1); TlbFlush(); }
    printf("CONTEXT0_CNTL=0x%08X PT_BASE=0x%08X_%08X\n",
        R(0xB460), R(0x6C90), R(0x6C8C));

    /* === Test A: MQD at ringPa (system RAM via GCVM) === */
    printf("\n=== Test A: MQD via GCVM at ringPa ===\n");
    UINT32 mqd[256] = {0};
    mqd[11] = 0xAABB0011;
    WritePhys(ringPa, mqd, 1024);

    UINT32 check[256]; ReadPhys(ringPa, check, 1024);
    printf("MQD[11]=0x%08X %s\n", check[11], check[11]==0xAABB0011?"OK":"FAIL");

    W(0x9104, (UINT32)(ringPa & 0xFFFFFFFF));
    W(0x910C, 1);
    Sleep(50);
    printf("PGM_LO=0x%08X (expect 0xAABB0011) %s\n", R(0x8110),
        R(0x8110)==0xAABB0011?"MATCH!":"no");
    W(0x910C, 0);

    /* === Test B: MQD at ringPa, TLB flush after base write === */
    printf("\n=== Test B: with TLB flush ===\n");
    mqd[11] = 0xBBCC0022;
    WritePhys(ringPa, mqd, 1024);
    W(0x9104, (UINT32)(ringPa & 0xFFFFFFFF));
    TlbFlush();
    W(0x910C, 1);
    Sleep(50);
    printf("PGM_LO=0x%08X (expect 0xBBCC0022) %s\n", R(0x8110),
        R(0x8110)==0xBBCC0022?"MATCH!":"no");
    W(0x910C, 0);

    /* === Test C: MQD at VRAM 0xC0000000 (no GCVM needed) === */
    printf("\n=== Test C: MQD at VRAM 0xC0000000 ===\n");
    UINT64 vramPa = 0xC0000000ULL;
    UINT32 vramMqd[256] = {0};
    vramMqd[11] = 0xCCDD0033;
    WritePhys(vramPa, vramMqd, 1024);
    ReadPhys(vramPa, check, 1024);
    printf("VRAM MQD[11]=0x%08X %s\n", check[11], check[11]==0xCCDD0033?"OK":"FAIL");

    W(0x9104, (UINT32)(vramPa & 0xFFFFFFFF));
    W(0x9104, (UINT32)(vramPa & 0xFFFFFFFF));  /* write twice */
    TlbFlush();
    W(0x910C, 1);
    Sleep(50);
    printf("PGM_LO=0x%08X (expect 0xCCDD0033) %s\n", R(0x8110),
        R(0x8110)==0xCCDD0033?"VRAM MQD WORKS!":"no");
    W(0x910C, 0);

    /* === Test D: Try HW MQD path via CP_HQD_ACTIVE=0 -> 1 -> 0 -> 1 toggle */
    printf("\n=== Test D: Toggle CP_HQD_ACTIVE ===\n");
    mqd[11] = 0xDDFF0044;
    WritePhys(ringPa, mqd, 1024);
    W(0x9104, (UINT32)(ringPa & 0xFFFFFFFF));
    W(0x910C, 0); Sleep(10);
    W(0x910C, 1); Sleep(50);
    printf("PGM_LO=0x%08X (expect 0xDDFF0044) %s\n", R(0x8110),
        R(0x8110)==0xDDFF0044?"MATCH!":"no");

    /* === Test E: Try HQD registers directly === */
    printf("\n=== Test E: Direct HQD regs ===\n");
    printf("CP_HQD_ACTIVE=0x%08X CP_HQD_VMID=0x%08X\n", R(0x910C), R(0x9110));
    printf("CP_HQD_PQ_BASE_LO=0x%08X CP_HQD_PQ_CONTROL=0x%08X\n", R(0x9124), R(0x9148));
    printf("CP_HQD_PQ_WPTR_LO=0x%08X\n", R(0x91DC));

    /* Check if we can write HQD regs directly */
    W(0x9110, 0x77);
    printf("CP_HQD_VMID after write=0x%08X %s\n", R(0x9110),
        R(0x9110)==0x77?"WRITABLE":"READ-ONLY");
    W(0x9110, 0); /* restore */

    CloseHandle(gH);
    printf("\nDone\n");
    return 0;
}
