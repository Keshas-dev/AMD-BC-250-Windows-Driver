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

static BOOL CheckGrbm(HANDLE h, const char *label) {
    UINT32 v = 0;
    ReadReg(h, 0x2004, &v);
    Log("  [%s] GRBM=0x%08X%s\n", label, v,
        (v != 0xFFFFFFFF && v != 0x00000000) ? " *** UNBLOCKED! ***" : "");
    fflush(g);
    return (v != 0xFFFFFFFF && v != 0x00000000);
}

int main(void) {
    g = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\nbio-unlock.log", "w");
    if (!g) { printf("Cannot open log\n"); return 1; }
    Log("=== NBIO Unlock v2 (Safe, Step-by-Step) ===\n\n"); fflush(g);

    HANDLE h = OpenKmd();
    if (h == INVALID_HANDLE_VALUE) {
        Log("KMD NOT FOUND\n"); fclose(g); return 1;
    }
    UINT32 v, rb;

    InitHardware(h);
    Log("Hardware initialized\n"); fflush(g);

    /* Baseline */
    Log("\n=== Baseline ===\n"); fflush(g);
    ReadReg(h, 0x0000, &v); Log("  GPU_ID: 0x%08X\n", v);
    CheckGrbm(h, "baseline");

    /* ============================================ */
    /* TEST 1: MMHUB VMHUB[0] permission (0x50C4) */
    /* ============================================ */
    Log("\n=== TEST 1: VMHUB[0] perm 0x50C4 ===\n"); fflush(g);
    {
        ReadReg(h, 0x50C4, &v);
        Log("  Before: 0x%08X\n", v);
        WriteReg(h, 0x50C4, 0x000000FF);
        ReadReg(h, 0x50C4, &rb);
        Log("  After:  0x%08X\n", rb);
        CheckGrbm(h, "after VMHUB perm");
        WriteReg(h, 0x50C4, v);
    }

    /* ============================================ */
    /* TEST 2: VMHUB[0] config (0x50D0)            */
    /* ============================================ */
    Log("\n=== TEST 2: VMHUB[0] config 0x50D0 ===\n"); fflush(g);
    {
        ReadReg(h, 0x50D0, &v);
        Log("  Before: 0x%08X\n", v);
        WriteReg(h, 0x50D0, 0x00004001);
        ReadReg(h, 0x50D0, &rb);
        Log("  After:  0x%08X\n", rb);
        CheckGrbm(h, "after VMHUB config");
        WriteReg(h, 0x50D0, v);
    }

    /* ============================================ */
    /* TEST 3: ALL 7 VMHUB perms at once           */
    /* ============================================ */
    Log("\n=== TEST 3: All 7 VMHUB perms 0xFF ===\n"); fflush(g);
    {
        UINT32 bases[] = {0x5000, 0x5180, 0x5300, 0x5480, 0x5600, 0x5780, 0x5900};
        UINT32 origC4[7], origD0[7];
        int i;
        for (i = 0; i < 7; i++) {
            ReadReg(h, bases[i] + 0xC4, &origC4[i]);
            ReadReg(h, bases[i] + 0xD0, &origD0[i]);
            WriteReg(h, bases[i] + 0xC4, 0x000000FF);
            WriteReg(h, bases[i] + 0xD0, 0x00004001);
        }
        CheckGrbm(h, "all VMHUB perms");
        for (i = 0; i < 7; i++) {
            WriteReg(h, bases[i] + 0xC4, origC4[i]);
            WriteReg(h, bases[i] + 0xD0, origD0[i]);
        }
    }

    /* ============================================ */
    /* TEST 4: NBIO 0xC174 (mask register)         */
    /* ============================================ */
    Log("\n=== TEST 4: NBIO mask 0xC174 ===\n"); fflush(g);
    {
        ReadReg(h, 0xC174, &v);
        Log("  Before: 0x%08X\n", v);
        /* Clear mask */
        WriteReg(h, 0xC174, 0x00000000);
        ReadReg(h, 0xC174, &rb);
        Log("  After (clear): 0x%08X\n", rb);
        CheckGrbm(h, "after NBIO mask clear");
        WriteReg(h, 0xC174, v);
    }

    /* ============================================ */
    /* TEST 5: NBIO 0xC1A4 (enable register)       */
    /* ============================================ */
    Log("\n=== TEST 5: NBIO enable 0xC1A4 ===\n"); fflush(g);
    {
        ReadReg(h, 0xC1A4, &v);
        Log("  Before: 0x%08X\n", v);
        WriteReg(h, 0xC1A4, v | 0x00000001);
        ReadReg(h, 0xC1A4, &rb);
        Log("  After:  0x%08X\n", rb);
        CheckGrbm(h, "after NBIO enable");
        WriteReg(h, 0xC1A4, v);
    }

    /* ============================================ */
    /* TEST 6: NBIO 0xC1E4 (config)                */
    /* ============================================ */
    Log("\n=== TEST 6: NBIO config 0xC1E4 ===\n"); fflush(g);
    {
        ReadReg(h, 0xC1E4, &v);
        Log("  Before: 0x%08X\n", v);
        WriteReg(h, 0xC1E4, v | 0x00000100);
        ReadReg(h, 0xC1E4, &rb);
        Log("  After:  0x%08X\n", rb);
        CheckGrbm(h, "after NBIO config");
        WriteReg(h, 0xC1E4, v);
    }

    /* ============================================ */
    /* TEST 7: NBIO 0xC0D8 (control)               */
    /* ============================================ */
    Log("\n=== TEST 7: NBIO ctrl 0xC0D8 ===\n"); fflush(g);
    {
        ReadReg(h, 0xC0D8, &v);
        Log("  Before: 0x%08X\n", v);
        WriteReg(h, 0xC0D8, 0x00000000);
        ReadReg(h, 0xC0D8, &rb);
        Log("  After:  0x%08X\n", rb);
        CheckGrbm(h, "after NBIO ctrl");
        WriteReg(h, 0xC0D8, v);
    }

    /* ============================================ */
    /* TEST 8: GC 0x3008 (known writable)          */
    /* ============================================ */
    Log("\n=== TEST 8: GC 0x3008 ===\n"); fflush(g);
    {
        ReadReg(h, 0x3008, &v);
        Log("  Before: 0x%08X\n", v);
        WriteReg(h, 0x3008, 0x00000001);
        ReadReg(h, 0x3008, &rb);
        Log("  After:  0x%08X\n", rb);
        CheckGrbm(h, "after GC write");
        WriteReg(h, 0x3008, v);
    }

    /* ============================================ */
    /* TEST 9: MMHUB base 0x5000 (full config)     */
    /* ============================================ */
    Log("\n=== TEST 9: VMHUB[0] base 0x5000 ===\n"); fflush(g);
    {
        ReadReg(h, 0x5000, &v);
        Log("  Before: 0x%08X\n", v);
        WriteReg(h, 0x5000, v | 0x00000001);
        ReadReg(h, 0x5000, &rb);
        Log("  After:  0x%08X\n", rb);
        CheckGrbm(h, "after VMHUB base");
        WriteReg(h, 0x5000, v);
    }

    /* ============================================ */
    /* TEST 10: DF 0x1A214 (MMIO range)            */
    /* ============================================ */
    Log("\n=== TEST 10: DF MMIO range 0x1A214 ===\n"); fflush(g);
    {
        ReadReg(h, 0x1A214, &v);
        Log("  Before: 0x%08X\n", v);
        /* Don't modify MMIO base - too dangerous */
        Log("  (skip write - MMIO base modification dangerous)\n");
        CheckGrbm(h, "DF read only");
    }

    /* ============================================ */
    /* TEST 11: Combined — all writable at once     */
    /* ============================================ */
    Log("\n=== TEST 11: Combined writable attack ===\n"); fflush(g);
    {
        UINT32 bases[] = {0x5000, 0x5180, 0x5300, 0x5480, 0x5600, 0x5780, 0x5900};
        UINT32 origC4[7], origD0[7], origBase[7];
        int i;

        /* Save originals */
        for (i = 0; i < 7; i++) {
            ReadReg(h, bases[i], &origBase[i]);
            ReadReg(h, bases[i] + 0xC4, &origC4[i]);
            ReadReg(h, bases[i] + 0xD0, &origD0[i]);
        }

        /* Modify all VMHUBs */
        for (i = 0; i < 7; i++) {
            WriteReg(h, bases[i], origBase[i] | 0x00000001);
            WriteReg(h, bases[i] + 0xC4, 0x000000FF);
            WriteReg(h, bases[i] + 0xD0, 0x00004001);
        }

        /* NBIO mods */
        WriteReg(h, 0xC174, 0x00000000);
        WriteReg(h, 0xC1A4, 0x02000001);
        WriteReg(h, 0xC1E4, 0x00020300);

        CheckGrbm(h, "combined attack");

        /* Check other blocked regs */
        UINT32 tmp;
        ReadReg(h, 0x2074, &tmp); Log("  Scratch[0x2074] = 0x%08X\n", tmp);
        ReadReg(h, 0x2000, &tmp); Log("  CP[0x2000] = 0x%08X\n", tmp);
        ReadReg(h, 0x0D00, &tmp); Log("  CLK[0x0D00] = 0x%08X\n", tmp);
        ReadReg(h, 0xA000, &tmp); Log("  RSMU[0xA000] = 0x%08X\n", tmp);
        fflush(g);

        /* Restore all */
        for (i = 0; i < 7; i++) {
            WriteReg(h, bases[i], origBase[i]);
            WriteReg(h, bases[i] + 0xC4, origC4[i]);
            WriteReg(h, bases[i] + 0xD0, origD0[i]);
        }
        WriteReg(h, 0xC174, 0x0FFFFFFC);
        WriteReg(h, 0xC1A4, 0x02000000);
        WriteReg(h, 0xC1E4, 0x00020200);
    }

    /* ============================================ */
    /* TEST 12: GRBM write attempt                 */
    /* ============================================ */
    Log("\n=== TEST 12: GRBM write attempt ===\n"); fflush(g);
    {
        ReadReg(h, 0x2004, &v);
        Log("  GRBM before: 0x%08X\n", v);
        BOOL ok = WriteReg(h, 0x2004, 0x00000000);
        ReadReg(h, 0x2004, &rb);
        Log("  GRBM after:  0x%08X (write=%s)\n", rb, ok ? "OK" : "FAIL");
        fflush(g);
    }

    CloseHandle(h);
    Log("\n=== NBIO Unlock v2 Complete ===\n"); fflush(g);
    if (g) fclose(g);
    printf("Done. Check output\\nbio-unlog.log\n");
    return 0;
}
