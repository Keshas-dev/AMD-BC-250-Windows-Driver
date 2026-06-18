/* scan-gcvm-quick.c — Quick scan: only key GCVM offsets + try to power up */
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

int main(void) {
    g = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\scan-gcvm-quick.log", "w");
    Log("=== Quick GCVM Test ===\n\n");

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
    Log("GRBM_STATUS[0x3260] = 0x%08X (baseline)\n\n", val);

    /* Test ALL MC_VM offsets at old 0x0520 range — these returned 0 earlier */
    Log("--- MC_VM range (old 0x0520-0x0550, HDP?) ---\n");
    for (unsigned off = 0x0520; off <= 0x0550; off += 4) {
        ReadReg(h, off, &val);
        Log("  [0x%04X] = 0x%08X\n", off, val);
    }

    /* Test MC_VM at hw_extra 0x9520 range */
    Log("\n--- MC_VM range (hw_extra 0x9520-0x9550) ---\n");
    for (unsigned off = 0x9520; off <= 0x9550; off += 4) {
        ReadReg(h, off, &val);
        Log("  [0x%04X] = 0x%08X\n", off, val);
    }

    /* Test old VM range 0x1A00-0x1A40 */
    Log("\n--- Old VM range (0x1A00-0x1A40) ---\n");
    for (unsigned off = 0x1A00; off <= 0x1A40; off += 4) {
        ReadReg(h, off, &val);
        if (val != 0xFFFFFFFF || (off % 0x10 == 0))
            Log("  [0x%04X] = 0x%08X\n", off, val);
    }

    /* Test hw_extra VM range 0x9B00-0x9B90 */
    Log("\n--- hw_extra VM range (0x9B00-0x9B90) ---\n");
    for (unsigned off = 0x9B00; off <= 0x9B90; off += 4) {
        ReadReg(h, off, &val);
        if (val != 0xFFFFFFFF || (off % 0x10 == 0))
            Log("  [0x%04X] = 0x%08X\n", off, val);
    }

    /* Test GCMC range 0x2980-0x2990 */
    Log("\n--- GCMC range (0x2980-0x2990) ---\n");
    for (unsigned off = 0x2980; off <= 0x2990; off += 4) {
        ReadReg(h, off, &val);
        Log("  [0x%04X] = 0x%08X\n", off, val);
    }

    /* Try write/readback at known-alive 0x0520 */
    Log("\n--- Write test at 0x0520 (was readable) ---\n");
    ReadReg(h, 0x0520, &val);
    Log("  Before: 0x%08X\n", val);
    WriteReg(h, 0x0520, 0x00010000);
    ReadReg(h, 0x0520, &val);
    Log("  After write 0x00010000: 0x%08X\n", val);
    WriteReg(h, 0x0520, 0x00000000);

    CloseHandle(h);
    Log("\n=== Done ===\n");
    if (g) fclose(g);
    printf("Done. Check output\\scan-gcvm-quick.log\n");
    return 0;
}
