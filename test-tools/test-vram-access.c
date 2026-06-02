#include <windows.h>
#include <stdio.h>
#include "amdbc250_ioctl.h"

static HANDLE h;

static BOOL Ioctl(DWORD code, void* in, DWORD inSize, void* out, DWORD outSize) {
    DWORD bytes = 0;
    return DeviceIoControl(h, code, in, inSize, out, outSize, &bytes, NULL);
}

static ULONG ReadReg(DWORD offset) {
    AMDBC250_IOCTL_REG_ACCESS reg = {0};
    reg.RegisterOffset = offset;
    Ioctl(0x80000B88, &reg, sizeof(reg), &reg, sizeof(reg));
    return reg.Value;
}

static void WriteReg(DWORD offset, ULONG val) {
    AMDBC250_IOCTL_REG_ACCESS reg = {0};
    reg.RegisterOffset = offset;
    reg.Value = val;
    Ioctl(0x80000B8C, &reg, sizeof(reg), NULL, 0);
}

int main(void) {
    printf("AMD BC-250 VRAM Access Test\n");
    printf("===========================\n\n");

    h = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("ERROR: Cannot open device\n");
        return 1;
    }
    printf("[OK] Device opened\n");

    AMDBC250_IOCTL_INIT_HARDWARE init = {0};
    init.MmioPhysicalBase = 0xFE800000;   /* BAR5 = registers (Linux dmesg confirmed) */
    init.MmioSize = 0x00080000;           /* 512KB */
    init.FbPhysicalBase = 0xC0000000;     /* BAR0 = VRAM */
    init.FbSize = 0x10000000;             /* 256MB */
    Ioctl(0x80000B80, &init, sizeof(init), NULL, 0);
    printf("[OK] INIT_HARDWARE: MMIO=0xFE800000(512KB), VRAM=0xC0000000(256MB)\n\n");

    printf("=== Test 1: VRAM Write-Read (verify memory access) ===\n");
    ULONG testPatterns[] = {0xDEADBEEF, 0x12345678, 0xFFFFFFFF, 0x00000000, 0xA5A5A5A5, 0x5A5A5A5A};
    int vramOk = 0;
    for (int i = 0; i < sizeof(testPatterns)/sizeof(testPatterns[0]); i++) {
        WriteReg(0x0, testPatterns[i]);
        ULONG readback = ReadReg(0x0);
        printf("  Write=0x%08X -> Read=0x%08X %s\n",
               testPatterns[i], readback,
               (readback == testPatterns[i]) ? "MATCH" : "differs");
        if (readback == testPatterns[i]) vramOk++;
    }
    printf("\n  VRAM: %s (%d/%d writes matched)\n",
           vramOk > 0 ? "ACCESSIBLE" : "NOT ACCESSIBLE", vramOk, 6);

    printf("\n=== Test 2: Check for mirrored regions ===\n");
    printf("  (write to 0x0, read from various offsets)\n");
    WriteReg(0x0, 0xAABBCCDD);
    for (DWORD off = 0; off <= 0x10000; off += 0x1000) {
        ULONG val = ReadReg(off);
        if (val == 0xAABBCCDD) {
            printf("  Offset 0x%08X: mirrors 0x0!\n", off);
        }
    }
    WriteReg(0x0, 0x00000000);

    printf("\n=== Test 3: Fill VRAM with pattern and verify ===\n");
    printf("  Writing 0x12345678 to offset 0, 4, 8, 12, 16, 20...\n");
    for (DWORD off = 0; off < 0x10000; off += 4) {
        WriteReg(off, 0x12345678);
    }
    int errors = 0;
    for (DWORD off = 0; off < 0x10000; off += 4) {
        ULONG val = ReadReg(off);
        if (val != 0x12345678) {
            if (errors < 5) {
                printf("  VERIFY ERROR at 0x%04X: got 0x%08X\n", off, val);
            }
            errors++;
        }
    }
    printf("  Verification: %d errors in %d locations\n", errors, 0x10000/4);

    printf("\n=== Summary ===\n");
    printf("VRAM write/read: %s\n", vramOk > 0 ? "WORKS" : "FAILS");
    printf("VRAM mirroring: %s\n", vramOk > 0 ? "depends on above" : "N/A");

    CloseHandle(h);
    return 0;
}
