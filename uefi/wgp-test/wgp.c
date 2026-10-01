/*
 * wgp.c - The WGP experiment, second revision.
 *
 * Revision 1 probed eight candidate SMN addresses in a single pass and that is
 * what killed the SMU: the low three answered 0, then 0x09010C3C timed out and
 * every call after it, including the 0x0115A870 control, failed. Reading an
 * unknown or unmapped SMN address through Q3 0x2A wedges the SMU, exactly as it
 * did in Windows. So this revision never reads an address it has not already
 * proven to be safe, and it checks the SMU is still answering between steps.
 *
 * What revision 1 did establish, and this revision builds on:
 *   - the unlock_smu() chain completes in the UEFI phase
 *   - sec_smn_read32 works: SMN 0x0005A870 read 0xFF
 *   - smn_write32 works: writing the core mask's own value returned rc 1 and it
 *     read back identical
 *
 * That is the piece Windows never had - arbitrary SMN read and write from the
 * SMU side, before SOS is done. So the decisive question is now narrow: can a
 * privileged write land on SPI_PG?
 *
 * SMN 0x00005C3C answered 0x00000000 while the SMU was still alive, and that is
 * the same value BAR5 offset 0x5C3C reports in Windows, which is weak evidence
 * it may be the real register rather than a hole. A read cannot settle it. This
 * writes a recognisable value and reads it back.
 *
 * Nothing here writes SMU SRAM. The upstream 60-patch firmware set is not
 * applied: its semantics are unknown and it targets core unlock, not the GPU.
 */

#include <efi.h>
#include "smu.h"
#include "msvc_compat.h"
#include "wgp.h"

/* --------------------------------------------------------------------- */
/* Console helpers                                                        */
/* --------------------------------------------------------------------- */

static void puts(EFI_SYSTEM_TABLE *st, const char *s)
{
    UINT16 buf[160];
    int i = 0;
    for (; *s && i < 159; s++) buf[i++] = (UINT16)(unsigned char)*s;
    buf[i] = 0;
    print(st, buf);
}

static void putnum(EFI_SYSTEM_TABLE *st, unsigned int v)
{
    UINT16 b[16];
    char t[12];
    int n = 0, i;
    if (!v) { b[0] = '0'; b[1] = 0; print(st, b); return; }
    while (v && n < 10) { t[n++] = (char)('0' + (v % 10)); v /= 10; }
    for (i = 0; i < n; i++) b[i] = (UINT16)t[n - 1 - i];
    b[n] = 0;
    print(st, b);
}

static void puthex32(EFI_SYSTEM_TABLE *st, unsigned int v)
{
    print_hex(st, v);   /* already includes the 0x prefix */
}

/* --------------------------------------------------------------------- */

static unsigned int active_wgp(EFI_SYSTEM_TABLE *st)
{
    unsigned int v = 0;
    if (!smu_send_msg_q0(st, 0x1E, NULL, 0, &v)) return 0xFFFFFFFFu;
    return v;
}

