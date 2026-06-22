/* pm4-default-page-test.c — Try DEFAULT_PAGE (GCVM bit1) + correct PM4 opcode */
#include <windows.h>
#include <stdio.h>

#define IOCTL_AMDBC250_INIT_HARDWARE  0x80000B80
#define IOCTL_AMDBC250_READ_REG       0x80000B88
#define IOCTL_AMDBC250_WRITE_REG      0x80000B8C
#define IOCTL_AMDBC250_GPU_KIQ_TEST   0x80000BD0
#define AMDBC250_INIT_FLAG_NBIO_MAP   0x00000001

#define GCVM_CONTEXT0_CNTL   0x0B460
#define SCRATCH_REG          0x32D4

static BOOL ReadReg(HANDLE h, unsigned offset, unsigned *val) {
    unsigned ra[2] = {offset, 0xDEADBEEF};
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_AMDBC250_READ_REG, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
    if (ok) *val = ra[1]; else *val = 0xDEADBEEF;
    return ok;
}

static BOOL WriteReg(HANDLE h, unsigned offset, unsigned val) {
    unsigned ra[2] = {offset, val};
    DWORD br = 0;
    return DeviceIoControl(h, IOCTL_AMDBC250_WRITE_REG, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
}

typedef struct {
    UINT32 Result;
    UINT32 ScratchBefore;
    UINT32 ScratchAfter;
    UINT32 MmioMapped;
    UINT32 RingAllocated;
    UINT32 HqdProgrammed;
    UINT32 Pm4Submitted;
    UINT32 Padding;
} GPU_KIQ_TEST_OUT;

int main(void)
{
    printf("=== PM4 DEFAULT PAGE TEST ===\n");
    printf("Tries: INIT_HARDWARE(NBIO_MAP) -> enable DEFAULT_PAGE -> GPU_KIQ_TEST\n\n");

    HANDLE h = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("Cannot open driver (err=%lu)\n", GetLastError());
        return 1;
    }

    DWORD br = 0;

    /* Step 1: INIT_HARDWARE with NBIO_MAP (just map MMIO, no GPU init) */
    printf("--- Step 1: INIT_HARDWARE (NBIO_MAP) ---\n");
    UCHAR initIn[32] = {0};
    *(unsigned __int64*)(initIn + 0) = 0xFE800000ULL;
    *(unsigned*)(initIn + 8) = 0x00080000;
    *(unsigned*)(initIn + 12) = AMDBC250_INIT_FLAG_NBIO_MAP;
    *(unsigned __int64*)(initIn + 16) = 0xC0000000ULL;
    *(unsigned*)(initIn + 24) = 0x10000000;
    BOOL ok = DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE, initIn, sizeof(initIn), NULL, 0, &br, NULL);
    printf("  INIT_HARDWARE: %s\n\n", ok ? "OK" : "FAILED");

    /* Step 2: Read current CONTEXT0_CNTL */
    printf("--- Step 2: Read GCVM CONTEXT0_CNTL ---\n");
    unsigned val;
    ReadReg(h, GCVM_CONTEXT0_CNTL, &val);
    printf("  CONTEXT0_CNTL = 0x%08X\n", val);

    /* Step 3: Set bit1 (ENABLE_DEFAULT_PAGE) */
    printf("\n--- Step 3: Enable DEFAULT_PAGE (set bit1) ---\n");
    unsigned newVal = val | 0x02;
    WriteReg(h, GCVM_CONTEXT0_CNTL, newVal);
    unsigned readBack;
    ReadReg(h, GCVM_CONTEXT0_CNTL, &readBack);
    printf("  Wrote 0x%08X, read back 0x%08X\n", newVal, readBack);
    if (readBack != newVal) {
        printf("  WARNING: CONTEXT0_CNTL may be read-only!\n");
    }

    /* Step 4: Read SCRATCH before PM4 */
    printf("\n--- Step 4: SCRATCH before PM4 ---\n");
    unsigned scratch;
    ReadReg(h, SCRATCH_REG, &scratch);
    printf("  SCRATCH = 0x%08X\n", scratch);

    /* Step 5: GPU_KIQ_TEST (allocates ring, programs HQD, submits PM4) */
    printf("\n--- Step 5: GPU_KIQ_TEST ---\n");
    GPU_KIQ_TEST_OUT kiqOut = {0};
    UCHAR empty[4] = {0};
    ok = DeviceIoControl(h, IOCTL_AMDBC250_GPU_KIQ_TEST,
        empty, sizeof(empty), &kiqOut, sizeof(kiqOut), &br, NULL);
    if (ok) {
        printf("  Result=%u Mmio=%u Ring=%u HQD=%u PM4=%u\n",
            kiqOut.Result, kiqOut.MmioMapped, kiqOut.RingAllocated,
            kiqOut.HqdProgrammed, kiqOut.Pm4Submitted);
        printf("  SCRATCH before: 0x%08X\n", kiqOut.ScratchBefore);
        printf("  SCRATCH after:  0x%08X\n", kiqOut.ScratchAfter);
        if (kiqOut.ScratchAfter == 0xCAFEBABE)
            printf("\n*** PM4 EXECUTED! SCRATCH = 0xCAFEBABE ***\n");
        else if (kiqOut.ScratchAfter != kiqOut.ScratchBefore)
            printf("\n*** SCRATCH CHANGED! 0x%08X -> 0x%08X ***\n",
                kiqOut.ScratchBefore, kiqOut.ScratchAfter);
        else
            printf("\nSCRATCH unchanged - PM4 did not execute\n");
    } else {
        printf("  GPU_KIQ_TEST FAILED (err=%lu)\n", GetLastError());
    }

    /* Step 6: Verify CONTEXT0_CNTL still has DEFAULT_PAGE */
    printf("\n--- Step 6: Verify CONTEXT0_CNTL ---\n");
    ReadReg(h, GCVM_CONTEXT0_CNTL, &val);
    printf("  CONTEXT0_CNTL = 0x%08X\n", val);

    CloseHandle(h);
    return 0;
}
