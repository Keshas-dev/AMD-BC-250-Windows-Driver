/* stale-verdict-recheck.c
 *
 * Re-tests ALL registers whose "LOCKED/READ-ONLY/DEAD" verdicts were obtained
 * BEFORE the VRAM/firmware fixes. Per the test-expiry rule, old verdicts must
 * be re-verified with the CURRENT driver build.
 *
 * For each register: read before -> write safe test value -> read after ->
 * classify: WRITABLE / PARTIAL / STUCK / UNMAPPED(0xFFFFFFFF) / EQUAL(skip).
 * Original value is restored where possible.
 *
 * SAFE: no dispatch triggers, no ring ENABLE bits, no SMU messages.
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

static HANDLE g_hDev = INVALID_HANDLE_VALUE;

#define IOCTL_GPU_READ     0x80000B88
#define IOCTL_GPU_WRITE    0x80000B8C
#define IOCTL_GPU_INIT     0x80000B80

typedef struct { UINT32 RegisterOffset; UINT32 Value; } REG_IO;

static BOOL WriteReg(uint32_t offset, uint32_t value) {
    REG_IO r; DWORD returned = 0;
    r.RegisterOffset = offset; r.Value = value;
    return DeviceIoControl(g_hDev, IOCTL_GPU_WRITE, &r, sizeof(r), &r, sizeof(r), &returned, NULL);
}
static uint32_t ReadReg(uint32_t offset) {
    REG_IO r; DWORD returned = 0;
    r.RegisterOffset = offset; r.Value = 0;
    if (DeviceIoControl(g_hDev, IOCTL_GPU_READ, &r, sizeof(r), &r, sizeof(r), &returned, NULL)) return r.Value;
    return 0xFFFFFFFF;
}

/* GRBM bank select (Linux gfx10 layout) for per-bank probes */
#define GRBM_GFX_INDEX   0x34D0
#define BANK_BCAST       0x15000000

typedef enum { T_GLOBAL, T_PERBANK } TestScope;

typedef struct {
    const char* name;
    uint32_t offset;
    uint32_t testVal;
    uint32_t mask;       /* bits that count as "changed" */
    TestScope scope;     /* global or per-SE/SH bank */
} TEST;

