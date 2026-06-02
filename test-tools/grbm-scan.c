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
    g = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\grbm-scan.log", "w");
    if (!g) { printf("Cannot open log\n"); return 1; }
    Log("=== GRBM Area Detailed Scan ===\n\n"); fflush(g);

    HANDLE h = OpenKmd();
    if (h == INVALID_HANDLE_VALUE) {
        Log("KMD NOT FOUND\n"); fclose(g); return 1;
    }
    UINT32 v;

    InitHardware(h);

    /* Part 1: Scan 0x2000-0x2080 every 4 bytes */
    Log("--- GRBM/CP area (0x2000-0x2080) ---\n"); fflush(g);
    {
        UINT32 off;
        for (off = 0x2000; off < 0x2080; off += 4) {
            ReadReg(h, off, &v);
            Log("  [0x%04X] = 0x%08X\n", off, v);
        }
        fflush(g);
    }

    /* Part 2: Scan 0x2080-0x2100 */
    Log("\n--- 0x2080-0x2100 ---\n"); fflush(g);
    {
        UINT32 off;
        for (off = 0x2080; off < 0x2100; off += 4) {
            ReadReg(h, off, &v);
            if (v != 0xFFFFFFFF)
                Log("  [0x%04X] = 0x%08X\n", off, v);
        }
        fflush(g);
    }

    /* Part 3: Scan other GPU blocks that standard AMD GPUs have */
    Log("\n--- Other GPU blocks ---\n"); fflush(g);
    {
        struct { UINT32 off; const char *name; } gpuRegs[] = {
            {0x2100, "GRBM_STATUS_SE0"},
            {0x2104, "GRBM_STATUS_SE1"},
            {0x2108, "GRBM_STATUS_SE2"},
            {0x210C, "GRBM_STATUS_SE3"},
            {0x2110, "GRBM_STATUS2"},
            {0x2180, "GRBM_STATUS_SE4"},
            {0x2184, "GRBM_STATUS_SE5"},
            {0x2188, "GRBM_STATUS_SE6"},
            {0x218C, "GRBM_STATUS_SE7"},
            {0x2300, "UVD_STATUS"},
            {0x2600, "SDMA0_STATUS"},
            {0x2800, "SDMA1_STATUS"},
            {0x300C, "GC_CONFIG"},
            {0x3010, "GC_CONFIG2"},
            {0x3014, "GC_CONFIG3"},
            {0x3080, "GC_ARB_CONFIG"},
            {0x4000, "Pre-MMHUB?"},
            {0x4100, "Mid-range?"},
            {0x4200, "Mid-range2?"},
            {0x4400, "Mid-range3?"},
            {0x4800, "Mid-range4?"},
            {0x6000, "Post-MMHUB?"},
            {0x7000, "Range7?"},
            {0x8000, "Range8?"},
            {0x9000, "Range9?"},
            {0xB000, "RangeB?"},
        };
        int i;
        for (i = 0; i < sizeof(gpuRegs)/sizeof(gpuRegs[0]); i++) {
            ReadReg(h, gpuRegs[i].off, &v);
            if (v != 0xFFFFFFFF && v != 0x00000000)
                Log("  %s [0x%04X] = 0x%08X\n", gpuRegs[i].name, gpuRegs[i].off, v);
        }
        fflush(g);
    }

    /* Part 4: Write test to GRBM area — maybe writing unlocks it */
    Log("\n--- Write test in GRBM area ---\n"); fflush(g);
    {
        UINT32 off;
        for (off = 0x2000; off < 0x2080; off += 4) {
            ReadReg(h, off, &v);
            if (v == 0xFFFFFFFF) {
                /* Try writing different values to see if it changes anything */
                WriteReg(h, off, 0x00000001);
                UINT32 rb = 0;
                ReadReg(h, off, &rb);
                if (rb != 0xFFFFFFFF && rb != 0x00000000) {
                    Log("  [0x%04X] changed after write! 0xFFFFFFFF -> 0x%08X\n", off, rb);
                }
            }
        }
        Log("  No GRBM area registers responded to writes\n");
        fflush(g);
    }

    CloseHandle(h);
    Log("\n=== GRBM Scan Complete ===\n"); fflush(g);
    if (g) fclose(g);
    printf("Done. Check output\\grbm-scan.log\n");
    return 0;
}
