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

int main(void) {
    gH = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (gH == INVALID_HANDLE_VALUE) { printf("ERR\n"); return 1; }

    UINT8 initBuf[32] = {0};
    *(UINT64*)(initBuf+0)=0xFE800000; *(UINT32*)(initBuf+8)=0x80000;
    *(UINT32*)(initBuf+12)=1; *(UINT64*)(initBuf+16)=0xC0000000;
    *(UINT32*)(initBuf+24)=0x20000000;
    DWORD br=0; DeviceIoControl(gH, 0x80000B80, initBuf, 32, NULL, 0, &br, NULL);

    /* Probe SDMA area */
    printf("=== SDMA area (0xE000-0xE0A0) ===\n");
    for (UINT32 off = 0xE000; off <= 0xE0A0; off += 4) {
        UINT32 v = R(off);
        printf("  0x%04X = 0x%08X", off, v);
        if (v == 0xFFFFFFFF) printf(" DEAD");
        else if (v == 0) printf(" (0)");
        else printf(" alive");
        printf("\n");
    }

    /* Also probe KIQ area (0xE060-0xE088) which we know */
    printf("\n=== KIQ area (0xE060-0xE088) ===\n");
    for (UINT32 off = 0xE060; off <= 0xE088; off += 4) {
        printf("  0x%04X = 0x%08X\n", off, R(off));
    }

    /* Try writing SDMA0_CNTL (at 0xE018 if correct) */
    printf("\n=== SDMA writability test ===\n");
    /* Test 0xE000 (RB_BASE_LO) */
    UINT32 v0 = R(0xE000);
    W(0xE000, 0xDEAD);
    printf("  0xE000: 0x%08X -> 0x%08X %s\n", v0, R(0xE000),
        (R(0xE000) != v0) ? "WRITABLE" : "read-only");
    W(0xE000, v0);

    /* Test 0xE004 (RB_BASE_HI) */
    v0 = R(0xE004);
    W(0xE004, 0xDEAD);
    printf("  0xE004: 0x%08X -> 0x%08X %s\n", v0, R(0xE004),
        (R(0xE004) != v0) ? "WRITABLE" : "read-only");
    W(0xE004, v0);

    /* Test 0xE008 (RB_CNTL) */
    v0 = R(0xE008);
    W(0xE008, 0x1000);
    printf("  0xE008: 0x%08X -> 0x%08X %s\n", v0, R(0xE008),
        (R(0xE008) != v0) ? "WRITABLE" : "read-only");
    W(0xE008, v0);

    /* Test 0xE010 (RB_WPTR) */
    v0 = R(0xE010);
    W(0xE010, 0x10);
    printf("  0xE010: 0x%08X -> 0x%08X %s\n", v0, R(0xE010),
        (R(0xE010) != v0) ? "WRITABLE" : "read-only");
    W(0xE010, v0);

    /* Test 0xE018 (SDMA0_CNTL) */
    v0 = R(0xE018);
    W(0xE018, v0 | 1);
    printf("  0xE018: 0x%08X -> 0x%08X %s\n", v0, R(0xE018),
        (R(0xE018) != v0) ? "WRITABLE" : "read-only");
    W(0xE018, v0);

    /* Also check GC_BASE-shifted versions */
    printf("\n=== Alternative SDMA area (GC_BASE relative) ===\n");
    for (UINT32 off = 0x1260 + 0x8000; off <= 0x1260 + 0x8040; off += 4) {
        UINT32 v = R(off);
        printf("  0x%04X = 0x%08X", off, v);
        if (v == 0xFFFFFFFF) printf(" DEAD");
        else if (v != 0) printf(" alive");
        printf("\n");
    }

    CloseHandle(gH);
    return 0;
}
