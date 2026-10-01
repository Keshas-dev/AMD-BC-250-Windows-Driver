/*
 * mmhub-vm-probe.c - READ-ONLY probe for the MMHUB / GFXHUB GPU-VM registers.
 *
 * Purpose
 * -------
 * AGENTS.md hypothesis B was that VM_CONTEXT0_CNTL.ENABLE_CONTEXT is the single
 * unfinished bit keeping the whole GFX/ring block locked. That is confirmed by
 * the Linux source: mmhub_v1_0_enable_system_domain() sets ENABLE_CONTEXT=1 and
 * PAGE_TABLE_DEPTH=0, and it is the only place the VM domain is switched on.
 *
 * This tool does NOT write anything. It only reads, so it is safe to run on a
 * live desktop. The purpose is to prove which addresses are actually backed by
 * real hardware before any code is allowed to write to them.
 *
 * Why the driver's existing offsets cannot be trusted
 * ---------------------------------------------------
 * inc/amdbc250_dream_hw.h uses MC_VM_AGP_BASE/TOP/BOT = 0x9528/0x952C/0x9530.
 * Linux places those registers in MMHUB at 0x0830/0x082E/0x082F, and MMHUB's
 * base on this board is 0x1A000 dwords, so the real BAR5 offsets are
 * 0x6A0C0/0x6A0B8/0x6A0BC. The driver's values are ~0x0E74 bytes off and land
 * in a different block entirely. This tool reads both sets so the difference is
 * visible in one run.
 */

#include <Windows.h>
#include <stdio.h>

#include "..\inc\amdbc250_ioctl.h"

#define DEVICENAME "\\\\.\\AMDBC250DreamV43"

/* MMHUB base from the board's own IP discovery: 0x1A000 dwords. */
#define MMHUB_BASE_DW 0x1A000u

/* Offset (dwords) -> BAR5 byte offset. */
#define MMHUB(off) (MMHUB_BASE_DW * 4u + (unsigned)(off) * 4u)

/* Offsets from drivers/gpu/drm/amd/include/asic_reg/mmhub/mmhub_1_0_offset.h */
#define mmVM_L2_CNTL                             0x0680
#define mmVM_L2_CNTL2                            0x0681
#define mmVM_L2_CNTL3                            0x0682
#define mmVM_L2_CNTL4                            0x0697
#define mmVM_CONTEXT0_CNTL                       0x06c0
#define mmVM_INVALIDATE_ENG0_SEM                 0x06d1
#define mmVM_INVALIDATE_ENG0_REQ                 0x06e3
#define mmVM_INVALIDATE_ENG0_ACK                 0x06f5
#define mmVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32  0x072b
#define mmVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32  0x072c
#define mmVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32 0x074b
#define mmVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32 0x074c
#define mmVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32   0x076b
#define mmVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32   0x076c
#define mmMC_VM_AGP_TOP                          0x082e
#define mmMC_VM_AGP_BOT                          0x082f
#define mmMC_VM_AGP_BASE                         0x0830
#define mmMC_VM_MX_L1_TLB_CNTL                   0x0833

typedef struct {
    const char *block;
    const char *name;
    unsigned    off;
} PROBE;

/*
 * The GFXHUB-side GCVM block is already known reachable from this driver
 * (GCVM_CONTEXT0_PT_BASE_LO is documented as verified writable), so it is
 * included as a live control: if these read sensibly and the MMHUB block does
 * not, the MMHUB base assumption is what is wrong.
 */
static const PROBE g_probes[] = {
    { "GFXHUB", "GCVM_L2_CNTL",            0x0B360 },
    { "GFXHUB", "GCVM_L2_CNTL2",           0x0B364 },
    { "GFXHUB", "GCVM_L2_CNTL3",           0x0B368 },
    { "GFXHUB", "GCVM_L2_CNTL4",           0x0B36C },
    { "GFXHUB", "GCVM_CONTEXT0_CNTL  <<<< ENABLE_CONTEXT", 0x0B460 },
    { "GFXHUB", "GCVM_CONTEXT0_PT_BASE_LO", 0x06C8C },
    { "GFXHUB", "GCVM_CONTEXT0_PT_BASE_HI", 0x06C90 },

    { "MMHUB",  "VM_L2_CNTL",              MMHUB(mmVM_L2_CNTL) },
    { "MMHUB",  "VM_L2_CNTL2",             MMHUB(mmVM_L2_CNTL2) },
    { "MMHUB",  "VM_L2_CNTL3",             MMHUB(mmVM_L2_CNTL3) },
    { "MMHUB",  "VM_L2_CNTL4",             MMHUB(mmVM_L2_CNTL4) },
    { "MMHUB",  "VM_CONTEXT0_CNTL  <<<< ENABLE_CONTEXT", MMHUB(mmVM_CONTEXT0_CNTL) },
    { "MMHUB",  "VM_INVALIDATE_ENG0_SEM",  MMHUB(mmVM_INVALIDATE_ENG0_SEM) },
    { "MMHUB",  "VM_INVALIDATE_ENG0_REQ",  MMHUB(mmVM_INVALIDATE_ENG0_REQ) },
    { "MMHUB",  "VM_INVALIDATE_ENG0_ACK",  MMHUB(mmVM_INVALIDATE_ENG0_ACK) },
    { "MMHUB",  "VM_CTX0_PT_BASE_LO32",    MMHUB(mmVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32) },
    { "MMHUB",  "VM_CTX0_PT_BASE_HI32",    MMHUB(mmVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32) },
    { "MMHUB",  "VM_CTX0_PT_START_LO32",   MMHUB(mmVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32) },
    { "MMHUB",  "VM_CTX0_PT_START_HI32",   MMHUB(mmVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32) },
    { "MMHUB",  "VM_CTX0_PT_END_LO32",     MMHUB(mmVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32) },
    { "MMHUB",  "VM_CTX0_PT_END_HI32",     MMHUB(mmVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32) },
    { "MMHUB",  "MC_VM_AGP_TOP",           MMHUB(mmMC_VM_AGP_TOP) },
    { "MMHUB",  "MC_VM_AGP_BOT",           MMHUB(mmMC_VM_AGP_BOT) },
    { "MMHUB",  "MC_VM_AGP_BASE",          MMHUB(mmMC_VM_AGP_BASE) },
    { "MMHUB",  "MC_VM_MX_L1_TLB_CNTL",    MMHUB(mmMC_VM_MX_L1_TLB_CNTL) },

    /* Driver's current (unverified) MC_VM offsets, read for comparison. */
    { "DRIVER", "MC_VM_AGP_BASE  (driver 0x9528)", 0x09528 },
    { "DRIVER", "MC_VM_AGP_TOP   (driver 0x952C)", 0x0952C },
    { "DRIVER", "MC_VM_AGP_BOT   (driver 0x9530)", 0x09530 },

    /* Known-good reference: BAR5 offset 0x0000 must be the GPU id. */
    { "REF",    "GPU_ID  (sanity, expect 0x9FFF97xx)", 0x0000 },
    /* Known-poison reference: unmapped reads come back as 0x45454545. */
    { "REF",    "POISON (unmapped, expect 0x45454545)", 0x11780 },
};

