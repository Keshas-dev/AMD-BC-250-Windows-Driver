/* smn-targeted-pci.c
 *
 * TARGETED (not scan!) PCI config read via GPU driver's SMN window.
 * Zen2 ECAM-in-SMN: addr = 0x04000000 + bus*0x100000 + dev*0x8000 + fn*0x1000
 * We know 143E sits at B01:D00.F02 (from DEVPKEY_LocationInfo).
 *
 * Only 3 single reads — validated step by step. READ-ONLY.
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
    printf("GPU_ID: 0x%08X\n\n", ReadReg(0x0000));

    /* B01:D00.F02 -> ECAM offset */
    uint32_t base = 0x04000000u + (1u << 20) + (0u << 15) + (2u << 12);

    uint32_t vd = SmnRead(base + 0x00);
    printf("Step1: B01:D00.F02 vendor/device @ SMN 0x%08X = 0x%08X", base, vd);
    if (vd == 0x143E1022u) {
        printf("  <-- MATCH! formula works\n");
        uint32_t barLo = SmnRead(base + 0x10);
        uint32_t barHi = SmnRead(base + 0x14);
        uint32_t cmd   = SmnRead(base + 0x04);
        uint64_t bar = barLo & ~0xFULL;
        printf("Step2: CMD     = 0x%08X %s\n", cmd, (cmd & 2) ? "(MEM-ON)" : "(MEM-OFF!)");
        printf("       BAR0 lo = 0x%08X hi = 0x%08X\n", barLo, barHi);
        if (barLo & 0x4) bar |= ((uint64_t)barHi) << 32;
        printf("       BAR0 PA = 0x%llX (%s)\n", bar, (barLo & 0x4) ? "64-bit" : "32-bit");
        printf("\n=> Pass this PA to PSP driver INIT_HW to map the REAL CPU-PSP BAR.\n");
    } else {
        printf("  <- no match (FF=%s). Formula wrong for this SoC or device hidden.\n",
               (vd == 0xFFFFFFFF) ? "unmapped" : "other");
    }
    CloseHandle(g_hDev);
    return 0;
}
