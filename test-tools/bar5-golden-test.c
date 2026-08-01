#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE g_hDev = INVALID_HANDLE_VALUE;

static BOOL WriteReg(uint32_t offset, uint32_t value) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD returned = 0;
    r.RegisterOffset = offset; r.Value = value;
    return DeviceIoControl(g_hDev, IOCTL_AMDBC250_WRITE_REG, &r, sizeof(r), &r, sizeof(r), &returned, NULL);
}
static uint32_t ReadReg(uint32_t offset) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD returned = 0;
    r.RegisterOffset = offset; r.Value = 0;
    if (DeviceIoControl(g_hDev, IOCTL_AMDBC250_READ_REG, &r, sizeof(r), &r, sizeof(r), &returned, NULL)) return r.Value;
    return 0xFFFFFFFF;
}

#define GC_BASE      0x1260
#define GC_BASE_SEG1 0xA000

#define GRBM_GFX_INDEX_LIVE  0x34D0   /* empirically-verified live GRBM_GFX_INDEX on BC-250 */
#define GRBM_GFX_INDEX_BRDCST 0xE0000000

/* Extra candidate for GB_ADDR_CONFIG: hw.h uses 0x9800 (verified writable) */
#define GB_ADDR_CONFIG_HW     0x9800
#define GB_ADDR_CONFIG_MM     0x61D8   /* 0x1260 + 0x13de*4 */

/* cyan_skillfish golden table from Linux gfx_v10_0.c (golden_settings_gc_10_0_cyan_skillfish)
 * mm value + BASE_IDX from gc_10_1_0_offset.h.
 * addr0 = GC_BASE   + mm*4
 * addr1 = GC_BASE_SEG1 + mm*4   (used when BASE_IDX==1)
 */
typedef struct {
    const char *name;
    uint32_t mm;
    int baseIdx;
    uint32_t andMask;
    uint32_t orVal;
} GoldenReg;

static const GoldenReg GOLDEN[] = {
    {"GRBM_GFX_INDEX",          0x2200, 1, 0xffffffff, 0xe0000000}, /* special: write via live 0x34D0 */
    {"GE_FAST_CLKS",            0x0fe8, 0, 0x3fffffff, 0x0000493e},
    {"CGTT_CPF_CLK_CTRL",       0x50b1, 1, 0xfcff8fff, 0xf8000100},
    {"CGTT_SPI_CLK_CTRL",       0x5080, 1, 0xff7f0fff, 0x3c000100},
    {"CB_HW_CONTROL_3",         0x1423, 0, 0xa0000000, 0xa0000000},
    {"CB_HW_CONTROL_4",         0x1422, 0, 0x00008000, 0x003c8014},
    {"CH_DRAM_BURST_CTRL",      0x2d84, 1, 0x00000010, 0x00000017},
    {"CH_PIPE_STEER",           0x2d90, 1, 0xffffffff, 0xd8d8d8d8},
    {"CH_VC5_ENABLE",           0x2d94, 1, 0x00000003, 0x00000003},
    {"CP_SD_CNTL",              0x1f57, 0, 0x800007ff, 0x000005ff},
    {"DB_DEBUG",                0x13ac, 0, 0xffffffff, 0x20000000},
    {"DB_DEBUG3",               0x13ae, 0, 0xffffffff, 0x00000200},
    {"DB_DEBUG4",               0x13af, 0, 0xffffffff, 0x04800000},
    {"DB_LAST_OF_BURST_CONFIG", 0x13ba, 0, 0xffffffff, 0x03860210},
    {"GB_ADDR_CONFIG",          0x13de, 0, 0x0c1800ff, 0x00000044},
    {"GCR_GENERAL_CNTL",        0x1583, 0, 0x00009d00, 0x00008500},
    {"GCMC_VM_CACHEABLE_DRAM_ADDRESS_END", 0x1712, 0, 0xffffffff, 0x000fffff},
    {"GL1_DRAM_BURST_CTRL",     0x2d04, 1, 0x00000010, 0x00000017},
    {"GL1_PIPE_STEER",          0x2d10, 1, 0xfcfcfcfc, 0xd8d8d8d8},
    {"GL2_PIPE_STEER_0",        0x2e25, 1, 0x77707770, 0x21302130},
    {"GL2_PIPE_STEER_1",        0x2e26, 1, 0x77707770, 0x21302130},
    {"GL2A_ADDR_MATCH_MASK",    0x2e21, 1, 0xffffffff, 0xffffffcf},
    {"GL2C_ADDR_MATCH_MASK",    0x2e03, 1, 0xffffffff, 0xffffffcf},
    {"GL2C_CGTT_SCLK_CTRL",     0x50ac, 1, 0x10000000, 0x10000100},
    {"GL2C_CTRL2",              0x2e01, 1, 0xfc02002f, 0x9402002f},
    {"GL2C_CTRL3",              0x2e0c, 1, 0x00002188, 0x00000188},
    {"PA_SC_ENHANCE",           0x109c, 0, 0x08000009, 0x08000009},
    {"PA_SC_BINNER_EVENT_CNTL_0", 0x106c, 0, 0xcc3fcc03, 0x842a4c02},
    {"PA_SC_LINE_STIPPLE_STATE",0x2281, 1, 0x0000000f, 0x00000000},
    {"RMI_SPARE",               0x153f, 0, 0xffff3109, 0xffff3101},
    {"SQ_ARB_CONFIG",           0x10ac, 0, 0x00000100, 0x00000130},
    {"SQ_LDS_CLK_CTRL",         0x5090, 1, 0xffffffff, 0xffffffff},
    {"TA_CNTL_AUX",             0x12e2, 0, 0x00030008, 0x01030000},
    {"UTCL1_CTRL",              0x1588, 0, 0x00800000, 0x00800000},
};
#define GOLDEN_COUNT (sizeof(GOLDEN) / sizeof(GOLDEN[0]))

