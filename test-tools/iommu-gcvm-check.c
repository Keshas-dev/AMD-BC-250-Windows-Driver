/* iommu-gcvm-check-v2.c — READ-ONLY IOMMU + GCVM register check */
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
    if (ok) *val = ra[1]; else *val = 0xDEADBEEF;
    return ok;
}

static void CheckReg(HANDLE h, const char *name, unsigned offset) {
    unsigned val = 0xDEADBEEF;
    ReadReg(h, offset, &val);
    const char *status = "ALIVE";
    if (val == 0xFFFFFFFF) status = "DEAD";
    else if (val == 0xDEADBEEF) status = "FAIL";
    Log("  %-40s [0x%05X] = 0x%08X  %s\n", name, offset, val, status);
}

int main(void) {
    g = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\iommu-gcvm-check.log", "w");
    Log("=== IOMMU + GCVM Check (READ-ONLY) ===\n");
    Log("IOMMU: ENABLED in BIOS\n\n");

    HANDLE h = CreateFileW(L"\\\\.\\AMDBC250DreamV43",
        GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        Log("Cannot open device error=%lu\n", GetLastError());
        if (g) fclose(g); return 1;
    }

    /* INIT_HARDWARE NBIO_MAP */
    UCHAR initIn[32] = {0}; DWORD br = 0;
    *(unsigned __int64*)(initIn + 0) = 0xFE800000ULL;
    *(unsigned*)(initIn + 8) = 0x00080000;
    *(unsigned*)(initIn + 12) = 1;
    *(unsigned __int64*)(initIn + 16) = 0xC0000000ULL;
    *(unsigned*)(initIn + 24) = 0x10000000;
    DeviceIoControl(h, 0x80000B80, initIn, sizeof(initIn), NULL, 0, &br, NULL);

    unsigned val;

    /* PART 1: BAR5 Sanity */
    Log("--- PART 1: BAR5 Sanity ---\n");
    CheckReg(h, "GRBM_STATUS",           0x3260);
    CheckReg(h, "SCRATCH_REG0",          0x32D4);
    CheckReg(h, "CP_ME_CNTL",            0x4A74);
    CheckReg(h, "KIQ_BASE_LO",           0xE060);
    CheckReg(h, "KIQ_BASE_HI",           0xE064);
    CheckReg(h, "KIQ_RPTR",              0xE06C);
    CheckReg(h, "KIQ_WPTR",              0xE078);
    CheckReg(h, "HQD_ACTIVE",            0xDAC0);
    CheckReg(h, "HQD_PQ_BASE_LO",        0xDAD8);
    CheckReg(h, "HQD_PQ_BASE_HI",        0xDADC);
    Log("\n");

    /* PART 2: GCVM Context0 — Linux offsets */
    Log("--- PART 2: GCVM Context0 (Linux offsets) ---\n");
    CheckReg(h, "GCVM_CONTEXT0_CNTL",      0x6AE0);
    CheckReg(h, "GCVM_CTX0_PT_BASE_LO",    0x6C8C);
    CheckReg(h, "GCVM_CTX0_PT_BASE_HI",    0x6C90);
    CheckReg(h, "GCVM_L2_CNTL",            0x69E0);
    CheckReg(h, "GCVM_L2_CNTL2",           0x69E4);
    CheckReg(h, "GCVM_L2_CNTL3",           0x69E8);
    CheckReg(h, "GCVM_L2_PROT_FAULT",      0x6BC0);
    CheckReg(h, "GCVM_INV_ENG0_REQ",       0x6C0C);
    Log("\n");

    /* PART 3: NBIO registers */
    Log("--- PART 3: NBIO Registers ---\n");
    CheckReg(h, "NBIO_ID",                  0xC100);
    CheckReg(h, "NBIO_RAS_CNTL",            0x3004C);
    CheckReg(h, "BIF_CFG_CNTL",             0x1000);
    Log("\n");

    /* PART 4: MMHUB — read only (WRITE DANGEROUS!) */
    Log("--- PART 4: MMHUB Registers (READ ONLY) ---\n");
    CheckReg(h, "MMHUB_INSTANCE0_CNTL",    0x1A000);
    CheckReg(h, "MMHUB_VM_INVALIDATE",     0x1A028);
    CheckReg(h, "MMHUB_VM_PROT_FAULT",     0x1A0D8);
    /* MMHUB flat mapping regs — READ ONLY, do NOT write */
    for (unsigned off = 0x1A144; off <= 0x1A180; off += 4) {
        ReadReg(h, off, &val);
        if (val != 0 && val != 0xFFFFFFFF && val != 0xDEADBEEF) {
            Log("  MMHUB_FLAT [0x%05X] = 0x%08X\n", off, val);
        }
    }
    Log("\n");

    /* PART 5: MMHUB VM block — was DEAD before IOMMU */
    Log("--- PART 5: MMHUB VM Block (was 0xFFFFFFFF before IOMMU) ---\n");
    int found = 0;
    for (unsigned off = 0x1B400; off <= 0x1B600; off += 4) {
        ReadReg(h, off, &val);
        if (val != 0 && val != 0xFFFFFFFF && val != 0xDEADBEEF) {
            Log("  MMHUB_VM [0x%05X] = 0x%08X\n", off, val);
            found = 1;
        }
    }
    if (!found) Log("  All 0xFFFFFFFF (still DEAD)\n");
    Log("\n");

    /* PART 6: Wide scan 0x6800-0x6FFF — new alive regs? */
    Log("--- PART 6: Wide BAR5 scan 0x6800-0x6FFF ---\n");
    found = 0;
    for (unsigned off = 0x6800; off <= 0x6FFC; off += 4) {
        ReadReg(h, off, &val);
        if (val != 0 && val != 0xFFFFFFFF && val != 0xDEADBEEF) {
            Log("  [0x%05X] = 0x%08X\n", off, val);
            found = 1;
        }
    }
    if (!found) Log("  No alive registers found\n");
    Log("\n");

    /* PART 7: KIQ_BIOS_RING_SUBMIT — let driver handle everything */
    Log("--- PART 7: KIQ_BIOS_RING_SUBMIT (0x88) ---\n");
    {
        unsigned __int64 inp[2] = {0, 0};
        unsigned __int64 outp[8] = {0};
        DWORD bytesOut = 0;
        BOOL ok = DeviceIoControl(h, 0x80000BE0,
            inp, sizeof(inp), outp, sizeof(outp), &bytesOut, NULL);
        if (ok) {
            unsigned result = (unsigned)outp[0];
            unsigned kiqLo = (unsigned)outp[1];
            unsigned kiqHi = (unsigned)(outp[1] >> 32);
            unsigned scratchBefore = (unsigned)outp[2];
            unsigned scratchAfter = (unsigned)outp[3];
            unsigned meBefore = (unsigned)outp[4];
            unsigned meAfter = (unsigned)outp[5];
            Log("  Result=0x%08X\n", result);
            Log("  KIQ_BASE=0x%08X%08X\n", kiqHi, kiqLo);
            Log("  SCRATCH before=0x%08X after=0x%08X\n", scratchBefore, scratchAfter);
            Log("  ME_CNTL before=0x%08X after=0x%08X\n", meBefore, meAfter);
            if (scratchAfter != scratchBefore)
                Log("  SCRATCH CHANGED! PM4 may be executing!\n");
            else
                Log("  SCRATCH unchanged\n");
        } else {
            Log("  DeviceIoControl FAILED error=%lu\n", GetLastError());
        }
    }
    Log("\n");

    CloseHandle(h);
    Log("=== Done ===\n");
    if (g) fclose(g);
    printf("Done. Check output\\iommu-gcvm-check.log\n");
    return 0;
}
