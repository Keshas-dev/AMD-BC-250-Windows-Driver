/* pa-v1-diag.c — diagnose why pa_v1 BAR0 reads 0xFFFFFFFF.
 *
 * Steps (single process, ordered):
 *  1. Scan PCI bus 0..3 for VEN_1022 DEV_143E
 *  2. Print Command + BAR0/BAR1 (64-bit aware)
 *  3. Enable IO+Memory+BM (cmd |= 7) if needed
 *  4. INIT_HW with discovered BAR0 (size 1MB per LKML, not 256KB)
 *  5. Re-read pa_v1 regs (bootloader 0x109EC smoking gun)
 *  6. Also read BAR0+0x0 / +0x100 for window liveness sanity
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

    /* 1. Scan bus 0..3 for 1022:143E. devFn encoding = (slot<<3)|func (Linux-style,
     * matches IOCTL_PSP_PCI_READ which re-encodes to HalGetBusData (slot<<5)|func). */
    printf("=== PCI scan 1022:143E (bus 0..3) ===\n");
    ULONG foundBus = 0xFFFFFFFF, foundDf = 0xFFFFFFFF;
    for (ULONG bus = 0; bus <= 3; bus++) {
        for (ULONG slot = 0; slot < 32; slot++) {
            for (ULONG func = 0; func < 8; func++) {
                ULONG df = (slot << 3) | func;
                ULONG id = 0;
                if (PciRead(bus, df, 0x00, &id) != 0) continue;
                if ((id & 0xFFFF) == 0x1022 && ((id >> 16) & 0xFFFF) == 0x143E) {
                    printf("  FOUND B%u.D%u.F%u id=0x%08X\n", bus, slot, func, id);
                    foundBus = bus;
                    foundDf = df;
                }
            }
        }
    }
    if (foundBus == 0xFFFFFFFF) {
        printf("  NOT FOUND on bus 0..3 — window will stay dead\n");
        CloseHandle(h);
        return 1;
    }

    ULONG slot = (foundDf >> 3) & 0x1F;
    ULONG func = foundDf & 7;

    /* 2. Command + BARs */
    ULONG cmd = 0, bar0 = 0, bar1 = 0;
    PciRead(foundBus, foundDf, 0x04, &cmd);
    PciRead(foundBus, foundDf, 0x10, &bar0);
    PciRead(foundBus, foundDf, 0x14, &bar1);
    printf("\n=== Config ===\n");
    printf("  Command = 0x%08X (IO=%d Mem=%d BM=%d)\n",
           cmd, (cmd & 1) ? 1 : 0, (cmd & 2) ? 1 : 0, (cmd & 4) ? 1 : 0);
    printf("  BAR0    = 0x%08X  BAR1(high)=0x%08X\n", bar0, bar1);

    ULONG barType = bar0 & 7;
    ULONG64 bar0Phys = 0;
    if ((barType & 6) == 4) { /* 64-bit */
        bar0Phys = ((ULONG64)bar1 << 32) | (bar0 & ~0xFU);
        printf("  BAR0 is 64-bit → PA=0x%llX\n", bar0Phys);
    } else {
        bar0Phys = bar0 & ~0xFU;
        printf("  BAR0 is 32-bit → PA=0x%llX\n", bar0Phys);
    }
    if (bar0 == 0 || bar0 == 0xFFFFFFFF) {
        printf("FAIL: BAR0 not programmed\n");
        CloseHandle(h);
        return 1;
    }

    /* 3. Enable memory if needed */
    if (!(cmd & 2)) {
        printf("  Memory DISABLED — enabling (cmd|=7)\n");
        if (PciWrite(foundBus, foundDf, 0x04, cmd | 7) != 0) {
            printf("  PCI_WRITE failed gle=%lu\n", GetLastError());
        }
        Sleep(50);
        ULONG cmd2 = 0;
        PciRead(foundBus, foundDf, 0x04, &cmd2);
        printf("  Command now = 0x%08X\n", cmd2);
    } else {
        printf("  Memory already enabled\n");
    }

    /* 4. Remap via INIT_HW with real BAR0 + 1MB (LKML window) */
    printf("\n=== INIT_HW PA=0x%llX size=0x100000 ===\n", bar0Phys);
    if (InitHw(bar0Phys, 0x100000) != 0) {
        printf("  INIT_HW FAIL gle=%lu\n", GetLastError());
    }

    /* 5. Sanity: low BAR0 + pa_v1 smoking gun */
    printf("\n=== Reads after remap ===\n");
    static const struct { ULONG off; const char* name; } regs[] = {
        { 0x00000, "BAR0+0x0 (sanity)" },
        { 0x00100, "BAR0+0x100 CCP ver (Linux=FFFFFFFF ok)" },
        { 0x10570, "cmdresp C2PMSG_28" },
        { 0x10574, "cmdbuff_lo C2PMSG_29" },
        { 0x10578, "cmdbuff_hi C2PMSG_30" },
        { 0x10690, "inten P2CMSG_INTEN" },
        { 0x10694, "intsts P2CMSG_INTSTS" },
        { 0x109EC, "bootloader_info C2PMSG_59" },
        { 0x109FC, "feature_reg C2PMSG_63" },
        { 0x10A24, "doorbell_button C2PMSG_73" },
        { 0x10A40, "doorbell_cmd C2PMSG_80" },
    };
    int live = 0;
    ULONG boot = 0;
    for (size_t i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
        ULONG v = 0;
        if (MmioRead(regs[i].off, &v) != 0) {
            printf("  0x%05X %-32s IOCTL FAIL gle=%lu\n", regs[i].off, regs[i].name, GetLastError());
            continue;
        }
        printf("  0x%05X %-32s = 0x%08X%s\n", regs[i].off, regs[i].name, v,
               (v != 0xFFFFFFFF) ? "  <<< LIVE" : "");
        if (v != 0xFFFFFFFF) live++;
        if (regs[i].off == 0x109EC) boot = v;
    }

    printf("\n=== Verdict ===\n");
    printf("  non-FF regs: %d/%zu\n", live, sizeof(regs) / sizeof(regs[0]));
    if (boot == 0x001C0102)
        printf("  PASS: bootloader=0x%08X — pa_v1 LIVE\n", boot);
    else if (boot != 0xFFFFFFFF && boot != 0)
        printf("  PARTIAL: bootloader=0x%08X (nonzero)\n", boot);
    else
        printf("  FAIL: bootloader=0x%08X — window still dead\n", boot);

    CloseHandle(h);
    return (boot != 0xFFFFFFFF && boot != 0) ? 0 : 1;
}
