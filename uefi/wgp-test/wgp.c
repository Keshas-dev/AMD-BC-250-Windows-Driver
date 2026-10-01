/*
 * wgp.c - The WGP experiment, third revision.
 *
 * Revision 1's output settled the two big questions and broke the third.
 *
 * What it proved:
 *   - unlock_smu() completes in the UEFI phase
 *   - sec_smn_read32 works: SMN 0x0005A870 read 0xFF
 *   - smn_write32 works: writing the core mask's own value returned rc 1 and read
 *     back identical
 *
 * That last one is the piece Windows never had: arbitrary SMN read and write
 * from the SMU side, before SOS is finished.
 *
 * What it broke: SMN 0x00005C3C, 0x0000A5BC and 0x0000E59C answered 0, then
 * 0x09010C3C timed out, and every call after it failed including the 0x0115A870
 * control that had just worked. Reading an unknown or unmapped SMN address
 * through Q3 0x2A wedges the SMU - the same failure seen in Windows. H3's output
 * was therefore meaningless: the SMU was already dead, which is why the feature
 * mask read back 0 and both the disable and the re-enable timed out. Nothing was
 * actually left switched off.
 *
 * So this revision only ever touches addresses already proven safe, and checks
 * the SMU is still answering between steps. No address above 0x0000FFFF appears
 * anywhere.
 *
 * TEST 1 is the decisive one. SMN 0x00005C3C answered 0 while the SMU was alive,
 * and 0 is the same value BAR5 offset 0x5C3C reports in Windows - weak evidence
 * it may be the real register rather than an unmapped hole. A read cannot
 * settle that, so write a recognisable value and read it back.
 *
 * TEST 3 exists so the UEFI numbers can be diffed against the Windows driver,
 * which reads the same addresses over CF8/CFC.
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
    const unsigned int SMN_SPI_PG = 0x00005C3Cu;

    puts(st, "\n== SMU liveness ==\r\n");
    {
        unsigned int v = 0;
        int rc = sec_smn_read32(st, 0x0115A870u, &v);
        puts(st, "  SMN 0x0115A870 = ");
        if (rc == 0x01) print_hex(st, v & 0xFFu);
        else { puts(st, "FAILED - SMU not answering, stopping\r\n"); return; }
        puts(st, "   [SMU answering]\r\n");
    }

    /* ------------------------------------------------------------------ */
    puts(st, "\n== TEST 1: is SMN 0x5C3C a real writable register? ==\r\n");
    puts(st, "  writes 0x5A5A0000 (neither 0 nor all-ones), reads back\r\n");
    {
        unsigned int before = 0, wrote = 0, after = 0;
        int rc;

        rc = sec_smn_read32(st, SMN_SPI_PG, &before);
        puts(st, "  read  = ");
        if (rc != 0x01) { puts(st, "FAILED\r\n"); return; }
        print_hex(st, before);
        puts(st, "\r\n");

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
            puts(st, "   *** REAL REGISTER - THE WRITE STUCK ***\r\n");
            /* Put back whatever was found, so a real register is not left
             * holding a garbage value. */
            rc = smn_write32(st, SMN_SPI_PG, before);
            puts(st, "  restored ");
            print_hex(st, before);
            puts(st, " rc=");
            putnum(st, (unsigned int)rc);
            puts(st, "\r\n");
        } else if (after == before) {
            puts(st, "   write did NOT stick (unmapped or read-only)\r\n");
        } else {
            puts(st, "   changed, but not to our value\r\n");
        }
    }

    /* ------------------------------------------------------------------ */
    puts(st, "\n== TEST 2: write the real mask and ask the SMU ==\r\n");
    {
        unsigned int wgp, v = 0;
        int rc;

        wgp = active_wgp(st);
        puts(st, "  ActiveWgp before = ");
        if (wgp == 0xFFFFFFFFu) puts(st, "(timeout)\r\n");
        else { putnum(st, wgp); puts(st, "\r\n"); }

        /* 0x1F = 5 WGPs = 40 CUs, the value Linux writes. */
        rc = smn_write32(st, SMN_SPI_PG, 0x1Fu);
        puts(st, "  SMN 0x5C3C := 0x1F  rc=");
        putnum(st, (unsigned int)rc);
        puts(st, "\r\n");

        v = 0;
        rc = sec_smn_read32(st, SMN_SPI_PG, &v);
        puts(st, "  read back        = ");
        if (rc != 0x01) { puts(st, "FAILED - SMU may be wedged\r\n"); return; }
        print_hex(st, v);
        puts(st, "\r\n");

        /* If the mask feeds a tick handler, one period is enough to act. */
        st->BootServices->Stall(200000);

        wgp = active_wgp(st);
        puts(st, "  ActiveWgp after  = ");
        if (wgp == 0xFFFFFFFFu) puts(st, "(timeout)\r\n");
        else { putnum(st, wgp); puts(st, "\r\n"); }

        puts(st, "  neighbours:\r\n");
        {
            static const unsigned int also[] = { 0x00005C38u, 0x00005C40u, 0x00005C44u };
            int i;
            for (i = 0; i < 3; i++) {
                unsigned int r = 0;
                rc = sec_smn_read32(st, also[i], &r);
                puts(st, "    SMN ");
                print_hex(st, also[i]);
                puts(st, " = ");
                if (rc == 0x01) print_hex(st, r); else { puts(st, "unreadable"); }
                puts(st, "\r\n");
            }
        }

        rc = smn_write32(st, SMN_SPI_PG, 0u);
        puts(st, "  restored 0, rc=");
        putnum(st, (unsigned int)rc);
        puts(st, "\r\n");
    }

    /* ------------------------------------------------------------------ */
    puts(st, "\n== TEST 3: values to compare against Windows ==\r\n");
    puts(st, "  the Windows driver reads these over CF8/CFC, so diff them\r\n");
    {
        static const unsigned int safe[] = {
            0x0115A870u,   /* CPU core presence mask, expect 0xFF       */
            0x0005A870u,   /* same register, SMU address space         */
            0x00005C3Cu,   /* SPI_PG candidate                         */
            0x0000A5BCu,   /* 0x4980 + 0x5C3C, GC base hypothesis      */
            0x0000E59Cu,   /* 0x4980 + 0x9C1C, CC_ARRAY hypothesis    */
            0x00000000u,   /* window base 0                            */
            0x00004980u    /* the GC base itself                       */
        };
        int i;
        for (i = 0; i < 7; i++) {
            unsigned int r = 0;
            int rc = sec_smn_read32(st, safe[i], &r);
            puts(st, "  SMN ");
            print_hex(st, safe[i]);
            puts(st, " = ");
            if (rc == 0x01) print_hex(st, r); else { puts(st, "unreadable"); }
            puts(st, "\r\n");
        }
    }

    /* ------------------------------------------------------------------ */
    puts(st, "\n== TEST 4: feature bit 6 round trip ==\r\n");
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
