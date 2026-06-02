#define INITGUID
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

/* IOCTL codes matching the driver */
#define IOCTL_PSP_INIT           0x80000B98
#define IOCTL_PSP_GET_STATUS     0x80000BA4
#define IOCTL_PSP_SEND_COMMAND   0x80000BA0
#define IOCTL_SMN_ACCESS         0x80000BC4

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

    Log("=== PSP INIT TEST ===\n\n");

    HANDLE h = OpenMyDriver();
    if (h == INVALID_HANDLE_VALUE) {
        Log("Driveris nerastas - reikia idiegti!\n");
        fclose(g_log);
        return 1;
    }

    Log("Driveris sekmingai atvertas!\n\n");

    DWORD bytesReturned = 0;
    BOOL ok;

    /* 1. PSP GET STATUS */
    Log("1. PSP GET STATUS:\n");
    UINT32 pspStatus[6] = {0};
    ok = DeviceIoControl(h, IOCTL_PSP_GET_STATUS,
        NULL, 0, pspStatus, sizeof(pspStatus), &bytesReturned, NULL);
    
    if (ok) {
        Log("  Initialized: %u\n", pspStatus[0]);
        Log("  SosAlive: %u\n", pspStatus[1]);
        Log("  FirmwareLoaded: %u\n", pspStatus[2]);
        Log("  MmioBase: 0x%08X\n", pspStatus[3]);
        Log("  SolRegister: 0x%08X\n", pspStatus[4]);
        Log("  C2PMSG_64: 0x%08X\n", pspStatus[5]);
    } else {
        Log("  PSP_GET_STATUS nepavyko (err=%u)\n", GetLastError());
    }
    fflush(g_log);

    /* 2. PSP INIT */
    Log("\n2. PSP INIT:\n");
    ok = DeviceIoControl(h, IOCTL_PSP_INIT,
        NULL, 0, NULL, 0, &bytesReturned, NULL);
    
    if (ok) {
        Log("  PSP_INIT sekmingas!\n");
    } else {
        Log("  PSP_INIT nepavyko (err=%u)\n", GetLastError());
    }
    fflush(g_log);

    /* 3. PSP GET STATUS again */
    Log("\n3. PSP GET STATUS (po init):\n");
    memset(pspStatus, 0, sizeof(pspStatus));
    ok = DeviceIoControl(h, IOCTL_PSP_GET_STATUS,
        NULL, 0, pspStatus, sizeof(pspStatus), &bytesReturned, NULL);
    
    if (ok) {
        Log("  Initialized: %u\n", pspStatus[0]);
        Log("  SosAlive: %u\n", pspStatus[1]);
        Log("  FirmwareLoaded: %u\n", pspStatus[2]);
        Log("  MmioBase: 0x%08X\n", pspStatus[3]);
        Log("  SolRegister: 0x%08X\n", pspStatus[4]);
        Log("  C2PMSG_64: 0x%08X\n", pspStatus[5]);
    } else {
        Log("  PSP_GET_STATUS nepavyko (err=%u)\n", GetLastError());
    }
    fflush(g_log);

    /* 4. SMN ACCESS test */
    Log("\n4. SMN ACCESS test:\n");
    UINT32 smnIn[6] = {0};
    UINT32 smnOut[6] = {0};
    smnIn[0] = 0x1A0E8; /* SMN address to read */
    smnIn[1] = 0;       /* Data (for write) */
    smnIn[2] = 0;       /* IsWrite = 0 (read) */
    smnIn[3] = 0;       /* IndexPort (use default) */
    smnIn[4] = 0;       /* DataPort (use default) */
    smnIn[5] = 0;       /* Result */
    
    ok = DeviceIoControl(h, IOCTL_SMN_ACCESS,
        smnIn, sizeof(smnIn), smnOut, sizeof(smnOut), &bytesReturned, NULL);
    
    if (ok) {
        Log("  SMN[0x%08X] = 0x%08X\n", smnIn[0], smnOut[1]);
        Log("  Result: %u\n", smnOut[5]);
    } else {
        Log("  SMN_ACCESS nepavyko (err=%u)\n", GetLastError());
    }
    fflush(g_log);

    CloseHandle(h);

    Log("\n=== PSP TEST BAIGTAS ===\n");
    fclose(g_log);

    printf("Baigta. Check output\\psp-test.log\n");
    return 0;
}