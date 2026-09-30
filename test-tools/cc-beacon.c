/*
 * cc-beacon.c - read the current value of CC_GC_SHADER_ARRAY_CONFIG through
 * BAR5, so it can be used as a distinctive "beacon" for a read-only SMN scan.
 *
 * WHY
 * ---
 * The SMU Q3 0x98 path (ungated SMN write) cannot be used to SEARCH for the
 * SPI_PG alias: it returns only OK/FAIL, writes a fixed 0x00FF, and every write
 * risks wedging the SMU on an unrelated register (that message has no bounds
 * check). A blind write gives no signal about whether it hit anything.
 *
 * CC_GC_SHADER_ARRAY_CONFIG is the one GC register confirmed host-writable on
 * this board (BAR5 0x9C1C): writes land, with the top bits masked, and the value
 * persists. The driver already writes 0xFFE00000 during init
 * (amdbc250_dream_kmd.c:932, :4571), so a distinctive value is very likely
 * ALREADY sitting in the register with no write needed from this tool.
 *
 * That makes it a beacon: if the GC block has an SMN alias, a read-only scan of
 * SMN space will find this exact value at the alias address, from which the
 * SPI_PG alias follows by offset arithmetic (SPI_PG is 0x5C3C, CC_ARRAY is
 * 0x9C1C, so SPI_PG_alias = CC_ARRAY_alias - 0x3FE0).
 *
 * This tool only READS. Usage: Administrator, atikmdag loaded, MMIO mapped.
 */

#include <windows.h>
#include <stdio.h>
#include "..\\inc\\amdbc250_ioctl.h"

#define DEV L"\\\\.\\AMDBC250DreamV43"

#define CC_ARRAY_OFF 0x9C1C
#define SPI_PG_OFF   0x5C3C
#define RLC_PG_OFF   0x3D64
#define GRBM_OFF     0x3260
#define SCRATCH_OFF  0x32D4

static HANDLE g_dev = INVALID_HANDLE_VALUE;

static int read_reg(UINT32 off, UINT32 *value)
{
    AMDBC250_IOCTL_REG_ACCESS req;
    DWORD returned = 0;

    memset(&req, 0, sizeof(req));
    req.RegisterOffset = off;
    if (!DeviceIoControl(g_dev, IOCTL_AMDBC250_READ_REG,
                         &req, sizeof(req), &req, sizeof(req),
                         &returned, NULL)) {
        return 0;
    }
    *value = req.Value;
    return 1;
}

int main(void)
{
    struct { const char *name; UINT32 off; } regs[] = {
        { "GRBM_STATUS            (control, expect 0)",        GRBM_OFF },
        { "SCRATCH                (control, expect 0x4D585042)", SCRATCH_OFF },
        { "CC_GC_SHADER_ARRAY_CONFIG  <== BEACON",             CC_ARRAY_OFF },
        { "SPI_PG_ENABLE_STATIC_WGP_MASK",                    SPI_PG_OFF },
        { "RLC_PG_ALWAYS_ON_WGP_MASK",                        RLC_PG_OFF },
    };
    UINT32 i, beacon = 0;
    int haveBeacon = 0;

    printf("=== CC_ARRAY beacon value (READ ONLY) ===\n\n");

    g_dev = CreateFileW(DEV, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_dev == INVALID_HANDLE_VALUE) {
        printf("FATAL: cannot open device (err %lu) - is atikmdag loaded?\n",
               GetLastError());
        return 2;
    }

    for (i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
        UINT32 v = 0;
        if (!read_reg(regs[i].off, &v)) {
            printf("  BAR5 +0x%04X  %-46s READ FAILED (err %lu)\n",
                   regs[i].off, regs[i].name, GetLastError());
            continue;
        }
        printf("  BAR5 +0x%04X  %-46s = 0x%08X\n",
               regs[i].off, regs[i].name, v);
        if (regs[i].off == CC_ARRAY_OFF) {
            beacon = v;
            haveBeacon = 1;
        }
    }

    if (!haveBeacon) {
        printf("\nFATAL: could not read CC_ARRAY, no beacon.\n");
        CloseHandle(g_dev);
        return 2;
    }

    printf("\nBeacon value for the SMN scan: 0x%08X\n", beacon);

    if (beacon == 0 || beacon == 0xFFFFFFFFu) {
        printf("WARNING: the beacon is %s. A zero or unmapped beacon will match\n"
               "        thousands of unrelated registers, so a scan would be\n"
               "        meaningless. CC_ARRAY is expected to hold 0x01F000000\n"
               "        after the driver's init writes 0xFFE00000 (top 3 bits\n"
               "        are masked by hardware). If this reads 0, the board was\n"
               "        only NBIO_MAP-initialised and the driver init never ran -\n"
               "        run full-init-test.exe first.\n",
               beacon == 0 ? "ZERO" : "UNMAPPED");
    } else {
        printf("This value is distinctive. A read-only SMN scan looking for\n"
               "0x%08X will reveal the GC block's SMN alias if one exists;\n"
               "SPI_PG_alias would then be that address - 0x%04X.\n",
               beacon, CC_ARRAY_OFF - SPI_PG_OFF);
    }

    CloseHandle(g_dev);
    return 0;
}