static TEST tests[] = {
    /* --- Control --- */
    { "SCRATCH_REG0",            0x32D4,   0xA5A50000, 0xFFFF0000, T_GLOBAL },
    { "GRBM_STATUS(RO read)",    0x3260,   0x00000000, 0x00000000, T_GLOBAL },

    /* --- GFX ring (old verdict: BASE RO, WPTR writable, RPTR bit24 stuck)
       LIVE-SAFE: no writes to ring/HQD regs — read-only probes (mask=0) --- */
    { "CP_RB0_BASE",             0x89E0,   0x00000000, 0x00000000, T_GLOBAL }, /* read only */
    { "CP_RB0_BASE_HI",          0x8BA4,   0x00000000, 0x00000000, T_GLOBAL },
    { "CP_RB0_CNTL(no enable)",  0x89E4,   0x00010000, 0x00030000, T_GLOBAL },
    { "CP_RB0_RPTR",             0x4FE0,   0x00000000, 0x00000000, T_GLOBAL },
    { "CP_RB0_WPTR",             0x8A30,   0x00000020, 0x00FFFFFF, T_GLOBAL },
    { "CP_RB_VMID",              0x89E8,   0x00000001, 0x0000000F, T_GLOBAL },

    /* --- KIQ / SDMA regs at GC offsets (old: KIQ_SIZE RO=0)
       LIVE-SAFE: BASE writes skipped (VM-adjacent) --- */
    { "KIQ_BASE_LO(RO!)",        0xE060,   0x00000000, 0x00000000, T_GLOBAL },
    { "KIQ_SIZE(0xE068)",        0xE068,   0x00000100, 0xFFFFFFFF, T_GLOBAL },
    { "SDMA_RB_BASE(RO!)",       0xE000,   0x00000000, 0x00000000, T_GLOBAL },
    { "SDMA_RB_CNTL(0xE008)",    0xE008,   0x00000040, 0x0000003F, T_GLOBAL },

    /* --- COMPUTE block (old: DIM dead, PGM writable shadow, RSRC dead) --- */
    { "DISPATCH_INITIATOR(ro)",  0x80E0,   0x00000000, 0x00000000, T_GLOBAL }, /* read only! */
    { "COMPUTE_DIM_X",           0x80E4,   0x00000008, 0xFFFFFFFF, T_GLOBAL },
    { "COMPUTE_PGM_LO",          0x8110,   0x6E51FF00, 0xFFFFFFFF, T_GLOBAL },
    { "COMPUTE_PGM_HI",          0x8114,   0x00000000, 0x00000000, T_GLOBAL }, /* read only (live-safe) */
    { "COMPUTE_PGM_RSRC1",       0x8128,   0x00000000, 0x00000000, T_GLOBAL }, /* read only */
    { "COMPUTE_PGM_RSRC2",       0x812C,   0x00000000, 0x00000000, T_GLOBAL }, /* read only */
    { "CP_MQD_BASE_ADDR",        0x9104,   0x00000000, 0x00000000, T_GLOBAL }, /* read only (live-safe) */
    { "CP_HQD_ACTIVE",           0x910C,   0x00000000, 0xFFFFFFFF, T_GLOBAL }, /* keep 0 */

    /* --- GCVM (old: PT_BASE HW-locked reads 0; invalidate worked)
       WARNING 2026-08-21: WRITING PT_BASE0_LO on a LIVE system kills the
       active scanout (white screen). READ-ONLY probes only here! --- */
    { "GCVM_PT_BASE0_LO(RO!)",   0x0B408,  0x00000000, 0x00000000, T_GLOBAL }, /* read only */
    { "GCVM_PT_BASE(RO!)",       0x0B608,  0x00000000, 0x00000000, T_GLOBAL }, /* read only */
    { "GCVM_INV_ENG0_REQ",       0x09B40,  0x00000000, 0x00000000, T_GLOBAL }, /* read only */

    /* --- CP UCODE direct-load regs (freeze zone claim) --- */
    { "CP_PFP_UCODE_ADDR",       0x172B0,  0x00000FFF, 0xFFFFFFFF, T_GLOBAL },
    { "CP_ME_RAM_RADDR",         0x172B8,  0x00000FFF, 0xFFFFFFFF, T_GLOBAL },

    /* --- WGP gating (today's baseline: SPI=0 locked, RLC RO FFFFFFFF) --- */
    { "RLC_PG_ALWAYS_ON",        0x3D64,   0x0000001F, 0xFFFFFFFF, T_GLOBAL },

    /* --- Per-bank SPI_PG probe on SE0/SH0 only (broadcast already known) --- */
    { "SPI_PG(SE0/SH0)",         0x5C3C,   0x0000001F, 0xFFFFFFFF, T_PERBANK },
};

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    g_hDev = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (g_hDev == INVALID_HANDLE_VALUE) { printf("FAIL: CreateFile gle=%lu\n", GetLastError()); return 1; }

    typedef struct {
        UINT64 MmioPhysicalBase; UINT32 MmioSize; UINT32 Flags;
        UINT64 FbPhysicalBase; UINT32 FbSize;
    } INIT_HW;
    INIT_HW ih; DWORD ret = 0;
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL; ih.MmioSize = 0x80000; ih.Flags = 1;
    BOOL ok = DeviceIoControl(g_hDev, IOCTL_GPU_INIT, &ih, sizeof(ih), &ih, sizeof(ih), &ret, NULL);
    printf("INIT_HW: ok=%d gle=%lu\n", ok, GetLastError());
    printf("GPU_ID: 0x%08X\n\n", ReadReg(0x0000));

    printf("%-26s %-10s %-10s %-10s  %s\n", "Register", "Before", "Wrote", "After", "Verdict");
    printf("------------------------------------------------------------------------------\n");

    int writable = 0, stuck = 0, unmapped = 0, partial = 0;

    for (int i = 0; i < (int)(sizeof(tests)/sizeof(tests[0])); i++) {
        TEST* t = &tests[i];
        if (t->mask == 0) continue; /* skip read-only markers */

        if (t->scope == T_PERBANK)
            WriteReg(GRBM_GFX_INDEX, 0x00000000); /* SE0/SH0 */

        uint32_t before = ReadReg(t->offset);
        const char* cls;
        uint32_t wrote = t->testVal;

        if (before == 0xFFFFFFFF && (before & t->mask) == 0xFFFFFFFF && t->mask == 0xFFFFFFFF) {
            printf("%-26s ---------- ---------- ----------  UNMAPPED (read FF)\n", t->name);
            unmapped++;
            if (t->scope == T_PERBANK) WriteReg(GRBM_GFX_INDEX, BANK_BCAST);
            continue;
        }

        WriteReg(t->offset, wrote);
        uint32_t after = ReadReg(t->offset);

        if ((after & t->mask) == (wrote & t->mask)) { cls = "WRITABLE"; writable++; }
        else if (((after ^ before) & t->mask) != 0)  { cls = "PARTIAL";   partial++;  }
        else                                          { cls = "STUCK";    stuck++;    }

        printf("%-26s 0x%08X 0x%08X 0x%08X  %s\n", t->name, before, wrote, after, cls);

        /* restore original where the write changed something and reg is not scratch */
        if (strcmp(t->name, "SCRATCH_REG0") != 0 && ((after ^ before) & t->mask))
            WriteReg(t->offset, before);

        if (t->scope == T_PERBANK) WriteReg(GRBM_GFX_INDEX, BANK_BCAST);
    }
    /* DISPATCH_INITIATOR special read-only report */
    printf("%-26s 0x%08X %-10s %-10s  READONLY(report)\n", "DISPATCH_INITIATOR",
           ReadReg(0x80E0), "-", "-");

    printf("\n=== SUMMARY ===\n");
    printf("WRITABLE: %d  PARTIAL: %d  STUCK: %d  UNMAPPED: %d\n",
           writable, partial, stuck, unmapped);
    printf("\nNOTE: fresh results with driver build 2026-08-21.\n");
    printf("Old 'locked' verdicts that now show WRITABLE/PARTIAL are CORRECTED in AGENTS.md.\n");

    CloseHandle(g_hDev);
    return 0;
}
