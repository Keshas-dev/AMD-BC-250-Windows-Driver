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
    if (gH == INVALID_HANDLE_VALUE) { printf("ERR\n"); return 1; }

    UINT8 initBuf[32] = {0};
    *(UINT64*)(initBuf+0)=0xFE800000; *(UINT32*)(initBuf+8)=0x80000;
    *(UINT32*)(initBuf+12)=1; *(UINT64*)(initBuf+16)=0xC0000000;
    *(UINT32*)(initBuf+24)=0x20000000;
    DWORD br=0; DeviceIoControl(gH, 0x80000B80, initBuf, 32, NULL, 0, &br, NULL);

    printf("=== CP_HQD area (0x9104-0x91F0) ===\n");
    for (UINT32 off = 0x9104; off <= 0x91F0; off += 4) {
        UINT32 v = R(off);
        printf("  0x%04X = 0x%08X", off, v);
        /* Mark dead or alive */
        if (v == 0xFFFFFFFF) printf(" DEAD");
        else if (v != 0) printf(" alive");
        printf("\n");
    }

    printf("\n=== Test write to 0x9124 (CP_HQD_PQ_BASE_LO) ===\n");
    UINT32 before = R(0x9124);
    W(0x9124, 0x12345678);
    UINT32 after = R(0x9124);
    printf("  0x9124: 0x%08X -> 0x%08X %s\n", before, after, (before != after && after == 0x12345678) ? "WRITABLE!" : "read-only");
    W(0x9124, before);  /* restore */

    printf("\n=== Test write to 0x9148 (CP_HQD_PQ_CONTROL) ===\n");
    before = R(0x9148);
    W(0x9148, 0x00001000);
    after = R(0x9148);
    printf("  0x9148: 0x%08X -> 0x%08X %s\n", before, after, (before != after && after == 0x00001000) ? "WRITABLE!" : "read-only");
    W(0x9148, before);

    printf("\n=== Test write to 0x91DC (CP_HQD_PQ_WPTR_LO) ===\n");
    before = R(0x91DC);
    W(0x91DC, 0x100);
    after = R(0x91DC);
    printf("  0x91DC: 0x%08X -> 0x%08X %s\n", before, after, (before != after) ? "WRITABLE!" : "read-only");
    W(0x91DC, before);

    printf("\n=== Test write to 0x910C (CP_HQD_ACTIVE) ===\n");
    W(0x910C, 0);
    UINT32 v = R(0x910C);
    printf("  0x910C = 0x%08X\n", v);
    W(0x910C, 1);
    v = R(0x910C);
    printf("  0x910C = 0x%08X\n", v);

    CloseHandle(gH);
    return 0;
}