static uint32_t GoldenAddr0(const GoldenReg *g) { return GC_BASE + g->mm * 4; }
static uint32_t GoldenAddr1(const GoldenReg *g) { return GC_BASE_SEG1 + g->mm * 4; }

static uint32_t ApplyGolden(uint32_t addr, const GoldenReg *g) {
    /* Linux soc15_program_register_sequence semantics */
    uint32_t tmp;
    if (g->andMask == 0xffffffff) {
        tmp = g->orVal;
    } else {
        tmp = ReadReg(addr);
        tmp &= ~g->andMask;
        tmp |= g->orVal & g->andMask;
    }
    WriteReg(addr, tmp);
    return tmp;
}

static void ProbeOne(const char *name, uint32_t addr0, uint32_t addr1, const GoldenReg *g) {
    uint32_t v0 = ReadReg(addr0);
    uint32_t v1 = (addr1 != addr0) ? ReadReg(addr1) : v0;
    int alive0 = (v0 != 0xFFFFFFFF);
    int alive1 = (addr1 != addr0 && v1 != 0xFFFFFFFF);
    printf("  %-38s ", name);
    if (addr0 != addr1)
        printf("0x%04X=0x%08X%s  0x%04X=0x%08X%s",
               addr0, v0, alive0 ? "" : "[dead]", addr1, v1, alive1 ? "" : "[dead]");
    else
        printf("0x%04X=0x%08X%s", addr0, v0, alive0 ? "" : "[dead]");
    if (alive0 || alive1) {
        printf("  want(0x%08X & 0x%08X)", g->orVal, g->andMask);
    }
    printf("\n");
}

static void ProbeAll(void) {
    printf("\n=== READ-ONLY PROBE of cyan_skillfish golden regs ===\n");
    for (size_t i = 0; i < GOLDEN_COUNT; i++) {
        const GoldenReg *g = &GOLDEN[i];
        if (strcmp(g->name, "GRBM_GFX_INDEX") == 0) {
            uint32_t v = ReadReg(GRBM_GFX_INDEX_LIVE);
            printf("  %-38s 0x%04X=0x%08X%s  want(0x%08X)\n",
                   g->name, GRBM_GFX_INDEX_LIVE, v,
                   (v == 0xFFFFFFFF) ? "[dead]" : "", GRBM_GFX_INDEX_BRDCST);
            continue;
        }
        if (strcmp(g->name, "GB_ADDR_CONFIG") == 0) {
            /* hw.h uses 0x9800, Linux formula gives 0x61D8 */
            uint32_t v0 = ReadReg(GB_ADDR_CONFIG_HW);
            uint32_t v1 = ReadReg(GB_ADDR_CONFIG_MM);
            printf("  %-38s 0x%04X=0x%08X%s  0x%04X=0x%08X%s\n",
                   g->name, GB_ADDR_CONFIG_HW, v0, (v0 == 0xFFFFFFFF) ? "[dead]" : "",
                   GB_ADDR_CONFIG_MM, v1, (v1 == 0xFFFFFFFF) ? "[dead]" : "");
            continue;
        }
        uint32_t a0 = GoldenAddr0(g);
        uint32_t a1 = (g->baseIdx == 1) ? GoldenAddr1(g) : a0;
        ProbeOne(g->name, a0, a1, g);
    }
}

