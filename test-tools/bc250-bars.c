/*
 * bc250-bars - read-only dump of the GPU function's PCI config space, focused on
 * the BARs.
 *
 * Motivation: ring registers are proven writable at 0x34D0/0x32D4 but locked in
 * the 0x89E0 block. A global ACL is ruled out, so the likely explanation is that
 * BAR5 is the wrong aperture for the GC block on this APU. Linux discovery lists
 * three GC base addresses (0x1260, 0xA000, 0x02402C00) and we only ever use the
 * first. If there is another BAR, that is the window worth trying.
 *
 * READ-ONLY: config space is only read.
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

static const char *BarName(uint32_t off) {
    switch (off) {
    case 0x10: return "BAR0";
    case 0x14: return "BAR1";
    case 0x18: return "BAR2";
    case 0x1C: return "BAR3";
    case 0x20: return "BAR4";
    case 0x24: return "BAR5";
    case 0x28: return "BAR6";
    default:   return "?";
    }
}

static void Dump(uint32_t bus, uint32_t dev, uint32_t fn) {
    PCI_CFG pc;
    if (!Cfg(bus, dev, fn, &pc)) {
        printf("B%d:D%d:F%d  read FAILED err=%lu\n", bus, dev, fn, GetLastError());
        return;
    }
    uint16_t ven = pc.ConfigData[0] | (pc.ConfigData[1] << 8);
    uint16_t did = pc.ConfigData[2] | (pc.ConfigData[3] << 8);
    if (ven == 0xFFFF || ven == 0x0000) return;
    printf("\n=== B%d:D%d:F%d  VEN 0x%04X DEV 0x%04X  bytes=%u ===\n",
           bus, dev, fn, ven, did, pc.BytesRead);

    uint16_t cmd = pc.ConfigData[0x04] | (pc.ConfigData[0x05] << 8);
    printf("  command=0x%04X (MEM=%d BUS=%d)  status=0x%04X\n",
           cmd, (cmd >> 1) & 1, (cmd >> 0) & 1,
           pc.ConfigData[0x06] | (pc.ConfigData[0x07] << 8));

    printf("  --- BARs ---\n");
    for (int o = 0x10; o <= 0x24; o += 4) {
        uint32_t v = (uint32_t)pc.ConfigData[o] | ((uint32_t)pc.ConfigData[o+1] << 8) |
                     ((uint32_t)pc.ConfigData[o+2] << 16) | ((uint32_t)pc.ConfigData[o+3] << 24);
        if (v == 0) { printf("  %s @0x%02X = 0x00000000  (unimplemented)\n", BarName(o), o); continue; }
        if (v & 1) {
            uint32_t b = v & ~0x3u;
            uint64_t end = (uint64_t)b + (v & 0x2 ? 512 : 256) - 1;
            printf("  %s @0x%02X = 0x%08X  IO  size=%u  [%08X-%08X]\n",
                   BarName(o), o, v, (v & 0x2) ? 512 : 256, b, (uint32_t)end);
        } else {
            uint32_t b = v & ~0xFu;
            printf("  %s @0x%02X = 0x%08X  MEM size flags=%u  base=0x%08X\n",
                   BarName(o), o, v, v & 0xF, b);
        }
    }

    printf("  --- other useful regs ---\n");
    uint32_t rom = (uint32_t)pc.ConfigData[0x30] | ((uint32_t)pc.ConfigData[0x31] << 8) |
                   ((uint32_t)pc.ConfigData[0x32] << 16) | ((uint32_t)pc.ConfigData[0x33] << 24);
    printf("  ROM BAR    @0x30 = 0x%08X\n", rom);
    uint32_t cls = pc.ConfigData[0x09] << 8 | pc.ConfigData[0x0A];
    printf("  class      @0x09 = 0x%06X\n", cls);
    uint32_t subsys = pc.ConfigData[0x2C] | ((uint32_t)pc.ConfigData[0x2D] << 8) |
                      ((uint32_t)pc.ConfigData[0x2E] << 16) | ((uint32_t)pc.ConfigData[0x2F] << 24);
    printf("  subsystem  @0x2C = 0x%08X\n", subsys);
    printf("  B8/BC (DF SMN port) = %02X%02X%02X%02X / %02X%02X%02X%02X\n",
           pc.ConfigData[0xB8], pc.ConfigData[0xB9], pc.ConfigData[0xBA], pc.ConfigData[0xBB],
           pc.ConfigData[0xBC], pc.ConfigData[0xBD], pc.ConfigData[0xBE], pc.ConfigData[0xBF]);
}

int main(void) {
    g_h = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (g_h == INVALID_HANDLE_VALUE) {
        printf("FAIL: cannot open GPU device (err=%lu)\n", GetLastError());
        return 1;
    }

    printf("=== scanning bus 0 and 1 for AMD display functions ===\n");
    for (uint32_t bus = 0; bus <= 1; bus++)
        for (uint32_t dev = 0; dev < 8; dev++)
            for (uint32_t fn = 0; fn < 4; fn++)
                Dump(bus, dev, fn);

    printf("\nDone (read-only).\n");
    CloseHandle(g_h);
    return 0;
}