/*
 * wgp-smn-probe.c - READ-ONLY search for SPI_PG / CC_ARRAY through a SECOND GC
 * register window, via the host SMN port.
 *
 * WHY
 * ---
 * The WGP unlock is closed on the BAR5 path: SPI_PG_ENABLE_STATIC_WGP_MASK
 * (BAR5 0x5C3C) reads 0 and host writes do not stick, which was attributed to an
 * SOS/PSP Data-Fabric ACL set before x86 release. Every method tried since has
 * written through the SAME window, so every method inherited the SAME lock.
 *
 * The Linux ip_discovery dump for this die lists three bases for GC, not one:
 *
 *     GC: 0x1260, 0xA000, 0x02402C00
 *         ^^^^^ BAR5-relative (what we use)  ^^^^ a HIGH SOC window, never tried
 *
 * 0x02402C00 is an SMN-space address. If the ACL is per-window rather than
 * per-register, the same SPI_PG register reached through the high window could
 * be visible and writable even though the BAR5 view of it is locked. That is the
 * one untried angle the existing hardware data already points at.
 *
 * WHAT THIS TOOL DOES
 * -------------------
 * Reads only. For each candidate it reports the raw 32-bit SMN value, so the
 * result is self-interpreting:
 *
 *   0xFFFFFFFF        unmapped / not a device register in this window
 *   0x00000000        mapped, reads as zero (exactly what SPI_PG shows on BAR5)
 *   anything else      the window decodes this register differently
 *
 * It also probes the neighbours of each target, because a window that aliases
 * the IP with a shifted base would show a recognisable pattern rather than the
 * expected value - and a shift is as useful a finding as a hit.
 *
 * NOTHING is written. All candidates are reads.
 *
 * Usage: Administrator, atikmdag loaded, MMIO mapped (gpu-init-explicit).
 */

#include <windows.h>
#include <stdio.h>
#include "..\\inc\\amdbc250_ioctl.h"

#define DEV L"\\\\.\\AMDBC250DreamV43"

/* BAR5-relative offsets we already know (amdbc250_dream_hw.h, GC_BASE=0x1260) */
#define SPI_PG_OFF   0x5C3C   /* SPI_PG_ENABLE_STATIC_WGP_MASK */
#define RLC_PG_OFF   0x3D64   /* RLC_PG_ALWAYS_ON_WGP_MASK     */
#define CC_ARRAY_OFF 0x9C1C   /* CC_GC_SHADER_ARRAY_CONFIG    */
#define GRBM_OFF     0x3260   /* GRBM_STATUS (known-good probe) */

/* GC bases from the Linux ip_discovery dump for this die */
static const UINT32 gc_bases[] = { 0x1260, 0xA000, 0x02402C00 };
#define GC_BASE_COUNT (sizeof(gc_bases) / sizeof(gc_bases[0]))

static const struct { const char *name; UINT32 bar5_off; } targets[] = {
    { "GRBM_STATUS   (known-good control)", GRBM_OFF },
    { "SPI_PG_WGP_MASK",                    SPI_PG_OFF },
    { "RLC_PG_ALWAYS_ON",                   RLC_PG_OFF },
    { "CC_GC_SHADER_ARRAY_CONFIG",          CC_ARRAY_OFF },
};
#define TARGET_COUNT (sizeof(targets) / sizeof(targets[0]))

static HANDLE g_dev = INVALID_HANDLE_VALUE;

/* The driver's SMN case takes AMDBC250_IOCTL_PCI_SMN_ACCESS (not the
 * AMDBC250_IOCTL_SMN_ACCESS of the older MMIO-index port path), and it also
 * returns the SAME address read through BAR5+0x38/0x3C in Bar5SmnData. That
 * gives both windows in one call, which is exactly the comparison this probe
 * exists to make. */
