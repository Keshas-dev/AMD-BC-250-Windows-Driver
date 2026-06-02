#define INITGUID
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <d3dkmthk.h>

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

int main(void) {
    g = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\deep-test.log", "w");
    if (!g) { printf("Cannot open log\n"); return 1; }
    Log("=== Deep NBIO/DF/MMHUB + Write Test ===\n"); fflush(g);

    {
        HANDLE h = OpenKmd();
        DWORD br = 0;
        UCHAR inBuf[64] = {0};
        UCHAR outBuf[64] = {0};
        UINT32 v;

        if (h == INVALID_HANDLE_VALUE) {
            Log("KMD NOT FOUND\n"); fclose(g); return 1;
        }

        /* INIT_HARDWARE */
        Log("INIT_HARDWARE...\n"); fflush(g);
        {
            UCHAR initIn[32] = {0};
            UCHAR initOut[32] = {0};
            *(UINT64*)(initIn + 0)  = 0xFE800000ULL;
            *(UINT32*)(initIn + 8)  = 0x00080000;
            *(UINT32*)(initIn + 12) = 1;
            *(UINT64*)(initIn + 16) = 0xC0000000ULL;
            *(UINT32*)(initIn + 24) = 0x10000000;
            DeviceIoControl(h, 0x80000B80, initIn, sizeof(initIn), initOut, sizeof(initOut), &br, NULL);
        }

        /* 1: Full DF scan (0x1A000-0x1A400) */
        Log("\n--- DF Full Scan (0x1A000-0x1A400) ---\n"); fflush(g);
        {
            int count = 0;
            UINT32 i;
            for (i = 0; i < 256; i++) {
                UINT32 off = 0x1A000 + i * 4;
                if (ReadReg(h, off, &v) && v != 0xFFFFFFFF && v != 0x00000000) {
                    Log("  DF[0x%05X] = 0x%08X\n", off, v);
                    count++;
                }
            }
            Log("  DF readable: %d\n", count); fflush(g);
        }

        /* 2: Full MMHUB scan (0x5000-0x5FFF) */
        Log("\n--- MMHUB Full Scan (0x5000-0x5FFF, 1024 regs) ---\n"); fflush(g);
        {
            int count = 0;
            UINT32 i;
            for (i = 0; i < 1024; i++) {
                UINT32 off = 0x5000 + i * 4;
                if (ReadReg(h, off, &v) && v != 0xFFFFFFFF && v != 0x00000000) {
                    Log("  MMHUB[0x%04X] = 0x%08X\n", off, v);
                    count++;
                }
            }
            Log("  MMHUB readable: %d\n", count); fflush(g);
        }

        /* 3: CLK block scan (0x0D00-0x0DFF) */
        Log("\n--- CLK Block Scan (0x0D00-0x0DFF) ---\n"); fflush(g);
        {
            int count = 0;
            UINT32 i;
            for (i = 0; i < 64; i++) {
                UINT32 off = 0x0D00 + i * 4;
                if (ReadReg(h, off, &v) && v != 0xFFFFFFFF && v != 0x00000000) {
                    Log("  CLK[0x%04X] = 0x%08X\n", off, v);
                    count++;
                }
            }
            Log("  CLK readable: %d\n", count); fflush(g);
        }

        /* 4: NBIO block scan (BAR5+0xC000-0xCFFF) */
        Log("\n--- NBIO Block Scan (BAR5+0xC000) ---\n"); fflush(g);
        {
            int count = 0;
            UINT32 i;
            for (i = 0; i < 128; i++) {
                UINT32 off = 0xC000 + i * 4;
                if (ReadReg(h, off, &v) && v != 0xFFFFFFFF && v != 0x00000000) {
                    Log("  NBIO[0x%04X] = 0x%08X\n", off, v);
                    count++;
                }
            }
            Log("  NBIO readable: %d\n", count); fflush(g);
        }

        /* 5: CP/GFX block scan (0x2000-0x2FFF) */
        Log("\n--- CP/GFX Block Scan (0x2000-0x2FFF) ---\n"); fflush(g);
        {
            int count = 0;
            UINT32 i;
            for (i = 0; i < 1024; i++) {
                UINT32 off = 0x2000 + i * 4;
                if (ReadReg(h, off, &v) && v != 0xFFFFFFFF && v != 0x00000000) {
                    Log("  CP[0x%04X] = 0x%08X\n", off, v);
                    count++;
                    if (count >= 30) break;
                }
            }
            Log("  CP/GFX readable: %d (first 30 shown)\n", count); fflush(g);
        }

        /* 6: UVD block (0x2300-0x23FF) */
        Log("\n--- UVD Block (0x2300) ---\n"); fflush(g);
        {
            int count = 0;
            UINT32 i;
            for (i = 0; i < 64; i++) {
                UINT32 off = 0x2300 + i * 4;
                if (ReadReg(h, off, &v) && v != 0xFFFFFFFF && v != 0x00000000) {
                    Log("  UVD[0x%04X] = 0x%08X\n", off, v);
                    count++;
                }
            }
            Log("  UVD readable: %d\n", count); fflush(g);
        }

        /* 7: SDMA block (0x2600-0x26FF) */
        Log("\n--- SDMA Block (0x2600) ---\n"); fflush(g);
        {
            int count = 0;
            UINT32 i;
            for (i = 0; i < 64; i++) {
                UINT32 off = 0x2600 + i * 4;
                if (ReadReg(h, off, &v) && v != 0xFFFFFFFF && v != 0x00000000) {
                    Log("  SDMA[0x%04X] = 0x%08X\n", off, v);
                    count++;
                }
            }
            Log("  SDMA readable: %d\n", count); fflush(g);
        }

        /* 8: WRITE TEST — try writing to SCRATCH (known blocked) */
        Log("\n--- Write Test ---\n"); fflush(g);
        Log("  Writing 0xDEADBEEF to SCRATCH[0x2074] (expect FAIL)...\n"); fflush(g);
        {
            BOOL ok = WriteReg(h, 0x2074, 0xDEADBEEF);
            ReadReg(h, 0x2074, &v);
            Log("  Write result: %s  ReadBack=0x%08X\n", ok ? "OK" : "FAIL", v);
            fflush(g);
        }

        /* 9: WRITE TEST — try writing to HDP[0x05DC] (known readable) */
        Log("  Writing 0x12345678 to HDP[0x05DC]...\n"); fflush(g);
        {
            ReadReg(h, 0x05DC, &v);
            Log("  Before: 0x%08X\n", v);
            BOOL ok = WriteReg(h, 0x05DC, 0x12345678);
            UINT32 rb = 0;
            ReadReg(h, 0x05DC, &rb);
            Log("  Write result: %s  ReadBack=0x%08X\n", ok ? "OK" : "FAIL", rb);
            if (rb == 0x12345678)
                Log("  *** WRITE TO HDP SUCCEEDED! ***\n");
            fflush(g);
        }

        /* 10: WRITE TEST — MMHUB[0x50D0] (known readable = 0x00004000) */
        Log("  Writing 0x00004001 to MMHUB[0x50D0]...\n"); fflush(g);
        {
            ReadReg(h, 0x50D0, &v);
            Log("  Before: 0x%08X\n", v);
            BOOL ok = WriteReg(h, 0x50D0, 0x00004001);
            UINT32 rb = 0;
            ReadReg(h, 0x50D0, &rb);
            Log("  Write result: %s  ReadBack=0x%08X\n", ok ? "OK" : "FAIL", rb);
            if (rb == 0x00004001)
                Log("  *** WRITE TO MMHUB SUCCEEDED! ***\n");
            fflush(g);
        }

        /* 11: WRITE TEST — GC[0x3008] (known 0) */
        Log("  Writing 0x00000001 to GC[0x3008]...\n"); fflush(g);
        {
            ReadReg(h, 0x3008, &v);
            Log("  Before: 0x%08X\n", v);
            BOOL ok = WriteReg(h, 0x3008, 0x00000001);
            UINT32 rb = 0;
            ReadReg(h, 0x3008, &rb);
            Log("  Write result: %s  ReadBack=0x%08X\n", ok ? "OK" : "FAIL", rb);
            if (rb == 0x00000001)
                Log("  *** WRITE TO GC SUCCEEDED! ***\n");
            fflush(g);
        }

        CloseHandle(h);
    }

    Log("\n=== Deep Test Complete ===\n"); fflush(g);
    if (g) fclose(g);
    printf("Done. Check output\\deep-test.log\n");
    return 0;
}
