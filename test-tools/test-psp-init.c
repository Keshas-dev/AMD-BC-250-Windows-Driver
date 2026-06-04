#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

/* PSP IOCTL codes (match amdbc250_psp_ioctl.h) */
#define IOCTL_PSP_SET_MMIO_BASE  0x80011414
#define IOCTL_PSP_INIT           0x80011400
#define IOCTL_PSP_GET_STATUS     0x8001140C
#define IOCTL_PSP_UNLOCK_NBIO    0x80011410
#define IOCTL_PSP_FW_LOAD        0x80011404
#define IOCTL_PSP_SMN_ACCESS     0x80011418
#define IOCTL_PSP_READ_REG       0x8001141C
#define IOCTL_PSP_WRITE_REG      0x80011420

static FILE *g_log = NULL;

void Log(const char *fmt, ...) {
    va_list a; va_start(a, fmt);
    vfprintf(stdout, fmt, a); va_end(a);
    if (g_log) { va_start(a, fmt); vfprintf(g_log, fmt, a); va_end(a); fflush(g_log); }
}

HANDLE OpenMyDriver() {
    return CreateFileW(L"\\\\.\\BC250PSP",
        GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
}

int main() {
    g_log = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\psp-test.log", "w");
    if (!g_log) { printf("Cannot open log\n"); return 1; }

    Log("=== PSP INIT TEST (naujas driveris) ===\n\n");

    HANDLE h = OpenMyDriver();
    if (h == INVALID_HANDLE_VALUE) {
        Log("Driveris nerastas - paleisk: sc start amdbc250_psp\n");
        fclose(g_log);
        return 1;
    }
    Log("Driveris atvertas!\n\n");

    DWORD ret = 0;
    BOOL ok;

    /* 1. GET STATUS (pries init) */
    Log("1. PSP GET STATUS (pries init):\n");
    UINT32 st[6] = {0};
    ok = DeviceIoControl(h, IOCTL_PSP_GET_STATUS, NULL, 0, st, sizeof(st), &ret, NULL);
    if (ok) {
        Log("  Initialized=%u SosAlive=%u FwLoaded=%u MmioMapped=%u Err=%u SOL=0x%08X\n",
            st[0], st[1], st[2], st[3], st[4], st[5]);
    } else {
        Log("  FAILED (err=%u)\n", GetLastError());
    }

    /* 2. GET STATUS (po open - turi buti 0) */
    Log("\n2. GET STATUS (dar neinit):\n");
    ok = DeviceIoControl(h, IOCTL_PSP_GET_STATUS, NULL, 0, st, sizeof(st), &ret, NULL);
    if (ok) {
        Log("  Initialized=%u SosAlive=%u FwLoaded=%u MmioMapped=%u Err=%u SOL=0x%08X\n",
            st[0], st[1], st[2], st[3], st[4], st[5]);
    } else {
        Log("  FAILED (err=%u)\n", GetLastError());
    }

    /* 3. SET MMIO BASE (GPU BAR 5 = 0xFE800000, ignored by driver) */
    Log("\n3. SET MMIO BASE:\n");
    UINT64 mmioBase = 0xFE800000ULL;
    UINT32 initIn[4] = { (UINT32)mmioBase, (UINT32)(mmioBase >> 32), 0x80000, 0 };
    ok = DeviceIoControl(h, IOCTL_PSP_SET_MMIO_BASE, initIn, sizeof(initIn), NULL, 0, &ret, NULL);
    Log("  SET_MMIO_BASE(0x%llX): %s\n", mmioBase, ok ? "OK" : "FAIL");

    /* 4. PSP INIT (maps GPU BAR 5 = 0xFE800000, discovers MP0 base) */
    Log("\n4. PSP INIT (pirmas karta):\n");
    ok = DeviceIoControl(h, IOCTL_PSP_INIT, NULL, 0, NULL, 0, &ret, NULL);
    Log("  PSP_INIT: %s (ret=%u)\n", ok ? "OK" : (GetLastError()==997 ? "PENDING" : "FAIL"), ok ? 0 : GetLastError());

    /* 5. GET STATUS (po init) */
    Log("\n5. GET STATUS (po init):\n");
    memset(st, 0, sizeof(st));
    ok = DeviceIoControl(h, IOCTL_PSP_GET_STATUS, NULL, 0, st, sizeof(st), &ret, NULL);
    if (ok) {
        Log("  Initialized=%u SosAlive=%u FwLoaded=%u MmioMapped=%u Err=%u SOL=0x%08X\n",
            st[0], st[1], st[2], st[3], st[4], st[5]);
    } else {
        Log("  FAILED (err=%u)\n", GetLastError());
    }

    /* 6. SMN ACCESS test (po init) */
    Log("\n6. SMN ACCESS test:\n");
    UINT32 smn[5] = {0x1A0E8, 0, 0, 0, 0};
    ok = DeviceIoControl(h, IOCTL_PSP_SMN_ACCESS, smn, sizeof(smn), smn, sizeof(smn), &ret, NULL);
    if (ok) {
        Log("  SMN[0x%08X]=0x%08X result=%u\n", smn[0], smn[1], smn[4]);
    } else {
        Log("  FAILED (err=%u)\n", GetLastError());
    }

    /* 7. NBIO UNLOCK bandymas (po init) */
    Log("\n7. NBIO UNLOCK bandymas:\n");
    UINT32 ul[3] = {0, 0, 0};
    ok = DeviceIoControl(h, IOCTL_PSP_UNLOCK_NBIO, NULL, 0, ul, sizeof(ul), &ret, NULL);
    if (ok) {
        Log("  Unlock Method=%u Result=%u MMHUB=0x%08X\n", ul[0], ul[1], ul[2]);
    } else {
        Log("  FAILED (err=%u)\n", GetLastError());
    }

    /* 8. READ_REG (GPU BAR offset 0 - tikrina ar BAR sumapinta) */
    Log("\n8. READ_REG(0x0000):\n");
    UINT32 reg[4] = {0x0000, 0, 0, 0};
    ok = DeviceIoControl(h, IOCTL_PSP_READ_REG, reg, sizeof(reg), reg, sizeof(reg), &ret, NULL);
    if (ok) {
        Log("  REG[0x%04X]=0x%08X\n", reg[0], reg[1]);
    } else {
        Log("  FAILED (err=%u) - tikimasi, MmioBase NULL\n", GetLastError());
    }

    CloseHandle(h);
    Log("\n=== PSP TEST BAIGTAS ===\n");
    fclose(g_log);
    printf("Baigta. Ziurek output\\psp-test.log\n");
    return 0;
}
