#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

/* Dream driver IOCTL codes */
#define IOCTL_DREAM_PSP_GET_STATUS  0x80000BA4
#define IOCTL_DREAM_READ_REG        0x80000B88
#define IOCTL_DREAM_WRITE_REG       0x80000B8C

static FILE *g_log = NULL;

void Log(const char *fmt, ...) {
    va_list a; va_start(a, fmt);
    vfprintf(stdout, fmt, a); va_end(a);
    if (g_log) { va_start(a, fmt); vfprintf(g_log, fmt, a); va_end(a); fflush(g_log); }
}

HANDLE OpenMyDriver() {
    return CreateFileW(L"\\\\.\\AMDBC250DreamV43",
        GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
}

int main() {
    g_log = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\psp-test.log", "w");
    if (!g_log) { printf("Cannot open log\n"); return 1; }

    Log("=== PSP STATUS (per AMDBC250DreamV43) ===\n\n");

    HANDLE h = OpenMyDriver();
    if (h == INVALID_HANDLE_VALUE) {
        Log("Dream driver nerastas - instaliuok per Device Manager\n");
        fclose(g_log);
        return 1;
    }
    Log("Driveris atvertas!\n\n");

    DWORD ret = 0;
    BOOL ok;

    /* GET PSP STATUS */
    Log("PSP GET STATUS:\n");
    UINT32 st[4] = {0};
    ok = DeviceIoControl(h, IOCTL_DREAM_PSP_GET_STATUS, st, sizeof(st), st, sizeof(st), &ret, NULL);
    if (ok) {
        Log("  PspInitialized=%u  SosAlive=%u  NbioUnlocked=%u  SOL=0x%08X\n",
            st[0], st[1], st[2], st[3]);
        if (st[0]) {
            Log("\n  PSP init OK. SOS=%s, NBIO=%s\n",
                st[1] ? "ALIVE" : "NOT FOUND",
                st[2] ? "UNLOCKED" : "LOCKED");
        } else {
            Log("\n  PSP NOT initialized (non-critical)\n");
        }
    } else {
        Log("  FAILED (err=%u)\n", GetLastError());
    }

    /* READ_REG at BAR5 offset 0 - verify BAR mapping */
    Log("\nREAD_REG(0x0000):\n");
    UINT32 reg[4] = {0x0000, 0, 0, 0};
    ok = DeviceIoControl(h, IOCTL_DREAM_READ_REG, reg, sizeof(reg), reg, sizeof(reg), &ret, NULL);
    if (ok) {
        Log("  REG[0x%04X]=0x%08X\n", reg[0], reg[1]);
    } else {
        Log("  FAILED (err=%u)\n", GetLastError());
    }

    CloseHandle(h);
    Log("\n=== PSP TEST BAIGTAS ===\n");
    fclose(g_log);
    printf("Baigta. Ziurek output\\psp-test.log\n");
    return 0;
}
