/* test-kiq-submit.c — KIQ ring: unhalt ME + activate HQD + submit NOP PM4 */
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

    /* === Phase 3: Write/readback test all KIQ HQD registers === */
    Log("\n--- Phase 3: KIQ HQD write test ---\n");
    RwTest(h, 0xE068, 0x00000007, "KIQ_CNTL (bufsz=8DW)");
    RwTest(h, 0xE06C, 0x00000000, "KIQ_RPTR = 0");
    RwTest(h, 0xDAC0, 0x00000001, "HQD_ACTIVE = 1");
    RwTest(h, 0xDAC4, 0x00000000, "HQD_VMID = 0");
    RwTest(h, 0xDAD0, 0x00000000, "HQD_PQ_RPTR = 0");
    RwTest(h, 0xDAD4, 0x00000008, "HQD_PQ_WPTR = 8");
    RwTest(h, 0xDAE0, 0x00000002, "HQD_PQ_CNTL");
    RwTest(h, 0xDAE4, 0x00000000, "HQD_IB_RPTR = 0");
    RwTest(h, 0xDAE8, 0x00000000, "HQD_IB_WPTR = 0");

    /* === Phase 4: Write KIQ WPTR to trigger processing === */
    Log("\n--- Phase 4: KIQ WPTR trigger ---\n");
    RwTest(h, 0xE078, 0x00000008, "KIQ_WPTR = 8");

    /* Wait 100ms */
    Sleep(100);

    /* Check for changes */
    Log("\n  After 100ms:\n");
    for (int i = 0; i < sizeof(regs)/sizeof(regs[0]); i++) {
        ReadReg(h, regs[i].off, &val);
        Log("  %s [0x%04X] = 0x%08X\n", regs[i].name, regs[i].off, val);
    }

    /* === Phase 5: Try GFX ring path too === */
    Log("\n--- Phase 5: GFX ring write test ---\n");
    RwTest(h, 0xDA68, 0x00000707, "GFX_CNTL (bufsz=4KB)");
    RwTest(h, 0xDA6C, 0x00000000, "GFX_RPTR = 0");
    RwTest(h, 0xDA78, 0x00000010, "GFX_WPTR = 16");

    Sleep(100);
    ReadReg(h, 0x3260, &val);
    Log("  GRBM after GFX WPTR: 0x%08X\n", val);
    ReadReg(h, 0x32D4, &val);
    Log("  SCRATCH[0] = 0x%08X\n", val);

    /* === Phase 6: Sweep all writable GC registers 0x3800-0x4B00 === */
    Log("\n--- Phase 6: Sweep for writable CP engine regs ---\n");
    unsigned wr = 0;
    for (unsigned off = 0x3800; off < 0x4C00; off += 4) {
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
    Log("  Writable in 0x3800-0x4B00: %u\n", wr);

    CloseHandle(h);
    Log("\n=== Done ===\n");
    if (g) fclose(g);
    printf("Done. Check output\\test-kiq-submit.log\n");
    return 0;
}
