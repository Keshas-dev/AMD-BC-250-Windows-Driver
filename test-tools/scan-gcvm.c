/* scan-gcvm.c — Scan for alive GCVM registers at corrected offsets */
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

static FILE *g = NULL;
static void Log(const char *fmt, ...) {
    va_list a; va_start(a, fmt); vfprintf(stdout, fmt, a); va_end(a);
    if (g) { va_start(a, fmt); vfprintf(g, fmt, a); va_end(a); }
}

static BOOL ReadReg(HANDLE h, unsigned offset, unsigned *val) {
    unsigned ra[2] = {offset, 0xDEADBEEF};
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, 0x80000B88, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
    if (ok) *val = ra[1];
    return ok;
}

static BOOL WriteReg(HANDLE h, unsigned offset, unsigned val) {
    unsigned ra[2] = {offset, val};
    DWORD br = 0;
    return DeviceIoControl(h, 0x80000B8C, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
}

int main(void) {
    g = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\scan-gcvm.log", "w");

    Log("=== GCVM Register Scan (corrected offsets) ===\n\n");

    HANDLE h = CreateFileW(L"\\\\.\\AMDBC250DreamV43",
        GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        Log("Cannot open device error=%lu\n", GetLastError());
        if (g) fclose(g);
        return 1;
    }

    /* INIT_HARDWARE to map BAR5 */
    UCHAR initIn[32] = {0};
    DWORD br = 0;
    *(unsigned __int64*)(initIn + 0) = 0xFE800000ULL;
    *(unsigned*)(initIn + 8) = 0x00080000;
    *(unsigned*)(initIn + 12) = 1;
    *(unsigned __int64*)(initIn + 16) = 0xC0000000ULL;
    *(unsigned*)(initIn + 24) = 0x10000000;
    DeviceIoControl(h, 0x80000B80, initIn, sizeof(initIn), NULL, 0, &br, NULL);

    /* Verify baseline: GRBM_STATUS at 0x3260 */
    unsigned val;
    ReadReg(h, 0x3260, &val);
    Log("Baseline: GRBM_STATUS[0x3260] = 0x%08X\n\n", val);

    /* --- GCVM registers (GC_BASE=0x1260 + Linux GCVM offset) --- */
    Log("--- GCVM registers (corrected BC-250 offsets) ---\n");
    struct { unsigned off; const char *name; } gcvm[] = {
        {0x2880, "GCVM_CONTEXT0_CNTL"},
        {0x2881, "GCVM_CONTEXT1_CNTL"},
        {0x28EB, "GCVM_CONTEXT0_PTB_ADDR_LO32"},
        {0x28EC, "GCVM_CONTEXT0_PTB_ADDR_HI32"},
        {0x290B, "GCVM_CONTEXT0_PT_START_LO32"},
        {0x290C, "GCVM_CONTEXT0_PT_START_HI32"},
        {0x2891, "GCVM_INVALIDATE_ENG0_SEM"},
        {0x28A3, "GCVM_INVALIDATE_ENG0_REQ"},
        {0x28B5, "GCVM_INVALIDATE_ENG0_ACK"},
        {0x28C7, "GCVM_INV_ENG0_ADDR_RANGE_LO"},
        {0x28C8, "GCVM_INV_ENG0_ADDR_RANGE_HI"},
        {0x2840, "GCVM_L2_CNTL"},
        {0x2841, "GCVM_L2_CNTL2"},
        {0x2842, "GCVM_L2_CNTL3"},
        {0x2987, "GCMC_VM_MX_L1_TLB_CNTL"},
        {0x2980, "GCMC_VM_FB_LOCATION_BASE"},
        {0x2981, "GCMC_VM_FB_LOCATION_TOP"},
        {0x2984, "GCMC_VM_AGP_BASE"},
        {0x2982, "GCMC_VM_AGP_TOP"},
        {0x2983, "GCMC_VM_AGP_BOT"},
        {0x2985, "GCMC_VM_SYSAPO_LOW"},
        {0x2986, "GCMC_VM_SYSAPO_HIGH"},
        {0x296C, "GCMC_VM_SYSAPO_DEF_LSB"},
        {0x296D, "GCMC_VM_SYSAPO_DEF_MSB"},
        {0x296B, "GCMC_VM_FB_OFFSET"},
    };
    for (int i = 0; i < sizeof(gcvm)/sizeof(gcvm[0]); i++) {
        if (ReadReg(h, gcvm[i].off, &val)) {
            Log("  %s [0x%04X] = 0x%08X\n", gcvm[i].name, gcvm[i].off, val);
        } else {
            Log("  %s [0x%04X] = READ FAILED\n", gcvm[i].name, gcvm[i].off);
        }
    }

    /* --- Old wrong offsets for comparison --- */
    Log("\n--- Old (WRONG) offsets for comparison ---\n");
    struct { unsigned off; const char *name; } old[] = {
        {0x1A00, "OLD VM_CONTEXT0_CNTL"},
        {0x1A04, "OLD VM_PT_BASE_LO32"},
        {0x1A08, "OLD VM_PT_BASE_HI32"},
        {0x0520, "OLD MC_VM_FB_LOC_BASE"},
        {0x0528, "OLD MC_VM_AGP_BASE"},
    };
    for (int i = 0; i < sizeof(old)/sizeof(old[0]); i++) {
        if (ReadReg(h, old[i].off, &val)) {
            Log("  %s [0x%04X] = 0x%08X\n", old[i].name, old[i].off, val);
        } else {
            Log("  %s [0x%04X] = READ FAILED\n", old[i].name, old[i].off);
        }
    }

    /* --- Quick scan around 0x2880 to see if GCVM block is mapped --- */
    Log("\n--- GCVM block scan (0x2800-0x28FF) ---\n");
    int alive = 0;
    for (unsigned i = 0x2800; i < 0x2900; i += 4) {
        if (ReadReg(h, i, &val) && val != 0xFFFFFFFF) {
            Log("  [0x%04X] = 0x%08X\n", i, val);
            alive++;
        }
    }
    Log("  GCVM block alive: %d registers\n", alive);

    /* --- Quick scan around 0x2960-0x2990 (GCMC) --- */
    Log("\n--- GCMC block scan (0x2960-0x299F) ---\n");
    alive = 0;
    for (unsigned i = 0x2960; i < 0x29A0; i += 4) {
        if (ReadReg(h, i, &val) && val != 0xFFFFFFFF) {
            Log("  [0x%04X] = 0x%08X\n", i, val);
            alive++;
        }
    }
    Log("  GCMC block alive: %d registers\n", alive);

    /* --- Test: write/readback GCVM_CONTEXT0_CNTL --- */
    Log("\n--- Write/readback test: GCVM_CONTEXT0_CNTL [0x2880] ---\n");
    ReadReg(h, 0x2880, &val);
    Log("  Before: 0x%08X\n", val);
    /* Try enable bit 0 */
    WriteReg(h, 0x2880, 0x1);
    ReadReg(h, 0x2880, &val);
    Log("  After write 0x1: 0x%08X\n", val);
    /* Try enable bit 0 | bit 1 */
    WriteReg(h, 0x2880, 0x3);
    ReadReg(h, 0x2880, &val);
    Log("  After write 0x3: 0x%08X\n", val);
    /* Restore */
    WriteReg(h, 0x2880, 0x0);
    ReadReg(h, 0x2880, &val);
    Log("  After write 0x0: 0x%08X\n", val);

    CloseHandle(h);
    Log("\n=== Scan Complete ===\n");
    if (g) fclose(g);
    printf("Done. Check output\\scan-gcvm.log\n");
    return 0;
}
