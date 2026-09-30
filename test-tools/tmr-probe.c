/*
 * tmr-probe.c - read-only probe of the PSP TMR (Trusted Memory Region)
 * configuration registers, using the register layout documented by the PS5
 * loader (ps5-linux-loader-main include/tmr.h).
 *
 * WHY THIS IS WORTH PROBING ON BC-250
 * -------------------------------------
 * The WGP/SOS investigation concluded (2026-09-24) that the gates are set
 * pre-x86: PSP_BL programs a Data Fabric ACL (~926 signed writes) before x86 is
 * released, and no host-reachable state (halt bits, ring setup, GART, aperture,
 * EFI variables) opens them. The one register family that was never examined is
 * the TMR control block - the PSP's own protection mechanism for the memory
 * region firmware uses, which is exactly the kind of "who is allowed to touch
 * what" gate the ACL verdict points at.
 *
 * The PS5 loader reads these through the PSP function's config space:
 *   ECAM_B0D18F2 = dmap + (0xF0000000 + 0x18 * 0x8000 + 2 * 0x1000)
 * i.e. config offset 0x80/0x84 of the PSP function (device 0x18, function 2 on
 * PS5). BC-250's PSP sits at a different BDF (01:00.2 per the Linux dmesg), but
 * the TMR index/data register block is a property of the PSP controller IP, so
 * the OFFSET is what should carry over, not the BDF.
 *
 * Register block (offsets from tmr.h, all times 4 for DWORD stepping):
 *   0x80  TMR_INDEX    - which TMR to select
 *   0x84  TMR_DATA     - data port for the selected TMR
 *   then, per selected TMR n:
 *     n*0x10+0x00  TMR_BASE(n)
 *     n*0x10+0x04  TMR_LIMIT(n)
 *     n*0x10+0x08  TMR_CONFIG(n)      (PS5 uses TMR_CFG_PERMISSIVE = 0x3F07)
 *     n*0x10+0x0C  TMR_REQUESTORS(n)  - which requestors may access the TMR
 *
 * This tool READS ONLY. It does not write 0x80 (index) or 0x84 (data): on the
 * PS5 the index/data pair is a doorbell into the TMR block, and a stray write
 * could reprogram a protection window. Every TMR is dumped from the single
 * 256-byte config read instead, so no register is touched at all.
 *
 * It also dumps the raw first 0x100 bytes so an unknown layout can be compared
 * against the documented one, and prints the GPU BARs for cross-reference.
 *
 * Usage: Administrator, atikmdag loaded.
 */

#include <windows.h>
#include <stdio.h>
#include "..\\inc\\amdbc250_ioctl.h"

#define DEV L"\\\\.\\AMDBC250DreamV43"

/* tmr.h layout, as DWORD indices inside the 256-byte config image */
#define TMR_INDEX_OFF   0x80
#define TMR_DATA_OFF    0x84
#define TMR_BASE(n)      ((n) * 0x10 + 0x00)
#define TMR_LIMIT(n)     ((n) * 0x10 + 0x04)
#define TMR_CONFIG(n)    ((n) * 0x10 + 0x08)
#define TMR_REQUESTORS(n)((n) * 0x10 + 0x0C)
#define TMR_CFG_PERMISSIVE 0x3F07u

#define TMR_COUNT 8   /* 8 * 0x10 = 0x80 bytes = the whole block after 0x80 */

static HANDLE g_dev = INVALID_HANDLE_VALUE;

static int read_config(UINT32 bus, UINT32 dev, UINT32 fn, UINT8 out[256])
{
    AMDBC250_IOCTL_READ_PCI_CONFIG req;
    DWORD returned = 0;

    memset(&req, 0, sizeof(req));
    memset(out, 0, 256);
    req.Bus = bus;
    req.Device = dev;
    req.Function = fn;
    if (!DeviceIoControl(g_dev, IOCTL_AMDBC250_READ_PCI_CONFIG,
                         &req, sizeof(req), &req, sizeof(req),
                         &returned, NULL)) {
        return 0;
    }
    if (req.BytesRead == 0) {
        return 0;
    }
    memcpy(out, req.ConfigData, 256);
    return 1;
}

static UINT32 dword_at(const UINT8 *buf, UINT32 off)
{
    if (off + 4 > 256) return 0;
    return (UINT32)buf[off] | ((UINT32)buf[off + 1] << 8) |
           ((UINT32)buf[off + 2] << 16) | ((UINT32)buf[off + 3] << 24);
}

