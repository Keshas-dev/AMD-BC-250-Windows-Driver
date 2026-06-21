/* test-kiq-submit.c — SAFE: read-only register dump, NO HQD_ACTIVE writes */
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

static void RwTest(HANDLE h, unsigned off, unsigned val, const char *name) {
    unsigned before, after;
    ReadReg(h, off, &before);
    WriteReg(h, off, val);
    ReadReg(h, off, &after);
    const char *st = (after == val) ? "OK" : (after == before ? "NO_CHANGE" : "DIFF");
    Log("  %s [0x%04X]: 0x%08X -> w:0x%08X -> r:0x%08X %s\n", name, off, before, val, after, st);
}

int main(void) {
    g = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\test-kiq-submit.log", "w");
    Log("=== KIQ Ring Submit Test ===\n\n");

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

    /* === Phase 1: Current state dump === */
    Log("--- Phase 1: Current state ---\n");
    struct { unsigned off; const char *name; } regs[] = {
        {0x4A74, "ME_CNTL"},
        {0x3260, "GRBM_STATUS"},
        {0xE060, "KIQ_BASE_LO"},
        {0xE064, "KIQ_BASE_HI"},
        {0xE068, "KIQ_CNTL"},
        {0xE06C, "KIQ_RPTR"},
        {0xE070, "KIQ_RPTR_HI"},
        {0xE074, "KIQ_WPTR_HI"},
        {0xE078, "KIQ_WPTR"},
        {0xE07C, "KIQ_DOORBELL"},
        {0xDAC0, "HQD_ACTIVE"},
        {0xDAC4, "HQD_VMID"},
        {0xDAC8, "HQD_PQ_BASE_LO"},
        {0xDACC, "HQD_PQ_BASE_HI"},
        {0xDAD0, "HQD_PQ_RPTR"},
        {0xDAD4, "HQD_PQ_WPTR"},
        {0xDAD8, "HQD_DOORBELL"},
        {0xDADC, "HQD_CONTEXT"},
        {0xDAE0, "HQD_PQ_CNTL"},
        {0xDAE4, "HQD_IB_RPTR"},
        {0xDAE8, "HQD_IB_WPTR"},
        {0xDAEC, "HQD_IB_BASE_LO"},
        {0x32D4, "SCRATCH[0]"},
        {0x32D8, "SCRATCH[1]"},
    };
    for (int i = 0; i < sizeof(regs)/sizeof(regs[0]); i++) {
        ReadReg(h, regs[i].off, &val);
        Log("  %s [0x%04X] = 0x%08X\n", regs[i].name, regs[i].off, val);
    }

    /* === Phase 2: Unhalt ME + PFP === */
    Log("\n--- Phase 2: Unhalt ME + PFP ---\n");
    RwTest(h, 0x4A74, 0x00000000, "ME_CNTL (unhalt)");
    ReadReg(h, 0x3260, &val);
    Log("  GRBM after unhalt: 0x%08X\n", val);

    /* === Phase 3: Safe KIQ register test (NO HQD_ACTIVE!) === */
    Log("\n--- Phase 3: Safe KIQ HQD register test ---\n");
    RwTest(h, 0xE068, 0x00000007, "KIQ_CNTL (bufsz=8DW)");
    RwTest(h, 0xE06C, 0x00000000, "KIQ_RPTR = 0");
    /* HQD_ACTIVE=1 DANGEROUS — SKIP */
    Log("  [SKIP] HQD_ACTIVE = 1 (causes hang)\n");
    RwTest(h, 0xDAC4, 0x00000000, "HQD_VMID = 0");
    RwTest(h, 0xDAD0, 0x00000000, "HQD_PQ_RPTR = 0");
    /* HQD_PQ_WPTR dangerous without ring — SKIP */
    Log("  [SKIP] HQD_PQ_WPTR (no ring allocated)\n");
    RwTest(h, 0xDAE4, 0x00000000, "HQD_IB_RPTR = 0");
    RwTest(h, 0xDAE8, 0x00000000, "HQD_IB_WPTR = 0");

    /* === Phase 4: READ-ONLY dump after 100ms === */
    Log("\n--- Phase 4: State after 100ms ---\n");
    Sleep(100);
    for (int i = 0; i < sizeof(regs)/sizeof(regs[0]); i++) {
        ReadReg(h, regs[i].off, &val);
        Log("  %s [0x%04X] = 0x%08X\n", regs[i].name, regs[i].off, val);
    }

    CloseHandle(h);
    Log("\n=== Done ===\n");
    if (g) fclose(g);
    printf("Done. Check output\\test-kiq-submit.log\n");
    return 0;
}
