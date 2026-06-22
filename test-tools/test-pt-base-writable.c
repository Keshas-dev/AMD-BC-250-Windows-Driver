/* test-pt-base-writable.c — Check if GCVM PT_BASE is writable with IOMMU OFF */
#include <windows.h>
#include <stdio.h>

static BOOL ReadReg(HANDLE h, unsigned offset, unsigned *val) {
    unsigned ra[2] = {offset, 0xDEADBEEF};
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, 0x80000B88, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
    if (ok) *val = ra[1]; else *val = 0xDEADBEEF;
    return ok;
}

static BOOL WriteReg(HANDLE h, unsigned offset, unsigned val) {
    unsigned ra[2] = {offset, val};
    DWORD br = 0;
    return DeviceIoControl(h, 0x80000B8C, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
}

int main(void) {
    printf("=== PT_BASE Write Test (IOMMU OFF) ===\n");

    HANDLE h = CreateFileW(L"\\\\.\\AMDBC250DreamV43",
        GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("Cannot open device error=%lu\n", GetLastError());
        return 1;
    }

    /* INIT_HARDWARE */
    UCHAR initIn[32] = {0}; DWORD br = 0;
    *(unsigned __int64*)(initIn + 0) = 0xFE800000ULL;
    *(unsigned*)(initIn + 8) = 0x00080000;
    *(unsigned*)(initIn + 12) = 1;
    *(unsigned __int64*)(initIn + 16) = 0xC0000000ULL;
    *(unsigned*)(initIn + 24) = 0x10000000;
    DeviceIoControl(h, 0x80000B80, initIn, sizeof(initIn), NULL, 0, &br, NULL);

    unsigned val, old, readback;

    /* Test old offset 0x0B608 */
    printf("\n--- Old offset 0x0B608 ---\n");
    ReadReg(h, 0x0B608, &old);
    printf("Before: 0x%08X\n", old);
    WriteReg(h, 0x0B608, 0xDEADBEEF);
    ReadReg(h, 0x0B608, &readback);
    printf("After write DEADBEEF: 0x%08X %s\n", readback,
        readback == 0xDEADBEEF ? "WRITABLE!" : "READ-ONLY");
    WriteReg(h, 0x0B608, old);

    /* Test Linux offset 0x6C8C */
    printf("\n--- Linux offset 0x6C8C ---\n");
    ReadReg(h, 0x6C8C, &old);
    printf("Before: 0x%08X\n", old);
    WriteReg(h, 0x6C8C, 0xDEADBEEF);
    ReadReg(h, 0x6C8C, &readback);
    printf("After write DEADBEEF: 0x%08X %s\n", readback,
        readback == 0xDEADBEEF ? "WRITABLE!" : "READ-ONLY");
    WriteReg(h, 0x6C8C, old);

    /* Test GCVM_CONTEXT0_CNTL at both offsets */
    printf("\n--- GCVM_CONTEXT0_CNTL ---\n");
    ReadReg(h, 0x0B460, &old);
    printf("OLD 0x0B460: 0x%08X\n", old);
    WriteReg(h, 0x0B460, 0x03);
    ReadReg(h, 0x0B460, &readback);
    printf("After write 0x03: 0x%08X %s\n", readback,
        readback == 0x03 ? "WRITABLE!" : "READ-ONLY");
    WriteReg(h, 0x0B460, old);

    ReadReg(h, 0x6AE0, &old);
    printf("NEW 0x6AE0: 0x%08X\n", old);
    WriteReg(h, 0x6AE0, 0x03);
    ReadReg(h, 0x6AE0, &readback);
    printf("After write 0x03: 0x%08X %s\n", readback,
        readback == 0x03 ? "WRITABLE!" : "READ-ONLY");
    WriteReg(h, 0x6AE0, old);

    /* Test GCVM_L2_CNTL at both offsets */
    printf("\n--- GCVM_L2_CNTL ---\n");
    ReadReg(h, 0x0B360, &old);
    printf("OLD 0x0B360: 0x%08X\n", old);
    ReadReg(h, 0x69E0, &old);
    printf("NEW 0x69E0: 0x%08X\n", old);

    /* Test SCRATCH write */
    printf("\n--- SCRATCH Write Test ---\n");
    ReadReg(h, 0x32D4, &old);
    printf("SCRATCH before: 0x%08X\n", old);
    WriteReg(h, 0x32D4, 0x12345678);
    ReadReg(h, 0x32D4, &readback);
    printf("After write 0x12345678: 0x%08X %s\n", readback,
        readback == 0x12345678 ? "WRITABLE!" : "READ-ONLY");
    WriteReg(h, 0x32D4, old);

    CloseHandle(h);
    printf("\n=== Done ===\n");
    return 0;
}
