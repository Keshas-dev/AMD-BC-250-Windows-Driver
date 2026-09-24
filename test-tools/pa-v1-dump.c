/* pa-v1-dump.c — READ-ONLY dump of PSP BAR2 pa_v1 region via IOCTL_PSP_READ_REG.
 * BAR2 1MB window (1022:143E BAR2 @ fe700000). Offsets from Linux pspv_bc250
 * (Mattia Tadini) + our pa_v1-diag2 readings. NO WRITES anywhere. */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "..\inc\PspIoctl.h"

static HANDLE h = INVALID_HANDLE_VALUE;

static int bar0_read(uint32_t off, uint32_t *out) {
    DWORD br = 0;
    ULONG in = off, val = 0xDEADBEEF;
    if (!DeviceIoControl(h, IOCTL_PSP_READ_REG, &in, sizeof(in),
                         &val, sizeof(val), &br, NULL))
        return 0;
    *out = val;
    return 1;
}

int main(void) {
    int i;
    uint32_t v;
    /* {offset, label} */
    static const struct { uint32_t off; const char *name; } regs[] = {
        {0x10544, "cmdresp (linux)"},
        {0x10570, "0x10570 (ours=0x80000000?)"},
        {0x10574, "0x10574"},
        {0x10578, "0x10578"},
        {0x10690, "inten"},
        {0x10694, "intsts"},
        {0x10900, "C2PMSG_0?"},
        {0x10970, "C2PMSG_28 (pa_v1 mbox0?)"},
        {0x10974, "C2PMSG_29 (pa_v1 mbox1?)"},
        {0x10978, "C2PMSG_30 (pa_v1 mbox2?)"},
        {0x1097C, "C2PMSG_31"},
        {0x109EC, "bootloader (C2PMSG_59)"},
        {0x109FC, "feature (C2PMSG_63)"},
        {0x10A24, "doorbell0"},
        {0x10A40, "doorbell1"},
    };

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== pa_v1 BAR2 dump (READ-ONLY) ===\n\n");

    h = CreateFileA("\\\\.\\AmdBcPsp", GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("FAIL: CreateFile AmdBcPsp gle=%lu\n", GetLastError());
        return 1;
    }

    for (i = 0; i < (int)(sizeof(regs) / sizeof(regs[0])); i++) {
        if (bar0_read(regs[i].off, &v))
            printf("BAR2+0x%05X = 0x%08X  (%s)\n", regs[i].off, v, regs[i].name);
        else
            printf("BAR2+0x%05X = READ FAIL gle=%lu (%s)\n",
                   regs[i].off, GetLastError(), regs[i].name);
    }

    CloseHandle(h);
    printf("\nDone. No writes performed.\n");
    return 0;
}
