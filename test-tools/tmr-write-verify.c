/*
 * tmr-write-verify.c - decisive test: does a write to the B0:D18:F2 select
 * register at config offset 0x80 actually stick?
 *
 * WHY
 * ---
 * tmr-index-probe wrote 0x00/0x04/0x08/0x0C to the select register and every
 * read of the data port returned the same 0x00000407. That is ambiguous: either
 * the select writes are dropped, or the data port is constant, or the
 * index/data model does not apply to this function. The restore made it worse -
 * it wrote back the value that was already there (0x08), so a subsequent read
 * could not distinguish "restore worked" from "nothing ever changed".
 *
 * This tool breaks the ambiguity by writing a value that DIFFERS from the
 * original, reading it back, and reporting the comparison.
 *
 * SAFETY
 * ------
 * One write of 0x00000000 to +0x80, then one restoring write of the original.
 * 0x00000000 is the documented TMR_BASE(0) select address, and selecting a
 * register is what the PS5 loader's tmr_read() does on every single read
 * (ps5-linux-loader-main source/tmr.c). Nothing is written to the data port at
 * +0x84, so no TMR window is created, resized or re-permissioned. The original
 * value is captured first and written back before exit.
 *
 * If the readback does not match, that is the finding: the register is not
 * host-writable, which closes the TMR array as a host-readable surface and says
 * the constant 0x00000407 is just a static read, not a selected register.
 *
 * Usage: Administrator, atikmdag loaded.
 */

#include <windows.h>
#include <stdio.h>
#include "..\\inc\\amdbc250_ioctl.h"

#define DEV L"\\\\.\\AMDBC250DreamV43"

#define TMR_BUS       0
#define TMR_DEVICE    0x18
#define TMR_FUNCTION  2
#define TMR_INDEX_OFF 0x80

/* The value under test: differs from the 0x08 found on the board, and is itself
 * a documented TMR field address (TMR_BASE(0) = 0*0x10 + 0x00). */
#define TEST_VALUE 0x00000000u

static HANDLE g_dev = INVALID_HANDLE_VALUE;

static int read_dword(UINT32 off, UINT32 *value)
{
    AMDBC250_IOCTL_READ_PCI_CONFIG req;
    DWORD returned = 0;

    memset(&req, 0, sizeof(req));
    req.Bus = TMR_BUS;
    req.Device = TMR_DEVICE;
    req.Function = TMR_FUNCTION;
    if (!DeviceIoControl(g_dev, IOCTL_AMDBC250_READ_PCI_CONFIG,
                         &req, sizeof(req), &req, sizeof(req),
                         &returned, NULL)) {
        return 0;
    }
    if (req.BytesRead < TMR_INDEX_OFF + 4) {
        return 0;
    }
    *value = (UINT32)req.ConfigData[TMR_INDEX_OFF] |
             ((UINT32)req.ConfigData[TMR_INDEX_OFF + 1] << 8) |
             ((UINT32)req.ConfigData[TMR_INDEX_OFF + 2] << 16) |
             ((UINT32)req.ConfigData[TMR_INDEX_OFF + 3] << 24);
    return 1;
}

static int write_dword(UINT32 off, UINT32 value)
{
    AMDBC250_IOCTL_WRITE_PCI_CONFIG req;
    DWORD returned = 0;

    memset(&req, 0, sizeof(req));
    req.Bus = TMR_BUS;
    req.Device = TMR_DEVICE;
    req.Function = TMR_FUNCTION;
    req.Offset = off;
    req.Value = value;
    return DeviceIoControl(g_dev, IOCTL_AMDBC250_WRITE_PCI_CONFIG,
                            &req, sizeof(req), &req, sizeof(req),
                            &returned, NULL) ? 1 : 0;
}

int main(void)
{
    UINT32 before = 0, after = 0, restored = 0;
    int wroteOk, readOk;

    g_dev = CreateFileW(DEV, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_dev == INVALID_HANDLE_VALUE) {
        printf("FATAL: cannot open device (err %lu) - is atikmdag loaded?\n",
               GetLastError());
        return 2;
    }

    printf("=== select-register write verification ===\n");
    printf("B%02X:D%02X:F%u +0x%02X\n\n", TMR_BUS, TMR_DEVICE, TMR_FUNCTION,
           TMR_INDEX_OFF);

    if (!read_dword(TMR_INDEX_OFF, &before)) {
        printf("FATAL: initial read failed (err %lu)\n", GetLastError());
        CloseHandle(g_dev);
        return 2;
    }
    printf("original value : 0x%08X\n", before);

    wroteOk = write_dword(TMR_INDEX_OFF, TEST_VALUE);
    printf("write 0x%08X   : %s\n", TEST_VALUE, wroteOk ? "IOCTL ok" : "IOCTL FAILED");

    readOk = read_dword(TMR_INDEX_OFF, &after);
    if (!readOk) {
        printf("readback       : FAILED (err %lu)\n", GetLastError());
    } else {
        printf("readback       : 0x%08X\n", after);
    }

    if (wroteOk && readOk) {
        if (after == TEST_VALUE) {
            printf("\nRESULT: WRITES STICK. The select register is host-writable,\n"
                   "        so the constant 0x00000407 from tmr-index-probe is a\n"
                   "        real static register, not a selected TMR field - the\n"
                   "        index/data model does not decode on this function.\n");
        } else if (after == before) {
            printf("\nRESULT: WRITES BLOCKED. The register kept its original value,\n"
                   "        so the select writes are being dropped. The TMR array is\n"
                   "        NOT host-readable this way and the constant 0x00000407\n"
                   "        is just a static config read.\n");
        } else {
            printf("\nRESULT: INCONCLUSIVE. Readback 0x%08X matches neither the\n"
                   "        written value nor the original - something else is\n"
                   "        rewriting it.\n", after);
        }
    }

    /* Restore, and verify the restore so this tool leaves no trace. */
    if (write_dword(TMR_INDEX_OFF, before)) {
        read_dword(TMR_INDEX_OFF, &restored);
        printf("\nrestore to 0x%08X: %s (readback 0x%08X)\n",
               before,
               (restored == before) ? "verified" : "MISMATCH",
               restored);
    } else {
        printf("\nrestore FAILED (err %lu)\n", GetLastError());
    }

    CloseHandle(g_dev);
    return 0;
}
