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
    unsigned ra[2] = {off, 0}; DWORD br = 0;
    BOOL ok = DeviceIoControl(h, 0x80000B88, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
    if (!ok || br < sizeof(ra)) {
        return 0xFFFFFFFF;
    }
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
        {0xDAC0,"HQD_ACT"},{0xDAC4,"HQD_VMD"},
        {0xDACC,"HQD_PIPE_PRIORITY"},{0xDAD0,"HQD_QUEUE_PRIORITY"},{0xDAD4,"HQD_QUANTUM"},
        {0xDAE0,"HQD_PQ_RPTR"},{0xDB90,"HQD_PQ_WPTR_LO"},{0xDB94,"HQD_PQ_WPTR_HI"},
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

    /* INIT_HARDWARE — match driver struct size (28 bytes) */
    struct { unsigned __int64 mmio; unsigned size; unsigned flags; } ih = {0};
    ih.mmio = 0xFE800000ULL;
    ih.size = 0x00080000;
    ih.flags = 1;
    DWORD br = 0;
    if (!DeviceIoControl(h, 0x80000B80, &ih, sizeof(ih), NULL, 0, &br, NULL)) {
        Log("INIT_HARDWARE failed (err=%lu) — BAR5 may not be mapped\n", GetLastError());
    }

    /* Step 0: Baseline */
    Log("--- Step 0: Baseline ---\n");
    Dump(h, "baseline");
    Sleep(500);
    Dump(h, "after 500ms (check stability)");

    /* Step 1: ONLY unhalt ME (clear bit 28) */
    Log("\n--- Step 1: Unhalt ME only (0x4A74 &= ~0x10000000) ---\n");
    unsigned me = ReadReg(h, 0x4A74);
    Log("  ME_CNTL before: 0x%08X\n", me);
    if (me == 0xFFFFFFFF) {
        Log("  FATAL: ME_CNTL read failed — aborting\n");
        CloseHandle(h); if(g) fclose(g); return 1;
    }
    unsigned me_new = me & ~(1u << 28);
    Log("  Write: 0x%08X (clear bit 28 only)\n", me_new);
    if (!WriteReg(h, 0x4A74, me_new)) {
        Log("  FATAL: ME_CNTL write failed — aborting\n");
        CloseHandle(h); if(g) fclose(g); return 1;
    }
    unsigned me_read = ReadReg(h, 0x4A74);
    Log("  Readback: 0x%08X\n", me_read);
    Sleep(200);
    Dump(h, "200ms after ME unhalt");
    Sleep(500);
    Dump(h, "500ms after ME unhalt");

    /* Step 2: Read KIQ_CNTL (it is READ-ONLY on BC-250) */
    Log("\n--- Step 2: KIQ_CNTL (0xE068) — READ-ONLY on BC-250 ---\n");
    unsigned kcntl = ReadReg(h, 0xE068);
    Log("  KIQ_CNTL current: 0x%08X\n", kcntl);

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

    /* Step 5: HQD_PQ_RPTR = 0 (0xDAE0, NOT 0xDAD0) */
    Log("\n--- Step 5: HQD_PQ_RPTR = 0 (0xDAE0) ---\n");
    WriteReg(h, 0xDAE0, 0x00000000);
    Log("  HQD_PQ_RPTR readback: 0x%08X\n", ReadReg(h, 0xDAE0));
    Sleep(100);

    /* Step 6: HQD_PQ_WPTR_LO = 0 (0xDB90, NOT 0xDAD4) */
    Log("\n--- Step 6: HQD_PQ_WPTR_LO = 0 (0xDB90) ---\n");
    WriteReg(h, 0xDB90, 0x00000000);
    Log("  HQD_PQ_WPTR_LO readback: 0x%08X\n", ReadReg(h, 0xDB90));
    Sleep(100);

    /* Step 6b: HQD_PQ_WPTR_HI = 0 (0xDB94) */
    Log("\n--- Step 6b: HQD_PQ_WPTR_HI = 0 (0xDB94) ---\n");
    WriteReg(h, 0xDB94, 0x00000000);
    Log("  HQD_PQ_WPTR_HI readback: 0x%08X\n", ReadReg(h, 0xDB94));
    Sleep(100);

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