static void WriteAll(void) {
    printf("\n=== GOLDEN WRITE (GRBM broadcast select first, then RMW) ===\n");
    printf("NOTE: hardware may hang/freeze — this mirrors Linux gfx_v10_0_init_golden_registers\n");

    /* Step 1: GRBM_GFX_INDEX broadcast select (verified live addr) */
    printf("\n[1] GRBM_GFX_INDEX 0x%04X = 0x%08X (broadcast)\n", GRBM_GFX_INDEX_LIVE, GRBM_GFX_INDEX_BRDCST);
    uint32_t before = ReadReg(GRBM_GFX_INDEX_LIVE);
    WriteReg(GRBM_GFX_INDEX_LIVE, GRBM_GFX_INDEX_BRDCST);
    uint32_t after = ReadReg(GRBM_GFX_INDEX_LIVE);
    printf("    before=0x%08X after=0x%08X %s\n", before, after,
           (after == GRBM_GFX_INDEX_BRDCST) ? "[SELECTED]" : "[DID NOT STICK]");

    /* Step 2: remaining golden regs, Linux order */
    printf("\n[2] golden RMW writes (Linux order)\n");
    for (size_t i = 0; i < GOLDEN_COUNT; i++) {
        const GoldenReg *g = &GOLDEN[i];
        if (strcmp(g->name, "GRBM_GFX_INDEX") == 0) continue;
        uint32_t addr;
        if (strcmp(g->name, "GB_ADDR_CONFIG") == 0) {
            /* prefer the address that read alive */
            uint32_t v0 = ReadReg(GB_ADDR_CONFIG_HW);
            uint32_t v1 = ReadReg(GB_ADDR_CONFIG_MM);
            addr = (v0 != 0xFFFFFFFF) ? GB_ADDR_CONFIG_HW : GB_ADDR_CONFIG_MM;
        } else {
            uint32_t a0 = GoldenAddr0(g);
            uint32_t a1 = (g->baseIdx == 1) ? GoldenAddr1(g) : a0;
            uint32_t v0 = ReadReg(a0);
            uint32_t v1 = (a1 != a0) ? ReadReg(a1) : v0;
            addr = (v1 != 0xFFFFFFFF) ? a1 : a0;   /* prefer SEG1 for idx1, fall back a0 */
            if (g->baseIdx == 0 && v0 == 0xFFFFFFFF) addr = a0;
        }
        uint32_t cur = ReadReg(addr);
        uint32_t want = ApplyGolden(addr, g);
        uint32_t rb = ReadReg(addr);
        int ok = ((rb & g->andMask) == (want & g->andMask));
        printf("  %-38s 0x%04X cur=0x%08X wrote=0x%08X rb=0x%08X %s\n",
               g->name, addr, cur, want, rb, ok ? "[OK]" : "[MISMATCH]");
    }

    printf("\n[3] GRBM broadcast still set? readback: 0x%08X\n", ReadReg(GRBM_GFX_INDEX_LIVE));
}

int main(int argc, char **argv) {
    int writeMode = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--write") == 0) writeMode = 1;
    }

    g_hDev = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (g_hDev == INVALID_HANDLE_VALUE) {
        printf("FAIL: cannot open GPU device (err=%lu)\n", GetLastError());
        return 1;
    }

    printf("=== INIT_HARDWARE (BAR5 + NBIO_MAP) ===\n");
    {
        AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br = 0;
        memset(&ih, 0, sizeof(ih));
        ih.MmioPhysicalBase = 0xFE800000ULL;
        ih.MmioSize = 0x80000;
        ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
        if (!DeviceIoControl(g_hDev, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), NULL, 0, &br, NULL)) {
            printf("FAIL: INIT_HARDWARE (err=%lu)\n", GetLastError());
            return 1;
        }
        printf("OK\n");
    }

    printf("GRBM_GFX_INDEX live addr probe: before=0x%08X\n", ReadReg(GRBM_GFX_INDEX_LIVE));

    if (writeMode) {
        WriteAll();
    } else {
        ProbeAll();
        printf("\n=== DONE (read-only). Run with --write to apply the golden table ===\n");
    }

    CloseHandle(g_hDev);
    return 0;
}
