/* smn-pci-scan.c
 *
 * Scans PCI config space via the GPU driver's SMN window (BAR5+0x38/0x3C).
 * On Zen2 APUs the PCIe ECAM is mirrored into SMN at 0x04000000:
 *   SMN = 0x04000000 + (bus << 20) + (device << 15) + (function << 12) + off
 *
 * Goal: find VEN_1022&DEV_143E (CPU-PSP) and dump its BAR0 -> then we can
 * map the REAL CPU-PSP mailbox instead of the stale 0xFD600000.
 *
 * READ-ONLY.
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

#define IOCTL_GPU_READ   0x80000B88
#define IOCTL_GPU_WRITE  0x80000B8C
#define IOCTL_GPU_INIT   0x80000B80

typedef struct { UINT32 RegisterOffset; UINT32 Value; } REG_IO;
static HANDLE g_hDev = INVALID_HANDLE_VALUE;

static BOOL WriteReg(uint32_t o, uint32_t v) {
    REG_IO r; DWORD ret = 0; r.RegisterOffset = o; r.Value = v;
    return DeviceIoControl(g_hDev, IOCTL_GPU_WRITE, &r, sizeof(r), &r, sizeof(r), &ret, NULL);
}
static uint32_t ReadReg(uint32_t o) {
    REG_IO r; DWORD ret = 0; r.RegisterOffset = o; r.Value = 0;
    if (DeviceIoControl(g_hDev, IOCTL_GPU_READ, &r, sizeof(r), &r, sizeof(r), &ret, NULL)) return r.Value;
    return 0xFFFFFFFF;
}
static uint32_t SmnRead(uint32_t a) { WriteReg(0x38, a); ReadReg(0x38); return ReadReg(0x3C); }

static uint32_t PciSmn(uint32_t b, uint32_t d, uint32_t f, uint32_t off) {
    return SmnRead(0x04000000u | (b << 20) | (d << 15) | (f << 12) | (off & ~3u));
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    g_hDev = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (g_hDev == INVALID_HANDLE_VALUE) { printf("FAIL open GPU drv\n"); return 1; }

    typedef struct { UINT64 MmioPhysicalBase; UINT32 MmioSize; UINT32 Flags;
                     UINT64 FbPhysicalBase; UINT32 FbSize; } INIT_HW;
    INIT_HW ih; DWORD ret = 0;
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL; ih.MmioSize = 0x80000; ih.Flags = 1;
    DeviceIoControl(g_hDev, IOCTL_GPU_INIT, &ih, sizeof(ih), &ih, sizeof(ih), &ret, NULL);

    /* Sanity: host bridge at B0:D0:F0 should be a real device (AMD root) */
    uint32_t vd0 = PciSmn(0, 0, 0, 0x00);
    printf("B00:D00.F0 vendor/device = 0x%08X %s\n", vd0,
           ((vd0 & 0xFFFF) == 0x1022) ? "(AMD root - SMN PCI WORKS)" : "(formula may be wrong)");

    printf("\nScanning buses 0-31 for 1022:143E...\n");
    int found = 0, devs = 0;
    for (uint32_t b = 0; b < 32 && !found; b++) {
        for (uint32_t d = 0; d < 32 && !found; d++) {
            for (uint32_t f = 0; f < 8; f++) {
                uint32_t vd = PciSmn(b, d, f, 0x00);
                if (vd == 0xFFFFFFFF || vd == 0 || vd == 0xFFFFFFFE) continue;
                if (b == 0 && f == 0 && d < 2) { /* print first few for sanity */ }
                devs++;
                if ((vd & 0xFFFF) == 0x1022 && (vd >> 16) == 0x143E) {
                    uint32_t barLo = PciSmn(b, d, f, 0x10);
                    uint32_t barHi = PciSmn(b, d, f, 0x14);
                    uint32_t cmd   = PciSmn(b, d, f, 0x04);
                    uint64_t bar = barLo & ~0xFULL;
                    if (barLo & 0x4) bar |= ((uint64_t)(barHi)) << 32;
                    printf("\nFOUND 143E at B%02u:D%02u.F%u\n", b, d, f);
                    printf("  CMD     = 0x%08X %s\n", cmd, (cmd & 2) ? "(MEM-ON)" : "(MEM-OFF!)");
                    printf("  BAR0 lo = 0x%08X  hi = 0x%08X\n", barLo, barHi);
                    printf("  BAR0 PA = 0x%llX (%s)\n", bar, (barLo & 0x4) ? "64-bit" : "32-bit");
                    printf("\n  vs old hardcoded 0xFD600000: %s\n",
                           (bar == 0xFD600000ULL) ? "same" : "DIFFERENT -> that's why BAR0 reads FF!");
                    found = 1;
                    break;
                }
            }
        }
    }
    printf("\nTotal live functions seen: %d\n", devs);
    if (!found) printf("143E not found — SMN PCI formula likely wrong for this SoC.\n");
    CloseHandle(g_hDev);
    return 0;
}
