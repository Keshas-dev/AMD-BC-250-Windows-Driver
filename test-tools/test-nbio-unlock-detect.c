#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

static FILE *g = NULL;
static int g_passed = 0, g_failed = 0;

static void Log(const char *fmt, ...) {
    va_list a; va_start(a, fmt);
    vfprintf(stdout, fmt, a); va_end(a); fflush(stdout);
    if (g) { va_start(a, fmt); vfprintf(g, fmt, a); va_end(a); fflush(g); }
}

static void Check(const char *desc, int condition, const char *passMsg, const char *failMsg) {
    if (condition) { Log("  PASS: %s — %s\n", desc, passMsg); g_passed++; }
    else { Log("  FAIL: %s — %s\n", desc, failMsg); g_failed++; }
}

static HANDLE hKmd = INVALID_HANDLE_VALUE;

static void OpenKmd(void) {
    hKmd = CreateFileW(L"\\\\.\\AMDBC250DreamV43",
        GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
}

static BOOL ReadReg(UINT32 offset, UINT32 *val) {
    UINT32 ra[2] = {offset, 0xDEADBEEF};
    DWORD br = 0;
    BOOL ok = DeviceIoControl(hKmd, 0x80000B88, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
    *val = ra[1];
    return ok;
}

static BOOL WriteReg(UINT32 offset, UINT32 val) {
    UINT32 ra[2] = {offset, val};
    DWORD br = 0;
    return DeviceIoControl(hKmd, 0x80000B8C, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
}

static void InitHardware(void) {
    UCHAR initIn[32] = {0}, initOut[32] = {0};
    DWORD br = 0;
    *(UINT64*)(initIn + 0)  = 0xFE800000ULL;
    *(UINT32*)(initIn + 8)  = 0x00080000;
    *(UINT32*)(initIn + 12) = 1;
    *(UINT64*)(initIn + 16) = 0xC0000000ULL;
    *(UINT32*)(initIn + 24) = 0x10000000;
    DeviceIoControl(hKmd, 0x80000B80, initIn, sizeof(initIn), initOut, sizeof(initOut), &br, NULL);
}

static void GetCUStatus(UINT32 *cuCount, UINT32 *wgpCount, UINT32 *ccReg, UINT32 *spiReg) {
    DWORD buf[8] = {0}, ret = 0;
    if (DeviceIoControl(hKmd, 0x80000984, NULL, 0, buf, sizeof(buf), &ret, NULL)) {
        *cuCount = buf[0]; *wgpCount = buf[1]; *ccReg = buf[2]; *spiReg = buf[3];
    }
}

int main(void) {
    g = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\nbio-detect.log", "w");
    Log("=== NBIO Unlock Detection Tool ===\n\n");

    Log("Opening KMD device...\n");
    OpenKmd();
    Check("KMD device exists", hKmd != INVALID_HANDLE_VALUE, "opened OK", "NOT FOUND");
    if (hKmd == INVALID_HANDLE_VALUE) { Log("\nFATAL: Cannot proceed\n"); goto done; }

    Log("\nInitializing hardware (NBIO_MAP)...\n");
    InitHardware();

    UINT32 v = 0, rb = 0;
    UINT32 cuCount = 0, wgpCount = 0, ccReg = 0, spiReg = 0;

    /* === PHASE 1: Sanity — are we talking to the chip? === */
    Log("\n=== PHASE 1: Chip Communication ===\n");

    ReadReg(0x0000, &v);
    Check("GPU_ID readable", v != 0xFFFFFFFF && v != 0x00000000,
        "OK", "All-ones or zero — no MMIO");
    Log("         GPU_ID = 0x%08X\n", v);

    ReadReg(0x3000, &v);
    Check("GC (0x3000) readable", v != 0xFFFFFFFF && v != 0x00000000,
        "OK", "All-ones or zero");
    Log("         GC     = 0x%08X\n", v);

    ReadReg(0x05A0, &v);
    Check("HDP (0x05A0) readable", v != 0xFFFFFFFF && v != 0x00000000,
        "OK", "All-ones or zero");
    Log("         HDP    = 0x%08X\n", v);

    /* === PHASE 2: PSP SOS alive? === */
    Log("\n=== PHASE 2: PSP SOS Status ===\n");

    ReadReg(0xC100, &v);
    Check("NBIO PS5 signature 1 (0xC100)", v == 0xFEDCBAEF || v != 0xFFFFFFFF,
        "PSP SOS alive", v == 0xFFFFFFFF ? "NBIO blocked" : "unexpected value");
    Log("         NBIO[0xC100] = 0x%08X\n", v);

    ReadReg(0xC180, &v);
    Check("NBIO PS5 signature 2 (0xC180)", v == 0xFEDCBADF || v != 0xFFFFFFFF,
        "PSP SOS alive", v == 0xFFFFFFFF ? "NBIO blocked" : "unexpected value");
    Log("         NBIO[0xC180] = 0x%08X\n", v);

    /* === PHASE 3: NBIO unlock test (the main event) === */
    Log("\n=== PHASE 3: NBIO Unlock Verification ===\n");

    /* CC_GC_SHADER_ARRAY_CONFIG — the CU config register */
    ReadReg(0x2004, &v);
    if (v != 0xFFFFFFFF) {
        const char *detail = (v == 0xFFF80000) ? "Stock 24 CU" :
                             (v == 0xFFE00000) ? "40 CU UNLOCKED!" : "Other value";
        Log("  PASS: CC_GC_SHADER_ARRAY_CONFIG (0x2004) not blocked — %s\n", detail);
        g_passed++;
    } else {
        Log("  FAIL: CC_GC_SHADER_ARRAY_CONFIG (0x2004) not blocked — BLOCKED (0xFFFFFFFF)\n");
        g_failed++;
    }
    Log("         CC_GC_SHADER_ARRAY_CONFIG = 0x%08X\n", v);
    UINT32 ccBaseline = v;

    /* SPI_PG_ENABLE_STATIC_WGP_MASK */
    ReadReg(0x229C, &v);
    if (v != 0xFFFFFFFF) {
        const char *detail = (v == 0x00000007) ? "Stock 3 WGP" :
                             (v == 0x0000001F) ? "5 WGP (40 CU)!" : "Other value";
        Log("  PASS: SPI_PG_ENABLE_STATIC_WGP_MASK (0x229C) not blocked — %s\n", detail);
        g_passed++;
    } else {
        Log("  FAIL: SPI_PG_ENABLE_STATIC_WGP_MASK (0x229C) not blocked — BLOCKED (0xFFFFFFFF)\n");
        g_failed++;
    }
    Log("         SPI_PG_ENABLE_STATIC_WGP_MASK = 0x%08X\n", v);

    /* GRBM_STATUS (0x2000) */
    ReadReg(0x2000, &v);
    Check("GRBM_STATUS (0x2000) not blocked",
        v != 0xFFFFFFFF, "OK — can read engine status", "BLOCKED (0xFFFFFFFF)");
    Log("         GRBM_STATUS = 0x%08X\n", v);

    /* Scratch registers (typical indicator of CP being alive) */
    ReadReg(0x2074, &v);
    Check("CP scratch (0x2074) not blocked",
        v != 0xFFFFFFFF,
        v != 0x00000000 ? "CP alive, scratch readable" : "scratch=0 (possibly zero)",
        "BLOCKED (0xFFFFFFFF)");

    /* RLCG test (0x2600 — SDMA) */
    ReadReg(0x2600, &v);
    Check("SDMA (0x2600) not blocked",
        v != 0xFFFFFFFF, "OK", "BLOCKED (0xFFFFFFFF)");
    Log("         SDMA[0x2600] = 0x%08X\n", v);

    /* === PHASE 4: Write + readback test === */
    Log("\n=== PHASE 4: Write Then Read (0x2004) ===\n");

    ReadReg(0x2004, &v);
    Log("         Before write: 0x%08X\n", v);

    if (v != 0xFFFFFFFF) {
        UINT32 testVal = (v == 0xFFF80000) ? 0xFFE00000 : 0xFFF80000;
        WriteReg(0x2004, testVal);
        ReadReg(0x2004, &rb);
        if (rb == testVal) {
            Log("  PASS: Write+readback on 0x2004 — WRITE WORKS (wrote 0x%08X, read 0x%08X)\n", testVal, rb);
            g_passed++;
        } else {
            Log("  FAIL: Write+readback on 0x2004 — wrote 0x%08X, read 0x%08X\n", testVal, rb);
            g_failed++;
        }
        /* Restore original */
        WriteReg(0x2004, v);
    } else {
        WriteReg(0x2004, 0xFFE00000);
        ReadReg(0x2004, &rb);
        Check("Write attempt on 0x2004", rb != 0xFFFFFFFF,
            "Unblocked by write!", "Still blocked after write attempt");
    }

    /* === PHASE 5: Driver-reported CU status === */
    Log("\n=== PHASE 5: Driver CU Status ===\n");
    GetCUStatus(&cuCount, &wgpCount, &ccReg, &spiReg);
    Log("         Active CUs: %lu\n", cuCount);
    Log("         Active WGPs: %lu\n", wgpCount);
    Log("         CC register: 0x%08X\n", ccReg);
    Log("         SPI register: 0x%08X\n", spiReg);
    Check("Driver reports register values", ccReg != 0xFFFFFFFF,
        "OK", "Driver also sees blocked registers");

    /* === FINAL VERDICT === */
    Log("\n========================================\n");
    Log("FINAL VERDICT\n");
    Log("========================================\n");

    if (ccBaseline == 0xFFE00000) {
        Log("RESULT: 40 CU ALREADY UNLOCKED! (Warm reboot from Linux worked!)\n");
        Log("ACTION: Run unlock IOCTL or proceed with compute workloads\n");
    } else if (ccBaseline != 0xFFFFFFFF) {
        Log("RESULT: NBIO NOT BLOCKED — but CUs not unlocked (%lu CUs)\n", cuCount);
        Log("ACTION: Run UNLOCK_40CU IOCTL (0x80000980 with enable=1)\n");
    } else {
        Log("RESULT: NBIO FIREWALL ACTIVE — GRBM/CP/SDMA blocked (0xFFFFFFFF)\n");
        Log("ACTION: Cold boot CachyOS Linux, verify NBIO, warm reboot into Windows\n");
    }

    Log("\nPassed: %d  Failed: %d\n", g_passed, g_failed);
    Log("=== NBIO Detection Complete ===\n");

done:
    if (hKmd != INVALID_HANDLE_VALUE) CloseHandle(hKmd);
    if (g) fclose(g);
    printf("Done. Check output\\nbio-detect.log\n");
    return g_failed > 0 ? 1 : 0;
}
