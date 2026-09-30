/*
 * tmr-index-probe.c - read the PSP TMR registers through the index/data
 * doorbell documented by the PS5 loader (ps5-linux-loader-main source/tmr.c).
 *
 * WHY A SECOND PROBE
 * ------------------
 * tmr-probe.c read the whole 256-byte config image and found the documented
 * layout did not decode. That is explained by tmr.c: the TMR array is NOT stored
 * in config space. It sits behind an index/data pair:
 *
 *   tmr_read(addr):  write addr -> ECAM_B0D18F2 + 0x80   (register select)
 *                     read       -> ECAM_B0D18F2 + 0x84   (data port)
 *
 * and the register address is encoded as n*0x10 + field, with field
 * BASE=0x00, LIMIT=0x04, CONFIG=0x08, REQUESTORS=0x0C. The earlier probe's
 * +0x80 = 0x00000008 is exactly TMR_CONFIG(0) sitting in the select register,
 * and +0x84 = 0x00000407 is the value that select last yielded - which is why a
 * flat decode looked like noise.
 *
 * SAFETY
 * ------
 * This tool writes to the SELECT register (0x80) and only ever READS the data
 * port (0x84). That is precisely what the PS5 loader's tmr_read() does, and it
 * is a pure register select: no TMR window is created, resized, or re-permissioned,
 * because reprogramming requires writing 0x84 and this tool never does. The
 * original select value is captured first and restored at the end, so the
 * hardware is left as found even on an early exit.
 *
 * Only n = 0..7 are probed. n*0x10 + field stays below 0x80, i.e. inside the
 * documented TMR_BASE/LIMIT/CONFIG/REQUESTORS field space, so no address that
 * would mean something else is ever written to the select register.
 *
 * Usage: Administrator, atikmdag loaded.
 */

#include <windows.h>
#include <stdio.h>
#include "..\\inc\\amdbc250_ioctl.h"

#define DEV L"\\\\.\\AMDBC250DreamV43"

/* ps5 tmr.h: ECAM_B0D18F2 = dmap + (0xF0000000 + 0x18*0x8000 + 2*0x1000) */
#define TMR_BUS       0
#define TMR_DEVICE    0x18
#define TMR_FUNCTION  2

#define TMR_INDEX_OFF 0x80
#define TMR_DATA_OFF  0x84

#define TMR_FIELD_BASE        0x00
#define TMR_FIELD_LIMIT       0x04
#define TMR_FIELD_CONFIG      0x08
#define TMR_FIELD_REQUESTORS  0x0C

#define TMR_COUNT 8
#define TMR_CFG_PERMISSIVE 0x3F07u   /* the value the PS5 loader WRITES, not one it reads */

static HANDLE g_dev = INVALID_HANDLE_VALUE;
static UINT32 g_saved_index = 0;

static int read_config(UINT8 out[256])
{
    AMDBC250_IOCTL_READ_PCI_CONFIG req;
    DWORD returned = 0;

    memset(&req, 0, sizeof(req));
    memset(out, 0, 256);
    req.Bus = TMR_BUS;
    req.Device = TMR_DEVICE;
    req.Function = TMR_FUNCTION;
    if (!DeviceIoControl(g_dev, IOCTL_AMDBC250_READ_PCI_CONFIG,
                         &req, sizeof(req), &req, sizeof(req),
                         &returned, NULL)) {
        return 0;
    }
    memcpy(out, req.ConfigData, 256);
    return 1;
}

static UINT32 dword_at(const UINT8 *buf, UINT32 off)
{
    return (UINT32)buf[off] | ((UINT32)buf[off + 1] << 8) |
           ((UINT32)buf[off + 2] << 16) | ((UINT32)buf[off + 3] << 24);
}

/* Write the select register. This is the only write this tool performs. */
static int select_reg(UINT32 index)
{
    AMDBC250_IOCTL_WRITE_PCI_CONFIG req;
    DWORD returned = 0;

    memset(&req, 0, sizeof(req));
    req.Bus = TMR_BUS;
    req.Device = TMR_DEVICE;
    req.Function = TMR_FUNCTION;
    req.Offset = TMR_INDEX_OFF;
    req.Value = index;
    return DeviceIoControl(g_dev, IOCTL_AMDBC250_WRITE_PCI_CONFIG,
                            &req, sizeof(req), &req, sizeof(req),
                            &returned, NULL) ? 1 : 0;
}

