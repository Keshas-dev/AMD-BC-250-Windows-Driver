/* robin3-metrics-test.c - dump Robin3 SMU table 3 (per-core CPU/L3 metrics) via Q4 mailbox.
 * Port of Linux bc250-steamos bc250-cyan-skillfish-8core-cpu-metrics.patch to our Windows driver.
 *
 * Protocol (verified in ROBIN3-METRICS-ANALYSIS.md):
 *   Q4 mailbox (SMN): CMD=0x03B10A24, RSP=0x03B10A84, ARG=0x03B10A8C
 *   msg 0x3e SET_TOOLS_HIGH (SMU DTOOLS addr high), 0x3f SET_TOOLS_LOW (low), 0x22 TRANSFER_TABLE
 *   TRANSFER arg = TABLE_PMSTATUSLOG = 3  (smu11_driver_if_cyan_skillfish.h: NOT the generic 7!)
 *   Table 3 = 209 floats (0x344 bytes); rows:
 *     CORE_POWER 0x118 [8] W  -> mW   (x1000)
 *     CORE_TEMP  0x158 [8] C  -> centi-C (x100)
 *     CORE_FREQ  0x198 [8] GHz-> MHz   (x1000)
 *     L3_TEMP    0x2A8 [2] C  -> centi-C
 *     L3_FREQ    0x2C0 [2] GHz-> MHz
 *   Gate: SMU version 0x00580600, CORE_PRESENCE 0x0115A870 == 0xFF (8 cores).
 *   Physical dest: ALLOC_VIDMEM (after INIT_HARDWARE this is MmAllocateContiguousMemory,
 *   32-bit <4GB, capped 64KB - amdbc250_dream_kmd.c:3262) + MMIO_TEST for user-mode
 *   read/write (VA returned by ALLOC_VIDMEM is kernel-only).
 *
 * Usage: robin3-metrics-test.exe [-force]
 *   -force: skip the SMU-version/8-core gates (exploratory).
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "..\inc\amdbc250_ioctl.h"

#define Q4_CMD   0x03B10A24UL
#define Q4_RSP   0x03B10A84UL
#define Q4_ARG   0x03B10A8CUL

#define Q0_CMD   0x03B10A08UL
#define Q0_RSP   0x03B10A68UL
#define Q0_ARG   0x03B10A48UL

#define Q4_SET_TOOLS_HIGH 0x3eUL
#define Q4_SET_TOOLS_LOW  0x3fUL
#define Q4_TRANSFER_TABLE 0x22UL
#define TABLE_PMSTATUSLOG 3UL

#define CORE_PRESENCE     0x0115A870UL
#define ROBIN3_SMU_VER    0x00580600UL

/* KMD IOCTL_AMDBC250_ALLOC_VIDMEM is hardwired as case 0x80000840 (see
 * amdbc250_dream_kmd.c:3262). The header macro computes 0x80000A00 (WRONG) - use the
 * raw code the driver actually dispatches on. Matches test-gpu-ioctls.c. */
#define IOCTL_ALLOC_VIDMEM_RAW 0x80000840UL

#define T3_SIZE     0x344      /* 209 floats */
#define T3_DWORDS   (T3_SIZE / 4)
#define T3_CORE_POWER 0x118
#define T3_CORE_TEMP  0x158
#define T3_CORE_FREQ  0x198
#define T3_L3_TEMP    0x2A8
#define T3_L3_FREQ    0x2C0
#define CORE_COUNT    8
#define L3_COUNT      2

static HANDLE h;

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
static uint32_t smnR(uint32_t a)        { W32(0x38, a); R32(0x38); return R32(0x3C); }
static uint32_t smu_rd(uint32_t reg)    { return smnR(reg); }
static void smu_wr(uint32_t reg, uint32_t val) { smnW(reg, val); }

