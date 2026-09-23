/* pa-v1-diag2.c — BAR0=0 fix attempt: dump config, write LKML BAR, probe pa_v1.
 *
 * Single-run diagnostics after pa-v1-diag showed:
 *   FOUND B1.D0.F2 id=0x143E1022 (PCIConfiguration fix works)
 *   BAR0=0x00000000 (not programmed by Windows/PnP)
 * Parent root port BootConfig has Memory 0xFE700000 len 0x200000.
 * LKML: BAR0 = 0xFE700000 size 1MB.
 *
 * Steps (one process):
 *  1. Full PCI config dump 0x00..0x3C (all BARs, cmd, status, cap ptr)
 *  2. If BAR0==0: PciWrite BAR0=0xFE700000 (32-bit), re-read
 *  3. Enable cmd|=7 (IO+Mem+BM), re-read
 *  4. INIT_HW PA=0xFE700000 size=0x100000
 *  5. Read pa_v1 regs; verdict bootloader==0x001C0102
 */
#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdint.h>

#define DEV_PATH L"\\\\.\\AmdBcPsp"
#define IOCTL_PSP_READ_REG  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PSP_INIT_HW   CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PSP_PCI_READ  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x811, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PSP_PCI_WRITE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x812, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define PSP_LKML_BAR0 0xFE700000ULL
#define PSP_LKML_SIZE 0x100000U

typedef struct { ULONG64 PhysicalAddress; ULONG Size; } INIT_REQ;

static HANDLE h = INVALID_HANDLE_VALUE;

static int PciRead(ULONG bus, ULONG devFn, ULONG off, ULONG* out)
{
    ULONG args[3] = { bus, devFn, off };
    ULONG val = 0;
    DWORD br = 0;
    if (!DeviceIoControl(h, IOCTL_PSP_PCI_READ, args, sizeof(args), &val, sizeof(val), &br, NULL))
        return -1;
    *out = val;
    return 0;
}

static int PciWrite(ULONG bus, ULONG devFn, ULONG off, ULONG val)
{
    ULONG args[4] = { bus, devFn, off, val };
    DWORD br = 0;
    return DeviceIoControl(h, IOCTL_PSP_PCI_WRITE, args, sizeof(args), NULL, 0, &br, NULL) ? 0 : -1;
}

static int MmioRead(ULONG offset, ULONG* out)
{
    ULONG req[2] = { offset, 0 };
    ULONG val = 0;
    DWORD br = 0;
    if (!DeviceIoControl(h, IOCTL_PSP_READ_REG, req, sizeof(req), &val, sizeof(val), &br, NULL))
        return -1;
    *out = val;
    return 0;
}

