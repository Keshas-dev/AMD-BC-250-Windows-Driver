#define INITGUID
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

static FILE *g = NULL;
static void Log(const char *fmt, ...) {
    va_list a; va_start(a, fmt);
    vfprintf(stdout, fmt, a); va_end(a); fflush(stdout);
    if (g) { va_start(a, fmt); vfprintf(g, fmt, a); va_end(a); fflush(g); }
}

static HANDLE OpenKmd(void) {
    return CreateFileW(L"\\\\.\\AMDBC250DreamV43",
        GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
}

static BOOL ReadReg(HANDLE h, UINT32 offset, UINT32 *val) {
    UINT32 ra[2] = {offset, 0xDEADBEEF};
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, 0x80000B88, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
    *val = ra[1];
    return ok;
}

static BOOL WriteReg(HANDLE h, UINT32 offset, UINT32 val) {
    UINT32 ra[2] = {offset, val};
    DWORD br = 0;
    return DeviceIoControl(h, 0x80000B8C, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
}

static void InitHardware(HANDLE h) {
    UCHAR initIn[32] = {0}, initOut[32] = {0};
    DWORD br = 0;
    *(UINT64*)(initIn + 0)  = 0xFE800000ULL;
    *(UINT32*)(initIn + 8)  = 0x00080000;
    *(UINT32*)(initIn + 12) = 1;
    *(UINT64*)(initIn + 16) = 0xC0000000ULL;
    *(UINT32*)(initIn + 24) = 0x10000000;
    DeviceIoControl(h, 0x80000B80, initIn, sizeof(initIn), initOut, sizeof(initOut), &br, NULL);
}

int main(void) {
    g = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\full-scan.log", "w");
    if (!g) { printf("Cannot open log\n"); return 1; }
    Log("=== Safe Range Scan + PCI Config ===\n\n"); fflush(g);

    HANDLE h = OpenKmd();
    if (h == INVALID_HANDLE_VALUE) {
        Log("KMD NOT FOUND\n"); fclose(g); return 1;
    }
    UINT32 v, rb;

    InitHardware(h);
    Log("Hardware initialized\n"); fflush(g);

    /* SAFE RANGES ONLY — based on known working ranges */
    struct { UINT32 start; UINT32 end; const char *name; } ranges[] = {
        {0x0000, 0x0100, "GPU ID + Config"},
        {0x0500, 0x0600, "HDP extended"},
        {0x0C00, 0x0E00, "CLK/Config"},
        {0x2000, 0x2100, "CP/GRBM/Scratch"},
        {0x3000, 0x3100, "GC"},
        {0x4000, 0x5000, "Pre-MMHUB"},
        {0x5000, 0x5A00, "MMHUB (7 VMHUBs)"},
        {0x8000, 0x9000, "Mid-range"},
        {0xA000, 0xA100, "RSMU"},
        {0xC000, 0xC200, "NBIO"},
        {0x10000, 0x11000, "Upper range"},
        {0x18000, 0x19000, "Pre-DF"},
        {0x1A000, 0x1A400, "DF"},
        {0x1C000, 0x1D000, "Post-DF"},
    };
    int numRanges = sizeof(ranges) / sizeof(ranges[0]);
    int ri;

    Log("=== Phase 1: Safe Range Read ===\n"); fflush(g);
    for (ri = 0; ri < numRanges; ri++) {
        int count = 0;
        UINT32 off;
        Log("\n  --- %s (0x%X-0x%X) ---\n", ranges[ri].name, ranges[ri].start, ranges[ri].end);
        for (off = ranges[ri].start; off < ranges[ri].end; off += 4) {
            if (ReadReg(h, off, &v) && v != 0xFFFFFFFF && v != 0x00000000) {
                Log("    [0x%05X] = 0x%08X\n", off, v);
                count++;
            }
        }
        Log("    readable: %d\n", count);
        fflush(g);
    }

    /* Write test — only known safe writable regs */
    Log("\n=== Phase 2: Write → GRBM check ===\n"); fflush(g);
    {
        UINT32 testRegs[] = {
            0x50C4, 0x50D0, 0x5000,
            0x51C4, 0x51D0, 0x5180,
            0x5244, 0x5250, 0x5300,
            0x3008, 0x3000, 0x3004,
            0xC174, 0xC1A4, 0xC1E4, 0xC0D8, 0xC100, 0xC180
        };
        int i;
        for (i = 0; i < sizeof(testRegs)/sizeof(testRegs[0]); i++) {
            ReadReg(h, testRegs[i], &v);
            UINT32 newVal = (v == 0) ? 1 : (v ^ 1);
            WriteReg(h, testRegs[i], newVal);
            UINT32 grbm = 0;
            ReadReg(h, 0x3260, &grbm);
            if (grbm != 0xFFFFFFFF) {
                Log("  *** UNLOCK! [0x%04X] 0x%08X→0x%08X GRBM=0x%08X ***\n",
                    testRegs[i], v, newVal, grbm);
            }
            WriteReg(h, testRegs[i], v);
        }
        ReadReg(h, 0x3260, &rb);
        Log("  GRBM after all writes: 0x%08X\n", rb);
        fflush(g);
    }

    /* PCI Config via IOCTL */
    Log("\n=== Phase 3: PCI Config ===\n"); fflush(g);
    {
        UINT32 bdf[][3] = {{1,0,0},{1,1,0},{2,0,0}};
        const char *names[] = {"NBIO B1:D0:F0","NBIO2 B1:D1:F0","GPU B2:D0:F0"};
        int i;
        for (i = 0; i < 3; i++) {
            UCHAR inBuf[280] = {0}, outBuf[280] = {0};
            DWORD br = 0;
            *(UINT32*)(inBuf + 0) = bdf[i][0];
            *(UINT32*)(inBuf + 4) = bdf[i][1];
            *(UINT32*)(inBuf + 8) = bdf[i][2];
            BOOL ok = DeviceIoControl(h, 0x80000BAC, inBuf, sizeof(inBuf),
                outBuf, sizeof(outBuf), &br, NULL);
            UINT32 bytesRead = *(UINT32*)(outBuf + 12);
            Log("  %s: %s  %lu bytes\n", names[i], ok ? "OK" : "FAIL", bytesRead);
            if (ok && bytesRead > 0) {
                UINT32 *d = (UINT32*)(outBuf + 16);
                int j;
                for (j = 0; j < 64; j++) {
                    if (d[j] != 0 && d[j] != 0xFFFFFFFF)
                        Log("    [0x%02X] = 0x%08X\n", j*4, d[j]);
                }
            }
            fflush(g);
        }
    }

    /* Final check */
    Log("\n=== Final ===\n"); fflush(g);
    ReadReg(h, 0x3260, &v); Log("  GRBM = 0x%08X\n", v);
    ReadReg(h, 0x0000, &v); Log("  GPU_ID = 0x%08X\n", v);
    fflush(g);

    CloseHandle(h);
    Log("\n=== Done ===\n"); fflush(g);
    if (g) fclose(g);
    printf("Done. Check output\\full-scan.log\n");
    return 0;
}