/* Generic mailbox send for a 3-register (cmd/rsp/arg) queue. Returns 1=OK, -1..-4=SMU error, -100=timeout. */
static int mbox_send(uint32_t cmd_reg, uint32_t rsp_reg, uint32_t arg_reg, uint32_t msg, uint32_t arg) {
    uint32_t end, st;
    for (end = 0; end < 2500; end++) {
        st = smu_rd(rsp_reg);
        if (st == 0x01 || st == 0xFF || st == 0xFE || st == 0xFD || st == 0xFC) break;
        Sleep(2);
    }
    smu_wr(rsp_reg, 0);
    smu_wr(arg_reg, arg);
    smu_wr(arg_reg + 4, 0);
    smu_wr(cmd_reg, msg);
    for (end = 0; end < 2500; end++) {
        st = smu_rd(rsp_reg);
        if (st == 0x01) return 1;
        if (st == 0xFF) return -1;
        if (st == 0xFE) return -2;
        if (st == 0xFD) return -3;
        if (st == 0xFC) return -4;
        Sleep(2);
    }
    return -100;
}
static int q4_send(uint32_t msg, uint32_t arg) { return mbox_send(Q4_CMD, Q4_RSP, Q4_ARG, msg, arg); }
static int q0_send(uint32_t msg, uint32_t arg) { return mbox_send(Q0_CMD, Q0_RSP, Q0_ARG, msg, arg); }

/* MMIO_TEST: map PhysicalAddress and read DWORD at OffsetRead. Returns 1=ok (Val filled). */
static int mmio_read(uint64_t pa, uint32_t off, uint32_t *val) {
    AMDBC250_IOCTL_MMIO_TEST m; DWORD b;
    ZeroMemory(&m, sizeof(m));
    m.PhysicalAddress = pa; m.Size = 0x1000;
    m.OffsetRead = off; m.OffsetWrite = 0;
    if (!DeviceIoControl(h, IOCTL_AMDBC250_MMIO_TEST, &m, sizeof(m), &m, sizeof(m), &b, NULL)) return 0;
    if (!m.MapResult) return 0;
    if (val) *val = m.ValueRead;
    return 1;
}
/* MMIO_TEST: write DWORD at OffsetWrite, verify read-back. Returns 1=ok (Back filled). */
static int mmio_write(uint64_t pa, uint32_t off, uint32_t val, uint32_t *back) {
    AMDBC250_IOCTL_MMIO_TEST m; DWORD b;
    ZeroMemory(&m, sizeof(m));
    m.PhysicalAddress = pa; m.Size = 0x1000;
    m.OffsetRead = off; m.OffsetWrite = off; m.ValueWrite = val;
    if (!DeviceIoControl(h, IOCTL_AMDBC250_MMIO_TEST, &m, sizeof(m), &m, sizeof(m), &b, NULL)) return 0;
    if (!m.MapResult) return 0;
    if (back) *back = m.ValueWrittenBack;
    return 1;
}

/* Decode a float32 field. scale multiplies the float (e.g. x1000). Returns 1=ok, 0=invalid (poison/NaN/neg/out-of-range). */
static int decode_float(uint32_t bits, uint32_t scale, uint32_t minv, uint32_t maxv, uint32_t *out) {
    if (bits & 0x80000000UL) return 0;          /* negative -> invalid */
    if (((bits >> 23) & 0xff) == 0xff) return 0; /* inf/nan (incl. poison 0xFFFFFFFF) */
    float f;
    memcpy(&f, &bits, 4);
    double v = (double)f * (double)scale;
    if (v < (double)minv || v > (double)maxv) return 0;
    *out = (uint32_t)v;
    return 1;
}

