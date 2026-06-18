/* scan-bar5-full.c — Full BAR5 scan to map alive/dead blocks */
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
    if (ok) *val = ra[1];
    return ok;
}

static BOOL WriteReg(HANDLE h, unsigned offset, unsigned val) {
    unsigned ra[2] = {offset, val};
    DWORD br = 0;
    return DeviceIoControl(h, 0x80000B8C, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
}

int main(void) {
    g = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\scan-bar5-full.log", "w");
    Log("=== Full BAR5 Block Map ===\n\n");

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

    /* Scan entire 512KB BAR5 in 4KB pages, report first non-dead, non-zero register per page */
    Log("Scanning BAR5 0x00000-0x7FFFF (512KB) in 4KB pages...\n\n");
    
    unsigned totalAlive = 0;
    unsigned totalDead = 0;
    unsigned totalZero = 0;

    for (unsigned page = 0; page < 0x80000; page += 0x1000) {
        unsigned alive = 0;
        unsigned dead = 0;
        unsigned zero = 0;
        unsigned firstAlive = 0, firstAliveVal = 0;
        unsigned firstZero = 0;

        for (unsigned off = page; off < page + 0x1000; off += 4) {
            unsigned val;
            if (ReadReg(h, off, &val)) {
                if (val == 0xFFFFFFFF) { dead++; }
                else if (val == 0x00000000) { zero++; if (!firstZero) firstZero = off; }
                else { alive++; if (!firstAlive) { firstAlive = off; firstAliveVal = val; } }
            }
        }

        totalAlive += alive;
        totalDead += dead;
        totalZero += zero;

        /* Report page if it has any non-dead, non-zero registers */
        if (alive > 0 || (alive == 0 && dead == 0 && zero > 0)) {
            Log("  0x%05X-0x%05X: alive=%3u zero=%3u dead=%3u",
                page, page + 0xFFF, alive, zero, dead);
            if (firstAlive)
                Log("  first=[0x%05X]=0x%08X", firstAlive, firstAliveVal);
            else if (firstZero)
                Log("  first_zero=[0x%05X]", firstZero);
            Log("\n");
        }
    }

    Log("\nSummary: alive=%u zero=%u dead=%u\n", totalAlive, totalZero, totalDead);

    /* Specifically test key known-good and test-new offsets */
    Log("\n--- Key register verification ---\n");
    struct { unsigned off; const char *name; } key[] = {
        {0x0000, "GPU_ID"},
        {0x0520, "MC_VM_FB_LOC_BASE (old)"},
        {0x0524, "MC_VM_FB_LOC_TOP (old)"},
        {0x0528, "MC_VM_AGP_BASE (old)"},
        {0x05A0, "HDP_MISC_CNTL"},
        {0x2880, "GCVM_CONTEXT0_CNTL (new)"},
        {0x2840, "GCVM_L2_CNTL (new)"},
        {0x2980, "GCMC_VM_FB_LOC_BASE (new)"},
        {0x2987, "GCMC_VM_MX_L1_TLB_CNTL (new)"},
        {0x3000, "GC_HW_INIT"},
        {0x3260, "GRBM_STATUS"},
        {0x3264, "CC_GC_SHADER_ARRAY"},
        {0x32D4, "SCRATCH[0]"},
        {0x34FC, "SPI_PG_ENABLE_STATIC_WGP_MASK"},
        {0x5000, "MMHUB? 0x5000"},
        {0x7000, "DF? 0x7000"},
        {0x9520, "MC_VM_FB_LOC (hw_extra)"},
        {0x9B00, "VM_PT_BASE (hw_extra)"},
        {0xA000, "RSMU?"},
        {0xC000, "NBIO?"},
        {0xC100, "NBIO_ID"},
        {0xC180, "NBIO_SIG2"},
        {0x16000, "MP0/PSP"},
        {0x1A000, "MMHUB"},
    };
    for (int i = 0; i < sizeof(key)/sizeof(key[0]); i++) {
        unsigned val;
        if (ReadReg(h, key[i].off, &val))
            Log("  %s [0x%05X] = 0x%08X%s\n", key[i].name, key[i].off, val,
                val == 0xFFFFFFFF ? " (DEAD)" : val == 0 ? " (zero)" : " (ALIVE)");
        else
            Log("  %s [0x%05X] = READ FAILED\n", key[i].name, key[i].off);
    }

    CloseHandle(h);
    Log("\n=== Done ===\n");
    if (g) fclose(g);
    printf("Done. Check output\\scan-bar5-full.log\n");
    return 0;
}
