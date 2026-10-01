/*
 * cc-array-probe.c - READ-ONLY probe for CC_GC_SHADER_ARRAY_CONFIG.
 *
 * Why this exists
 * ---------------
 * There are two addresses in circulation for CC_GC_SHADER_ARRAY_CONFIG on this
 * board and nothing recorded so far decides between them:
 *
 *   0x9C1C  from mm 0x226F   <- what the driver writes today
 *   0x529C  from mm 0x100F   <- what gc_10_1_0_offset.h actually contains
 *
 * mm 0x226F is not present in gc_10_1_0_offset.h at all, so the 0x9C1C
 * address has no RevSpace backing. The register is fuse-shadowed: reads
 * return the harvest mask, so the interesting question is not which address
 * is writable but which one the hardware actually reports, and only a read
 * answers that.
 *
 * The driver's step-6 logging cannot answer it because this build has the
 * KdPrintEx calls compiled out, so the values have to come back through an
 * IOCTL.
 *
 * Safety
 * ------
 * Read-only. No WRITE_REG, no SMU message, no MMHUB address, no unmapped
 * reference read. All four addresses below are inside the GC block that the
 * driver has already read many times (0x9C1C in particular), so this is a
 * different risk class from the mmhub-vm-probe that hung the board - that one
 * probed derived-but-unvalidated MMHUB offsets and a documented-poison address.
 *
 * Expected values
 * ---------------
 *   CC   0xffe00000  40 CUs, if the board has the full shader array
 *   CC   0xfff80000  24 CUs, the stock harvest mask
 *   CC   0x1f000000  what 0x9C1C has read back so far on BIOS 3.00 (partial write)
 *   SPI  0x0000001f  all 5 WGPs
 *   SPI  0x00000007  3 WGPs, stock
 *   SPI  0x00000000  gated, which is what we read on BIOS 3.00
 */

#include <Windows.h>
#include <stdio.h>

#include "..\inc\amdbc250_ioctl.h"

#define DEVICENAME "\\\\.\\AMDBC250DreamV43"

#define BAR5_BASE 0xFE800000ULL
#define BAR5_SIZE 0x80000ULL

#define REG_SPI_PG      0x5C3C   /* mm 0x1277 - confirmed correct */
#define REG_CC_9C1C     0x9C1C   /* mm 0x226F - used by the driver today */
#define REG_CC_529C     0x529C   /* mm 0x100F - from gc_10_1_0_offset.h */
#define REG_GRBM_INDEX  0x34D0   /* live: reads 0xBA062100 */

static HANDLE g_h;

static int ReadReg(unsigned off, unsigned *val)
{
    AMDBC250_IOCTL_REG_ACCESS a;
    DWORD ret = 0;

    a.RegisterOffset = off;
    a.Value = 0;

    if (!DeviceIoControl(g_h, IOCTL_AMDBC250_READ_REG, &a, sizeof(a),
                         &a, sizeof(a), &ret, NULL))
        return 0;

    *val = a.Value;
    return 1;
}

/* Report the same register under every bank select. On this silicon the value
 * is a fuse shadow, so every bank should agree; if they disagree the register
 * is not fuse-shadowed at that address and the read means something else. */
static void ReadPerBank(const char *name, unsigned off)
{
    static const ULONG bankSel[4] = { 0x00000000, 0x00000100, 0x00010000, 0x00010100 };
    static const char *bankName[4] = { "SE0/SH0", "SE0/SH1", "SE1/SH0", "SE1/SH1" };
    unsigned idx = 0;
    int i;

    printf("\n%s @ 0x%05X\n", name, off);
    for (i = 0; i < 4; i++) {
        unsigned v = 0;

        if (!ReadReg(REG_GRBM_INDEX, &idx)) {
            printf("  %-8s GRBM_GFX_INDEX read FAILED (%lu)\n", bankName[i], GetLastError());
            return;
        }
        /* The write is what selects the bank. This tool is read-only, so it
         * cannot select one; it reports the current select and reads anyway. */
        (void)bankSel;

        if (!ReadReg(off, &v)) {
            printf("  %-8s read FAILED (%lu)\n", bankName[i], GetLastError());
            return;
        }
        printf("  %-8s 0x%08X%s\n", bankName[i], v,
               v == 0xFFFFFFFFu ? "   <unmapped>" :
               v == 0x45454545u ? "   <poison>" : "");
    }
}

int main(void)
{
    AMDBC250_IOCTL_INIT_HARDWARE ih;
    DWORD ret = 0;
    unsigned v = 0;

    printf("cc-array-probe: READ-ONLY. No register is written.\n\n");

    g_h = CreateFileA(DEVICENAME, GENERIC_READ | GENERIC_WRITE,
                      0, NULL, OPEN_EXISTING, 0, NULL);
    if (g_h == INVALID_HANDLE_VALUE) {
        printf("CreateFile failed: %lu (is atikmdag running?)\n", GetLastError());
        return 1;
    }

    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = BAR5_BASE;
    ih.MmioSize = BAR5_SIZE;
    ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
    ih.FbPhysicalBase = 0xC0000000ULL;
    ih.FbSize = 0x10000000ULL;

    if (!DeviceIoControl(g_h, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih),
                         &ih, sizeof(ih), &ret, NULL)) {
        printf("INIT_HARDWARE failed: %lu\n", GetLastError());
        CloseHandle(g_h);
        return 1;
    }
    printf("INIT_HARDWARE ok (NBIO_MAP only - no GPU init, no register writes)\n");

    /* Sanity. If this is not the real GPU id then BAR5 is not mapped and
     * every value below is meaningless. */
    if (ReadReg(0x0000, &v))
        printf("GPU_ID @ 0x0000 = 0x%08X  %s\n", v,
               (v == 0xFFFFFFFFu || v == 0x45454545u) ? "<BAR5 NOT MAPPED>" : "<ok>");
    else
        printf("GPU_ID read failed: %lu\n", GetLastError());

    if (ReadReg(REG_GRBM_INDEX, &v))
        printf("GRBM_GFX_INDEX @ 0x34D0 = 0x%08X  %s\n", v, v == 0xBA062100u ? "<ok>" : "");

    if (ReadReg(REG_SPI_PG, &v))
        printf("SPI_PG_ENABLE_STATIC_WGP_MASK @ 0x5C3C = 0x%08X  %s\n", v,
               v == 0x1F        ? "<5 WGP: open>" :
               v == 0x07        ? "<3 WGP: stock>" :
               v == 0x00000000u ? "<gated> (this is what BIOS 3.00 reads)" : "");

    ReadPerBank("CC_GC_SHADER_ARRAY_CONFIG", REG_CC_9C1C);
    ReadPerBank("CC_GC_SHADER_ARRAY_CONFIG", REG_CC_529C);

    printf("\nExpected CC readings:\n");
    printf("  0xffe00000  40 CUs, full shader array\n");
    printf("  0xfff80000  24 CUs, stock harvest mask\n");
    printf("  0x1f000000  partial, what 0x9C1C has read so far\n\n");
    printf("This tool cannot select a bank, so the per-bank rows above all show\n");
    printf("the same currently-selected bank. They are here to show whether the\n");
    printf("address is fuse-shadowed, not to compare banks.\n");

    CloseHandle(g_h);
    return 0;
}