int main(int argc, char **argv) {
    int force = 0;
    if (argc > 1 && strcmp(argv[1], "-force") == 0) force = 1;
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== BC-250 Robin3 SMU table 3 dump (Q4 mailbox -> ALLOC_VIDMEM) ===\n\n");

    h = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("FAIL: CreateFile gle=%lu\n", GetLastError());
        return 1;
    }

    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br = 0;
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL;
    ih.MmioSize = 0x80000;
    ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
    if (!DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &br, NULL)) {
        printf("INIT_HARDWARE FAILED gle=%lu\n", GetLastError());
        CloseHandle(h);
        return 1;
    }
    printf("INIT_HARDWARE OK\n");

    /* Gates */
    printf("\n--- Gates ---\n");
    uint32_t ver = 0;
    if (q0_send(0x02, 0) == 1) ver = smu_rd(Q0_ARG);
    printf("SMU version (Q0 msg 0x02) = 0x%08X (Robin3 gate 0x00580600)\n", ver);
    uint32_t mask = smnR(CORE_PRESENCE);
    printf("CORE_PRESENCE SMN[0x%08X] = 0x%08X (%s)\n", CORE_PRESENCE, mask,
        (mask & 0xFF) == 0xFF ? "8 cores" : "NOT 8 cores");

    if (!force) {
        if (ver != ROBIN3_SMU_VER) {
            printf("FAIL: SMU version != Robin3 (0x00580600). Use -force to override.\n");
            CloseHandle(h);
            return 1;
        }
        if ((mask & 0xFF) != 0xFF) {
            printf("FAIL: cores not unlocked (0x%02X). Run smn-core-unlock-test + reboot first. Use -force to override.\n",
                mask & 0xFF);
            CloseHandle(h);
            return 1;
        }
    }

    /* Allocate DMA buffer. NOTE: since INIT_HARDWARE set HardwareInitialized=TRUE, the KMD
     * lands on the alloc-VIDMEM handler at amdbc250_dream_kmd.c:3262: MmAllocateContiguousMemory
     * capped at 64KB, physical < 4GB (32-bit). OutData[0]=physical, OutData[1]=kernel VA.
     * (The 40-bit MDL handler only runs when HardwareInitialized is FALSE.) */
    printf("\n--- ALLOC_VIDMEM ---\n");
    ULONG  inData[3] = { 4096, 0, 0 };
    ULONG64 outData[2] = { 0, 0 };
    DWORD br2 = 0;
    if (!DeviceIoControl(h, IOCTL_ALLOC_VIDMEM_RAW, inData, sizeof(inData),
        outData, sizeof(outData), &br2, NULL)) {
        printf("FAIL: ALLOC_VIDMEM gle=%lu\n", GetLastError());
        CloseHandle(h);
        return 1;
    }
    uint64_t pa = outData[0];
    printf("Alloc 4096B: PA=0x%llX (kernel VA=0x%llX)\n",
        (unsigned long long)pa, (unsigned long long)outData[1]);

    /* Poison the table region. CRITICAL: KMD MMIO_TEST skips the write when OffsetWrite==0,
     * so offset 0 can never be poisoned - start at DWORD index 1. No decoded field lives at
     * byte 0, so skipping it is harmless. */
    printf("\n--- Poisoning table (write 0xFFFFFFFF x %u) ---\n", T3_DWORDS - 1);
    int poisoned = 0;
    for (int i = 1; i < T3_DWORDS; i++) {
        uint32_t back = 0;
        if (!mmio_write(pa, (uint32_t)(i * 4), 0xFFFFFFFFUL, &back) || back != 0xFFFFFFFFUL) {
            printf("FAIL: poison write@0x%X back=0x%08X\n", i * 4, back);
            break;
        }
        poisoned++;
    }
    if (poisoned != T3_DWORDS - 1) {
        printf("FAIL: poison incomplete (%d/%d) - cannot trust readback.\n", poisoned, T3_DWORDS - 1);
        CloseHandle(h);
        return 1;
    }
    printf("Poison OK (%d DWORDS 1..%u). Buffer is host-writable/readable via MMIO_TEST.\n",
        poisoned, T3_DWORDS - 1);

    /* Program SMU tools DMA address + transfer table 3 */
    printf("\n--- Q4 transfer (table %u) ---\n", TABLE_PMSTATUSLOG);
    uint32_t hi = (uint32_t)(pa >> 32), lo = (uint32_t)(pa & 0xFFFFFFFF);
    printf("SET_TOOLS_HIGH (0x3e) hi=0x%08X ... ", hi);
    int r = q4_send(Q4_SET_TOOLS_HIGH, hi);
    printf("status=%d\n", r);
    printf("SET_TOOLS_LOW  (0x3f) lo=0x%08X ... ", lo);
    r = q4_send(Q4_SET_TOOLS_LOW, lo);
    printf("status=%d\n", r);
    printf("TRANSFER_TABLE (0x22) table=%u ... ", TABLE_PMSTATUSLOG);
    r = q4_send(Q4_TRANSFER_TABLE, TABLE_PMSTATUSLOG);
    printf("status=%d\n", r);
    if (r != 1) {
        printf("FAIL: transfer returned status %d\n", r);
        CloseHandle(h);
        return 1;
    }

    Sleep(100);

    /* Read back the whole table. DWORD 0 was never poisoned (KMD skips OffsetWrite==0), so
     * only count DWORDS 1.. for the "changed from poison" detection. */
    printf("\n--- Readback table (%u DWORDS) ---\n", T3_DWORDS);
    uint32_t buf[T3_DWORDS];
    int changed = 0;
    for (int i = 0; i < T3_DWORDS; i++) {
        uint32_t v = 0;
        if (!mmio_read(pa, (uint32_t)(i * 4), &v)) {
            printf("FAIL: read@0x%X\n", i * 4);
            CloseHandle(h);
            return 1;
        }
        buf[i] = v;
        if (i > 0 && v != 0xFFFFFFFFUL) changed++;
    }
    printf("Readback OK: %d/%d DWORDS (1..) changed from poison\n", changed, T3_DWORDS - 1);
    if (changed == 0) {
        printf("NO SMU WRITE DETECTED. Transfer either failed silently or table 3 not populated.\n");
    }

    /* Decode known rows */
    printf("\n--- Per-core metrics (Robin3 table 3) ---\n");
    int rows = 0;
    for (int c = 0; c < CORE_COUNT; c++) {
        uint32_t pw = 0, tmp = 0, fq = 0;
        int okp = decode_float(buf[(T3_CORE_POWER + c * 4) / 4], 1000, 1, 30000, &pw);
        int okt = decode_float(buf[(T3_CORE_TEMP + c * 4) / 4], 100, 1, 15000, &tmp);
        int okf = decode_float(buf[(T3_CORE_FREQ + c * 4) / 4], 1000, 1, 6000, &fq);
        printf("Core%d: power=%s%3umW  temp=%s%3u.%02uC  freq=%s%4uMHz\n",
            c,
            okp ? "" : "? ", pw,
            okt ? "" : "? ", tmp / 100, tmp % 100,
            okf ? "" : "? ", fq);
        rows += (okp | okt | okf);
    }
    for (int l = 0; l < L3_COUNT; l++) {
        uint32_t tmp = 0, fq = 0;
        int okt = decode_float(buf[(T3_L3_TEMP + l * 4) / 4], 100, 1, 15000, &tmp);
        int okf = decode_float(buf[(T3_L3_FREQ + l * 4) / 4], 1000, 1, 6000, &fq);
        printf("L3#%d: temp=%s%3u.%02uC  freq=%s%4uMHz\n",
            l,
            okt ? "" : "? ", tmp / 100, tmp % 100,
            okf ? "" : "? ", fq);
        rows += (okt | okf);
    }
    if (rows == 0) {
        printf("No valid decoded rows (all poison/invalid). Table may not be populated.\n");
    }

    /* Full float dump (all 209) for analysis of undocumented fields */
    printf("\n--- Full table dump (float32 at each DWORD offset) ---\n");
    for (int i = 0; i < T3_DWORDS; i++) {
        uint32_t b = buf[i];
        float f;
        memcpy(&f, &b, 4);
        printf("0x%03X: 0x%08X  %.6f\n", i * 4, b, f);
    }

    printf("\nDone.\n");
    CloseHandle(h);
    return 0;
}
