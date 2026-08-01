/* smn-gc-alias-scan.c - find SMN aliases for BC-250 GPU (GC) registers.
 *
 * v2 (2026-07-31): hardened against the v1 hang.
 *  - Logs progress to log file as it goes, so we can see WHERE it stopped
 *    after a reboot (v1 printed nothing before the hang).
 *  - Step 1 (per-bank GRBM readback) uses CORRECT GRBM_GFX_INDEX bank
 *    select bits (SH = 1<<8, SE = 1<<16) - v1's 0x01/0x10/0x11 selected
 *    INSTANCE index, not SH/SE banks.
 *  - Step 2 SMN scan restricted to known-safe ranges and always logs a
 *    heartbeat; NEVER scans the full 64MB blindly.
 *  - Step 3 SMU msg 0x98 write is DISABLED by default and only runs with
 *    an explicit `-write` arg (v1 wrote 0x00FF to any address that
 *    happened to match GPU_ID - extremely dangerous).
 *
 * Usage:  smn-gc-alias-scan.exe [-scan] [-write <smnaddr>]
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include "..\inc\amdbc250_ioctl.h"

#define MASK_REG 0x0115A870UL
#define MSG_WRITE_FF 0x98UL
#define Q3_CMD 0x03B10A20UL
#define Q3_RSP 0x03B10A80UL
#define Q3_ARG 0x03B10A88UL

#define GRBM_GFX_INDEX 0x34D0
#define SPI_PG         0x5C3C
#define RLC_PG         0x3D64
#define CC_ARRAY       0x9C1C
#define GPU_ID         0x0000
#define GRBM_STATUS    0x3260
#define SCRATCH        0x32D4

static HANDLE h;
static FILE *lf = NULL;

static void Trace(const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    if (lf) { va_copy(ap2, ap); vfprintf(lf, fmt, ap2); va_end(ap2); fflush(lf); }
    vprintf(fmt, ap);
    fflush(stdout);
    va_end(ap);
}

static BOOL W32(uint32_t o, uint32_t v) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD b;
    r.RegisterOffset = o; r.Value = v;
    return DeviceIoControl(h, IOCTL_AMDBC250_WRITE_REG, &r, sizeof(r), &r, sizeof(r), &b, NULL);
}
static uint32_t R32(uint32_t o) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD b;
    r.RegisterOffset = o; r.Value = 0;
    if (DeviceIoControl(h, IOCTL_AMDBC250_READ_REG, &r, sizeof(r), &r, sizeof(r), &b, NULL)) return r.Value;
    return 0xFFFFFFFF;
}
static void smnW(uint32_t a, uint32_t v) { W32(0x38, a); W32(0x3C, v); }
static uint32_t smnR(uint32_t a)         { W32(0x38, a); R32(0x38); return R32(0x3C); }

static int smu_send(uint32_t msg, uint32_t arg) {
    uint32_t end, st;
    for (end = 0; end < 2500; end++) {
        st = smnR(Q3_RSP);
        if (st == 0x01 || st == 0xFF || st == 0xFE || st == 0xFD || st == 0xFC) break;
        Sleep(2);
    }
    smnW(Q3_RSP, 0);
    smnW(Q3_ARG, arg);
    smnW(Q3_ARG + 4, 0);
    smnW(Q3_CMD, msg);
    for (end = 0; end < 2500; end++) {
        st = smnR(Q3_RSP);
        if (st == 0x01) return 1;
        if (st == 0xFF) return -1;
        if (st == 0xFE) return -2;
        if (st == 0xFD) return -3;
        if (st == 0xFC) return -4;
        Sleep(2);
    }
    return -100;
}

static void grbm_select(uint32_t val) { W32(GRBM_GFX_INDEX, val); }

/* GRBM_GFX_INDEX per-bank selection. Layout (gfx10):
 *   bits 7:0  = INSTANCE_INDEX, bits 15:8 = SH_INDEX, bits 23:16 = SE_INDEX
 *   bit 24 = INSTANCE_BROADCAST_WRITES, bit 25 = INSTANCE_BROADCAST_READS
 *   bit 26 = SH_BROADCAST_WRITES,       bit 27 = SH_BROADCAST_READS
 *   bit 28 = SE_BROADCAST_WRITES,       bit 29 = SE_BROADCAST_READS
 */