static int ReadReg(HANDLE h, unsigned off, unsigned *val)
{
    AMDBC250_IOCTL_REG_ACCESS a;
    DWORD ret = 0;

    a.RegisterOffset = off;
    a.Value = 0;

    if (!DeviceIoControl(h, IOCTL_AMDBC250_READ_REG, &a, sizeof(a),
                         &a, sizeof(a), &ret, NULL))
        return 0;

    *val = a.Value;
    return 1;
}

/* Values that mean "no real hardware here". */
static const char *Classify(unsigned v)
{
    if (v == 0xFFFFFFFFu) return "UNMAPPED (all ones)";
    if (v == 0x45454545u) return "UNMAPPED (poison 0x45454545)";
    if (v == 0x00000000u) return "zero";
    return "live";
}

int main(void)
{
    HANDLE h;
    AMDBC250_IOCTL_INIT_HARDWARE ih;
    DWORD ret = 0;
    unsigned live = 0, dead = 0, failed = 0;
    size_t i;

    printf("mmhub-vm-probe: READ-ONLY. No register is written.\n");
    printf("(enable test signing, run elevated)\n\n");

    h = CreateFileA(DEVICENAME, GENERIC_READ | GENERIC_WRITE,
                    0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("CreateFile failed: %lu (is atikmdag running?)\n",
               GetLastError());
        return 1;
    }

    /*
     * Map BAR5 only. Flags=NBIO_MAP deliberately skips full HW init.
     *
     * The PA and size are passed explicitly rather than left at 0 for the
     * driver's autodetect. Autodetect returns ERROR_INVALID_PARAMETER (87)
     * here - the same call that gpu-init-explicit.exe makes succeeds with
     * explicit values, so that is the known-good form.
     */
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL;  /* GPU BAR5 */
    ih.MmioSize = 0x80000;                /* 512KB */
    ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
    ih.FbPhysicalBase = 0xC0000000ULL;    /* BAR0 / VRAM aperture */
    ih.FbSize = 0x10000000ULL;            /* 256MB */

    if (!DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih),
                         &ih, sizeof(ih), &ret, NULL)) {
        printf("INIT_HARDWARE failed: %lu (explicit BAR5)\n", GetLastError());
        printf("Falling back to driver autodetect (MmioPhysicalBase=0)...\n");

        ZeroMemory(&ih, sizeof(ih));
        ih.MmioPhysicalBase = 0;
        ih.MmioSize = 0;
        ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;

        if (!DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih),
                             &ih, sizeof(ih), &ret, NULL)) {
            printf("INIT_HARDWARE autodetect also failed: %lu\n", GetLastError());
            CloseHandle(h);
            return 1;
        }
    }
    printf("INIT_HARDWARE ok (NBIO_MAP). MMIO mapped.\n\n");

    printf("%-7s %-42s %-10s %-12s %s\n",
           "BLOCK", "REGISTER", "BAR5", "VALUE", "VERDICT");
    printf("---------------------------------------------------------------------------\n");

    for (i = 0; i < sizeof(g_probes) / sizeof(g_probes[0]); i++) {
        unsigned v = 0;

        if (!ReadReg(h, g_probes[i].off, &v)) {
            printf("%-7s %-42s 0x%05X   <READ FAILED %lu>\n",
                   g_probes[i].block, g_probes[i].name, g_probes[i].off,
                   GetLastError());
            failed++;
            continue;
        }

        printf("%-7s %-42s 0x%05X   0x%08X  %s\n",
               g_probes[i].block, g_probes[i].name, g_probes[i].off, v,
               Classify(v));

        if (v == 0xFFFFFFFFu || v == 0x45454545u)
            dead++;
        else
            live++;
    }

    printf("---------------------------------------------------------------------------\n");
    printf("live=%u  unmapped=%u  read-failed=%u\n", live, dead, failed);

    if (g_probes[0].off == 0x0000) {
        printf("\nSanity: GPU_ID must NOT be 0xFFFFFFFF or 0x45454545.\n");
        printf("If it is, BAR5 is not mapped and every other line is meaningless.\n");
    }

    printf("\nNext step if MMHUB reads live: write ENABLE_CONTEXT=1 into\n");
    printf("VM_CONTEXT0_CNTL. That is NOT done by this tool.\n");

    CloseHandle(h);
    return 0;
}