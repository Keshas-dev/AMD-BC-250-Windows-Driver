/* test-kiq-safe.c — VERY conservative: one register at a time, verify between */
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

static FILE *g = NULL;
static void Log(const char *fmt, ...) {
    va_list a; va_start(a, fmt); vfprintf(stdout, fmt, a); va_end(a);
    if (g) { va_start(a, fmt); vfprintf(g, fmt, a); va_end(a); }
}

static unsigned ReadReg(HANDLE h, unsigned off) {
    unsigned ra[2] = {off, 0xDEADBEEF}; DWORD br = 0;
    DeviceIoControl(h, 0x80000B88, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
    return ra[1];
}

static BOOL WriteReg(HANDLE h, unsigned off, unsigned val) {
    unsigned ra[2] = {off, val}; DWORD br = 0;
    return DeviceIoControl(h, 0x80000B8C, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
}

static void Dump(HANDLE h, const char *label) {
    Log("  [%s]\n", label);
    struct { unsigned o; const char *n; } r[] = {
        {0x4A74,"ME_CNTL"},{0x3260,"GRBM"},{0x32D4,"SCRATCH0"},{0x32D8,"SCRATCH1"},
        {0xE060,"KIQ_BASE"},{0xE068,"KIQ_CNTL"},{0xE06C,"KIQ_RPTR"},{0xE078,"KIQ_WPTR"},
        {0xDAC0,"HQD_ACT"},{0xDAC4,"HQD_VMD"},{0xDACC,"HQD_PQ"},{0xDAD0,"HQD_RPTR"},{0xDAD4,"HQD_WPTR"},
    };
    for (int i = 0; i < sizeof(r)/sizeof(r[0]); i++)
        Log("    %s=0x%08X", r[i].n, ReadReg(h, r[i].o));
    Log("\n");
}

int main(void) {
    g = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\test-kiq-safe.log", "w");
    Log("=== KIQ SAFE Test (one step at a time) ===\n\n");

    HANDLE h = CreateFileW(L"\\\\.\\AMDBC250DreamV43",
        GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { Log("Open failed\n"); if(g) fclose(g); return 1; }

    /* INIT_HARDWARE */
    UCHAR i[32]={0}; DWORD br=0;
    *(unsigned __int64*)(i+0)=0xFE800000ULL; *(unsigned*)(i+8)=0x00080000;
    *(unsigned*)(i+12)=1; *(unsigned __int64*)(i+16)=0xC0000000ULL; *(unsigned*)(i+24)=0x10000000;
    DeviceIoControl(h, 0x80000B80, i, sizeof(i), NULL, 0, &br, NULL);

    /* Step 0: Baseline */
    Log("--- Step 0: Baseline ---\n");
    Dump(h, "baseline");
    Sleep(500);
    Dump(h, "after 500ms (check stability)");

    /* Step 1: ONLY unhalt ME (clear bit 28) */
    Log("\n--- Step 1: Unhalt ME only (0x4A74 &= ~0x10000000) ---\n");
    unsigned me = ReadReg(h, 0x4A74);
    Log("  ME_CNTL before: 0x%08X\n", me);
    unsigned me_new = me & ~(1u << 28);  /* Only clear ME_HALT, keep PFP_HALT */
    Log("  Write: 0x%08X (clear bit 28 only)\n", me_new);
    WriteReg(h, 0x4A74, me_new);
    unsigned me_read = ReadReg(h, 0x4A74);
    Log("  Readback: 0x%08X\n", me_read);
    Sleep(200);
    Dump(h, "200ms after ME unhalt");
    Sleep(500);
    Dump(h, "500ms after ME unhalt");

    /* Step 2: Write KIQ_CNTL (bufsz only) */
    Log("\n--- Step 2: KIQ_CNTL bufsz=7 (0xE068) ---\n");
    unsigned kcntl = ReadReg(h, 0xE068);
    Log("  KIQ_CNTL before: 0x%08X\n", kcntl);
    WriteReg(h, 0xE068, 0x00000007);
    unsigned kcntl_r = ReadReg(h, 0xE068);
    Log("  KIQ_CNTL after: 0x%08X\n", kcntl_r);
    Sleep(200);
    Dump(h, "200ms after KIQ_CNTL");

    /* Step 3: KIQ_RPTR = 0 */
    Log("\n--- Step 3: KIQ_RPTR = 0 (0xE06C) ---\n");
    WriteReg(h, 0xE06C, 0x00000000);
    Log("  KIQ_RPTR readback: 0x%08X\n", ReadReg(h, 0xE06C));
    Sleep(100);

    /* Step 4: KIQ_WPTR = 0 (clear) */
    Log("\n--- Step 4: KIQ_WPTR = 0 (0xE078) ---\n");
    WriteReg(h, 0xE078, 0x00000000);
    Log("  KIQ_WPTR readback: 0x%08X\n", ReadReg(h, 0xE078));
    Sleep(100);

    /* Step 5: HQD_PQ_RPTR = 0 */
    Log("\n--- Step 5: HQD_PQ_RPTR = 0 (0xDAD0) ---\n");
    WriteReg(h, 0xDAD0, 0x00000000);
    Log("  HQD_PQ_RPTR readback: 0x%08X\n", ReadReg(h, 0xDAD0));

    /* Step 6: HQD_PQ_WPTR = 0 */
    Log("\n--- Step 6: HQD_PQ_WPTR = 0 (0xDAD4) ---\n");
    WriteReg(h, 0xDAD4, 0x00000000);
    Log("  HQD_PQ_WPTR readback: 0x%08X\n", ReadReg(h, 0xDAD4));

    /* STOP HERE — review log before continuing */
    Log("\n=== STOPS HERE — review log before Step 7 ===\n");
    Log("Step 7 would be HQD_ACTIVE=1 (DANGEROUS)\n");
    Log("Step 8 would be KIQ_WPTR=8 (trigger)\n");
    Log("Step 9 would be PFP unhalt\n");
    Log("Step 10 would be full ME unhalt\n");

    CloseHandle(h);
    Log("\n=== Done (system alive?) ===\n");
    if (g) fclose(g);
    printf("Done. Check output\\test-kiq-safe.log\n");
    return 0;
}
