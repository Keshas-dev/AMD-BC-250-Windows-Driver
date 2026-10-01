/*
 * bc250-scan-d18 - find the ECAM function the PS5 Linux loader writes through.
 *
 * ps5-linux-loader writes GPU TMR registers via ECAM at B0:D18:F2:
 *     #define ECAM_B0D18F2  dmap + (0xF0000000 + 0x18*0x8000 + 2*0x1000)
 *     kwrite32(ECAM_B0D18F2 + TMR_INDEX_OFF, addr);   // 0x80
 *     kwrite32(ECAM_B0D18F2 + TMR_DATA_OFF,  val);    // 0x84
 *
 * That is a third PCI function carrying GPU access, not the BAR5 window we use.
 * Our earlier scan only covered bus 0 devices 0..7 and bus 1 devices 0..7, so
 * device 0x18 (24) was never looked at.
 *
 * READ-ONLY: config space only, no BAR writes (writing a BAR to probe its size
 * can unmap the window everyone else is using).
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define IOCTL_READ_PCI_CONFIG 0x80000BAC

typedef struct { uint32_t Bus, Device, Function, BytesRead; uint8_t ConfigData[256]; } PCI_CFG;

static HANDLE g_h;

static BOOL Cfg(uint32_t bus, uint32_t dev, uint32_t fn, PCI_CFG *pc) {
    DWORD br = 0;
    memset(pc, 0, sizeof(*pc));
    pc->Bus = bus; pc->Device = dev; pc->Function = fn;
    return DeviceIoControl(g_h, IOCTL_READ_PCI_CONFIG, pc, sizeof(*pc),
                           pc, sizeof(*pc), &br, NULL);
}

static void Show(uint32_t bus, uint32_t dev, uint32_t fn) {
    PCI_CFG pc;
    if (!Cfg(bus, dev, fn, &pc)) return;
    uint16_t ven = pc.ConfigData[0] | (pc.ConfigData[1] << 8);
    uint16_t did = pc.ConfigData[2] | (pc.ConfigData[3] << 8);
    if (ven == 0xFFFF || ven == 0x0000) return;

    uint16_t cmd = pc.ConfigData[0x04] | (pc.ConfigData[0x05] << 8);
    uint32_t cls = ((uint32_t)pc.ConfigData[0x0B] << 16) |
                   ((uint32_t)pc.ConfigData[0x0A] << 8) | pc.ConfigData[0x09];

    printf("B%d:D%-2d:F%d  %04X:%04X  class 0x%06X  cmd 0x%04X%s\n",
           bus, dev, fn, ven, did, cls, cmd,
           (cmd & 2) ? "  [MEM enabled]" : "");

    for (int o = 0x10; o <= 0x24; o += 4) {
        uint32_t v = (uint32_t)pc.ConfigData[o] | ((uint32_t)pc.ConfigData[o+1] << 8) |
                     ((uint32_t)pc.ConfigData[o+2] << 16) | ((uint32_t)pc.ConfigData[o+3] << 24);
        if (!v) continue;
        if (v & 1)
            printf("            BAR%d @0x%02X = 0x%08X  IO base 0x%08X\n",
                   (o - 0x10) / 4, o, v, v & ~0x3u);
        else
            printf("            BAR%d @0x%02X = 0x%08X  MEM base 0x%08X (type=%u, pref=%u)\n",
                   (o - 0x10) / 4, o, v, v & ~0xFu, (v >> 1) & 3, (v >> 3) & 1);
    }
    /* Windows assigns resources; bridge windows show up here too. */
    uint16_t hdr = pc.ConfigData[0x0E];
    if ((hdr & 0x7) == 1) {
        printf("            P2P bridge: primary=%02X secondary=%02x subordinate=%02x\n",
               pc.ConfigData[0x18], pc.ConfigData[0x19], pc.ConfigData[0x1A]);
    }
}

int main(void) {
    g_h = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (g_h == INVALID_HANDLE_VALUE) {
        printf("FAIL: cannot open GPU device (err=%lu)\n", GetLastError());
        return 1;
    }

    printf("=== bus 0 device 0x18 (24) - the PS5 loader's target ===\n");
    for (uint32_t fn = 0; fn < 8; fn++) Show(0, 0x18, fn);

    printf("\n=== bus 0 device 0x19 (25) and 0x1A (26) ===\n");
    for (uint32_t fn = 0; fn < 8; fn++) Show(0, 0x19, fn);
    for (uint32_t fn = 0; fn < 8; fn++) Show(0, 0x1A, fn);

    printf("\n=== full bus 0 sweep, all devices/functions, present only ===\n");
    for (uint32_t dev = 0; dev < 32; dev++)
        for (uint32_t fn = 0; fn < 8; fn++)
            Show(0, dev, fn);

    printf("\nDone (read-only).\n");
    CloseHandle(g_h);
    return 0;
}