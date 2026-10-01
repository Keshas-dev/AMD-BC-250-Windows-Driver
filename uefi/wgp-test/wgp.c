/*
 * wgp.c - The WGP experiment, fourth revision.
 *
 * WHAT REVISION 3 ESTABLISHED - this is the important part:
 *
 *   SMN 0x00000000 read 0x9FFF9700 through the SMU's secure path.
 *   0x9FFF97xx is the GPU_ID signature the Windows driver reads at BAR5 0x0000.
 *   Therefore GC registers live at SMN address == BAR5 offset, with no base and
 *   no aliasing. That makes SMN 0x00005C3C SPI_PG and SMN 0x00009C1C
 *   CC_GC_SHADER_ARRAY_CONFIG. The whole privileged GC register window is now
 *   readable, and the "is there an SMN alias" question is answered: yes, trivially.
 *
 *   Writing SMN 0x5C3C returned rc 1 - the SMU handler ran and reported success -
 *   but the value did not change. So the Data Fabric ACL that blocks host writes
 *   also blocks SMU writes to SPI_PG. That route is closed.
 *
 *   Feature bit 6 toggles cleanly: 0xDD602C7D -> 0xDD602C3D -> 0xDD602C7D. The
 *   feature framework is live and controllable. But ActiveWgp stayed 0 across the
 *   toggle, so bit 6 is not the WGP power gate. The earlier theory that it was is
 *   wrong.
 *
 * SO: SPI_PG is a dead end from any direction we can reach. CC_ARRAY is not.
 * The Windows driver found CC_ARRAY partially writable - 0xFFF80000 became
 * 0x1F000000, bits 24-28 persisted. If SMN writes land on the bits the host could
 * not set, that is the 40 CU unlock. Revision 4 tests exactly that, and RLC_PG
 * alongside it since Windows reports it as 0xFFFFFFFF read-only.
 *
 * SAFETY: every address used here is below 0x10000 and is either a GC register
 * offset already proven to answer or the two write targets. Nothing that wedged
 * the SMU in revision 1 is touched, and the SMU is checked between steps.
 *
 * Nothing writes SMU SRAM. The upstream 60-patch firmware set is still not
 * applied.
 */

#include <efi.h>
#include "smu.h"
#include "msvc_compat.h"
#include "wgp.h"

/* GC register offsets. Identical as BAR5 offsets and as SMN addresses, per the
 * GPU_ID finding above. */
#define GC_GPU_ID        0x00000u
#define GC_SCRATCH       0x032D4u
#define GC_GRBM_INDEX    0x034D0u
#define GC_RLC_PG        0x03D64u
#define GC_ME_CNTL       0x04A74u
#define GC_MEC_CNTL      0x04B14u
#define GC_SPI_PG        0x05C3Cu
#define GC_CC_ARRAY      0x09C1Cu

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

static unsigned int active_wgp(EFI_SYSTEM_TABLE *st)
{
    unsigned int v = 0;
    if (!smu_send_msg_q0(st, 0x1E, NULL, 0, &v)) return 0xFFFFFFFFu;
    return v;
}

/* Stop everything the moment the SMU stops answering, so one bad access cannot
 * invalidate the rest of the run. */
static int smu_ok(EFI_SYSTEM_TABLE *st, const char *where)
{
    unsigned int v = 0;
    int rc = sec_smn_read32(st, 0x0115A870u, &v);
    if (rc == 0x01) return 1;
    puts(st, "  !! SMU stopped answering at: ");
    puts(st, where);
    puts(st, "\r\n");
    return 0;
}

/* --------------------------------------------------------------------- */

