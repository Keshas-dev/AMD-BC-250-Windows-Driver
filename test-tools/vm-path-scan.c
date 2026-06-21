/* vm-path-scan.c — Scan ALL GCVM + MC_VM + MMHUB registers
 *
 * Purpose: Find flat mapping or page table configuration
 * that BIOS may have set up for BC-250.
 *
 * Expected output: Register values at each offset.
 * If MC_VM registers are alive → flat mapping path works
 * If GCVM_CONTEXT0 has page tables → use existing TLB entries
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

int main(void) {
    printf("=== VM Path Scan — BC-250 GCVM + MC_VM Registers ===\n\n");

    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) {
        printf("Cannot open device\n");
        return 1;
    }

    /* ================================================================
     * SECTION 1: MC_VM registers (MMHUB-based, HDP block)
     * These use Linux offsets 0x0520-0x0548 which is HDP base
     * BC-250 formula: BAR5_offset = HDP_BASE(0x0F20) + offset*4?
     * Or: direct offsets as-is if they're BAR5 byte offsets
     * ================================================================ */
    printf("--- MC_VM Registers (HDP Block, offset as-is) ---\n");
    struct { const char *name; UINT32 offset; } mc_vm[] = {
        {"MC_VM_FB_OFFSET",             0x0000},
        {"MC_VM_FB_LOCATION_BASE",      0x0520},
        {"MC_VM_FB_LOCATION_TOP",       0x0524},
        {"MC_VM_AGP_BASE",              0x0528},
        {"MC_VM_AGP_TOP",               0x052C},
        {"MC_VM_AGP_BOT",               0x0530},
        {"MC_VM_AGP_CNTL",              0x0534},
        {"MC_VM_SYSTEM_APERTURE_LOW",   0x0540},
        {"MC_VM_SYSTEM_APERTURE_HIGH",  0x0544},
        {"MC_VM_SYSTEM_APERTURE_DEF",   0x0548},
    };
    for (int i = 0; i < sizeof(mc_vm)/sizeof(mc_vm[0]); i++) {
        UINT32 val = R(h, mc_vm[i].offset);
        printf("  0x%05X %-35s = 0x%08X\n", mc_vm[i].offset, mc_vm[i].name, val);
    }

    /* ================================================================
     * SECTION 2: GCVM registers (GC_BASE shifted, BAR5 offsets)
     * These are at 0x0B000-0x0B700 range (verified alive)
     * ================================================================ */
    printf("\n--- GCVM Registers (GC_BASE shifted, 0x0B000-0x0B700) ---\n");
    struct { const char *name; UINT32 offset; } gcvm[] = {
        /* L2 TLB region (0x0B31C-0x0B36C) */
        {"GCVM_L2_CNTL",                    0x0B360},
        {"GCVM_L2_CNTL2",                   0x0B364},
        {"GCVM_L2_CNTL3",                   0x0B368},
        {"GCVM_L2_CNTL4",                   0x0B36C},
        /* Context0 region (0x0B400-0x0B4D4) */
        {"GCVM_CONTEXT0_CNTL",              0x0B460},
        {"GCVM_CONTEXT0_PT_BASE_LO",        0x0B608},
        {"GCVM_CONTEXT0_PT_BASE_HI",        0x0B60C},
        /* TLB entries — Context0 page table */
        {"CTX0_TLB_0 (0x0B408)",            0x0B408},
        {"CTX0_TLB_1 (0x0B40C)",            0x0B40C},
        {"CTX0_TLB_2 (0x0B410)",            0x0B410},
        {"CTX0_TLB_3 (0x0B414)",            0x0B414},
        {"CTX0_TLB_4 (0x0B418)",            0x0B418},
        {"CTX0_TLB_5 (0x0B41C)",            0x0B41C},
        {"CTX0_TLB_6 (0x0B420)",            0x0B420},
        {"CTX0_TLB_7 (0x0B424)",            0x0B424},
        {"CTX0_TLB_8 (0x0B428)",            0x0B428},
        {"CTX0_TLB_9 (0x0B42C)",            0x0B42C},
        {"CTX0_TLB_10 (0x0B430)",           0x0B430},
        {"CTX0_TLB_11 (0x0B434)",           0x0B434},
        {"CTX0_TLB_12 (0x0B438)",           0x0B438},
        {"CTX0_TLB_13 (0x0B43C)",           0x0B43C},
        {"CTX0_TLB_14 (0x0B440)",           0x0B440},
        {"CTX0_TLB_15 (0x0B444)",           0x0B444},
        {"CTX0_TLB_16 (0x0B448)",           0x0B448},
        {"CTX0_TLB_17 (0x0B44C)",           0x0B44C},
        {"CTX0_TLB_18 (0x0B450)",           0x0B450},
        {"CTX0_TLB_19 (0x0B454)",           0x0B454},
        {"CTX0_CFG_0 (0x0B4C0)",            0x0B4C0},
        {"CTX0_CFG_1 (0x0B4C4)",            0x0B4C4},
        {"CTX0_CFG_2 (0x0B4C8)",            0x0B4C8},
        {"CTX0_CFG_3 (0x0B4CC)",            0x0B4CC},
        {"CTX0_CFG_4 (0x0B4D0)",            0x0B4D0},
        {"CTX0_CFG_5 (0x0B4D4)",            0x0B4D4},
    };
    for (int i = 0; i < sizeof(gcvm)/sizeof(gcvm[0]); i++) {
        UINT32 val = R(h, gcvm[i].offset);
        printf("  0x%05X %-35s = 0x%08X\n", gcvm[i].offset, gcvm[i].name, val);
    }

    /* ================================================================
     * SECTION 3: MMHUB registers (GC_BASE shifted, 0x1A000+ range)
     * These are at 0x1BA00+ (GC_BASE=0x1260, Linux offset*4)
     * ================================================================ */
    printf("\n--- MMHUB Registers (GC_BASE shifted, 0x1BA00+) ---\n");
    struct { const char *name; UINT32 offset; } mmhub[] = {
        {"MMHUB_VM_CONTEXT0_CNTL",          0x1BA00},
        {"MMHUB_VM_PT_BASE_LO",             0x1BA04},
        {"MMHUB_VM_PT_BASE_HI",             0x1BA08},
        {"MMHUB_VM_PT_START_LO",            0x1BA0C},
        {"MMHUB_VM_PT_START_HI",            0x1BA10},
        {"MMHUB_VM_PT_END_LO",              0x1BA14},
        {"MMHUB_VM_PT_END_HI",              0x1BA18},
        {"MMHUB_VM_PT_DEPTH",               0x1BA20},
        {"MMHUB_VM_PROT_FAULT_DEF",         0x1BA28},
        {"MMHUB_VM_INVALID_PAGE_FAULT_LO",  0x1BA30},
        {"MMHUB_VM_INVALID_PAGE_FAULT_HI",  0x1BA34},
        {"MMHUB_VM_INVREQ0_REQ",            0x1BA80},
        {"MMHUB_VM_INVREQ0_ACK",            0x1BA84},
        {"MMHUB_VM_INVREQ0_RANGE_LO",       0x1BA88},
        {"MMHUB_VM_INVREQ0_RANGE_HI",       0x1BA8C},
    };
    for (int i = 0; i < sizeof(mmhub)/sizeof(mmhub[0]); i++) {
        UINT32 val = R(h, mmhub[i].offset);
        printf("  0x%05X %-35s = 0x%08X\n", mmhub[i].offset, mmhub[i].name, val);
    }

    /* ================================================================
     * SECTION 4: HDP registers (direct offset, no shift)
     * Some HDP registers may control flat mapping
     * ================================================================ */
    printf("\n--- HDP Registers (direct offset) ---\n");
    struct { const char *name; UINT32 offset; } hdp[] = {
        {"HDP_HW_VERSION",                  0x0000},
        {"HDP_HW_MIN_IP_HARVEST",          0x0004},
        {"HDP_HW_MAX_IP_HARVEST",          0x0008},
        {"HDP_MEM_POWER_LS",               0x0D20},
        {"HDP_MEM_POWER_SS",               0x0D24},
        {"HDP_MEM_POWER_SD",               0x0D28},
        {"HDP_MEM_POWER_LS_CNTL",          0x0D2C},
        {"HDP_MMHUB_CNTL",                 0x0E00},
        {"HDP_MMHUB_SDMA0_CNTL",           0x0E04},
        {"HDP_MMHUB_SDMA1_CNTL",           0x0E08},
        {"HDP_XDP_CNTL",                   0x0E40},
        {"HDP_CNTL",                       0x000C},
    };
    for (int i = 0; i < sizeof(hdp)/sizeof(hdp[0]); i++) {
        UINT32 val = R(h, hdp[i].offset);
        printf("  0x%05X %-35s = 0x%08X\n", hdp[i].offset, hdp[i].name, val);
    }

    /* ================================================================
     * SECTION 5: PT_BASE unlock test
     * Try writing PT_BASE after clearing various CNTL bits
     * ================================================================ */
    printf("\n--- PT_BASE Unlock Test ---\n");

    UINT32 cntl_orig = R(h, 0x0B460);
    UINT32 pt_lo_orig = R(h, 0x0B608);
    UINT32 pt_hi_orig = R(h, 0x0B60C);
    printf("  Before: CNTL=0x%08X, PT_BASE_LO=0x%08X, PT_BASE_HI=0x%08X\n",
           cntl_orig, pt_lo_orig, pt_hi_orig);

    /* Test 1: Write PT_BASE without changing CNTL */
    W(h, 0x0B608, 0xDEADBEEF);
    UINT32 t1 = R(h, 0x0B608);
    printf("  Test 1 (direct write):  PT_BASE_LO=0x%08X %s\n",
           t1, t1 == 0xDEADBEEF ? "OK" : "BLOCKED");

    /* Test 2: Clear bit 0, write PT_BASE, restore */
    if (cntl_orig & 1) {
        W(h, 0x0B460, cntl_orig & ~1);
        W(h, 0x0B608, 0xCAFECAFE);
        UINT32 t2 = R(h, 0x0B608);
        printf("  Test 2 (clear bit0):    PT_BASE_LO=0x%08X %s\n",
               t2, t2 == 0xCAFECAFE ? "OK" : "BLOCKED");
        W(h, 0x0B460, cntl_orig);
    } else {
        printf("  Test 2: SKIPPED (bit0 already 0)\n");
    }

    /* Test 3: Clear bit 31 (lock bit), write PT_BASE */
    if (cntl_orig & 0x80000000) {
        W(h, 0x0B460, cntl_orig & ~0x80000000);
        W(h, 0x0B608, 0xBEEFBEEF);
        UINT32 t3 = R(h, 0x0B608);
        printf("  Test 3 (clear bit31):   PT_BASE_LO=0x%08X %s\n",
               t3, t3 == 0xBEEFBEEF ? "OK" : "BLOCKED");
        W(h, 0x0B460, cntl_orig);
    } else {
        printf("  Test 3: SKIPPED (bit31 already 0)\n");
    }

    /* Test 4: Write PT_BASE with CNTL=0 (all bits off) */
    W(h, 0x0B460, 0x00000000);
    W(h, 0x0B608, 0xCAFEBABE);
    UINT32 t4 = R(h, 0x0B608);
    printf("  Test 4 (CNTL=0):        PT_BASE_LO=0x%08X %s\n",
           t4, t4 == 0xCAFEBABE ? "OK" : "BLOCKED");
    W(h, 0x0B460, cntl_orig);

    /* Test 5: Write PT_BASE_HI also */
    W(h, 0x0B460, 0x00000000);
    W(h, 0x0B608, 0x11111111);
    W(h, 0x0B60C, 0x22222222);
    UINT32 t5lo = R(h, 0x0B608);
    UINT32 t5hi = R(h, 0x0B60C);
    printf("  Test 5 (CNTL=0 + HI):   LO=0x%08X HI=0x%08X %s\n",
           t5lo, t5hi, (t5lo == 0x11111111 && t5hi == 0x22222222) ? "OK" : "BLOCKED");
    W(h, 0x0B460, cntl_orig);

    /* Restore PT_BASE */
    W(h, 0x0B608, pt_lo_orig);
    W(h, 0x0B60C, pt_hi_orig);

    /* ================================================================
     * SECTION 6: MC_VM flat mapping test
     * Can we write MC_VM_FB_LOCATION_BASE to enable flat mapping?
     * ================================================================ */
    printf("\n--- MC_VM Flat Mapping Test ---\n");

    UINT32 fb_base_orig = R(h, 0x0520);
    UINT32 fb_top_orig = R(h, 0x0524);
    UINT32 agp_base_orig = R(h, 0x0528);
    printf("  Before: FB_BASE=0x%08X, FB_TOP=0x%08X, AGP_BASE=0x%08X\n",
           fb_base_orig, fb_top_orig, agp_base_orig);

    /* Try writing FB_LOCATION_BASE */
    W(h, 0x0520, 0x00000010);  /* 16MB >> 24 = 16 */
    UINT32 t6 = R(h, 0x0520);
    printf("  Test 6 (FB_BASE write):  0x%08X %s\n",
           t6, t6 == 0x00000010 ? "OK" : "BLOCKED");

    /* Restore */
    W(h, 0x0520, fb_base_orig);

    CloseHandle(h);

    printf("\n=== Scan Complete ===\n");
    printf("Check results: Which sections are alive? Which are blocked?\n");
    printf("If MC_VM is alive → flat mapping path\n");
    printf("If GCVM is alive → TLB injection path\n");
    printf("If PT_BASE unlockable → page table path\n");

    return 0;
}
