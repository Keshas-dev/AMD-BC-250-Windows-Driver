/*
 * bc250-d18f2 - dump the full config space of B0:D24:F2 (1022:13F2).
 *
 * The PS5 Linux loader reaches GPU memory registers through this function's
 * ECAM window rather than through a BAR:
 *     ECAM_B0D18F2 = dmap + (0xF0000000 + 0x18*0x8000 + 2*0x1000)   = 0xF00E2000
 *     kwrite32(ECAM_B0D18F2 + 0x80, addr)   // TMR_INDEX
 *     kwrite32(ECAM_B0D18F2 + 0x84, val)    // TMR_DATA
 *
 * Offsets 0x80/0x84 sit past the 256-byte config space, so on the PS5 this is a
 * vendor window on that function. Worth knowing whether BC-250's copy exposes
 * anything there, and what the class/header/BARs look like.
 *
 * READ-ONLY.
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define IOCTL_READ_PCI_CONFIG 0x80000BAC

typedef struct { uint32_t Bus, Device, Function, BytesRead; uint8_t ConfigData[256]; } PCI_CFG;

int main(void) {
    HANDLE h = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { printf("FAIL: %lu\n", GetLastError()); return 1; }

    PCI_CFG pc; DWORD br = 0;
    memset(&pc, 0, sizeof(pc));
    pc.Bus = 0; pc.Device = 0x18; pc.Function = 2;
    if (!DeviceIoControl(h, IOCTL_READ_PCI_CONFIG, &pc, sizeof(pc), &pc, sizeof(pc), &br, NULL)) {
        printf("read FAILED err=%lu\n", GetLastError());
        CloseHandle(h); return 1;
    }

    printf("B0:D18:F2  VEN %04X DEV %04X  bytes=%u\n",
           pc.ConfigData[0] | (pc.ConfigData[1] << 8),
           pc.ConfigData[2] | (pc.ConfigData[3] << 8), pc.BytesRead);

    printf("  command  = 0x%04X  (MEM=%d)\n",
           pc.ConfigData[4] | (pc.ConfigData[5] << 8), (pc.ConfigData[4] >> 1) & 1);
    printf("  status   = 0x%04X\n", pc.ConfigData[6] | (pc.ConfigData[7] << 8));
    printf("  revision = 0x%02X   prog-if 0x%02X   class 0x%06X\n",
           pc.ConfigData[8], pc.ConfigData[9],
           ((uint32_t)pc.ConfigData[11] << 16) | ((uint32_t)pc.ConfigData[10] << 8) | pc.ConfigData[9]);
    printf("  hdr type = %u  (0=endpoint 1=bridge)\n", pc.ConfigData[0x0E] & 7);

    printf("\n  --- BARs 0x10..0x27 ---\n");
    for (int o = 0x10; o <= 0x24; o += 4) {
        uint32_t v = (uint32_t)pc.ConfigData[o] | ((uint32_t)pc.ConfigData[o+1] << 8) |
                     ((uint32_t)pc.ConfigData[o+2] << 16) | ((uint32_t)pc.ConfigData[o+3] << 24);
        printf("    0x%02X = 0x%08X%s\n", o, v, v ? "" : "   (zero)");
    }

    printf("\n  --- offsets 0x40..0xFF (what the PS5 TMR window would live in) ---\n");
    for (int base = 0x40; base < 0x100; base += 16) {
        int allZero = 1;
        for (int i = 0; i < 16; i++) if (pc.ConfigData[base + i]) allZero = 0;
        if (allZero) continue;
        printf("    0x%02X: ", base);
        for (int i = 0; i < 16; i++) printf("%02X ", pc.ConfigData[base + i]);
        printf("\n");
    }
    printf("\n  --- 0x80/0x84 specifically (PS5 TMR_INDEX / TMR_DATA) ---\n");
    printf("    0x80 = 0x%08X\n", *(uint32_t *)(pc.ConfigData + 0x80));
    printf("    0x84 = 0x%08X\n", *(uint32_t *)(pc.ConfigData + 0x84));

    CloseHandle(h);
    return 0;
}