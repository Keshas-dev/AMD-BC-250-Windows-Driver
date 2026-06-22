/* gcvm-offset-verify.c — Verify GCVM register offsets: old vs Linux gc_10_1_0 vs Gemini */
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

static void CheckReg(HANDLE h, const char *name, unsigned offset, unsigned expect_alive) {
    unsigned val = 0xDEADBEEF;
    ReadReg(h, offset, &val);
    BOOL alive = (val != 0xFFFFFFFF && val != 0xDEADBEEF);
    const char *status = alive ? "ALIVE" : (val == 0xFFFFFFFF ? "DEAD(0xFFFFFFFF)" : "FAIL");
    Log("  %-30s [0x%05X] = 0x%08X  %s\n", name, offset, val, status);
}

int main(void) {
    g = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\gcvm-offset-verify.log", "w");
    Log("=== GCVM Offset Verification Test ===\n");
    Log("Comparing: OLD offsets (current driver) vs NEW offsets (Linux gc_10_1_0) vs Gemini\n\n");

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
    ReadReg(h, 0x3260, &val);
    Log("GRBM_STATUS = 0x%08X (sanity check)\n\n", val);

    /* ============================================================ */
    /* PART 1: Known-working registers to validate BAR5 mapping     */
    /* ============================================================ */
    Log("--- PART 1: Known-working registers (BAR5 sanity) ---\n");
    CheckReg(h, "GRBM_STATUS",        0x3260, 1);
    CheckReg(h, "SCRATCH_REG0",       0x32D4, 1);
    CheckReg(h, "GRBM_GFX_INDEX",     0x34D0, 1);
    CheckReg(h, "KIQ_BASE_LO",        0xE060, 1);
    CheckReg(h, "KIQ_WPTR",           0xE078, 1);
    CheckReg(h, "CP_ME_CNTL",         0x4A74, 1);
    Log("\n");

    /* ============================================================ */
    /* PART 2: GCVM registers — OLD offsets (current driver)        */
    /* Formula: OLD = byte_offset directly in header                */
    /* ============================================================ */
    Log("--- PART 2: GCVM registers at OLD offsets (current driver) ---\n");
    /* GCVM_L2_CNTL = 0x0B360 */
    CheckReg(h, "GCVM_L2_CNTL (OLD)",           0x0B360, 1);
    /* GCVM_L2_CNTL2 = 0x0B364 */
    CheckReg(h, "GCVM_L2_CNTL2 (OLD)",          0x0B364, 1);
    /* GCVM_CONTEXT0_CNTL = 0x0B460 */
    CheckReg(h, "GCVM_CONTEXT0_CNTL (OLD)",     0x0B460, 1);
    /* GCVM_CONTEXT0_PT_BASE_LO = 0x0B608 */
    CheckReg(h, "GCVM_CTX0_PT_BASE_LO (OLD)",   0x0B608, 1);
    /* GCVM_CONTEXT0_PT_BASE_HI = 0x0B60C */
    CheckReg(h, "GCVM_CTX0_PT_BASE_HI (OLD)",   0x0B60C, 1);
    Log("\n");

    /* ============================================================ */
    /* PART 3: GCVM registers — Linux gc_10_1_0 offsets             */
    /* Formula: BAR5 = GC_BASE(0x1260) + DWORD_offset * 4          */
    /* gc_10_1_0_offset.h defines:                                  */
    /*   mmGCVM_L2_CNTL = 0x15E0  → 0x1260 + 0x15E0*4 = 0x69E0   */
    /*   mmGCVM_CONTEXT0_CNTL = 0x1620 → 0x1260 + 0x1620*4 = 0x6AE0 */
    /*   mmGCVM_CTX0_PT_BASE_LO32 = 0x168B → 0x1260+0x168B*4 = 0x6C8C */
    /*   mmGCVM_CTX0_PT_BASE_HI32 = 0x168C → 0x1260+0x168C*4 = 0x6C90 */
    /*   mmGCVM_L2_CNTL2 = 0x15E1 → 0x1260 + 0x15E1*4 = 0x69E4  */
    /*   mmGCVM_L2_CNTL3 = 0x15E2 → 0x1260 + 0x15E2*4 = 0x69E8  */
    /*   mmGRBM_GFX_INDEX = 0x2200 → 0x1260 + 0x2200*4 = 0x9A60  */
    /*   mmCP_ME_CNTL = 0x0F56 → 0x1260 + 0x0F56*4 = 0x5018      */
    /*   mmCP_RB_WPTR = 0x1DF4 → 0x1260 + 0x1DF4*4 = 0x8838      */
    /*   mmCP_RB_RPTR = 0x0F60 → 0x1260 + 0x0F60*4 = 0x50E0      */
    /*   mmCP_RB_BASE = 0x1DE0 → 0x1260 + 0x1DE0*4 = 0x87E0      */
    /*   mmSH_MEM_BASES = 0x10AA → 0x1260 + 0x10AA*4 = 0x5528    */
    /*   mmSH_MEM_CONFIG = 0x10AD → 0x1260 + 0x10AD*4 = 0x5534   */
    /*   mmGCVM_INVALIDATE_ENG0_REQ = 0x1643 → 0x1260+0x1643*4=0x6C0C */
    /*   mmGCVM_L2_PROTECTION_FAULT_STATUS = 0x15EC → 0x1260+0x15EC*4=0x6BC0 */
    /* ============================================================ */
    Log("--- PART 3: GCVM registers at Linux gc_10_1_0 offsets ---\n");
    CheckReg(h, "GRBM_GFX_INDEX (Linux)",    0x9A60, 1);
    CheckReg(h, "CP_ME_CNTL (Linux)",        0x5018, 1);
    CheckReg(h, "CP_RB_BASE (Linux)",        0x87E0, 1);
    CheckReg(h, "CP_RB_WPTR (Linux)",        0x8838, 1);
    CheckReg(h, "CP_RB_RPTR (Linux)",        0x50E0, 1);
    CheckReg(h, "SH_MEM_BASES (Linux)",      0x5528, 1);
    CheckReg(h, "SH_MEM_CONFIG (Linux)",     0x5534, 1);
    CheckReg(h, "GCVM_L2_CNTL (Linux)",      0x69E0, 1);
    CheckReg(h, "GCVM_L2_CNTL2 (Linux)",     0x69E4, 1);
    CheckReg(h, "GCVM_L2_CNTL3 (Linux)",     0x69E8, 1);
    CheckReg(h, "GCVM_CONTEXT0_CNTL (Linux)", 0x6AE0, 1);
    CheckReg(h, "GCVM_CTX0_PT_BASE_LO (Linux)", 0x6C8C, 1);
    CheckReg(h, "GCVM_CTX0_PT_BASE_HI (Linux)", 0x6C90, 1);
    CheckReg(h, "GCVM_INV_ENG0_REQ (Linux)", 0x6C0C, 1);
    CheckReg(h, "GCVM_L2_PROT_FAULT (Linux)", 0x6BC0, 1);
    Log("\n");

    /* ============================================================ */
    /* PART 4: Wide BAR5 scan in GCVM region (old 0x0B000-0x0BFFF)  */
    /* ============================================================ */
    Log("--- PART 4: BAR5 scan 0x0B000-0x0BFFF (4KB, old GCVM region) ---\n");
    for (unsigned off = 0x0B000; off <= 0x0BFFC; off += 4) {
        ReadReg(h, off, &val);
        if (val != 0 && val != 0xFFFFFFFF && val != 0xDEADBEEF) {
            Log("  [0x%05X] = 0x%08X\n", off, val);
        }
    }
    Log("\n");

    /* ============================================================ */
    /* PART 5: Wide BAR5 scan in Linux GCVM region (0x6800-0x6FFF) */
    /* ============================================================ */
    Log("--- PART 5: BAR5 scan 0x6800-0x6FFF (2KB, Linux GCVM region) ---\n");
    for (unsigned off = 0x6800; off <= 0x6FFC; off += 4) {
        ReadReg(h, off, &val);
        if (val != 0 && val != 0xFFFFFFFF && val != 0xDEADBEEF) {
            Log("  [0x%05X] = 0x%08X\n", off, val);
        }
    }
    Log("\n");

    /* ============================================================ */
    /* PART 6: Write test to GCVM_CONTEXT0_CNTL at both offsets     */
    /* ============================================================ */
    Log("--- PART 6: Write test to GCVM_CONTEXT0_CNTL ---\n");
    /* Read old offset */
    ReadReg(h, 0x0B460, &val);
    Log("  OLD 0x0B460 before: 0x%08X\n", val);
    /* Write test pattern */
    unsigned ra[2] = {0x0B460, 0x12345678};
    DeviceIoControl(h, 0x80000B8C, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
    ReadReg(h, 0x0B460, &val);
    Log("  OLD 0x0B460 after:  0x%08X (expect 0x12345678 if writable)\n", val);
    /* Restore */
    ra[1] = 0x010CA88D;
    DeviceIoControl(h, 0x80000B8C, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);

    /* Read new offset */
    ReadReg(h, 0x6AE0, &val);
    Log("  NEW 0x6AE0 before: 0x%08X\n", val);
    ra[0] = 0x6AE0; ra[1] = 0x12345678;
    DeviceIoControl(h, 0x80000B8C, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
    ReadReg(h, 0x6AE0, &val);
    Log("  NEW 0x6AE0 after:  0x%08X (expect 0x12345678 if writable)\n", val);
    /* Restore */
    ra[1] = 0x010CA88D;
    DeviceIoControl(h, 0x80000B8C, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
    Log("\n");

    /* ============================================================ */
    /* PART 7: Write test to PT_BASE at both offsets                */
    /* ============================================================ */
    Log("--- PART 7: Write test to GCVM_CTX0_PT_BASE ---\n");
    ReadReg(h, 0x0B608, &val);
    Log("  OLD 0x0B608 before: 0x%08X\n", val);
    ra[0] = 0x0B608; ra[1] = 0xDEADBEEF;
    DeviceIoControl(h, 0x80000B8C, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
    ReadReg(h, 0x0B608, &val);
    Log("  OLD 0x0B608 after:  0x%08X (expect DEADBEEF if writable)\n", val);

    ReadReg(h, 0x6C8C, &val);
    Log("  NEW 0x6C8C before: 0x%08X\n", val);
    ra[0] = 0x6C8C; ra[1] = 0xDEADBEEF;
    DeviceIoControl(h, 0x80000B8C, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
    ReadReg(h, 0x6C8C, &val);
    Log("  NEW 0x6C8C after:  0x%08X (expect DEADBEEF if writable)\n", val);
    Log("\n");

    CloseHandle(h);
    Log("=== Done ===\n");
    if (g) fclose(g);
    printf("Done. Check output\\gcvm-offset-verify.log\n");
    return 0;
}