static int smn_read(UINT32 addr, UINT32 *pciValue, UINT32 *bar5Value)
{
    AMDBC250_IOCTL_PCI_SMN_ACCESS req;
    DWORD returned = 0;

    memset(&req, 0, sizeof(req));
    req.SmnAddress = addr;
    req.IsWrite = 0;
    req.Bus = 0;          /* host bridge 00:00.0 owns the SMN index port */
    req.Device = 0;
    req.Function = 0;
    if (!DeviceIoControl(g_dev, IOCTL_AMDBC250_PCI_SMN_ACCESS,
                         &req, sizeof(req), &req, sizeof(req),
                         &returned, NULL)) {
        return 0;
    }
    if (req.Result == 0) {
        return 0;
    }
    if (pciValue)  *pciValue  = req.SmnData;
    if (bar5Value) *bar5Value = req.Bar5SmnData;
    return 1;
}

static const char *classify(UINT32 v)
{
    if (v == 0xFFFFFFFFu) return "unmapped";
    if (v == 0x00000000u) return "mapped, reads 0";
    return "NON-ZERO";
}

int main(void)
{
    UINT32 b, t;
    int hits = 0;

    printf("=== WGP register search across GC register windows (READ ONLY) ===\n");
    printf("Nothing is written. 0xFFFFFFFF = unmapped, 0 = mapped-but-zero,\n"
           "anything else = this window decodes the register differently.\n\n");

    g_dev = CreateFileW(DEV, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_dev == INVALID_HANDLE_VALUE) {
        printf("FATAL: cannot open device (err %lu) - is atikmdag loaded?\n",
               GetLastError());
        return 2;
    }

    for (b = 0; b < GC_BASE_COUNT; b++) {
        printf("--- GC base 0x%08X (%s) ---\n", gc_bases[b],
               gc_bases[b] == 0x1260 ? "BAR5-relative, the locked view" :
               gc_bases[b] == 0xA000 ? "SEG1 alias" :
                                       "HIGH SOC window, never tried");

        for (t = 0; t < TARGET_COUNT; t++) {
            UINT32 smnAddr = gc_bases[b] + targets[t].bar5_off;
            UINT32 pciV = 0, barV = 0;

            if (!smn_read(smnAddr, &pciV, &barV)) {
                printf("   SMN 0x%08X  %-32s  READ FAILED (err %lu)\n",
                       smnAddr, targets[t].name, GetLastError());
                continue;
            }
            printf("   SMN 0x%08X  %-32s  pci=0x%08X %-18s bar5=0x%08X %s\n",
                   smnAddr, targets[t].name,
                   pciV, classify(pciV),
                   barV, classify(barV));
            if (pciV != 0xFFFFFFFFu && pciV != 0x00000000u) {
                hits++;
            }
        }
        printf("\n");
    }

    /* Neighbour sweep around the high window, to catch a shifted alias: if the
     * high window maps GC at a different base, a walk of the SPI_PG offset
     * neighbourhood should show SOMETHING non-FFFFFFFF somewhere. */
    printf("--- neighbour sweep around the high window (shifted-alias check) ---\n");
    {
        UINT32 offs[] = { 0x5C3C, 0x5C40, 0x5C44, 0x5C48,
                          0x9C1C, 0x9C20, 0x9C24,
                          0x3D64, 0x3D68, 0x3D6C };
        UINT32 i, nonzero = 0, total = (UINT32)(sizeof(offs) / sizeof(offs[0]));
        for (i = 0; i < total; i++) {
            UINT32 pciV = 0, barV = 0;
            if (smn_read(0x02402C00u + offs[i], &pciV, &barV)) {
                printf("   0x02402C00+0x%04X  pci=0x%08X %-18s bar5=0x%08X\n",
                       offs[i], pciV, classify(pciV), barV);
                if (pciV != 0xFFFFFFFFu) nonzero++;
            } else {
                printf("   0x02402C00+0x%04X  READ FAILED (err %lu)\n",
                       offs[i], GetLastError());
            }
        }
        printf("   %u of %u neighbours returned something other than 0xFFFFFFFF\n",
               nonzero, total);
    }

    printf("\n=== %d non-zero/unmapped-distinct values seen ===\n", hits);
    printf("Interpretation: a NON-ZERO SPI_PG / CC_ARRAY through the high window\n"
           "would mean the register is reachable there and the BAR5 lock is\n"
           "per-window. All-zero everywhere means the high window does not decode\n"
           "GC at this base, and the ACL verdict stands.\n");

    CloseHandle(g_dev);
    return 0;
}