static int InitHw(ULONG64 pa, ULONG size)
{
    INIT_REQ req;
    ULONG out = 0;
    DWORD br = 0;
    req.PhysicalAddress = pa;
    req.Size = size;
    if (!DeviceIoControl(h, IOCTL_PSP_INIT_HW, &req, sizeof(req), &out, sizeof(out), &br, NULL))
        return -1;
    printf("  INIT_HW proxy_flag=%lu\n", out);
    return 0;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    h = CreateFileW(DEV_PATH, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("FAIL: open %ls gle=%lu\n", DEV_PATH, GetLastError());
        return 1;
    }
    printf("Opened %ls OK\n\n", DEV_PATH);

    /* Find 1022:143E — known B1.D0.F2 but scan for robustness */
    printf("=== PCI scan 1022:143E ===\n");
    ULONG bus = 0xFFFFFFFF, df = 0xFFFFFFFF;
    for (ULONG b = 0; b <= 3; b++) {
        for (ULONG s = 0; s < 32; s++) {
            for (ULONG f = 0; f < 8; f++) {
                ULONG d = (s << 3) | f, id = 0;
                if (PciRead(b, d, 0x00, &id) != 0) continue;
                if ((id & 0xFFFF) == 0x1022 && (id >> 16) == 0x143E) {
                    printf("  FOUND B%u.D%u.F%u id=0x%08X\n", b, s, f, id);
                    bus = b; df = d;
                }
            }
        }
    }
    if (bus == 0xFFFFFFFF) {
        printf("  NOT FOUND\n");
        CloseHandle(h);
        return 1;
    }

    /* 1. Full config dump 0x00..0x3C */
    printf("\n=== Config dump (dword) ===\n");
    for (ULONG off = 0; off <= 0x3C; off += 4) {
        ULONG v = 0;
        if (PciRead(bus, df, off, &v) != 0) {
            printf("  +0x%02X  <read fail gle=%lu>\n", off, GetLastError());
            continue;
        }
        const char* tag = "";
        if (off == 0x00) tag = "  DEV/VEN";
        else if (off == 0x04) tag = "  CMD/STAT";
        else if (off == 0x08) tag = "  CLASS/REV";
        else if (off == 0x0C) tag = "  CLS/HEAD/BS/CL";
        else if (off == 0x10) tag = "  BAR0";
        else if (off == 0x14) tag = "  BAR1";
        else if (off == 0x18) tag = "  BAR2";
        else if (off == 0x1C) tag = "  BAR3";
        else if (off == 0x20) tag = "  BAR4";
        else if (off == 0x24) tag = "  BAR5";
        else if (off == 0x2C) tag = "  SVID/SSID";
        else if (off == 0x34) tag = "  CAP_PTR";
        else if (off == 0x3C) tag = "  INT/LAT/PIN";
        printf("  +0x%02X  0x%08X%s\n", off, v, tag);
    }

    ULONG cmd = 0, bar0 = 0, bar1 = 0, bar2 = 0, bar3 = 0;
    PciRead(bus, df, 0x04, &cmd);
    PciRead(bus, df, 0x10, &bar0);
    PciRead(bus, df, 0x14, &bar1);
    PciRead(bus, df, 0x18, &bar2);
    PciRead(bus, df, 0x1C, &bar3);

    printf("\n  BAR0=0x%08X BAR1=0x%08X BAR2=0x%08X BAR3=0x%08X\n", bar0, bar1, bar2, bar3);

    /* 2. Program BAR0 if zero — LKML value; parent bridge window covers 0xFE700000 */
    ULONG64 pa = 0;
    if (bar0 == 0 || bar0 == 0xFFFFFFFF) {
        printf("\n=== BAR0 not programmed — writing 0x%08X (LKML) ===\n", (ULONG)PSP_LKML_BAR0);
        if (PciWrite(bus, df, 0x10, (ULONG)PSP_LKML_BAR0) != 0) {
            printf("  BAR0 WRITE fail gle=%lu\n", GetLastError());
        }
        Sleep(50);
        ULONG bar0b = 0, bar1b = 0;
        PciRead(bus, df, 0x10, &bar0b);
        PciRead(bus, df, 0x14, &bar1b);
        printf("  BAR0 now=0x%08X BAR1=0x%08X\n", bar0b, bar1b);
        if ((bar0b & ~0xFU) == (ULONG)PSP_LKML_BAR0)
            printf("  BAR0 WRITE STICKED\n");
        else
            printf("  BAR0 write DID NOT stick (readback=0x%08X)\n", bar0b);
        bar0 = bar0b; bar1 = bar1b;
    }

    ULONG barType = bar0 & 7;
    if ((barType & 6) == 4)
        pa = ((ULONG64)bar1 << 32) | (bar0 & ~0xFU);
    else
        pa = bar0 & ~0xFU;

    if (pa == 0) {
        /* Last resort: use LKML PA even if config read is 0 — bridge may hardwarp */
        printf("  Config BAR still 0 — falling back to LKML PA 0x%llX\n", PSP_LKML_BAR0);
        pa = PSP_LKML_BAR0;
    }

    /* 3. Enable memory decode */
    if (!(cmd & 2)) {
        printf("  Enabling Mem/IO/BM (cmd|=7): was 0x%08X\n", cmd);
        PciWrite(bus, df, 0x04, cmd | 7);
        Sleep(20);
        ULONG c2 = 0;
        PciRead(bus, df, 0x04, &c2);
        printf("  Command now=0x%08X\n", c2);
        cmd = c2;
    } else {
        printf("  Memory already enabled (cmd=0x%08X)\n", cmd);
    }

    /* 4. INIT_HW */
    printf("\n=== INIT_HW PA=0x%llX size=0x%X ===\n", pa, PSP_LKML_SIZE);
    if (InitHw(pa, PSP_LKML_SIZE) != 0)
        printf("  INIT_HW FAIL gle=%lu\n", GetLastError());

    /* 5. pa_v1 probe */
    printf("\n=== pa_v1 regs ===\n");
    static const struct { ULONG off; const char* name; } regs[] = {
        { 0x00000, "BAR0+0x0 sanity" },
        { 0x00100, "BAR0+0x100 CCP ver" },
        { 0x10570, "cmdresp C2PMSG_28" },
        { 0x10574, "cmdbuff_lo C2PMSG_29" },
        { 0x10578, "cmdbuff_hi C2PMSG_30" },
        { 0x10690, "inten P2CMSG_INTEN" },
        { 0x10694, "intsts P2CMSG_INTSTS" },
        { 0x109EC, "bootloader C2PMSG_59" },
        { 0x109FC, "feature C2PMSG_63" },
        { 0x10A24, "doorbell_button C2PMSG_73" },
        { 0x10A40, "doorbell_cmd C2PMSG_80" },
    };
    int live = 0;
    ULONG boot = 0;
    for (size_t i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
        ULONG v = 0;
        if (MmioRead(regs[i].off, &v) != 0) {
            printf("  0x%05X %-28s IOCTL FAIL gle=%lu\n", regs[i].off, regs[i].name, GetLastError());
            continue;
        }
        printf("  0x%05X %-28s = 0x%08X%s\n", regs[i].off, regs[i].name, v,
               (v != 0xFFFFFFFF) ? "  <<< LIVE" : "");
        if (v != 0xFFFFFFFF) live++;
        if (regs[i].off == 0x109EC) boot = v;
    }

    printf("\n=== Verdict ===\n");
    printf("  non-FF regs: %d/%zu\n", live, sizeof(regs) / sizeof(regs[0]));
    if (boot == 0x001C0102)
        printf("  PASS: bootloader=0x%08X — pa_v1 LIVE\n", boot);
    else if (boot != 0xFFFFFFFF && boot != 0)
        printf("  PARTIAL: bootloader=0x%08X (nonzero — window responds)\n", boot);
    else
        printf("  FAIL: bootloader=0x%08X — window dead\n", boot);

    /* Final BAR0 state for diagnosis */
    ULONG bar0f = 0;
    PciRead(bus, df, 0x10, &bar0f);
    printf("  Final BAR0=0x%08X cmd=0x%08X\n", bar0f, cmd);

    CloseHandle(h);
    return (boot != 0xFFFFFFFF && boot != 0) ? 0 : 1;
}
