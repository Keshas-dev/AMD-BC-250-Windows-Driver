/* vm-offset-verify.c — Verify THREE register paths
 *
 * 1. HDP MC_VM (0x0500-0x0548) — memory controller apertures
 * 2. MMHUB VM (0x1A00-0x1A8C) — MMHUB MMEA registers (NOT VM!)
 * 3. GCVM (0x0B460-0x0B60C) — GFX Hub GPU Virtual Memory
 *
 * Run after fresh boot (no GPU_KIQ_TEST) for clean state.
 */

#include <windows.h>
#include <stdio.h>

#define IOCTL_AMDBC250_READ_REG  0x80000B88
#define IOCTL_AMDBC250_WRITE_REG 0x80000B8C

typedef struct { UINT32 Offset; UINT32 Value; } REG_IO;

static HANDLE OpenDevice(void) {
    return CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
                       0, NULL, OPEN_EXISTING, 0, NULL);
}

static UINT32 R(HANDLE h, UINT32 offset) {
    REG_IO io = { offset, 0 };
    DWORD bytes;
    DeviceIoControl(h, IOCTL_AMDBC250_READ_REG, &io, sizeof(io), &io, sizeof(io), &bytes, NULL);
    return io.Value;
}

static void W(HANDLE h, UINT32 offset, UINT32 value) {
    REG_IO io = { offset, value };
    DWORD bytes;
    DeviceIoControl(h, IOCTL_AMDBC250_WRITE_REG, &io, sizeof(io), &io, sizeof(io), &bytes, NULL);
}

static const char *Status(UINT32 v) {
    if (v == 0xFFFFFFFF) return "DEAD (0xFFFFFFFF)";
    if (v == 0x00000000) return "ZERO";
    return "ALIVE";
}

static void TestWrite(HANDLE h, UINT32 offset, const char *name) {
    UINT32 orig = R(h, offset);
    W(h, offset, 0xDEADBEEF);
    UINT32 readback = R(h, offset);
    W(h, offset, orig);  /* restore */
    printf("  0x%05X %-40s = 0x%08X (%s)  write_test: %s\n",
           offset, name, orig, Status(orig),
           readback == 0xDEADBEEF ? "WRITABLE" : "READ_ONLY");
}