/* One documented read: select, then read the data port. */
static int tmr_read(UINT32 index, UINT32 *value)
{
    UINT8 cfg[256];

    if (!select_reg(index)) return 0;
    if (!read_config(cfg)) return 0;
    *value = dword_at(cfg, TMR_DATA_OFF);
    return 1;
}

int main(void)
{
    UINT8 cfg[256];
    UINT32 n;
    int restored = 0;

    g_dev = CreateFileW(DEV, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_dev == INVALID_HANDLE_VALUE) {
        printf("FATAL: cannot open device (err %lu) - is atikmdag loaded?\n",
               GetLastError());
        return 2;
    }

    printf("=== PSP TMR registers via the index/data doorbell ===\n");
    printf("Target: B%02X:D%02X:F%u (ECAM B0D18F2, the BDF ps5 tmr.h uses)\n",
           TMR_BUS, TMR_DEVICE, TMR_FUNCTION);
    printf("Writes: ONLY the select register at +0x%02X. The data port at\n"
           "+0x%02X is read-only here, so no TMR window is ever reprogrammed.\n\n",
           TMR_INDEX_OFF, TMR_DATA_OFF);

    if (!read_config(cfg)) {
        printf("FATAL: initial config read failed (err %lu)\n", GetLastError());
        CloseHandle(g_dev);
        return 2;
    }

    printf("Function ID: vendor=%04X device=%04X class=%06X\n",
           (UINT16)dword_at(cfg, 0x00),
           (UINT16)(dword_at(cfg, 0x00) >> 16),
           (dword_at(cfg, 0x08) >> 8) & 0xFFFFFF);

    g_saved_index = dword_at(cfg, TMR_INDEX_OFF);
    printf("Select register as found: 0x%08X  (TMR_CONFIG(0) if the field"
           " encoding holds)\n", g_saved_index);
    printf("Data port as found:       0x%08X\n\n", dword_at(cfg, TMR_DATA_OFF));

    printf("  %-4s %-10s %-10s %-10s %-12s %s\n",
           "n", "BASE", "LIMIT", "CONFIG", "REQUESTORS", "notes");
    for (n = 0; n < TMR_COUNT; n++) {
        UINT32 base = 0, lim = 0, cfgr = 0, req = 0;
        int okb, okl, okc, okr;
        char note[80];

        note[0] = 0;
        okb = tmr_read(n * 0x10 + TMR_FIELD_BASE, &base);
        okl = tmr_read(n * 0x10 + TMR_FIELD_LIMIT, &lim);
        okc = tmr_read(n * 0x10 + TMR_FIELD_CONFIG, &cfgr);
        okr = tmr_read(n * 0x10 + TMR_FIELD_REQUESTORS, &req);

        if (!okb || !okl || !okc || !okr) {
            printf("  %-4u <read failed, err %lu>\n", n, GetLastError());
            continue;
        }

        if (cfgr == TMR_CFG_PERMISSIVE) {
            strcpy(note, "CONFIG == TMR_CFG_PERMISSIVE (0x3F07)");
        } else if (base == 0 && lim == 0 && cfgr == 0 && req == 0) {
            strcpy(note, "all zero (unused slot)");
        } else {
            if (lim > base && lim - base >= 1024 * 1024) {
                sprintf(note, "window %u MB", (lim - base) / (1024 * 1024));
            }
            if (req != 0) {
                char tmp[48];
                sprintf(tmp, "requestors=0x%X", req);
                strcat(note, note[0] ? "; " : "");
                strcat(note, tmp);
            }
        }

        printf("  %-4u %08X   %08X   %08X   %08X     %s\n",
               n, base, lim, cfgr, req, note);
    }

    /* Leave the hardware as found. */
    if (select_reg(g_saved_index)) {
        restored = 1;
    }

    printf("\nSelect register restored to 0x%08X: %s\n",
           g_saved_index, restored ? "yes" : "NO - read failed");
    printf("=== done (only the select register was ever written) ===\n");

    CloseHandle(g_dev);
    return 0;
}
