/*
 * wgp-bank-probe.c - per-bank readback of the WGP registers.
 *
 * Why this exists
 * ---------------
 * SPI_PG_ENABLE_STATIC_WGP_MASK and CC_GC_SHADER_ARRAY_CONFIG are indexed by
 * GRBM_GFX_INDEX. cc-array-probe reads them without selecting a bank and
 * reported zero, which is exactly what an unselected bank reads - so it could
 * not tell "gated" from "not selected". User mode cannot fix that without
 * writing GRBM_GFX_INDEX itself, and the driver documents that write as
 * display-fatal, so the selection has to happen in the driver.
 *
 * The driver saves GRBM_GFX_INDEX, walks the four gfx10 shader-array banks,
 * and restores the original value. Nothing else is written.
 *
 * How to read the output
 * ----------------------
 *   SPI  0x1f   5 WGP, open   - what a P5.00 board reports from POST
 *   SPI  0x07   3 WGP, stock
 *   SPI  0x00   gated
 *   CC   0xffe00000  40 CUs   CC   0xfff80000  24 CUs, stock harvest mask
 *
 *   GrbmEcho must equal BankSel for every bank. If it does not, the write
 *   did not take and none of the values mean anything.
 *
 *   GrbmIndexRestored must be 1. If it is 0 the display is on an unknown
 *   bank and a reboot is the safe move.
 */

#include <Windows.h>
#include <stdio.h>

#include "..\inc\amdbc250_ioctl.h"

#define DEVICENAME "\\\\.\\AMDBC250DreamV43"
#define BAR5_BASE  0xFE800000ULL
#define BAR5_SIZE  0x80000ULL

static const char *k_bankName[4] = { "SE0/SH0", "SE0/SH1", "SE1/SH0", "SE1/SH1" };

static const char *SpiVerdict(ULONG v)
{
    if (v == 0x1F) return "5 WGP: OPEN";
    if (v == 0x07) return "3 WGP: stock";
    if (v == 0x00) return "GATED";
    return "";
}

static const char *CcVerdict(ULONG v)
{
    if (v == 0xFFE00000) return "40 CUs";
    if (v == 0xFFF80000) return "24 CUs: stock harvest";
    if (v == 0x1F000000) return "partial (0x1F written into bits 24-28)";
    if (v == 0x00000000) return "zero";
    return "";
}

