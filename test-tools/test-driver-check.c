#define INITGUID
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

/* IOCTL codes matching the driver */
#define IOCTL_GET_GPU_INFO       0x80000C00
#define IOCTL_GET_FIREWALL_STATUS 0x80000C04
#define IOCTL_TEST_REGISTER      0x80000C08

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
    g_log = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\driver-check.log", "w");
    if (!g_log) { printf("Cannot open log\n"); return 1; }

    Log("=== DRIVERIO PATIKRINIMAS ===\n\n");

    HANDLE h = OpenMyDriver();
    if (h == INVALID_HANDLE_VALUE) {
        Log("Driveris nerastas - reikia idiegti!\n");
        fclose(g_log);
        return 1;
    }

    Log("Driveris sekmingai atvertas!\n\n");

    DWORD bytesReturned = 0;
    BOOL ok;

    /* 1. GPU INFO - 48 bytes output, raw UINT32 array */
    UINT32 gpuInfo[12] = {0};
    ok = DeviceIoControl(h, IOCTL_GET_GPU_INFO,
        NULL, 0, gpuInfo, sizeof(gpuInfo), &bytesReturned, NULL);

    if (ok) {
        Log("GPU INFO:\n");
        Log("  GPU ID:    0x%08X\n", gpuInfo[0]);
        Log("  Vendor ID: 0x%04X\n", gpuInfo[1]);
        Log("  Device ID: 0x%04X\n", gpuInfo[2]);
        Log("  CUs:       %u\n", gpuInfo[3]);
        Log("  Shaders:   %u\n", gpuInfo[4]);
        /* Architecture is packed as 4-byte chars */
        char arch[25] = {0};
        memcpy(arch, &gpuInfo[5], 20);
        Log("  Arch:      %s\n", arch);
    } else {
        Log("GPU_INFO testas nepavyko (err=%u)\n", GetLastError());
    }
    fflush(g_log);

    /* 2. FIREWALL STATUS - 12 bytes output */
    UINT32 fwStatus[3] = {0};
    ok = DeviceIoControl(h, IOCTL_GET_FIREWALL_STATUS,
        NULL, 0, fwStatus, sizeof(fwStatus), &bytesReturned, NULL);

    if (ok) {
        Log("\nFIREWALL STATUS:\n");
        Log("  Leidziami blokai: %u\n", fwStatus[0]);
        Log("  Uzblokuoti (skaitymas): %u\n", fwStatus[1]);
        Log("  Uzblokuoti (rasymas): %u\n", fwStatus[2]);
    } else {
        Log("FIREWALL_STATUS testas nepavyko (err=%u)\n", GetLastError());
    }
    fflush(g_log);

    /* 3. REGISTER TEST - MMHUB[0x50D0] - 8 bytes in, 20 bytes out */
    UINT32 regIn[2] = {0x50D0, 0x4001};
    UINT32 regOut[5] = {0};

    ok = DeviceIoControl(h, IOCTL_TEST_REGISTER,
        regIn, sizeof(regIn), regOut, sizeof(regOut), &bytesReturned, NULL);

    if (ok) {
        Log("\nMMHUB[0x50D0] TEST:\n");
        Log("  Pries: 0x%08X\n", regOut[0]);
        Log("  Po:    0x%08X\n", regOut[1]);
        Log("  Sekmingas: %s\n", regOut[2] ? "TAIP" : "NE");
    } else {
        Log("MMHUB[0x50D0] testas nepavyko (err=%u)\n", GetLastError());
    }
    fflush(g_log);

    /* 4. REGISTER TEST - GC[0x3008] */
    regIn[0] = 0x3008;
    regIn[1] = 0x0001;
    memset(regOut, 0, sizeof(regOut));

    ok = DeviceIoControl(h, IOCTL_TEST_REGISTER,
        regIn, sizeof(regIn), regOut, sizeof(regOut), &bytesReturned, NULL);

    if (ok) {
        Log("\nGC[0x3008] TEST:\n");
        Log("  Pries: 0x%08X\n", regOut[0]);
        Log("  Po:    0x%08X\n", regOut[1]);
        Log("  Sekmingas: %s\n", regOut[2] ? "TAIP" : "NE");
    } else {
        Log("GC[0x3008] testas nepavyko (err=%u)\n", GetLastError());
    }
    fflush(g_log);

    CloseHandle(h);

    Log("\n=== PATIKRINIMAS BAIGTAS ===\n");
    fclose(g_log);

    printf("Baigta. Check output\\driver-check.log\n");
    return 0;
}