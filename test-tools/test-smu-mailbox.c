#include <windows.h>
#include <stdio.h>
#include "amdbc250_ioctl.h"

static FILE *g_log = NULL;

static void Log(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);
    fflush(stdout);
    if (g_log) {
        va_start(args, fmt);
        vfprintf(g_log, fmt, args);
        va_end(args);
        fflush(g_log);
    }
}

static UINT32 MmioRead(HANDLE h, UINT64 pa) {
    AMDBC250_IOCTL_MMIO_TEST m = {0};
    m.PhysicalAddress = pa; m.Size = 4; m.OffsetRead = 0;
    DWORD bytes = 0;
    DeviceIoControl(h, IOCTL_AMDBC250_MMIO_TEST, &m, sizeof(m), &m, sizeof(m), &bytes, NULL);
    return m.MapResult ? m.ValueRead : 0xDEAD0000;
}

int main(void) {
    g_log = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\smu-probe.log", "w");
    if (!g_log) { printf("Cannot open log\n"); return 1; }

    Log("=== SAFE Probe v3 Start ===\n\n");

    HANDLE hDev = CreateFileA("\\\\.\\AMDBC250DreamV43",
        GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hDev == INVALID_HANDLE_VALUE) {
        Log("FATAL: Cannot open driver (error %lu)\n", GetLastError());
        fclose(g_log);
        return 1;
    }
    Log("Device opened OK\n\n");

    /* SECTION 1: GPU BAR5 +0x0000-0x0FFF (known safe) */
    Log("=== S1: BAR5 +0x000-0xFFF ===\n");
    for (UINT64 off = 0; off < 0x1000; off += 4) {
        UINT32 v = MmioRead(hDev, 0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000) Log("  +0x%04X: 0x%08X\n", (UINT32)off, v);
    }
    Log("S1 DONE\n\n");

    /* SECTION 2: GPU BAR5 +0x1000-0x1FFF */
    Log("=== S2: BAR5 +0x1000-0x1FFF ===\n");
    for (UINT64 off = 0x1000; off < 0x2000; off += 4) {
        UINT32 v = MmioRead(hDev, 0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000) Log("  +0x%04X: 0x%08X\n", (UINT32)off, v);
    }
    Log("S2 DONE\n\n");

    /* SECTION 3: GPU BAR5 +0x2000-0x2FFF */
    Log("=== S3: BAR5 +0x2000-0x2FFF ===\n");
    for (UINT64 off = 0x2000; off < 0x3000; off += 4) {
        UINT32 v = MmioRead(hDev, 0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000) Log("  +0x%04X: 0x%08X\n", (UINT32)off, v);
    }
    Log("S3 DONE\n\n");

    /* SECTION 4: GPU BAR5 +0x3000-0x3FFF */
    Log("=== S4: BAR5 +0x3000-0x3FFF ===\n");
    for (UINT64 off = 0x3000; off < 0x4000; off += 4) {
        UINT32 v = MmioRead(hDev, 0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000) Log("  +0x%04X: 0x%08X\n", (UINT32)off, v);
    }
    Log("S4 DONE\n\n");

    /* SECTION 5: GPU BAR5 +0x4000-0x4FFF */
    Log("=== S5: BAR5 +0x4000-0x4FFF ===\n");
    for (UINT64 off = 0x4000; off < 0x5000; off += 4) {
        UINT32 v = MmioRead(hDev, 0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000) Log("  +0x%04X: 0x%08X\n", (UINT32)off, v);
    }
    Log("S5 DONE\n\n");

    /* SECTION 6: GPU BAR5 +0x5000-0x5FFF */
    Log("=== S6: BAR5 +0x5000-0x5FFF ===\n");
    for (UINT64 off = 0x5000; off < 0x6000; off += 4) {
        UINT32 v = MmioRead(hDev, 0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000) Log("  +0x%04X: 0x%08X\n", (UINT32)off, v);
    }
    Log("S6 DONE\n\n");

    /* SECTION 7: GPU BAR5 +0x6000-0x6FFF */
    Log("=== S7: BAR5 +0x6000-0x6FFF ===\n");
    for (UINT64 off = 0x6000; off < 0x7000; off += 4) {
        UINT32 v = MmioRead(hDev, 0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000) Log("  +0x%04X: 0x%08X\n", (UINT32)off, v);
    }
    Log("S7 DONE\n\n");

    /* SECTION 8: GPU BAR5 +0x7000-0x7FFF */
    Log("=== S8: BAR5 +0x7000-0x7FFF ===\n");
    for (UINT64 off = 0x7000; off < 0x8000; off += 4) {
        UINT32 v = MmioRead(hDev, 0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000) Log("  +0x%04X: 0x%08X\n", (UINT32)off, v);
    }
    Log("S8 DONE\n\n");

    /* SECTION 9: GPU BAR5 +0x8000-0x8FFF */
    Log("=== S9: BAR5 +0x8000-0x8FFF ===\n");
    for (UINT64 off = 0x8000; off < 0x9000; off += 4) {
        UINT32 v = MmioRead(hDev, 0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000) Log("  +0x%04X: 0x%08X\n", (UINT32)off, v);
    }
    Log("S9 DONE\n\n");

    /* SECTION 10: GPU BAR5 +0x9000-0x9FFF */
    Log("=== S10: BAR5 +0x9000-0x9FFF ===\n");
    for (UINT64 off = 0x9000; off < 0xA000; off += 4) {
        UINT32 v = MmioRead(hDev, 0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000) Log("  +0x%04X: 0x%08X\n", (UINT32)off, v);
    }
    Log("S10 DONE\n\n");

    /* SECTION 11: GPU BAR5 +0xA000-0xAFFF */
    Log("=== S11: BAR5 +0xA000-0xAFFF ===\n");
    for (UINT64 off = 0xA000; off < 0xB000; off += 4) {
        UINT32 v = MmioRead(hDev, 0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000) Log("  +0x%04X: 0x%08X\n", (UINT32)off, v);
    }
    Log("S11 DONE\n\n");

    /* SECTION 12: GPU BAR5 +0xB000-0xBFFF */
    Log("=== S12: BAR5 +0xB000-0xBFFF ===\n");
    for (UINT64 off = 0xB000; off < 0xC000; off += 4) {
        UINT32 v = MmioRead(hDev, 0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000) Log("  +0x%04X: 0x%08X\n", (UINT32)off, v);
    }
    Log("S12 DONE\n\n");

    /* SECTION 13: GPU BAR5 +0xC000-0xCFFF */
    Log("=== S13: BAR5 +0xC000-0xCFFF ===\n");
    for (UINT64 off = 0xC000; off < 0xD000; off += 4) {
        UINT32 v = MmioRead(hDev, 0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000) Log("  +0x%04X: 0x%08X\n", (UINT32)off, v);
    }
    Log("S13 DONE\n\n");

    /* SECTION 14: GPU BAR5 +0xD000-0xDFFF */
    Log("=== S14: BAR5 +0xD000-0xDFFF ===\n");
    for (UINT64 off = 0xD000; off < 0xE000; off += 4) {
        UINT32 v = MmioRead(hDev, 0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000) Log("  +0x%04X: 0x%08X\n", (UINT32)off, v);
    }
    Log("S14 DONE\n\n");

    /* SECTION 15: GPU BAR5 +0xE000-0xEFFF */
    Log("=== S15: BAR5 +0xE000-0xEFFF ===\n");
    for (UINT64 off = 0xE000; off < 0xF000; off += 4) {
        UINT32 v = MmioRead(hDev, 0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000) Log("  +0x%04X: 0x%08X\n", (UINT32)off, v);
    }
    Log("S15 DONE\n\n");

    /* SECTION 16: GPU BAR5 +0xF000-0xFFFF */
    Log("=== S16: BAR5 +0xF000-0xFFFF ===\n");
    for (UINT64 off = 0xF000; off < 0x10000; off += 4) {
        UINT32 v = MmioRead(hDev, 0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000) Log("  +0x%04X: 0x%08X\n", (UINT32)off, v);
    }
    Log("S16 DONE\n\n");

    /* SECTION 17: NBIO 0xFEB00000 page (safe) */
    Log("=== S17: NBIO 0xFEB00000 page ===\n");
    for (UINT64 off = 0; off < 0x1000; off += 4) {
        UINT32 v = MmioRead(hDev, 0xFEB00000 + off);
        if (v != 0 && v != 0xDEAD0000) Log("  +0x%04X: 0x%08X\n", (UINT32)off, v);
    }
    Log("S17 DONE\n\n");

    /* SECTION 18: GPU BAR5 +0x10000-0x10FFF */
    Log("=== S18: BAR5 +0x10000-0x10FFF ===\n");
    for (UINT64 off = 0x10000; off < 0x11000; off += 4) {
        UINT32 v = MmioRead(hDev, 0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000) Log("  +0x%05X: 0x%08X\n", (UINT32)off, v);
    }
    Log("S18 DONE\n\n");

    /* SECTION 19: GPU BAR5 +0x11000-0x11FFF */
    Log("=== S19: BAR5 +0x11000-0x11FFF ===\n");
    for (UINT64 off = 0x11000; off < 0x12000; off += 4) {
        UINT32 v = MmioRead(hDev, 0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000) Log("  +0x%05X: 0x%08X\n", (UINT32)off, v);
    }
    Log("S19 DONE\n\n");

    /* SECTION 20: GPU BAR5 +0x12000-0x12FFF */
    Log("=== S20: BAR5 +0x12000-0x12FFF ===\n");
    for (UINT64 off = 0x12000; off < 0x13000; off += 4) {
        UINT32 v = MmioRead(hDev, 0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000) Log("  +0x%05X: 0x%08X\n", (UINT32)off, v);
    }
    Log("S20 DONE\n\n");

    /* SECTION 21: GPU BAR5 +0x13000-0x13FFF */
    Log("=== S21: BAR5 +0x13000-0x13FFF ===\n");
    for (UINT64 off = 0x13000; off < 0x14000; off += 4) {
        UINT32 v = MmioRead(hDev, 0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000) Log("  +0x%05X: 0x%08X\n", (UINT32)off, v);
    }
    Log("S21 DONE\n\n");

    /* SECTION 22: GPU BAR5 +0x14000-0x14FFF */
    Log("=== S22: BAR5 +0x14000-0x14FFF ===\n");
    for (UINT64 off = 0x14000; off < 0x15000; off += 4) {
        UINT32 v = MmioRead(hDev, 0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000) Log("  +0x%05X: 0x%08X\n", (UINT32)off, v);
    }
    Log("S22 DONE\n\n");

    /* SECTION 23: GPU BAR5 +0x15000-0x15FFF */
    Log("=== S23: BAR5 +0x15000-0x15FFF ===\n");
    for (UINT64 off = 0x15000; off < 0x16000; off += 4) {
        UINT32 v = MmioRead(hDev, 0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000) Log("  +0x%05X: 0x%08X\n", (UINT32)off, v);
    }
    Log("S23 DONE\n\n");

    /* SECTION 24: GPU BAR5 +0x16000-0x16FFF */
    Log("=== S24: BAR5 +0x16000-0x16FFF ===\n");
    for (UINT64 off = 0x16000; off < 0x17000; off += 4) {
        UINT32 v = MmioRead(hDev, 0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000) Log("  +0x%05X: 0x%08X\n", (UINT32)off, v);
    }
    Log("S24 DONE\n\n");

    /* SECTION 25: GPU BAR5 +0x17000-0x17FFF */
    Log("=== S25: BAR5 +0x17000-0x17FFF ===\n");
    for (UINT64 off = 0x17000; off < 0x18000; off += 4) {
        UINT32 v = MmioRead(hDev, 0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000) Log("  +0x%05X: 0x%08X\n", (UINT32)off, v);
    }
    Log("S25 DONE\n\n");

    /* SECTION 26: GPU BAR5 +0x18000-0x18FFF */
    Log("=== S26: BAR5 +0x18000-0x18FFF ===\n");
    for (UINT64 off = 0x18000; off < 0x19000; off += 4) {
        UINT32 v = MmioRead(hDev, 0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000) Log("  +0x%05X: 0x%08X\n", (UINT32)off, v);
    }
    Log("S26 DONE\n\n");

    /* SECTION 27: GPU BAR5 +0x19000-0x19FFF */
    Log("=== S27: BAR5 +0x19000-0x19FFF ===\n");
    for (UINT64 off = 0x19000; off < 0x1A000; off += 4) {
        UINT32 v = MmioRead(hDev, 0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000) Log("  +0x%05X: 0x%08X\n", (UINT32)off, v);
    }
    Log("S27 DONE\n\n");

    /* SECTION 28: GPU BAR5 +0x1A000-0x1AFFF */
    Log("=== S28: BAR5 +0x1A000-0x1AFFF ===\n");
    for (UINT64 off = 0x1A000; off < 0x1B000; off += 4) {
        UINT32 v = MmioRead(hDev, 0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000) Log("  +0x%05X: 0x%08X\n", (UINT32)off, v);
    }
    Log("S28 DONE\n\n");

    /* SECTION 29: GPU BAR5 +0x1B000-0x1BFFF */
    Log("=== S29: BAR5 +0x1B000-0x1BFFF ===\n");
    for (UINT64 off = 0x1B000; off < 0x1C000; off += 4) {
        UINT32 v = MmioRead(hDev, 0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000) Log("  +0x%05X: 0x%08X\n", (UINT32)off, v);
    }
    Log("S29 DONE\n\n");

    /* SECTION 30: GPU BAR5 +0x1C000-0x1CFFF */
    Log("=== S30: BAR5 +0x1C000-0x1CFFF ===\n");
    for (UINT64 off = 0x1C000; off < 0x1D000; off += 4) {
        UINT32 v = MmioRead(hDev, 0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000) Log("  +0x%05X: 0x%08X\n", (UINT32)off, v);
    }
    Log("S30 DONE\n\n");

    /* SECTION 31: GPU BAR5 +0x1D000-0x1DFFF */
    Log("=== S31: BAR5 +0x1D000-0x1DFFF ===\n");
    for (UINT64 off = 0x1D000; off < 0x1E000; off += 4) {
        UINT32 v = MmioRead(hDev, 0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000) Log("  +0x%05X: 0x%08X\n", (UINT32)off, v);
    }
    Log("S31 DONE\n\n");

    /* SECTION 32: GPU BAR5 +0x1E000-0x1EFFF */
    Log("=== S32: BAR5 +0x1E000-0x1EFFF ===\n");
    for (UINT64 off = 0x1E000; off < 0x1F000; off += 4) {
        UINT32 v = MmioRead(hDev, 0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000) Log("  +0x%05X: 0x%08X\n", (UINT32)off, v);
    }
    Log("S32 DONE\n\n");

    /* SECTION 33: GPU BAR5 +0x1F000-0x1FFFF */
    Log("=== S33: BAR5 +0x1F000-0x1FFFF ===\n");
    for (UINT64 off = 0x1F000; off < 0x20000; off += 4) {
        UINT32 v = MmioRead(hDev, 0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000) Log("  +0x%05X: 0x%08X\n", (UINT32)off, v);
    }
    Log("S33 DONE\n\n");

    CloseHandle(hDev);
    Log("=== ALL DONE ===\n");
    fclose(g_log);
    printf("Done.\n");
    return 0;
}