int main(void)
{
    HANDLE h;
    AMDBC250_IOCTL_INIT_HARDWARE ih;
    AMDBC250_IOCTL_WGP_BANK_PROBE p;
    DWORD ret = 0;
    ULONG b;
    int echoOk = 1, spiAllSame = 1, ccSame = 1;

    printf("wgp-bank-probe: driver selects the bank. Writes only GRBM_GFX_INDEX,\n");
    printf("                    and restores it. Close the desktop app if it flickers.\n\n");

    h = CreateFileA(DEVICENAME, GENERIC_READ | GENERIC_WRITE,
                    0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("CreateFile failed: %lu\n", GetLastError());
        return 1;
    }

    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = BAR5_BASE;
    ih.MmioSize = BAR5_SIZE;
    ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
    ih.FbPhysicalBase = 0xC0000000ULL;
    ih.FbSize = 0x10000000ULL;

    if (!DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih),
                         &ih, sizeof(ih), &ret, NULL)) {
        printf("INIT_HARDWARE failed: %lu\n", GetLastError());
        CloseHandle(h);
        return 1;
    }

    /*
     * Fill with a sentinel instead of zeros. If the driver never reaches its
     * case, or returns early before writing, the untouched sentinel is still
     * here - which distinguishes "handler ran and read zero" from "handler
     * never ran". With ZeroMemory the two are indistinguishable.
     */
    {
        unsigned i;
        ULONG *w = (ULONG *)&p;
        for (i = 0; i < sizeof(p) / sizeof(ULONG); i++)
            w[i] = 0xDEADBEEFu;
    }

    if (!DeviceIoControl(h, IOCTL_AMDBC250_WGP_BANK_PROBE, &p, sizeof(p),
                         &p, sizeof(p), &ret, NULL)) {
        printf("WGP_BANK_PROBE failed: %lu\n", GetLastError());
        printf("driver may not be 4.3.0.22 (hash check) or the IOCTL code is wrong\n");
        CloseHandle(h);
        return 1;
    }

    if (p.MmioVirtualBase == 0xDEADBEEFu) {
        printf("SENTINEL SURVIVED: the driver did not write the output buffer at all.\n");
        printf("The WGP_BANK_PROBE case was not reached, so every value below is void.\n");
        CloseHandle(h);
        return 2;
    }
    CloseHandle(h);

    printf("GPU_ID              = 0x%08X  %s\n", p.GpuId,
           (p.GpuId == 0xFFFFFFFFu || p.GpuId == 0x45454545u) ? "<BAR5 NOT MAPPED>" : "<ok>");
    printf("driver state: MmioVirtualBase=0x%08X  MmioSize=0x%X  HwInit=%u  ReadOk=%u\n",
           p.MmioVirtualBase, p.MmioSize, p.HardwareInitialized, p.DeviceReadOk);
    printf("GRBM_GFX_INDEX saved = 0x%08X   restored=%u%s\n\n",
           p.GrbmIndexSaved, p.GrbmIndexRestored,
           p.GrbmIndexRestored ? "" : "   <-- REBOOT IF 0");

    for (b = 0; b < 4; b++) {
        if (p.GrbmEcho[b] != p.BankSel[b]) echoOk = 0;
        if (b && p.SpiPg[b] != p.SpiPg[0]) spiAllSame = 0;
        if (b && p.Cc9c1c[b] != p.Cc9c1c[0]) ccSame = 0;
    }

    printf("%-8s %-10s %-10s %-12s %-12s %-10s\n",
           "BANK", "GRBM_ECHO", "SPI_PG", "CC 0x9C1C", "CC 0x529C", "");
    printf("--------------------------------------------------------------------------------\n");
    for (b = 0; b < 4; b++) {
        printf("%-8s 0x%08X 0x%08X 0x%08X    0x%08X  %s\n",
               k_bankName[b], p.GrbmEcho[b], p.SpiPg[b], p.Cc9c1c[b], p.Cc529c[b],
               SpiVerdict(p.SpiPg[b]));
    }
    printf("--------------------------------------------------------------------------------\n");

    printf("\nGRBM_GFX_INDEX write took effect : %s\n", echoOk ? "yes" : "NO - values are meaningless");
    printf("SPI_PG identical across banks    : %s%s\n", spiAllSame ? "yes" : "NO",
           spiAllSame ? "  (consistent with a fuse shadow)" : "  (register is per-bank, not fused)");
    printf("CC 0x9C1C identical across banks : %s\n", ccSame ? "yes" : "NO");
    printf("CC 0x529C == CC 0x9C1C           : %s\n",
           (p.Cc9c1c[0] == p.Cc529c[0]) ? "yes" : "NO - different registers");

    printf("\nSPI_PG bank 0 = 0x%08X  %s\n", p.SpiPg[0], SpiVerdict(p.SpiPg[0]));
    printf("CC 0x9C1C     = 0x%08X  %s\n", p.Cc9c1c[0], CcVerdict(p.Cc9c1c[0]));
    printf("CC 0x529C     = 0x%08X  %s\n", p.Cc529c[0], CcVerdict(p.Cc529c[0]));

    if (p.SpiPg[0] == 0x1F)
        printf("\nSPI_PG is OPEN. The 40 CU unlock is not gated by SPI_PG on this board,\n"
               "so CC is the only remaining lever and it reads back the fuse shadow.\n");
    if (p.SpiPg[0] == 0x00)
        printf("\nSPI_PG reads 0 with a bank selected, so this is a real gate rather than\n"
               "an artefact of the probe. docs/16 measured 0x1f from POST on BIOS P5.00,\n"
               "and this board runs BIOS 3.00, which is the likely difference.\n");

    return 0;
}
