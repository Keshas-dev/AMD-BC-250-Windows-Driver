/* smn-core-unlock-test.c - unlock BC-250's 2 disabled CPU cores via SMU Q3 msg 0x98.
 * Port of rw-r-r-0644/bc250-core-unlock (Linux) to our Windows driver.
 * SMN transport: NBIO BAR5 0x38/0x3C (Windows equivalent of PCI config 0xB8/0xBC).
 *
 * Writes core presence mask (SMN 0x0115A870) 0x77 -> 0xFF. Takes effect on next reboot.
 * Volatile: a cold power cycle reverts it.
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

#define MASK_REG  0x0115A870UL
#define MSG_WRITE_FF 0x98UL
#define Q3_CMD    0x03B10A20UL
#define Q3_RSP    0x03B10A80UL
#define Q3_ARG    0x03B10A88UL

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

static uint32_t smu_rd(uint32_t reg) { return smnR(reg); }
static void smu_wr(uint32_t reg, uint32_t val) { smnW(reg, val); }

/* Returns 1=OK, 0xFF/-1=fail, 0xFE/-2=unknown, 0xFD/-3=rejected, 0xFC/-4=busy, -100=timeout */
static int smu_send(uint32_t msg, uint32_t arg) {
    uint32_t end, st;
    /* wait for RSP idle (done state) */
    for (end = 0; end < 2500; end++) {
        st = smu_rd(Q3_RSP);
        if (st == 0x01 || st == 0xFF || st == 0xFE || st == 0xFD || st == 0xFC) break;
        Sleep(2);
    }
    smu_wr(Q3_RSP, 0);
    smu_wr(Q3_ARG, arg);
    smu_wr(Q3_ARG + 4, 0);
    smu_wr(Q3_CMD, msg);
    for (end = 0; end < 2500; end++) {
        st = smu_rd(Q3_RSP);
        if (st == 0x01) return 1;
        if (st == 0xFF) return -1;
        if (st == 0xFE) return -2;
        if (st == 0xFD) return -3;
        if (st == 0xFC) return -4;
        Sleep(2);
    }
    return -100;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== BC-250 CPU Core Unlock (SMU Q3 msg 0x98 -> SMN 0x0115A870) ===\n\n");

    h = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
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

    /* Sanity: SMU alive via Q3 test message (msg 0x01, returns arg+1) */
    printf("\n--- SMU sanity ---\n");
    int r = smu_send(0x01, 123);
    uint32_t resp = smu_rd(Q3_ARG);
    printf("Q3 test(123): status=%d resp=%u (expect 124)\n", r, resp);
    if (r != 1 || resp != 124) {
        printf("Q3 not responding (status=%d) - aborting\n", r);
        CloseHandle(h);
        return 1;
    }

    /* Read core presence mask */
    printf("\n--- Core presence mask ---\n");
    uint32_t before = smnR(MASK_REG);
    printf("SMN[0x%08X] core mask = 0x%08X\n", MASK_REG, before);

    if ((before & 0xFF) == 0xFF) {
        printf("Mask already 0xFF - all 8 cores enabled. Reboot to pick them up.\n");
        CloseHandle(h);
        return 0;
    }
    if ((before & 0xFF) != 0x77) {
        printf("Unexpected mask 0x%02X (expected 0x77) - ABORTING, refusing to touch\n", before & 0xFF);
        CloseHandle(h);
        return 1;
    }
    printf("Mask is 0x77 = 6 cores. Sending SMU Q3 msg 0x98 (write 0x00FF)...\n");

    /* Send Q3 msg 0x98 with arg = SMN address of core presence mask */
    r = smu_send(MSG_WRITE_FF, MASK_REG);
    printf("Q3 0x98 -> SMN[0x%08X] status=%d\n", MASK_REG, r);
    if (r != 1) {
        printf("FAIL: msg 0x98 returned status %d (expect 1/OK). Did it stick?\n", r);
        CloseHandle(h);
        return 1;
    }

    Sleep(200);
    uint32_t after = smnR(MASK_REG);
    printf("\nAfter write:  SMN[0x%08X] core mask = 0x%08X\n", MASK_REG, after);
    if ((after & 0xFF) != 0xFF) {
        printf("FAIL: mask did not take (still 0x%02X)\n", after & 0xFF);
        CloseHandle(h);
        return 1;
    }

    printf("\n============================================================\n");
    printf("  OK! Core mask is now 0xFF.\n");
    printf("  REBOOT to bring up all 8 cores (16 threads).\n");
    printf("  WARNING: cold power cycle may revert it - rerun after.\n");
    printf("  Stress-test cores before trusting them (MCEs possible).\n");
    printf("============================================================\n");

    CloseHandle(h);
    return 0;
}
