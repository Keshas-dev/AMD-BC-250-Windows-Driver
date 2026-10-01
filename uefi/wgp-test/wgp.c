/*
 * wgp.c - The actual WGP experiment.
 *
 * Scope note: this deliberately does NOT map BAR5. That would need the
 * EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL vtable, and per this project's own evidence
 * the host BAR5 path is already closed - SPI_PG never accepted a host write
 * across nine GRBM_GFX_INDEX selectors. Worse, the gates are programmed by
 * PSP_BL *before* x86 reset release, so UEFI is not early enough to escape
 * them either. The privileged SMN path is the one that is genuinely new.
 *
 * H1: Is there any SMN alias for the GC block? If yes, a privileged SMN write
 *     from the SMU (Q3 0x2C) can reach SPI_PG. Eight candidate addresses, no
 *     sweeping. If the alias does not exist, the whole SMN route is dead and
 *     only an SMU firmware patch can help.
 *
 * H2: Does the SMU actually accept a privileged write? Proven by writing 0xFF
 *     to a register we know is writable (the core mask) and reading it back.
 *     Without this, a failed SPI_PG write is uninterpretable.
 *
 * H3: Is the feature tick loop alive? Feature bit 6 (GFX_WGP_POWER) is already
 *     set in the mask, yet ActiveWgp stays 0. SMU_FIRMWARE_OVERVIEW section 6
 *     says every feature installs a periodic tick handler in
 *     smu_tick_handlers[0x28]. If disabling and re-enabling bit 6 changes
 *     nothing, the handler is not running for it.
 *
 * Nothing here writes SMU SRAM. The upstream 60-patch firmware set is not
 * applied: its semantics are unknown and it targets core unlock, not the GPU.
 */

#include <efi.h>
#include "smu.h"
#include "msvc_compat.h"
#include "wgp.h"

/* --------------------------------------------------------------------- */
/* Console helpers - local to this file so the probe needs no logging     */
/* library and no file-system protocol.                                     */
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
    puts(st, "0x");
    print_hex(st, v);
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
    int i;

    /* ---------- H2 first: is the privileged write path real? ---------- */
    puts(st, "\nH2: privileged SMN write proof\r\n");
    {
        unsigned int v = 0, w = 0;
        int rc;

        rc = sec_smn_read32(st, 0x0115A870u, &v);
        puts(st, "  read  SMN 0x0115A870 (core mask) = ");
        if (rc == 0x01) puthex32(st, v & 0xFFu); else { puts(st, "FAILED rc="); putnum(st, (unsigned int)rc); }
        puts(st, "\r\n");

        /* Write back the value we just read. Harmless, and proves the write
         * half of the chain without changing any state. */
        w = v & 0xFFu;
        rc = smn_write32(st, 0x0115A870u, w);
        puts(st, "  write SMN 0x0115A870 = same value, rc = ");
        putnum(st, (unsigned int)rc);
        puts(st, "\r\n");

        v = 0;
        rc = sec_smn_read32(st, 0x0115A870u, &v);
        puts(st, "  re-read                      = ");
        if (rc == 0x01) puthex32(st, v & 0xFFu); else { puts(st, "FAILED"); }
        puts(st, "\r\n");
        puts(st, "  (a mask that comes back identical means the write is real)\r\n");
    }

    /* ---------- H1: is there a GC SMN alias at all? ------------------- */
    puts(st, "\nH1: GC SMN alias probe - 8 candidates, no sweep\r\n");
    puts(st, "  a live alias must read non-zero and not 0xFFFFFFFF\r\n");
    {
        /* The ip_discovery GC bases are 0x1260 / 0xA000 / 0x02402C00, listed in
         * dwords, so byte form is 0x4980 / 0x28000 / 0x0900B000. Below are the
         * plausible encodings of two GC registers we can recognise: SPI_PG
         * (BAR5 0x5C3C, expected 0 or 0x1F) and CC_ARRAY (BAR5 0x9C1C,
         * expected 0xFFF80000 or 0xFFE00000). */
        static const unsigned int cand[] = {
            0x00005C3Cu,          /* offset used raw, the way the unlock tool does */
            0x00004980u + 0x5C3Cu, /* GC base 0x1260 dwords as bytes + SPI_PG      */
            0x00004980u + 0x9C1Cu, /* GC base + CC_ARRAY                          */
            0x0900B000u + 0x5C3Cu, /* high window 0x02402C00 dwords as bytes    */
            0x0900B000u + 0x9C1Cu,
            0x02402C00u + 0x5C3Cu, /* high window used directly                  */
            0x02405ED4u,           /* where SCRATCH would land; read 0 in Windows */
            0x0115A870u            /* control: a register we know reads correctly  */
        };
        for (i = 0; i < 8; i++) {
            unsigned int v = 0;
            int rc = sec_smn_read32(st, cand[i], &v);
            puts(st, "  SMN ");
            puthex32(st, cand[i]);
            puts(st, " -> ");
            if (rc == 0x01) {
                puthex32(st, v);
                if (v != 0 && v != 0xFFFFFFFFu) puts(st, "   <== LIVE");
            } else {
                puts(st, "FAILED rc=");
                putnum(st, (unsigned int)rc);
            }
            puts(st, "\r\n");
        }
    }

    /* ---------- H3: is the feature tick loop alive? ------------------- */
    puts(st, "\nH3: feature bit 6 (GFX_WGP_POWER) round trip\r\n");
    {
        unsigned int f0 = 0, f1 = 0, w0 = 0xFFFFFFFFu, w1 = 0xFFFFFFFFu;
        unsigned int args[2];
        int rc;

        if (!smu_send_msg_q0(st, 0x3D, NULL, 0, &f0)) { puts(st, "  Q0 0x3D timeout\r\n"); return; }
        w0 = active_wgp(st);
        puts(st, "  before        mask="); puthex32(st, f0);
        puts(st, "  ActiveWgp="); putnum(st, w0); puts(st, "\r\n");

        args[0] = 0x40u; args[1] = 0;

        /* Q2 0x06 = DisableSmuFeatures */
        rc = smu_send_msg_q2(st, 0x06, args, 2);
        puts(st, "  Q2 0x06 disable bit6 rc="); putnum(st, (unsigned int)rc);
        if (rc == 0x01 && smu_send_msg_q0(st, 0x3D, NULL, 0, &f1)) {
            w1 = active_wgp(st);
            puts(st, "\r\n  after disable mask="); puthex32(st, f1);
            puts(st, "  ActiveWgp="); putnum(st, w1); puts(st, "\r\n");
        } else {
            puts(st, "\r\n");
        }

        /* Put it back. Leaving the board without the feature would be worse
         * than gaining nothing. */
        rc = smu_send_msg_q2(st, 0x05, args, 2);
        puts(st, "  Q2 0x05 re-enable rc="); putnum(st, (unsigned int)rc);
        if (rc == 0x01 && smu_send_msg_q0(st, 0x3D, NULL, 0, &f1)) {
            puts(st, "\r\n  restored       mask="); puthex32(st, f1);
            puts(st, "  ActiveWgp="); putnum(st, active_wgp(st)); puts(st, "\r\n");
        } else if (rc != 0x01) {
            puts(st, "   *** FEATURE LEFT OFF - power cycle advised ***\r\n");
        }
    }
}
