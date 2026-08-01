#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AMDBC250_DEVICE_PATH L"\\\\.\\AMDBC250DreamV43"

#define IOCTL_AMDBC250_PSP_SMU_MSG ((ULONG)0x80002490)
#define IOCTL_AMDBC250_INIT_HARDWARE ((ULONG)0x80000B80)

typedef struct {
    ULONG64 MmioPhysicalBase;
    ULONG MmioSize;
    ULONG Flags;
    ULONG64 FbPhysicalBase;
    ULONG FbSize;
} INIT_HW;

typedef struct {
    ULONG Message;
    ULONG Argument;
    ULONG Response;
    ULONG ResponseStatus;
    ULONG Result;
} SMU_MSG;

static HANDLE g_h = INVALID_HANDLE_VALUE;

static int OpenGpu(void) {
    g_h = CreateFileW(AMDBC250_DEVICE_PATH, GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    return (g_h != INVALID_HANDLE_VALUE) ? 0 : -1;
}

static int InitHw(void) {
    INIT_HW ih = {0};
    ih.MmioPhysicalBase = 0xFE800000ULL;
    ih.MmioSize = 0x80000;
    ih.Flags = 1;
    DWORD ret = 0;
    return DeviceIoControl(g_h, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &ret, NULL) ? 0 : -1;
}

static int SmuMsg(ULONG msg, ULONG arg, const char *label) {
    SMU_MSG req = {msg, arg, 0, 0, 0};
    DWORD ret = 0;
    BOOL ok = DeviceIoControl(g_h, IOCTL_AMDBC250_PSP_SMU_MSG, &req, sizeof(req), &req, sizeof(req), &ret, NULL);
    if (ok)
        printf("  %s: resp=0x%08X status=%u result=%u\n", label, req.Response, req.ResponseStatus, req.Result);
    else
        printf("  %s: FAILED (err=%lu)\n", label, GetLastError());
    return (ok && req.Result == 1) ? 0 : -1;
}

int main(void) {
    printf("=== SMU Communication Test (Queue 0) ===\n");
    if (OpenGpu() != 0) { printf("ERROR: Cannot open GPU driver\n"); return 1; }
    if (InitHw() != 0) { printf("Init HW: FAILED (err=%lu)\n", GetLastError()); return 1; }
    printf("Driver OK\n\n");

    SmuMsg(0x01, 0x1234, "TestMessage (0x01)");
    SmuMsg(0x02, 0,      "GetSmuVersion (0x02)");
    SmuMsg(0x03, 0,      "GetDriverIfVersion (0x03)");
    SmuMsg(0x3D, 0,      "GetEnabledSmuFeatures (0x3D)");
    SmuMsg(0x37, 0,      "GetGfxFrequency (0x37)");
    SmuMsg(0x0F, 0,      "QueryGfxclk (0x0F)");
    SmuMsg(0x38, 0,      "GetGfxVid (0x38)");
    SmuMsg(0x1E, 0,      "QueryActiveWgp (0x1E)");

    CloseHandle(g_h);
    return 0;
}