void wgp_probe(EFI_SYSTEM_TABLE *st)
{
    /* SPI_PG, as a raw SMN address. The only one revision 1 got a live answer
     * from, and the same value BAR5 0x5C3C reports. */
    const unsigned int SMN_SPI_PG = 0x00005C3Cu;

    puts(st, "\n== SMU liveness ==\r\n");
    {
        unsigned int v = 0;
        int rc = sec_smn_read32(st, 0x0115A870u, &v);
        puts(st, "  SMN 0x0115A870 = ");
        if (rc == 0x01) print_hex(st, v & 0xFFu); else { puts(st, "FAILED"); return; }
        puts(st, "   [SMU answering]\r\n");
    }

    /* ------------------------------------------------------------------ */
    puts(st, "\n== TEST 1: is SMN 0x5C3C a real writable register? ==\r\n");
    puts(st, "  writes a value and reads it back, then restores\r\n");
    {
        unsigned int before = 0, wrote = 0, after = 0;
        int rc;

        rc = sec_smn_read32(st, SMN_SPI_PG, &before);
        puts(st, "  read  = ");
        if (rc != 0x01) { puts(st, "FAILED\r\n"); return; }
        print_hex(st, before);
        puts(st, "\r\n");

        /* A pattern that is neither 0 nor all-ones, so "unmapped reads 0" and
         * "all-ones on a dead bus" can both be told apart from a real hit. */
        wrote = 0x5A5A0000u;
        rc = smn_write32(st, SMN_SPI_PG, wrote);
        puts(st, "  write ");
        print_hex(st, wrote);
        puts(st, " rc=");
        putnum(st, (unsigned int)rc);
        puts(st, "\r\n");

        after = 0;
        rc = sec_smn_read32(st, SMN_SPI_PG, &after);
        puts(st, "  read  = ");
        if (rc != 0x01) { puts(st, "FAILED - SMU may be wedged\r\n"); return; }
        print_hex(st, after);

        if (after == wrote) {
            puts(st, "   *** REAL REGISTER - the write stuck ***\r\n");
        } else if (after == before) {
            puts(st, "   write did not stick (unmapped or read-only)\r\n");
        } else {
            puts(st, "   changed, but not to our value\r\n");
        }

        /* Restore whatever was there before. */
        if (after == wrote) {
            rc = smn_write32(st, SMN_SPI_PG, before);
            puts(st, "  restored ");
            print_hex(st, before);
            puts(st, " rc=");
            putnum(st, (unsigned int)rc);
            puts(st, "\r\n");
        }
    }

    /* ------------------------------------------------------------------ */
    puts(st, "\n== TEST 2: put the real value in and ask the SMU ==\r\n");
    {
        unsigned int wgp = 0xFFFFFFFFu;
        unsigned int v = 0;
        int rc;

        rc = sec_smn_read32(st, 0x0115A870u, &v);
        if (rc != 0x01) { puts(st, "  SMU gone, skipping\r\n"); return; }

        puts(st, "  ActiveWgp before = ");
        wgp = active_wgp(st);
        if (wgp == 0xFFFFFFFFu) { puts(st, "(timeout)\r\n"); }
        else { putnum(st, wgp); puts(st, "\r\n"); }

        /* The Linux value: 0x1F is 5 WGPs = all 40 CUs. */
        rc = smn_write32(st, SMN_SPI_PG, 0x1Fu);
        puts(st, "  SMN 0x5C3C := 0x1F  rc=");
        putnum(st, (unsigned int)rc);
        puts(st, "\r\n");

        v = 0;
        rc = sec_smn_read32(st, SMN_SPI_PG, &v);
        puts(st, "  read back        = ");
        if (rc == 0x01) print_hex(st, v); else { puts(st, "FAILED"); return; }
        puts(st, "\r\n");

        /* Give the SMU a moment: if the mask feeds a tick handler, one period
         * is enough for it to act. */
        st->BootServices->Stall(200000);

        wgp = active_wgp(st);
        puts(st, "  ActiveWgp after  = ");
        if (wgp == 0xFFFFFFFFu) puts(st, "(timeout)\r\n");
        else { putnum(st, wgp); puts(st, "\r\n"); }

        /* Also the related pairs, in case 0x1F alone is not what drives power. */
        {
            static const unsigned int also[] = { 0x00005C40u, 0x00005C44u, 0x00005C38u };
            int i;
            for (i = 0; i < 3; i++) {
                unsigned int r = 0;
                rc = sec_smn_read32(st, also[i], &r);
                puts(st, "  neighbour SMN ");
                print_hex(st, also[i]);
                puts(st, " = ");
                if (rc == 0x01) print_hex(st, r); else { puts(st, "unreadable"); }
                puts(st, "\r\n");
            }
        }
    }

    /* ------------------------------------------------------------------ */
    puts(st, "\n== TEST 3: feature bit 6 round trip ==\r\n");
    {
        unsigned int f0 = 0, f1 = 0;
        unsigned int args[2];
        int rc;

        if (!smu_send_msg_q0(st, 0x3D, NULL, 0, &f0)) {
            puts(st, "  Q0 0x3D timeout, SMU not answering\r\n");
            return;
        }
        puts(st, "  mask before = "); print_hex(st, f0); puts(st, "\r\n");

        args[0] = 0x40u; args[1] = 0;

        rc = smu_send_msg_q2(st, 0x06, args, 2);
        puts(st, "  Q2 0x06 disable rc="); putnum(st, (unsigned int)rc);
        if (rc == 0x01 && smu_send_msg_q0(st, 0x3D, NULL, 0, &f1)) {
            puts(st, "\r\n  mask now    = "); print_hex(st, f1); puts(st, "\r\n");
        }

        rc = smu_send_msg_q2(st, 0x05, args, 2);
        puts(st, "  Q2 0x05 enable  rc="); putnum(st, (unsigned int)rc);
        if (rc == 0x01 && smu_send_msg_q0(st, 0x3D, NULL, 0, &f1)) {
            puts(st, "\r\n  mask back  = "); print_hex(st, f1); puts(st, "\r\n");
        } else if (rc != 0x01) {
            puts(st, "\r\n  *** could not re-enable bit 6 - power cycle advised ***\r\n");
        }
    }
}