void wgp_probe(EFI_SYSTEM_TABLE *st)
{
    /* ================= 1. GC register survey ========================= */
    puts(st, "\n== 1. GC register survey via privileged SMN ==\r\n");
    puts(st, "   GPU_ID matching 0x9FFF97xx proves SMN offset == BAR5 offset\r\n");
    {
        static const struct { unsigned int off; const char *name; } tab[] = {
            { GC_GPU_ID,     "GPU_ID" },
            { GC_SCRATCH,    "SCRATCH" },
            { GC_GRBM_INDEX, "GRBM_GFX_INDEX" },
            { GC_RLC_PG,     "RLC_PG_ALWAYS_ON" },
            { GC_ME_CNTL,    "CP_ME_CNTL" },
            { GC_MEC_CNTL,   "CP_MEC_CNTL" },
            { GC_SPI_PG,     "SPI_PG_STATIC_WGP" },
            { GC_CC_ARRAY,   "CC_GC_SHADER_ARRAY" },
        };
        int i;
        for (i = 0; i < 8; i++) {
            unsigned int r = 0;
            int rc = sec_smn_read32(st, tab[i].off, &r);
            puts(st, "  ");
            puts(st, tab[i].name);
            puts(st, " @0x");
            print_hex(st, tab[i].off);
            puts(st, " = ");
            if (rc == 0x01) print_hex(st, r);
            else { puts(st, "unreadable"); if (!smu_ok(st, tab[i].name)) return; }
            puts(st, "\r\n");
        }
    }

    /* ================= 2. CC_ARRAY write test ======================== */
    puts(st, "\n== 2. CC_GC_SHADER_ARRAY write test (the 40 CU key) ==\r\n");
    puts(st, "   Windows could only move bits 24-28 here. Test the SMU path.\r\n");
    {
        unsigned int before = 0, after = 0;
        int rc;

        rc = sec_smn_read32(st, GC_CC_ARRAY, &before);
        puts(st, "  before = ");
        if (rc != 0x01) { puts(st, "unreadable\r\n"); return; }
        print_hex(st, before);
        puts(st, "\r\n");

        /* Distinctive pattern first: proves writability independently of whether
         * the value we actually want happens to be accepted. */
        rc = smn_write32(st, GC_CC_ARRAY, 0xA5A50000u);
        puts(st, "  write 0xA5A50000 rc="); putnum(st, (unsigned int)rc);
        after = 0;
        rc = sec_smn_read32(st, GC_CC_ARRAY, &after);
        puts(st, "  read back = ");
        if (rc != 0x01) { puts(st, "unreadable - SMU wedged\r\n"); return; }
        print_hex(st, after);

        if (after == 0xA5A50000u) {
            puts(st, "   *** CC_ARRAY IS WRITABLE FROM THE SMU ***\r\n");
        } else if (after == before) {
            puts(st, "   pattern did not stick\r\n");
        } else {
            puts(st, "   changed but masked - which bits survived?\r\n");
        }

        /* Now the actual 40 CU value. duggasco's patch writes 0xFFE00000;
         * the cu-live-manager community writes 0. Try 0 first because a cleared
         * harvest mask is what the live tooling uses. */
        {
            static const unsigned int want[] = { 0x00000000u, 0xFFE00000u };
            int i;
            for (i = 0; i < 2; i++) {
                rc = smn_write32(st, GC_CC_ARRAY, want[i]);
                puts(st, "  try CC_ARRAY = ");
                print_hex(st, want[i]);
                puts(st, " rc="); putnum(st, (unsigned int)rc);
                rc = sec_smn_read32(st, GC_CC_ARRAY, &after);
                puts(st, " -> ");
                if (rc == 0x01) print_hex(st, after); else { puts(st, "unreadable"); return; }
                puts(st, "\r\n");
                if (!smu_ok(st, "CC_ARRAY write")) return;
            }
        }

        /* Put back whatever was there. */
        rc = smn_write32(st, GC_CC_ARRAY, before);
        puts(st, "  restored ");
        print_hex(st, before);
        puts(st, " rc="); putnum(st, (unsigned int)rc);
        puts(st, "\r\n");
    }

    /* ================= 3. RLC_PG write test ========================= */
    puts(st, "\n== 3. RLC_PG_ALWAYS_ON write test ==\r\n");
    puts(st, "   Linux writes 0x1F here alongside CC. Windows read 0xFFFFFFFF.\r\n");
    {
        unsigned int before = 0, after = 0;
        int rc = sec_smn_read32(st, GC_RLC_PG, &before);
        puts(st, "  before = ");
        if (rc != 0x01) { puts(st, "unreadable\r\n"); return; }
        print_hex(st, before);
        puts(st, "\r\n");

        rc = smn_write32(st, GC_RLC_PG, 0x0000001Fu);
        puts(st, "  write 0x0000001F rc="); putnum(st, (unsigned int)rc);
        after = 0;
        rc = sec_smn_read32(st, GC_RLC_PG, &after);
        puts(st, "  read back = ");
        if (rc != 0x01) { puts(st, "unreadable - SMU wedged\r\n"); return; }
        print_hex(st, after);
        puts(st, (after == 0x1Fu) ? "   *** WRITABLE ***\r\n" : "   did not stick\r\n");
    }

    /* ================= 4. did anything change the WGP count? ======== */
    puts(st, "\n== 4. WGP state after the writes ==\r\n");
    {
        unsigned int wgp = active_wgp(st);
        puts(st, "  ActiveWgp = ");
        if (wgp == 0xFFFFFFFFu) puts(st, "(timeout)\r\n");
        else { putnum(st, wgp); puts(st, "\r\n"); }
    }

    /* ================= 5. feature bit 6, watching ActiveWgp ======== */
    puts(st, "\n== 5. feature bit 6, watching ActiveWgp this time ==\r\n");
    {
        unsigned int f0 = 0, f1 = 0, args[2];
        int rc;

        if (!smu_send_msg_q0(st, 0x3D, NULL, 0, &f0)) {
            puts(st, "  Q0 0x3D timeout\r\n");
            return;
        }
        puts(st, "  mask = "); print_hex(st, f0);
        puts(st, "  ActiveWgp = ");
        { unsigned int w = active_wgp(st); if (w == 0xFFFFFFFFu) puts(st, "(to)"); else putnum(st, w); }
        puts(st, "\r\n");

        args[0] = 0x40u; args[1] = 0;

        rc = smu_send_msg_q2(st, 0x06, args, 2);
        puts(st, "  disable bit6 rc="); putnum(st, (unsigned int)rc);
        if (rc == 0x01 && smu_send_msg_q0(st, 0x3D, NULL, 0, &f1)) {
            puts(st, "  mask = "); print_hex(st, f1);
            puts(st, "  ActiveWgp = ");
            { unsigned int w = active_wgp(st); if (w == 0xFFFFFFFFu) puts(st, "(to)"); else putnum(st, w); }
            puts(st, "\r\n");
        }

        /* Q0 0x18 is the documented "set active compute-unit count". Now that
         * feature 6 is clear, the SMU is supposed to accept it. */
        {
            static const unsigned int counts[] = { 0x05u, 0x04u, 0x03u, 0x02u, 0x01u, 0x10u, 0x18u };
            int i;
            for (i = 0; i < 7; i++) {
                unsigned int resp = 0;
                rc = smu_send_msg_q0(st, 0x18, &counts[i], 1, &resp);
                puts(st, "  Q0 0x18 count=");
                putnum(st, counts[i]);
                puts(st, " rc="); putnum(st, (unsigned int)rc);
                puts(st, " resp=");
                if (rc == 0x01) print_hex(st, resp); else { puts(st, "-"); }
                puts(st, "  ActiveWgp=");
                { unsigned int w = active_wgp(st); if (w == 0xFFFFFFFFu) puts(st, "(to)"); else putnum(st, w); }
                puts(st, "\r\n");
            }
        }

        rc = smu_send_msg_q2(st, 0x05, args, 2);
        puts(st, "  re-enable bit6 rc="); putnum(st, (unsigned int)rc);
        if (rc == 0x01 && smu_send_msg_q0(st, 0x3D, NULL, 0, &f1)) {
            puts(st, "  mask = "); print_hex(st, f1); puts(st, "\r\n");
        } else if (rc != 0x01) {
            puts(st, "  *** could not re-enable bit 6 - power cycle advised ***\r\n");
        }
    }
}