static void dump_range(const UINT8 *buf, UINT32 from, UINT32 to)
{
    UINT32 off;
    for (off = from; off < to; off += 16) {
        UINT32 i;
        printf("  %04X ", off);
        for (i = 0; i < 16 && off + i < to; i += 4) {
            printf(" %08X", dword_at(buf, off + i));
        }
        printf("\n");
    }
}

static void probe(UINT32 bus, UINT32 dev, UINT32 fn, const char *label)
{
    UINT8 cfg[256];
    UINT32 n;

    printf("\n=== %s : B%02u:D%02u:F%u ===\n", label, bus, dev, fn);

    if (!read_config(bus, dev, fn, cfg)) {
        printf("  config read FAILED (err %lu) - no device or not accessible\n",
               GetLastError());
        return;
    }

    printf("  ID vendor=%04X device=%04X class=%06X\n",
           (UINT16)dword_at(cfg, 0x00),
           (UINT16)(dword_at(cfg, 0x00) >> 16),
           (dword_at(cfg, 0x08) >> 8) & 0xFFFFFF);
    printf("  raw 0x00-0x40 (headers + BARs):\n");
    dump_range(cfg, 0x00, 0x40);
    printf("  raw 0x80-0x%02X (documented TMR block):\n", TMR_COUNT * 0x10 + 0x80);
    dump_range(cfg, TMR_INDEX_OFF, TMR_COUNT * 0x10 + TMR_INDEX_OFF);

    printf("\n  decoded TMR (offsets relative to 0x%02X):\n", TMR_INDEX_OFF);
    printf("    %-10s %-10s %-10s %-10s %s\n",
           "BASE", "LIMIT", "CONFIG", "REQUESTORS", "notes");
    for (n = 0; n < TMR_COUNT; n++) {
        UINT32 base = dword_at(cfg, TMR_INDEX_OFF + TMR_BASE(n));
        UINT32 lim  = dword_at(cfg, TMR_INDEX_OFF + TMR_LIMIT(n));
        UINT32 cfgv = dword_at(cfg, TMR_INDEX_OFF + TMR_CONFIG(n));
        UINT32 reqv = dword_at(cfg, TMR_INDEX_OFF + TMR_REQUESTORS(n));
        char note[64];

        note[0] = 0;
        if (cfgv == TMR_CFG_PERMISSIVE) {
            strcpy(note, "CONFIG == PS5 TMR_CFG_PERMISSIVE");
        } else if (base == 0 && lim == 0 && cfgv == 0 && reqv == 0) {
            strcpy(note, "all zero (unused slot)");
        } else if (lim != 0 && lim > base) {
            sprintf(note, "window %u MB", (lim - base) / (1024 * 1024));
        }
        printf("    %08X   %08X   %08X   %08X   %s\n", base, lim, cfgv, reqv, note);
    }
}

int main(void)
{
    g_dev = CreateFileW(DEV, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_dev == INVALID_HANDLE_VALUE) {
        printf("FATAL: cannot open device (err %lu) - is atikmdag loaded?\n",
               GetLastError());
        return 2;
    }

    printf("=== PSP TMR configuration probe (READ ONLY) ===\n");
    printf("Register layout from ps5-linux-loader include/tmr.h.\n");
    printf("No register is written: the block is read out of one 256-byte\n"
           "config image, so the index/data doorbell at +0x80/+0x84 is never\n"
           "touched.\n");

    /* The PS5 BDF from tmr.h, tried first in case the board answers there. */
    probe(0, 0x18, 2, "PSP TMR home per PS5 tmr.h (ECAM B0 D18 F2)");

    /* BC-250's PSP per the Linux dmesg: 01:00.2 [1022:143e], class 0x108000. */
    probe(1, 0x00, 2, "BC-250 PSP (Linux dmesg: 01:00.2)");

    /* The GPU itself, as a control: shows what a non-PSP function's +0x80
     * block looks like, so a zero TMR block can be told apart from "this
     * function has no TMR registers at all". */
    probe(1, 0x00, 0, "GPU control (01:00.0)");

    printf("\n=== done (read-only; nothing was written) ===\n");
    CloseHandle(g_dev);
    return 0;
}
