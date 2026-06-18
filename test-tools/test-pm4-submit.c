/* test-pm4-submit.c — Probe CP registers + writability + ME unhalt */
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

static FILE *g = NULL;
static void Log(const char *fmt, ...) {
    va_list a; va_start(a, fmt); vfprintf(stdout, fmt, a); va_end(a);
    if (g) { va_start(a, fmt); vfprintf(g, fmt, a); va_end(a); }
}

static BOOL ReadReg(HANDLE h, unsigned offset, unsigned *val) {
    unsigned ra[2] = {offset, 0xDEADBEEF};
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, 0x80000B88, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
    if (ok) *val = ra[1]; else *val = 0xDEADBEEF;
    return ok;
}

static BOOL WriteReg(HANDLE h, unsigned offset, unsigned val) {
    unsigned ra[2] = {offset, val};
    DWORD br = 0;
    return DeviceIoControl(h, 0x80000B8C, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
}

static void ProbeWrite(HANDLE h, unsigned off, unsigned val, const char *name) {
    unsigned before, after;
    ReadReg(h, off, &before);
    WriteReg(h, off, val);
    ReadReg(h, off, &after);
    const char *st = (after == val) ? "OK" : (after == before ? "NO_CHANGE" : "PARTIAL");
    Log("  %s [0x%04X]: 0x%08X -> w:0x%08X -> r:0x%08X %s\n",
        name, off, before, val, after, st);
}

int main(void) {
    g = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\test-pm4-submit.log", "w");
    Log("=== PM4 Register Probe ===\n\n");

    HANDLE h = CreateFileW(L"\\\\.\\AMDBC250DreamV43",
        GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        Log("Cannot open device error=%lu\n", GetLastError());
        if (g) fclose(g); return 1;
    }

    /* INIT_HARDWARE */
    UCHAR initIn[32] = {0}; DWORD br = 0;
    *(unsigned __int64*)(initIn + 0) = 0xFE800000ULL;
    *(unsigned*)(initIn + 8) = 0x00080000;
    *(unsigned*)(initIn + 12) = 1;
    *(unsigned __int64*)(initIn + 16) = 0xC0000000ULL;
    *(unsigned*)(initIn + 24) = 0x10000000;
    DeviceIoControl(h, 0x80000B80, initIn, sizeof(initIn), NULL, 0, &br, NULL);

    unsigned val;
    ReadReg(h, 0x3260, &val);
    Log("GRBM_STATUS = 0x%08X\n\n", val);

    /* === 1. Probe GC CP ring registers (0x3360+) === */
    Log("--- 1. GC CP ring (0x3360-0x33C0) ---\n");
    for (unsigned off = 0x3360; off <= 0x33C0; off += 4) {
        ReadReg(h, off, &val);
        if (val != 0xFFFFFFFF)
            Log("  [0x%04X] = 0x%08X\n", off, val);
    }

    /* === 2. Probe NBIO CP ring (0xC100+) === */
    Log("\n--- 2. NBIO CP ring (0xC100-0xC180) ---\n");
    for (unsigned off = 0xC100; off <= 0xC180; off += 4) {
        ReadReg(h, off, &val);
        if (val != 0xFFFFFFFF)
            Log("  [0x%04X] = 0x%08X\n", off, val);
    }

    /* === 3. Probe GC doorbell (0xDA60-0xDB00) === */
    Log("\n--- 3. GC doorbell (0xDA60-0xDB00) ---\n");
    for (unsigned off = 0xDA60; off <= 0xDB00; off += 4) {
        ReadReg(h, off, &val);
        if (val != 0xFFFFFFFF && val != 0x00000000)
            Log("  [0x%04X] = 0x%08X\n", off, val);
    }

    /* === 4. Probe HQD/KIQ (0xDAC0-0xDB40) === */
    Log("\n--- 4. HQD/KIQ (0xDAC0-0xDB40) ---\n");
    for (unsigned off = 0xDAC0; off <= 0xDB40; off += 4) {
        ReadReg(h, off, &val);
        if (val != 0xFFFFFFFF)
            Log("  [0x%04X] = 0x%08X\n", off, val);
    }

    /* === 5. Probe UCODE/IB/PM4 engine (0x3800-0x3C00) === */
    Log("\n--- 5. CP engine (0x3800-0x3C00) ---\n");
    unsigned live = 0;
    for (unsigned off = 0x3800; off <= 0x3C00; off += 4) {
        ReadReg(h, off, &val);
        if (val != 0xFFFFFFFF && val != 0x00000000) {
            Log("  [0x%04X] = 0x%08X\n", off, val);
            live++;
        }
    }
    Log("  Live in 0x3800-0x3C00: %u\n", live);

    /* === 6. Writable test on key CP registers === */
    Log("\n--- 6. CP register write test ---\n");
    /* GC path */
    ProbeWrite(h, 0x3360, 0x12345678, "RB0_BASE_LO(GC)");
    ProbeWrite(h, 0x3364, 0x00000000, "RB0_BASE_HI(GC)");
    ProbeWrite(h, 0x3368, 0x00000707, "RB0_CNTL(GC)");
    /* NBIO path */
    ProbeWrite(h, 0xC100, 0x12345678, "RB0_BASE_LO(NBIO)");
    ProbeWrite(h, 0xC104, 0x00000000, "RB0_BASE_HI(NBIO)");
    ProbeWrite(h, 0xC108, 0x00000707, "RB0_CNTL(NBIO)");
    /* ME_CNTL */
    ProbeWrite(h, 0x4A74, 0x00000000, "ME_CNTL unhalt(GC)");

    /* === 7. Full writability scan of CP/GFX range === */
    Log("\n--- 7. Writable scan 0x3300-0x3400 ---\n");
    unsigned wr = 0;
    for (unsigned off = 0x3300; off < 0x3400; off += 4) {
        unsigned before, after;
        ReadReg(h, off, &before);
        if (before == 0xFFFFFFFF) continue;
        WriteReg(h, off, before ^ 0x00000001);
        ReadReg(h, off, &after);
        if (after != before) {
            Log("  [0x%04X]: 0x%08X -> 0x%08X\n", off, before, after);
            WriteReg(h, off, before);
            wr++;
        }
    }
    Log("  Writable: %u\n", wr);

    Log("\n--- 8. Writable scan 0x4A00-0x4B00 (ME_CNTL area) ---\n");
    wr = 0;
    for (unsigned off = 0x4A00; off < 0x4B00; off += 4) {
        unsigned before, after;
        ReadReg(h, off, &before);
        if (before == 0xFFFFFFFF) continue;
        WriteReg(h, off, before ^ 0x00000001);
        ReadReg(h, off, &after);
        if (after != before) {
            Log("  [0x%04X]: 0x%08X -> 0x%08X\n", off, before, after);
            WriteReg(h, off, before);
            wr++;
        }
    }
    Log("  Writable: %u\n", wr);

    /* Final GRBM */
    ReadReg(h, 0x3260, &val);
    Log("\nFinal GRBM_STATUS: 0x%08X\n", val);

    CloseHandle(h);
    Log("\n=== Done ===\n");
    if (g) fclose(g);
    printf("Done. Check output\\test-pm4-submit.log\n");
    return 0;
}
