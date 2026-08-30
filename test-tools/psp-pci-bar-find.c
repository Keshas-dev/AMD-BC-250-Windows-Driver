/* psp-pci-bar-find.c
 *
 * Finds the CURRENT PCI BAR0 of the CPU-PSP device (VEN_1022 & DEV_143E)
 * by scanning PCI config space through \\.\AmdBcPsp PCI_READ IOCTL.
 * The old hardcoded 0xFD600000 is stale (BARs reassigned each boot).
 *
 * READ-ONLY.
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

#define DEV_PATH      L"\\\\.\\AmdBcPsp"
#define PSP_PCI_READ  0x222044UL   /* CTL_CODE(0x22, 0x811, METHOD_BUFFERED, ANY) */

static HANDLE h = INVALID_HANDLE_VALUE;

static ULONG PciRead(ULONG bus, ULONG devFn, ULONG off) {
    ULONG in[3] = { bus, devFn, off };
    DWORD ret = 0;
    if (!DeviceIoControl(h, PSP_PCI_READ, in, sizeof(in), in, sizeof(in), &ret, NULL))
        return 0xFFFFFFFE;
    return in[0];
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    h = CreateFileW(DEV_PATH, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { printf("FAIL open gle=%lu\n", GetLastError()); return 1; }

    printf("Scanning PCI buses 0-15 for VEN_1022&DEV_143E...\n\n");
    int found = 0;
    for (ULONG bus = 0; bus < 16 && !found; bus++) {
        for (ULONG dev = 0; dev < 32 && !found; dev++) {
            for (ULONG fn = 0; fn < 8 && !found; fn++) {
                ULONG devFn = (dev << 3) | fn;
                ULONG vd = PciRead(bus, devFn, 0x00);
                if (vd == 0xFFFFFFFF || vd == 0xFFFFFFFE || vd == 0) continue;
                if ((vd & 0xFFFF) != 0x1022) continue;
                ULONG did = vd >> 16;
                /* print all AMD functions briefly for context */
                if (did == 0x143E) {
                    ULONG barLo = PciRead(bus, devFn, 0x10);
                    ULONG barHi = PciRead(bus, devFn, 0x14);
                    ULONG cmd   = PciRead(bus, devFn, 0x04);
                    ULONG subs  = PciRead(bus, devFn, 0x2C);
                    printf("FOUND 143E at B%02lu:D%02lu.F%lu\n", bus, dev, fn);
                    printf("  CMD     = 0x%08X %s\n", cmd, (cmd & 2) ? "(MEM-ENABLED)" : "(MEM-DISABLED!)");
                    printf("  BAR0 lo = 0x%08X\n", barLo);
                    printf("  BAR0 hi = 0x%08X\n", barHi);
                    ULONG64 bar = barLo & ~0xFULL;
                    if (barLo & 0x4) bar |= ((ULONG64)(barHi & ~0xFULL)) << 32;
                    printf("  BAR0 PA = 0x%llX  %s  size-flag=%s\n", bar,
                           (barLo & 0x4) ? "(64-bit)" : "(32-bit)",
                           (barLo & 0x8) ? "prefetch" : "non-prefetch");
                    printf("  SUBSYS  = 0x%08X\n", subs);
                    printf("\n  OLD hardcoded was 0xFD600000 -> %s\n",
                           ((bar & ~0xFULL) == 0xFD600000ULL) ? "MATCHES" : "STALE! (differs)");
                    found = 1;
                }
            }
        }
    }
    if (!found) printf("143E NOT FOUND in scanned range (or PCI_READ route dead).\n");
    CloseHandle(h);
    return 0;
}