static const uint32_t BANK_SELECT[] = {
    0x00000000,  /* SE0 SH0 INST0 */
    0x00000100,  /* SE0 SH1 INST0 */
    0x00010000,  /* SE1 SH0 INST0 */
    0x00010100,  /* SE1 SH1 INST0 */
    0x15000000,  /* BCAST writes (INST bit24 + SH bit26 + SE bit28) */
};
static const char *BANK_NAME[] = { "SE0/SH0", "SE0/SH1", "SE1/SH0", "SE1/SH1", "BCAST" };

/* Safe SMN scan regions for BC-250. ONLY ranges proven not to hang.
 *   - 0x01100000-0x01200000 : core/PSP/misc (proven: core-unlock mask write)
 *   - 0x03B10000-0x03B11000 : SMU mailbox blocks (proven)
 * NOTE: range 0x00000000-0x00100000 was REMOVED (2026-07-31) - scanning it
 * hung the machine. GC registers, if aliased, are most likely within these.
 */
static const struct { uint32_t lo, hi; } SAFE_RANGES[] = {
    { 0x01100000UL, 0x01200000UL },
    { 0x03B10000UL, 0x03B11000UL },
};

int main(int argc, char **argv) {
    BOOL doScan = FALSE, doWrite = FALSE;
    uint32_t writeAddr = 0;

    for (int i = 1; i < argc; i++) {
        if (!_stricmp(argv[i], "-scan")) doScan = TRUE;
        else if (!_stricmp(argv[i], "-write") && i + 1 < argc) {
            char *end = NULL;
            uint32_t val = (uint32_t)strtoul(argv[i + 1], &end, 16);
            if (end == argv[i + 1] || *end != '\0') {
                printf("bad -write address: %s (use hex like 0x0115A870)\n", argv[i + 1]);
                return 2;
            }
            doWrite = TRUE; writeAddr = val; i++;
        } else {
            Trace("unknown arg: %s\n", argv[i]);
        }
    }

    char logPath[MAX_PATH];
    GetModuleFileNameA(NULL, logPath, MAX_PATH);
    {
        char *dot = strrchr(logPath, '.');
        if (dot) *dot = '\0';
        strncat(logPath, ".log", MAX_PATH - strlen(logPath) - 1);
    }
    lf = fopen(logPath, "w");
    setvbuf(stdout, NULL, _IONBF, 0);
    Trace("=== SMN GC alias scan v2 ===\n");
    Trace("log: %s\n", logPath);
    Trace("doScan=%d doWrite=%d writeAddr=0x%08X\n", doScan, doWrite, writeAddr);

    h = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { Trace("FAIL CreateFile gle=%lu\n", GetLastError()); if (lf) fclose(lf); return 1; }

    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br = 0;
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL; ih.MmioSize = 0x80000; ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
    if (!DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &br, NULL)) {
        Trace("INIT FAILED gle=%lu\n", GetLastError()); CloseHandle(h); if (lf) fclose(lf); return 1;
    }
    Trace("INIT OK\n");

    /* Step 1: Per-bank SPI_PG / RLC_PG / CC readback */
    Trace("\n=== Step 1: per-bank GRBM readback ===\n");
    uint32_t gpuId = R32(GPU_ID);
    uint32_t grbmStatus = R32(GRBM_STATUS);
    uint32_t grbmIdxLive = R32(GRBM_GFX_INDEX);
    Trace("GPU_ID(0x0000)=0x%08X GRBM_STATUS=0x%08X GRBM_GFX_INDEX=0x%08X\n",
        gpuId, grbmStatus, grbmIdxLive);

    Trace("Bank          SPI_PG(0x5C3C)  RLC_PG(0x3D64)   CC_ARRAY(0x9C1C)  GFX_IDX_after\n");
    Trace("---------------------------------------------------------------\n");
    for (int i = 0; i < 5; i++) {
        grbm_select(BANK_SELECT[i]);
        uint32_t idxAfter = R32(GRBM_GFX_INDEX);
        uint32_t spi = R32(SPI_PG);
        uint32_t rlc = R32(RLC_PG);
        uint32_t cc  = R32(CC_ARRAY);
        Trace("%-13s 0x%08X   0x%08X   0x%08X   0x%08X\n", BANK_NAME[i], spi, rlc, cc, idxAfter);
    }
    grbm_select(0x15000000); /* restore broadcast writes */

    /* Step 1b: per-bank SPI_PG / CC WRITE test (duggasco 40CU unlock values).
     * Linux proves host CAN write these (via kernel/UMR); our unit reads 0.
     * Write the unlock values with correct per-bank GRBM select and check
     * if they stick. CC alone only changes enumeration; SPI is the dispatch
     * gate - BOTH are required. Safe: same values Linux community writes. */
    Trace("\n=== Step 1b: per-bank SPI_PG / CC WRITE test ===\n");
    Trace("Bank          SPI 0x07->0x1F  RLC->0x1F     CC->0xFFE00000\n");
    Trace("-------------------------------------------------------------\n");
    for (int i = 0; i < 5; i++) {
        grbm_select(BANK_SELECT[i]);
        W32(SPI_PG, 0x1F);
        W32(RLC_PG, 0x1F);
        W32(CC_ARRAY, 0xFFE00000UL);
        uint32_t spi = R32(SPI_PG);
        uint32_t rlc = R32(RLC_PG);
        uint32_t cc  = R32(CC_ARRAY);
        Trace("%-13s 0x%08X   0x%08X   0x%08X\n", BANK_NAME[i], spi, rlc, cc);
    }
    grbm_select(0x15000000); /* restore broadcast writes */
    Trace("GRBM_STATUS=0x%08X SCRATCH=0x%08X (after writes)\n",
        R32(GRBM_STATUS), R32(SCRATCH));

    /* Step 2: restricted SMN scan (only with -scan) */
    if (doScan) {
        Trace("\n=== Step 2: restricted SMN scan ===\n");
        int foundGpuId = 0, foundCc = 0;
        int rangeCount = (int)(sizeof(SAFE_RANGES) / sizeof(SAFE_RANGES[0]));
        for (int r = 0; r < rangeCount; r++) {
            uint32_t lo = SAFE_RANGES[r].lo, hi = SAFE_RANGES[r].hi;
            Trace("scanning 0x%08X-0x%08X step 0x100\n", lo, hi);
            uint64_t iter = 0;
            for (uint64_t a = lo; a < hi; a += 0x100) {
                if ((iter & 0xFF) == 0) {
                    Trace("  at 0x%08X (GPU_ID=%d CC=%d)\n", (uint32_t)a, foundGpuId, foundCc);
                }
                iter++;
                uint32_t v = smnR((uint32_t)a);
                if (v == gpuId) {
                    Trace("  GPU_ID match: SMN[0x%08X] = 0x%08X\n", (uint32_t)a, v);
                    foundGpuId++;
                }
                if (v == 0xFFF80000UL || v == 0xFFE00000UL) {
                    Trace("  CC match: SMN[0x%08X] = 0x%08X\n", (uint32_t)a, v);
                    foundCc++;
                }
                if (v == 0x07 && (a & 0xFF) == 0x3C) {
                    Trace("  SPI(0x07) candidate: SMN[0x%08X] = 0x%08X\n", (uint32_t)a, v);
                }
            }
        }
        Trace("Scan done: GPU_ID matches=%d CC matches=%d\n", foundGpuId, foundCc);
    }

    /* Step 3: explicit SMU 0x98 write (only with -write <addr>) */
    if (doWrite) {
        /* Safety: never touch the mailbox block we ourselves are using. */
        if (writeAddr == 0 || (writeAddr >= 0x03B10000UL && writeAddr <= 0x03B11000UL)) {
            Trace("REFUSING unsafe -write address 0x%08X\n", writeAddr);
            CloseHandle(h); if (lf) fclose(lf); return 2;
        }
        Trace("\n=== Step 3: SMU msg 0x98 -> SMN[0x%08X] ===\n", writeAddr);
        uint32_t before = smnR(writeAddr);
        Trace("before = 0x%08X\n", before);
        if (before == 0xFFFFFFFF) {
            Trace("REFUSING: readback failed (dead/black-hole address)\n");
            CloseHandle(h); if (lf) fclose(lf); return 2;
        }
        int r = smu_send(MSG_WRITE_FF, writeAddr);
        Trace("status=%d\n", r);
        if (r != 1) {
            Trace("FAIL: msg 0x98 returned %d\n", r);
            CloseHandle(h); if (lf) fclose(lf); return 1;
        }
        Sleep(200);
        uint32_t after = smnR(writeAddr);
        Trace("after  = 0x%08X\n", after);
        if (after == 0x00FF)
            Trace("*** SMU WRITE STUCK - possible alias! ***\n");
        else if (after == 0xFFFFFFFF)
            Trace("readback failed (0xFFFFFFFF) - cannot confirm\n");
    }

    CloseHandle(h);
    Trace("\nDONE\n");
    if (lf) fclose(lf);
    return 0;
}
