#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>

#define IOCTL_AMDBC250_READ_REG   0x900
#define IOCTL_AMDBC250_WRITE_REG  0x901
#define IOCTL_AMDBC250_GPU_KIQ_TEST 0x80000BD0

static HANDLE g_hGpu = INVALID_HANDLE_VALUE;

static BOOL GpuReadReg(ULONG offset, ULONG *value) {
    ULONG out = 0; DWORD br = 0;
    BOOL ok = DeviceIoControl(g_hGpu, IOCTL_AMDBC250_READ_REG,
        &offset, sizeof(offset), &out, sizeof(out), &br, NULL);
    if (ok && value) *value = out;
    return ok;
}

static BOOL GpuWriteReg(ULONG offset, ULONG value) {
    ULONG params[2] = { offset, value }; DWORD br = 0;
    return DeviceIoControl(g_hGpu, IOCTL_AMDBC250_WRITE_REG,
        params, sizeof(params), NULL, 0, &br, NULL);
}

static void DumpVmAparture(void) {
    ULONG val;
    printf("=== GPU VM / Memory Controller Registers ===\n\n");

    struct { const char *name; ULONG off; } mcvm[] = {
        {"MC_VM_FB_LOCATION_BASE",             0x0520},
        {"MC_VM_FB_LOCATION_TOP",              0x0524},
        {"MC_VM_AGP_BASE",                     0x0528},
        {"MC_VM_AGP_TOP",                      0x052C},
        {"MC_VM_AGP_BOT",                      0x0530},
        {"MC_VM_AGP_CNTL",                     0x0534},
        {"MC_VM_SYSTEM_APERTURE_LOW_ADDR",     0x0540},
        {"MC_VM_SYSTEM_APERTURE_HIGH_ADDR",    0x0544},
        {"MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR", 0x0548},
    };
    printf("MC_VM Registers:\n");
    for (int i = 0; i < (int)(sizeof(mcvm)/sizeof(mcvm[0])); i++) {
        if (GpuReadReg(mcvm[i].off, &val))
            printf("  %-42s = 0x%08X\n", mcvm[i].name, val);
        else
            printf("  %-42s = READ FAILED\n", mcvm[i].name);
    }

    /* GCVM registers: test BOTH raw 0x1A00 AND GC_BASE-shifted 0x2C60 */
    printf("\nGCVM Registers (raw offset 0x1A00 vs GC_BASE-shifted 0x2C60):\n");
    {
        struct { const char *name; ULONG raw; ULONG shifted; } gcvm[] = {
            {"VM_CONTEXT0_CNTL",                         0x1A00, 0x2C60},
            {"VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32",    0x1A04, 0x2C64},
            {"VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32",    0x1A08, 0x2C68},
            {"VM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32",   0x1A0C, 0x2C6C},
            {"VM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32",   0x1A10, 0x2C70},
            {"VM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32",     0x1A14, 0x2C74},
            {"VM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32",     0x1A18, 0x2C78},
            {"VM_CONTEXT0_PAGE_TABLE_DEPTH",              0x1A20, 0x2C80},
            {"VM_CONTEXT1_CNTL",                         0x1A40, 0x2CA0},
            {"VM_CONTEXT1_PAGE_TABLE_BASE_ADDR_LO32",    0x1A44, 0x2CA4},
        };
        for (int i = 0; i < (int)(sizeof(gcvm)/sizeof(gcvm[0])); i++) {
            ULONG rawVal = 0, shiftedVal = 0;
            BOOL rawOk = GpuReadReg(gcvm[i].raw, &rawVal);
            BOOL shiftedOk = GpuReadReg(gcvm[i].shifted, &shiftedVal);
            printf("  %-48s raw=0x%08X  shifted=0x%08X\n",
                gcvm[i].name,
                rawOk ? rawVal : 0xDEAD,
                shiftedOk ? shiftedVal : 0xDEAD);
        }
    }

    /* Also try amdbc250_hw_extra.h offsets (0x9B00 range) */
    printf("\nGCVM Registers (hw_extra.h offsets 0x9B00 range):\n");
    {
        struct { const char *name; ULONG off; } gcvm2[] = {
            {"VM_CTX0_CNTL (0x9A00)",             0x9A00},
            {"VM_CTX0_CNTL (0x9A04)",             0x9A04},
            {"VM_CTX0_CNTL (0x9B00)",             0x9B00},
            {"VM_CTX0_PGT_BASE_LO32 (0x9B00)", 0x9B00},
            {"VM_CTX0_PGT_BASE_HI32 (0x9B04)", 0x9B04},
            {"VM_CTX0_PGT_START_LO32 (0x9B08)", 0x9B08},
            {"VM_CTX0_PGT_START_HI32 (0x9B0C)", 0x9B0C},
            {"VM_CTX0_PGT_END_LO32 (0x9B10)",   0x9B10},
            {"VM_CTX0_PGT_END_HI32 (0x9B14)",   0x9B14},
        };
        for (int i = 0; i < (int)(sizeof(gcvm2)/sizeof(gcvm2[0])); i++) {
            if (GpuReadReg(gcvm2[i].off, &val))
                printf("  %-40s = 0x%08X\n", gcvm2[i].name, val);
        }
    }

    /* Try writing to 0x9B00 to see if it's writable */
    printf("\nWrite test to 0x9B00:\n");
    GpuReadReg(0x9B00, &val);
    printf("  Before: 0x%08X\n", val);
    GpuWriteReg(0x9B00, 0x1);
    Sleep(10);
    GpuReadReg(0x9B00, &val);
    printf("  After write 0x1: 0x%08X\n", val);
    GpuWriteReg(0x9B00, 0x0);
    Sleep(10);

    printf("\nGPU Status:\n");
    if (GpuReadReg(0x32D4, &val)) printf("  SCRATCH (0x32D4)            = 0x%08X\n", val);
    if (GpuReadReg(0x3260, &val)) printf("  GRBM_STATUS (0x3260)       = 0x%08X\n", val);
    if (GpuReadReg(0x4A74, &val)) printf("  CP_ME_CNTL (0x4A74)        = 0x%08X\n", val);
    if (GpuReadReg(0x34D0, &val)) printf("  GRBM_GFX_INDEX (0x34D0)    = 0x%08X\n", val);

    printf("\nNBIO Registers:\n");
    if (GpuReadReg(0x1000, &val)) printf("  NBIO_ID (0x1000)           = 0x%08X\n", val);

    printf("\nKIQ/HQD Registers:\n");
    if (GpuReadReg(0xE060, &val)) printf("  KIQ_BASE_LO (0xE060)       = 0x%08X\n", val);
    if (GpuReadReg(0xE064, &val)) printf("  KIQ_BASE_HI (0xE064)       = 0x%08X\n", val);
    if (GpuReadReg(0xE078, &val)) printf("  KIQ_WPTR (0xE078)          = 0x%08X\n", val);
    if (GpuReadReg(0xDAC0, &val)) printf("  HQD_ACTIVE (0xDAC0)        = 0x%08X\n", val);
    if (GpuReadReg(0xDAD8, &val)) printf("  HQD_PQ_BASE (0xDAD8)       = 0x%08X\n", val);
    if (GpuReadReg(0xDADC, &val)) printf("  HQD_PQ_BASE_HI (0xDADC)    = 0x%08X\n", val);
}

