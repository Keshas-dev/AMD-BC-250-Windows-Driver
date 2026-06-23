#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>

static HANDLE OpenKmd(void) {
    return CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
}

static BOOL ReadReg(HANDLE h, UINT32 off, UINT32 *val) {
    UINT32 ra[2] = {off, 0xDEADBEEF};
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, 0x80000B88, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
    if (ok && val) *val = ra[1];
    return ok;
}

int main(int argc, char *argv[]) {
    printf("=== Minimal Read Test (NO INIT_HARDWARE) ===\n");

    HANDLE h = OpenKmd();
    if (h == INVALID_HANDLE_VALUE) {
        printf("Cannot open driver (err=%lu)\n", GetLastError());
        return 1;
    }
    printf("Driver opened\n");

    UINT32 v;
    
    printf("\n--- SCRATCH (0x32D4) ---\n");
    if (ReadReg(h, 0x32D4, &v))
        printf("  SCRATCH = 0x%08X\n", v);
    else
        printf("  SCRATCH read FAILED (err=%lu)\n", GetLastError());

    printf("\n--- GPU_ID (0x0000) ---\n");
    if (ReadReg(h, 0x0000, &v))
        printf("  GPU_ID = 0x%08X\n", v);
    else
        printf("  GPU_ID read FAILED (err=%lu)\n", GetLastError());

    printf("\n--- GRBM_STATUS (0x3260) ---\n");
    if (ReadReg(h, 0x3260, &v))
        printf("  GRBM_STATUS = 0x%08X\n", v);
    else
        printf("  GRBM_STATUS read FAILED (err=%lu)\n", GetLastError());

    CloseHandle(h);
    printf("\n=== Done ===\n");
    return 0;
}
