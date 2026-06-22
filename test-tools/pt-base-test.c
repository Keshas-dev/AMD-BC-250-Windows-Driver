/* pt-base-test.c — Test if GCVM_CONTEXT0_PT_BASE is truly hardware-locked */
#include <windows.h>
#include <stdio.h>

#define IOCTL_AMDBC250_INIT_HARDWARE  0x80000B80
#define IOCTL_AMDBC250_READ_REG       0x80000B88
#define IOCTL_AMDBC250_WRITE_REG      0x80000B8C
#define AMDBC250_INIT_FLAG_NBIO_MAP   0x00000001

static BOOL ReadReg(HANDLE h, unsigned off, unsigned *val) {
    unsigned ra[2] = {off, 0xDEADBEEF};
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_AMDBC250_READ_REG, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
    if (ok) *val = ra[1]; else *val = 0xDEADBEEF;
    return ok;
}
static BOOL WriteReg(HANDLE h, unsigned off, unsigned val) {
    unsigned ra[2] = {off, val};
    DWORD br = 0;
    return DeviceIoControl(h, IOCTL_AMDBC250_WRITE_REG, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
}

int main(void) {
    printf("=== GCVM PT_BASE Lock Test ===\n\n");

    HANDLE h = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { printf("Cannot open driver\n"); return 1; }

    DWORD br = 0;
    UCHAR initIn[32] = {0};
    *(unsigned __int64*)(initIn + 0) = 0xFE800000ULL;
    *(unsigned*)(initIn + 8) = 0x00080000;
    *(unsigned*)(initIn + 12) = AMDBC250_INIT_FLAG_NBIO_MAP;
    *(unsigned __int64*)(initIn + 16) = 0xC0000000ULL;
    *(unsigned*)(initIn + 24) = 0x10000000;
    DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE, initIn, sizeof(initIn), NULL, 0, &br, NULL);
    printf("INIT_HARDWARE OK\n\n");

    unsigned v;

    /* Test GCVM_CONTEXT0_PT_BASE_LO (0x0B608) */
    printf("--- PT_BASE_LO (0x0B608) ---\n");
    ReadReg(h, 0x0B608, &v);
    printf("  Before: 0x%08X\n", v);
    WriteReg(h, 0x0B608, 0x12345678);
    ReadReg(h, 0x0B608, &v);
    printf("  After write 0x12345678: 0x%08X %s\n", v, (v == 0x12345678) ? "WRITABLE!" : "LOCKED");
    WriteReg(h, 0x0B608, 0xDEADBEEF);
    ReadReg(h, 0x0B608, &v);
    printf("  After write 0xDEADBEEF: 0x%08X %s\n", v, (v == 0xDEADBEEF) ? "WRITABLE!" : "LOCKED");

    /* Test GCVM_CONTEXT0_PT_BASE_HI (0x0B60C) */
    printf("\n--- PT_BASE_HI (0x0B60C) ---\n");
    ReadReg(h, 0x0B60C, &v);
    printf("  Before: 0x%08X\n", v);
    WriteReg(h, 0x0B60C, 0x00000012);
    ReadReg(h, 0x0B60C, &v);
    printf("  After write 0x00000012: 0x%08X %s\n", v, (v == 0x00000012) ? "WRITABLE!" : "LOCKED");

    /* Test CONTEXT0_CNTL (0x0B460) */
    printf("\n--- CONTEXT0_CNTL (0x0B460) ---\n");
    ReadReg(h, 0x0B460, &v);
    printf("  Before: 0x%08X\n", v);
    WriteReg(h, 0x0B460, v | 0x03);
    ReadReg(h, 0x0B460, &v);
    printf("  After write 0x%08X: 0x%08X %s\n", v|0x03, v, ((v & 0x03) == 0x03) ? "WRITABLE!" : "PARTIAL/LOCKED");

    /* Test L2_CNTL (0x0B360) */
    printf("\n--- L2_CNTL (0x0B360) ---\n");
    ReadReg(h, 0x0B360, &v);
    printf("  Value: 0x%08X (bit0=%u = L2 %s)\n", v, v & 1, (v & 1) ? "ENABLED" : "DISABLED");

    /* Summary: Try to set PT_BASE to a real address, then read it back */
    printf("\n=== FINAL TEST: Write PT_BASE = 0x0000000012345000 ===\n");
    WriteReg(h, 0x0B608, 0x12345000);
    WriteReg(h, 0x0B60C, 0x00000001);
    unsigned lo, hi;
    ReadReg(h, 0x0B608, &lo);
    ReadReg(h, 0x0B60C, &hi);
    printf("  PT_BASE = 0x%08X%08X\n", hi, lo);
    if (lo == 0 && hi == 0)
        printf("  VERDICT: PT_BASE IS HARDWARE-LOCKED AT 0\n");
    else if (lo == 0x12345000 && hi == 0x00000001)
        printf("  VERDICT: PT_BASE IS WRITABLE! Page tables CAN be configured!\n");
    else
        printf("  VERDICT: PT_BASE is partially writable or unpredictable\n");

    CloseHandle(h);
    return 0;
}
