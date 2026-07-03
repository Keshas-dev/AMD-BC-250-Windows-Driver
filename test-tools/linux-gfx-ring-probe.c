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

static void ProbeRange(const char* label, UINT32 addr, UINT32 cnt, UINT32 step) {
    printf("\n=== %s (0x%04X-0x%04X) ===\n", label, addr, addr + (cnt-1)*step);
    for (UINT32 i = 0; i < cnt; i++) {
        UINT32 a = addr + i * step;
        UINT32 val = R(a);
        printf("  0x%04X = 0x%08X", a, val);
        W(a, 0xA5A5A5A5);
        UINT32 after = R(a);
        W(a, val);
        if (after != val) {
            printf(" -> WROTE (read 0x%08X)", after);
        } else {
            printf(" -> RO");
        }
        printf("%s\n", (val == 0xFFFFFFFF) ? " DEAD" : "");
    }
}

static void WriteTest(const char* name, UINT32 addr, UINT32 testVal) {
    UINT32 before = R(addr);
    W(addr, testVal);
    UINT32 after = R(addr);
    W(addr, before);
    printf("  %s (0x%04X): 0x%08X -> 0x%08X -> 0x%08X %s\n",
        name, addr, before, testVal, after,
        (after == testVal) ? "WRITABLE!" : (after == before) ? "RO" : "W1C?");
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

    printf("=== GFX Ring Register Probe ===\n");
    printf("Comparing Linux-corrected (gc_10_1_0_offset.h) vs current hw.h\n\n");

    printf("Current hw.h addresses (GC_BASE + 0xC800):\n");
    printf("  Name                Addr      Current      Writable?\n");
    WriteTest("CP_RING0_BASE_LO",  0xDA60, 0x12345678);
    WriteTest("CP_RING0_BASE_HI",  0xDA64, 0x00005678);
    WriteTest("CP_RING0_CNTL",     0xDA68, 0x00400001);
    WriteTest("CP_RING0_RPTR",     0xDA6C, 0x00000000);
    WriteTest("CP_RING0_RPTR_ADDR",0xDA70, 0x12345678);
    WriteTest("CP_RING0_WPTR",     0xDA78, 0x00000000);
    WriteTest("CP_RING0_DOORBELL", 0xDA80, 0x00000000);

    printf("\nLinux-corrected addresses (GC_BASE + mm*4):\n");
    printf("  mmCP_RB0_BASE    (0x1DE0):\n");
    WriteTest("  -> RB0_BASE",     0x89E0, 0x12345678);
    WriteTest("  -> RB0_BASE_hi",  0x8BA4, 0x00000000);
    WriteTest("  -> RB0_CNTL",     0x89E4, 0x00400001);
    WriteTest("  -> RB0_WPTR",     0x8A30, 0x00000008);
    WriteTest("  -> RB0_WPTR_HI",  0x8A34, 0x00000000);
    WriteTest("  -> RB0_WPTR_POLL",0x8C90, 0x00000000);
    WriteTest("  -> RB0_RPTR_ADDR",0x89EC, 0x12345678);
    WriteTest("  -> RB0_RPTR_ADDR_HI",0x89F0, 0x00000000);

    printf("\nCP_STAT area (mmCP_RB0_RPTR at 0x0F60):\n");
    WriteTest("  CP_STAT",         0x4F60, 0x00000000);
    WriteTest("  CP_RB0_RPTR",     0x4FE0, 0x00000000);
    WriteTest("  CP_RB_WPTR_DELAY",0x4FE4, 0x00000000);
    WriteTest("  CP_ME_CNTL",      0x4FB8, 0x00000000);

    printf("\nDetailed range scan (Linux-corrected CP_RB0 area):\n");
    ProbeRange("CP_RB0 area (0x89E0-0x8A3F)", 0x89E0, 0x60, 4);

    printf("\nDetailed range scan (current hw.h 0xDA60 area):\n");
    ProbeRange("hw.h CP area (0xDA60-0xDA7F)", 0xDA60, 8, 4);

    printf("\nDetailed range scan (RPTR area 0x4FE0-0x4FFF):\n");
    ProbeRange("CP_RB0_RPTR area (0x4FE0-0x4FFF)", 0x4FE0, 8, 4);

    CloseHandle(gH);
    printf("\nDone\n");
    return 0;
}
