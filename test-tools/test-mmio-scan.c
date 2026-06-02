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

static int TryInit(ULONG64 pa, ULONG size, const char* name) {
    printf("\n=== Trying %s: PA=0x%llX, Size=0x%X ===\n", name, pa, size);
    AMDBC250_IOCTL_INIT_HARDWARE init = {0};
    init.MmioPhysicalBase = pa;
    init.MmioSize = size;
    BOOL ok = Ioctl(0x80000B80, &init, sizeof(init), NULL, 0);
    printf("  Init: %s\n", ok ? "OK" : "FAILED");
    if (!ok) return 0;

    printf("  Register reads:\n");
    ULONG vals[5];
    for (int i = 0; i < 5; i++) {
        vals[i] = ReadReg(i * 4);
    }
    printf("    0x0000-0x0010: %08X %08X %08X %08X %08X\n",
           vals[0], vals[1], vals[2], vals[3], vals[4]);

    ULONG id = ReadReg(0x0000);
    ULONG ver = ReadReg(0x0004);
    printf("  GPU_ID=0x%08X, REV=0x%08X\n", id, ver);

    if (id != 0 || ver != 0) {
        printf("  *** NON-ZERO REGISTERS DETECTED! ***\n");
        return 1;
    }
    printf("  (all zeros - no registers at this base)\n");
    return 0;
}

int main(void) {
    printf("AMD BC-250 MMIO Base Address Scanner\n");
    printf("=====================================\n\n");

    h = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
                    0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("ERROR: Cannot open device\n");
        return 1;
    }
    printf("[OK] Device opened\n");

    int found = 0;

    found |= TryInit(0xFE800000, 0x00080000, "MMIO BAR5 (Linux dmesg confirmed!)");
    found |= TryInit(0xD0000000, 0x00200000, "MMIO BAR2 (lspci)");
    found |= TryInit(0xC0000000, 0x10000000, "VRAM BAR0 (not registers)");
    found |= TryInit(0x03B00000, 0x00100000, "SMN base (SOC15)");
    found |= TryInit(0x03A00000, 0x00100000, "SMN alt");
    found |= TryInit(0x03F00000, 0x00100000, "SMN alt 2");
    found |= TryInit(0xFEDC0000, 0x00001000, "Chipset SMN");

    printf("\n=====================================\n");
    if (found)
        printf("RESULT: Non-zero registers FOUND!\n");
    else
        printf("RESULT: No GPU registers found at any tested address\n");

    CloseHandle(h);
    return 0;
}