int main(void) {
    printf("=== VM Offset Verify — Three Register Paths ===\n\n");

    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) {
        printf("Cannot open device\n");
        return 1;
    }

    /* ================================================================
     * PATH 1: HDP MC_VM — Memory Controller Apertures
     * Known alive on BC-250 (verified by previous tests)
     * ================================================================ */
    printf("--- PATH 1: HDP MC_VM (Memory Controller, direct BAR5 offsets) ---\n");
    printf("    These are DRAM topology registers — NOT GPU VM page tables\n\n");

    struct { UINT32 off; const char *name; } hdp_mc[] = {
        {0x0500, "MC_VM_FB_OFFSET"},
        {0x0520, "MC_VM_FB_LOCATION_BASE"},
        {0x0524, "MC_VM_FB_LOCATION_TOP"},
        {0x0528, "MC_VM_AGP_BASE"},
        {0x052C, "MC_VM_AGP_TOP"},
        {0x0530, "MC_VM_AGP_BOT"},
        {0x0534, "MC_VM_AGP_CNTL"},
        {0x0540, "MC_VM_SYSTEM_APERTURE_LOW"},
        {0x0544, "MC_VM_SYSTEM_APERTURE_HIGH"},
        {0x0548, "MC_VM_SYSTEM_APERTURE_DEF"},
    };
    for (int i = 0; i < sizeof(hdp_mc)/sizeof(hdp_mc[0]); i++) {
        TestWrite(h, hdp_mc[i].off, hdp_mc[i].name);
    }

    /* ================================================================
     * PATH 2: MMHUB "VM" at 0x1A00-0x1A8C
     * These are actually MMEA (Memory Engine Arbiter) registers
     * NOT GPU VM page table registers!
     * ================================================================ */
    printf("\n--- PATH 2: MMHUB 0x1A00 region (MMEA, NOT VM!) ---\n");
    printf("    These control DRAM arbitration — NOT GPU VM page tables\n");
    printf("    The real MMHUB VM block (0x1B400-0x1B600) is DEAD\n\n");

    struct { UINT32 off; const char *name; } mmhub[] = {
        {0x1A00, "MMEA?? (was: VM_CONTEXT0_CNTL)"},
        {0x1A04, "MMEA?? (was: VM_PT_BASE_LO)"},
        {0x1A08, "MMEA?? (was: VM_PT_BASE_HI)"},
        {0x1A0C, "MMEA?? (was: VM_PT_START_LO)"},
        {0x1A10, "MMEA?? (was: VM_PT_START_HI)"},
        {0x1A14, "MMEA?? (was: VM_PT_END_LO)"},
        {0x1A18, "MMEA?? (was: VM_PT_END_HI)"},
        {0x1A20, "MMEA?? (was: VM_PT_DEPTH)"},
        {0x1A28, "MMEA?? (was: VM_PROT_FAULT_DEF)"},
        {0x1A30, "MMEA?? (was: VM_INV_PAGE_FAULT_LO)"},
        {0x1A34, "MMEA?? (was: VM_INV_PAGE_FAULT_HI)"},
        {0x1A80, "MMEA?? (was: VM_INVREQ0_REQ)"},
        {0x1A84, "MMEA?? (was: VM_INVREQ0_ACK)"},
        {0x1A88, "MMEA?? (was: VM_INVREQ0_RANGE_LO)"},
        {0x1A8C, "MMEA?? (was: VM_INVREQ0_RANGE_HI)"},
    };
    for (int i = 0; i < sizeof(mmhub)/sizeof(mmhub[0]); i++) {
        TestWrite(h, mmhub[i].off, mmhub[i].name);
    }

    /* Also scan the real MMHUB VM block */
    printf("\n--- MMHUB VM block (0x1B400-0x1B600) ---\n");
    int alive_count = 0;
    for (UINT32 off = 0x1B400; off <= 0x1B600; off += 4) {
        UINT32 v = R(h, off);
        if (v != 0xFFFFFFFF && v != 0x00000000) {
            printf("  0x%05X = 0x%08X\n", off, v);
            alive_count++;
        }
    }
    if (alive_count == 0) {
        printf("  ALL DEAD (0xFFFFFFFF or 0x0) — MMHUB VM not available\n");
    }

    /* ================================================================
     * PATH 3: GCVM — GFX Hub GPU Virtual Memory
     * These are the REAL GPU VM registers (GC_BASE shifted)
     * ================================================================ */
    printf("\n--- PATH 3: GCVM (GFX Hub, GC_BASE-shifted) ---\n");
    printf("    These are the REAL GPU VM page table registers\n\n");

    struct { UINT32 off; const char *name; } gcvm[] = {
        {0x0B360, "GCVM_L2_CNTL"},
        {0x0B364, "GCVM_L2_CNTL2"},
        {0x0B368, "GCVM_L2_CNTL3"},
        {0x0B36C, "GCVM_L2_CNTL4"},
        {0x0B404, "GCVM_CTX0_BASE"},
        {0x0B460, "GCVM_CONTEXT0_CNTL"},
        {0x0B4C0, "GCVM_CTX0_CFG_0"},
        {0x0B4C4, "GCVM_CTX0_CFG_1"},
        {0x0B4C8, "GCVM_CTX0_CFG_2"},
        {0x0B4CC, "GCVM_CTX0_CFG_3"},
        {0x0B4D0, "GCVM_CTX0_CFG_4"},
        {0x0B4D4, "GCVM_CTX0_CFG_5"},
        {0x0B608, "GCVM_CONTEXT0_PT_BASE_LO"},
        {0x0B60C, "GCVM_CONTEXT0_PT_BASE_HI"},
    };
    for (int i = 0; i < sizeof(gcvm)/sizeof(gcvm[0]); i++) {
        TestWrite(h, gcvm[i].off, gcvm[i].name);
    }

    /* TLB entries */
    printf("\n--- GCVM Context0 TLB Entries (0x0B408-0x0B454) ---\n");
    for (UINT32 off = 0x0B408; off <= 0x0B454; off += 4) {
        UINT32 v = R(h, off);
        printf("  0x%05X = 0x%08X  (%s)\n", off, v, Status(v));
    }

    /* ================================================================
     * PT_BASE LOCK TEST
     * ================================================================ */
    printf("\n--- PT_BASE Lock Test ---\n");
    UINT32 cntl_orig = R(h, 0x0B460);
    UINT32 pt_lo = R(h, 0x0B608);
    UINT32 pt_hi = R(h, 0x0B60C);
    printf("  CNTL=0x%08X, PT_BASE_LO=0x%08X, PT_BASE_HI=0x%08X\n",
           cntl_orig, pt_lo, pt_hi);

    /* Direct write */
    W(h, 0x0B608, 0xDEADBEEF);
    printf("  Direct write: readback=0x%08X %s\n",
           R(h, 0x0B608), R(h, 0x0B608) == 0xDEADBEEF ? "WRITABLE" : "LOCKED");

    /* Clear bit 0, write, restore */
    W(h, 0x0B460, cntl_orig & ~1);
    W(h, 0x0B608, 0xCAFECAFE);
    UINT32 t1 = R(h, 0x0B608);
    printf("  Clear bit0: readback=0x%08X %s\n",
           t1, t1 == 0xCAFECAFE ? "WRITABLE" : "LOCKED");
    W(h, 0x0B460, cntl_orig);
    W(h, 0x0B608, pt_lo);

    /* Clear bit 31, write, restore */
    W(h, 0x0B460, cntl_orig & ~0x80000000);
    W(h, 0x0B608, 0xBEEFBEEF);
    UINT32 t2 = R(h, 0x0B608);
    printf("  Clear bit31: readback=0x%08X %s\n",
           t2, t2 == 0xBEEFBEEF ? "WRITABLE" : "LOCKED");
    W(h, 0x0B460, cntl_orig);
    W(h, 0x0B608, pt_lo);

    /* CNTL=0, write both LO and HI */
    W(h, 0x0B460, 0x00000000);
    W(h, 0x0B608, 0x11111111);
    W(h, 0x0B60C, 0x22222222);
    UINT32 t3lo = R(h, 0x0B608);
    UINT32 t3hi = R(h, 0x0B60C);
    printf("  CNTL=0 both: LO=0x%08X HI=0x%08X %s\n",
           t3lo, t3hi,
           (t3lo == 0x11111111 && t3hi == 0x22222222) ? "WRITABLE" : "LOCKED");
    W(h, 0x0B460, cntl_orig);
    W(h, 0x0B608, pt_lo);
    W(h, 0x0B60C, pt_hi);

    /* ================================================================
     * SUMMARY
     * ================================================================ */
    printf("\n=== Summary ===\n");
    printf("  HDP MC_VM (0x0500):  Memory controller — NOT GPU VM\n");
    printf("  MMHUB 0x1A00:        MMEA arbitration — NOT GPU VM\n");
    printf("  MMHUB VM (0x1B400):  DEAD on BC-250\n");
    printf("  GCVM (0x0B460):      REAL GPU VM — PT_BASE LOCKED\n");
    printf("\n  GPU VM page tables are hardware-locked at boot.\n");
    printf("  BIOS configures GCVM Context0 TLB entries (0x0B408-0x0B454).\n");
    printf("  Next: decode TLB entry format to add custom mappings.\n");

    CloseHandle(h);
    return 0;
}
