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
    g = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\rlcg-test.log", "w");
    if (!g) { printf("Cannot open log\n"); return 1; }
    Log("=== RLCG Quick Test ===\n\n"); fflush(g);

    HANDLE h = OpenKmd();
    if (h == INVALID_HANDLE_VALUE) {
        Log("KMD NOT FOUND\n"); fclose(g); return 1;
    }
    UINT32 v, rb;

    InitHardware(h);

    /* Only read the specific RLCG register offsets */
    Log("--- RLCG registers only (0x1500-0x1514) ---\n"); fflush(g);
    {
        UINT32 rlcgOffs[] = {0x1500, 0x1504, 0x1508, 0x150C, 0x1510, 0x1514};
        const char *names[] = {"RLCG_CNTL", "RLCG_DATA", "RLCG_CNTL2", "RLCG_CNTL3", "RLCG_CNTL4", "RLCG_CNTL5"};
        int i;
        for (i = 0; i < 6; i++) {
            ReadReg(h, rlcgOffs[i], &v);
            Log("  %s [0x%04X] = 0x%08X\n", names[i], rlcgOffs[i], v);
        }
        fflush(g);
    }

    /* If RLCG regs are not 0xFFFFFFFF, try indirect read */
    ReadReg(h, 0x1500, &v);
    if (v != 0xFFFFFFFF) {
        Log("\n--- RLCG accessible! Trying GRBM read ---\n"); fflush(g);

        /* Step 1: Write GRBM_STATUS offset to RLCG_CNTL */
        UINT32 cmd = (0x2004 << 2) | 0x1; /* GC_READ */
        WriteReg(h, 0x1500, cmd);
        Log("  Wrote RLCG_CNTL = 0x%08X\n", cmd);

        Sleep(10);

        /* Step 2: Read data */
        ReadReg(h, 0x1504, &rb);
        Log("  RLCG_DATA = 0x%08X\n", rb);

        /* Step 3: Read status */
        ReadReg(h, 0x150C, &v);
        Log("  RLCG_CNTL3 = 0x%08X\n", v);
    } else {
        Log("\n--- RLCG registers blocked ---\n");
    }

    /* Direct compare: read GRBM directly vs RLCG */
    Log("\n--- Direct GRBM read ---\n"); fflush(g);
    ReadReg(h, 0x2004, &v);
    Log("  GRBM direct: 0x%08X\n", v);

    /* Also try writing to RLCG_CNTL with different values */
    Log("\n--- RLCG write probe ---\n"); fflush(g);
    {
        ReadReg(h, 0x1500, &v);
        Log("  RLCG_CNTL before: 0x%08X\n", v);
        WriteReg(h, 0x1500, 0x00000000);
        ReadReg(h, 0x1500, &rb);
        Log("  RLCG_CNTL after write 0: 0x%08X\n", rb);
        WriteReg(h, 0x1500, v);
    }

    Log("\n--- PSP C2PMSG alias probe (GC_BASE shifted aliases) ---\n"); fflush(g);
    {
        UINT32 c2pAlias[] = {
            0x1500, 0x1504, 0x1508, 0x150C, 0x1510, 0x1514
        };
        const char *c2pAliasName[] = {
            "RLCG_CNTL aliased", "RLCG_DATA aliased", "RLCG_CNTL2 aliased",
            "RLCG_CNTL3 aliased", "RLCG_CNTL4 aliased", "RLCG_CNTL5 aliased"
        };
        int j;
        for (j = 0; j < _countof(c2pAlias); j++) {
            ReadReg(h, c2pAlias[j], &v);
            Log("  shifted 0x%04X = 0x%08X  (%s)\n", c2pAlias[j], v, c2pAliasName[j]);
        }
    }

    CloseHandle(h);
    Log("\n=== Done ===\n"); fflush(g);
    if (g) fclose(g);
    printf("Done. Check output\\rlcg-test.log\n");
    return 0;
}
