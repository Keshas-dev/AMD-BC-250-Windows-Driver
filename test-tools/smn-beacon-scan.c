/*
 * ============================================================================
 * ⛔⛔ DO NOT RUN THIS TOOL ⛔⛔  (2026-09-30)
 *
 * This is committed only so the failure is documented and nobody repeats it.
 * On 2026-09-30 it was run on the target board and it HUNG THE MACHINE. The
 * system stopped responding and needed a reboot. The "read-only" claim in the
 * original header comment was wrong: SMN space is not uniformly side-effect
 * free, and sweeping it blind touches registers that do have read side effects.
 *
 * What it cost, and what it bought:
 *   - The board recovered after a reboot. No damage.
 *   - It produced nothing. No GC alias was found, and a later, properly
 *     controlled test with a positive control established that there is none.
 *
 * If a GC SMN alias ever needs to be looked for again, do it one known address
 * at a time through the secure-SMN read path, with a control register read
 * alongside, and never sweep. See "bankprobe" in smu-unlock-staged.c for the
 * kind of control that makes such a test trustworthy.
 *
 * Also note: reading the SMU's own mailbox block (SMN 0x03B1xxxx) through Q3
 * 0x2A hangs the SMU permanently until an AC power cycle. Never point any SMN
 * reader at that range.
 * ============================================================================
 */

/*
 * smn-beacon-scan.c - READ-ONLY scan of SMN space for the GC block's alias,
 * using the GPU SCRATCH register's magic value as a beacon.
 *
 * WHY A SCAN AND NOT A Q3 0x98 SEARCH
 * -----------------------------------
 * SMU Q3 0x98 ("ungated SMN write") is the one primitive that can write an SMN
 * address the host cannot reach directly - and it is useless for FINDING that
 * address. It returns only OK/FAIL, it writes a fixed 0x00FF, and it has no
 * bounds check, so a blind sweep would (a) give no signal about what it hit and
 * (b) risk wedging the SMU on unrelated registers. Every previous attempt to
 * locate SPI_PG this way produced nothing but risk.
 *
 * So: locate the alias by READING. The driver stamps SCRATCH (BAR5 0x32D4) with
 * the magic 0x4D585042 ("MSPB"); it reads back as such on this board right now.
 * That value is 32 bits wide and effectively unique, so a read-only sweep that
 * finds it has found the GC block's SMN window - and SPI_PG follows by offset
 * arithmetic:
 *
 *     SPI_PG_alias = scratch_hit - 0x32D4 + 0x5C3C
 *
 * A negative SMN alias has already been ruled out for the high SOC window:
 * wgp-smn-probe read 0x02402C00+0x32D4 as 0, not 0x4D585042, so 0x02402C00 is
 * some other register block rather than a GC view.
 *
 * NOTHING is written. Ranges are scanned with the host SMN port (CF8/CFC via
 * 00:00.0), which is the same read-only transport the Linux Bc250PciTransport
 * uses.
 *
 * Usage: Administrator, atikmdag loaded, MMIO mapped.
 *        Optional: start [end] in hex to override the range.
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "..\\inc\\amdbc250_ioctl.h"

#define DEV L"\\\\.\\AMDBC250DreamV43"

#define SCRATCH_OFF  0x32D4
#define SCRATCH_MAGIC 0x4D585042u
#define SPI_PG_OFF   0x5C3C

/* Default range: the SOC "high" register area, which is the part of SMN space
 * no previous scan covered (0x011xxxxx and 0x03B1xxxx were already searched
 * and came up empty). */
#define DEFAULT_START 0x02400000u
#define DEFAULT_END   0x02420000u

static HANDLE g_dev = INVALID_HANDLE_VALUE;

static int smn_read(UINT32 addr, UINT32 *value)
{
    AMDBC250_IOCTL_PCI_SMN_ACCESS req;
    DWORD returned = 0;

    memset(&req, 0, sizeof(req));
    req.SmnAddress = addr;
    req.IsWrite = 0;
    req.Bus = 0;
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
    *value = req.SmnData;
    return 1;
}

int main(int argc, char **argv)
{
    UINT32 start = DEFAULT_START, end = DEFAULT_END;
    UINT32 addr, hits = 0, reads = 0, failed = 0;
    const char *env;

    printf("=== read-only SMN scan for the GC block (beacon 0x%08X) ===\n\n",
           SCRATCH_MAGIC);

    if (argc >= 3) {
        start = (UINT32)strtoul(argv[1], NULL, 16);
        end   = (UINT32)strtoul(argv[2], NULL, 16);
    }
    printf("Range 0x%08X - 0x%08X (%u dwords), READ ONLY\n",
           start, end, end - start);
    printf("Looking for the SCRATCH magic; SPI_PG would be at hit-0x%04X.\n\n",
           SCRATCH_OFF - SPI_PG_OFF);

    g_dev = CreateFileW(DEV, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_dev == INVALID_HANDLE_VALUE) {
        printf("FATAL: cannot open device (err %lu) - is atikmdag loaded?\n",
               GetLastError());
        return 2;
    }

    /* Sanity: confirm the beacon really is in the register right now, so a
     * negative scan result means "no alias" rather than "beacon was stale". */
    {
        AMDBC250_IOCTL_REG_ACCESS rr;
        DWORD returned = 0;
        memset(&rr, 0, sizeof(rr));
        rr.RegisterOffset = SCRATCH_OFF;
        if (DeviceIoControl(g_dev, IOCTL_AMDBC250_READ_REG,
                            &rr, sizeof(rr), &rr, sizeof(rr),
                            &returned, NULL)) {
            printf("SCRATCH (BAR5 0x%04X) currently = 0x%08X %s\n\n",
                   SCRATCH_OFF, rr.Value,
                   rr.Value == SCRATCH_MAGIC ? "(beacon confirmed)" :
                                                "(BEACON MISMATCH - scan would be invalid)");
        } else {
            printf("SCRATCH read failed (err %lu); scanning anyway.\n\n",
                   GetLastError());
        }
    }

    for (addr = start; addr < end; addr += 4) {
        UINT32 v = 0;
        if (!smn_read(addr, &v)) {
            failed++;
            continue;
        }
        reads++;
        if (v == SCRATCH_MAGIC) {
            hits++;
            printf("  HIT  SMN 0x%08X = 0x%08X   -> SPI_PG alias would be 0x%08X\n",
                   addr, v, (addr - SCRATCH_OFF) + SPI_PG_OFF);
        }
        if ((addr - start) % 0x10000 == 0 && addr != start) {
            printf("  ... scanned to 0x%08X (%u reads, %u hits)\n",
                   addr, reads, hits);
        }
    }

    printf("\n=== %u reads, %u failed, %u hits ===\n", reads, failed, hits);
    if (hits == 0) {
        printf("No GC alias in this range. A zero-hit range is a real result:\n"
               "it means the GC block is not mirrored into SMN space at all in\n"
               "this window, which is consistent with the pre-x86 ACL verdict.\n"
               "Try another range, e.g. 01000000 01400000.\n");
    } else {
        printf("Verify a hit before trusting it: read the candidate address,\n"
               "then also read candidate-0x%04X (should be SPI_PG = 0) and\n"
               "candidate-0x6BB8 (should be GRBM_STATUS = 0). Two agreeing\n"
               "neighbours is what distinguishes a real alias from a coincidence.\n",
               SCRATCH_OFF - SPI_PG_OFF);
    }

    CloseHandle(g_dev);
    (void)env;
    return 0;
}