static void TestKiqWithVmDisabled(void) {
    ULONG val;
    printf("\n=== Test: Try writing to GC_BASE-shifted VM_CONTEXT0_CNTL ===\n");

    /* Read from raw offset 0x1A00 */
    GpuReadReg(0x1A00, &val);
    printf("VM_CONTEXT0_CNTL (raw 0x1A00) before: 0x%08X\n", val);

    /* Read from GC_BASE-shifted 0x2C60 */
    GpuReadReg(0x2C60, &val);
    printf("VM_CONTEXT0_CNTL (shifted 0x2C60) before: 0x%08X\n", val);

    /* Try writing 0x1 to the shifted offset */
    printf("Writing 0x1 to VM_CONTEXT0_CNTL at 0x2C60...\n");
    GpuWriteReg(0x2C60, 0x1);
    Sleep(10);
    GpuReadReg(0x2C60, &val);
    printf("VM_CONTEXT0_CNTL (shifted 0x2C60) after: 0x%08X\n", val);

    /* Also try disabling it */
    printf("Writing 0x0 to VM_CONTEXT0_CNTL at 0x2C60...\n");
    GpuWriteReg(0x2C60, 0x0);
    Sleep(10);
    GpuReadReg(0x2C60, &val);
    printf("VM_CONTEXT0_CNTL (shifted 0x2C60) after disable: 0x%08X\n", val);

    /* Re-enable */
    GpuWriteReg(0x2C60, 0x1);
    Sleep(10);

    printf("\nRunning GPU_KIQ_TEST...\n");
    ULONG kiqOut[8] = {0};
    DWORD br = 0;
    BOOL ok = DeviceIoControl(g_hGpu, IOCTL_AMDBC250_GPU_KIQ_TEST,
        NULL, 0, kiqOut, sizeof(kiqOut), &br, NULL);
    if (ok) {
        printf("  MmioMapped=%u RingAllocated=%u HqdProgrammed=%u Pm4Submitted=%u\n",
            kiqOut[0], kiqOut[1], kiqOut[2], kiqOut[3]);
        printf("  ScratchBefore=0x%08X ScratchAfter=0x%08X Result(WPTR)=0x%08X\n",
            kiqOut[4], kiqOut[5], kiqOut[6]);
        if (kiqOut[4] != kiqOut[5])
            printf("  *** SCRATCH CHANGED! ***\n");
        else
            printf("  SCRATCH unchanged\n");
    } else {
        printf("  GPU_KIQ_TEST IOCTL failed (err=%lu)\n", GetLastError());
    }
}

int main(int argc, char *argv[]) {
    printf("=== GPU VM Diagnostic Tool ===\n\n");

    g_hGpu = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_hGpu == INVALID_HANDLE_VALUE) {
        printf("Cannot open GPU driver (err=%lu)\n", GetLastError());
        return 1;
    }
    printf("GPU driver opened\n\n");

    DumpVmAparture();

    if (argc > 1 && strcmp(argv[1], "--test-vm") == 0) {
        TestKiqWithVmDisabled();
    } else {
        printf("\nUsage: %s --test-vm\n", argv[0]);
        printf("  --test-vm: Disable VM context 0, run GPU_KIQ_TEST, re-enable\n");
    }

    CloseHandle(g_hGpu);
    printf("\nDone.\n");
    return 0;
}
