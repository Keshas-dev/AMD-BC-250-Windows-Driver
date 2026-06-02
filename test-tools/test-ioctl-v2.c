#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

static FILE *g = NULL;
static void Log(const char *fmt, ...) {
    va_list a; va_start(a, fmt);
    vfprintf(stdout, fmt, a); va_end(a); fflush(stdout);
    if (g) { va_start(a, fmt); vfprintf(g, fmt, a); va_end(a); fflush(g); }
}

int main(void) {
    g = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\ioctl-test.log", "w");
    Log("=== AMDBC250 IOCTL Test v2 ===\n\n");

    /* S1: Open device */
    Log("=== S1: Open Device ===\n");
    HANDLE h = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        Log("FATAL: Cannot open device, error=%lu (0x%08X)\n", err, err);
        fclose(g); return 1;
    }
    Log("Device opened OK\n");

    /* S2: GET_CAPS - 0x80000800 */
    Log("\n=== S2: GET_CAPS (0x80000800) ===\n");
    {
        DWORD buf[16] = {0};
        DWORD ret = 0;
        BOOL ok = DeviceIoControl(h, 0x80000800, NULL, 0, buf, sizeof(buf), &ret, NULL);
        DWORD err = GetLastError();
        Log("  ok=%d  ret=%lu  err=%lu (0x%08X)\n", ok, ret, err, err);
        if (ok) { for (int i = 0; i < (int)(ret/4) && i < 16; i++) Log("  [%d]: 0x%08X\n", i, buf[i]); }
    }

    /* S3: GET_VRAM_INFO - 0x80000804 */
    Log("\n=== S3: GET_VRAM_INFO (0x80000804) ===\n");
    {
        DWORD buf[16] = {0};
        DWORD ret = 0;
        BOOL ok = DeviceIoControl(h, 0x80000804, NULL, 0, buf, sizeof(buf), &ret, NULL);
        DWORD err = GetLastError();
        Log("  ok=%d  ret=%lu  err=%lu (0x%08X)\n", ok, ret, err, err);
        if (ok) { for (int i = 0; i < (int)(ret/4) && i < 16; i++) Log("  [%d]: 0x%08X\n", i, buf[i]); }
    }

    /* S4: DISCOVER_PCI - 0x80000BB4 */
    Log("\n=== S4: DISCOVER_PCI (0x80000BB4) ===\n");
    {
        DWORD buf[64] = {0};
        DWORD ret = 0;
        BOOL ok = DeviceIoControl(h, 0x80000BB4, NULL, 0, buf, sizeof(buf), &ret, NULL);
        DWORD err = GetLastError();
        Log("  ok=%d  ret=%lu  err=%lu (0x%08X)\n", ok, ret, err, err);
        if (ok) { for (int i = 0; i < (int)(ret/4) && i < 64; i++) Log("  [%d]: 0x%08X\n", i, buf[i]); }
    }

    /* S5: GET_RESOURCE_BARS - 0x80000BB8 */
    Log("\n=== S5: GET_RESOURCE_BARS (0x80000BB8) ===\n");
    {
        DWORD buf[32] = {0};
        DWORD ret = 0;
        BOOL ok = DeviceIoControl(h, 0x80000BB8, NULL, 0, buf, sizeof(buf), &ret, NULL);
        DWORD err = GetLastError();
        Log("  ok=%d  ret=%lu  err=%lu (0x%08X)\n", ok, ret, err, err);
        if (ok) { for (int i = 0; i < (int)(ret/4) && i < 32; i++) Log("  [%d]: 0x%08X\n", i, buf[i]); }
    }

    /* S6: GET_HW_STATUS - 0x80000B90 */
    Log("\n=== S6: GET_HW_STATUS (0x80000B90) ===\n");
    {
        DWORD buf[32] = {0};
        DWORD ret = 0;
        BOOL ok = DeviceIoControl(h, 0x80000B90, NULL, 0, buf, sizeof(buf), &ret, NULL);
        DWORD err = GetLastError();
        Log("  ok=%d  ret=%lu  err=%lu (0x%08X)\n", ok, ret, err, err);
        if (ok) { for (int i = 0; i < (int)(ret/4) && i < 32; i++) Log("  [%d]: 0x%08X\n", i, buf[i]); }
    }

    /* S7: GET_CU_STATUS - 0x80000984 */
    Log("\n=== S7: GET_CU_STATUS (0x80000984) ===\n");
    {
        DWORD buf[16] = {0};
        DWORD ret = 0;
        BOOL ok = DeviceIoControl(h, 0x80000984, NULL, 0, buf, sizeof(buf), &ret, NULL);
        DWORD err = GetLastError();
        Log("  ok=%d  ret=%lu  err=%lu (0x%08X)\n", ok, ret, err, err);
        if (ok) { for (int i = 0; i < (int)(ret/4) && i < 16; i++) Log("  [%d]: 0x%08X\n", i, buf[i]); }
    }

    /* S8: READ_REG (GPU_ID) - 0x80000B88 */
    Log("\n=== S8: READ_REG GPU_ID (0x80000B88) ===\n");
    {
        DWORD inBuf[4] = {5, 0x000, 0, 0}; /* BAR5, offset 0x000 */
        DWORD buf[16] = {0};
        DWORD ret = 0;
        BOOL ok = DeviceIoControl(h, 0x80000B88, inBuf, sizeof(inBuf), buf, sizeof(buf), &ret, NULL);
        DWORD err = GetLastError();
        Log("  ok=%d  ret=%lu  err=%lu (0x%08X)\n", ok, ret, err, err);
        if (ok) { for (int i = 0; i < (int)(ret/4) && i < 16; i++) Log("  [%d]: 0x%08X\n", i, buf[i]); }
    }

    /* S9: READ_REG (GRBM_STATUS) - BAR5+0x2004 */
    Log("\n=== S9: READ_REG GRBM_STATUS (0x80000B88) ===\n");
    {
        DWORD inBuf[4] = {5, 0x2004, 0, 0}; /* BAR5, offset 0x2004 */
        DWORD buf[16] = {0};
        DWORD ret = 0;
        BOOL ok = DeviceIoControl(h, 0x80000B88, inBuf, sizeof(inBuf), buf, sizeof(buf), &ret, NULL);
        DWORD err = GetLastError();
        Log("  ok=%d  ret=%lu  err=%lu (0x%08X)\n", ok, ret, err, err);
        if (ok) { for (int i = 0; i < (int)(ret/4) && i < 16; i++) Log("  [%d]: 0x%08X\n", i, buf[i]); }
    }

    /* S10: READ_PCI_CONFIG (GPU) - B1:D0:F0 */
    Log("\n=== S10: READ_PCI_CONFIG (0x80000BAC) ===\n");
    {
        DWORD inBuf[4] = {1, 0, 0, 0}; /* Bus 1, Dev 0, Func 0, Offset 0x00 */
        DWORD buf[16] = {0};
        DWORD ret = 0;
        BOOL ok = DeviceIoControl(h, 0x80000BAC, inBuf, sizeof(inBuf), buf, sizeof(buf), &ret, NULL);
        DWORD err = GetLastError();
        Log("  ok=%d  ret=%lu  err=%lu (0x%08X)\n", ok, ret, err, err);
        if (ok) { for (int i = 0; i < (int)(ret/4) && i < 16; i++) Log("  [%d]: 0x%08X\n", i, buf[i]); }
    }

    /* S11: READ_PCI_BAR - 0x80000B94 */
    Log("\n=== S11: READ_PCI_BAR (0x80000B94) ===\n");
    {
        DWORD inBuf[4] = {0, 0, 0, 0}; /* BAR index 0 */
        DWORD buf[16] = {0};
        DWORD ret = 0;
        BOOL ok = DeviceIoControl(h, 0x80000B94, inBuf, sizeof(inBuf), buf, sizeof(buf), &ret, NULL);
        DWORD err = GetLastError();
        Log("  ok=%d  ret=%lu  err=%lu (0x%08X)\n", ok, ret, err, err);
        if (ok) { for (int i = 0; i < (int)(ret/4) && i < 16; i++) Log("  [%d]: 0x%08X\n", i, buf[i]); }
    }

    /* S12: MMIO_TEST - 0x80000BC0 */
    Log("\n=== S12: MMIO_TEST (0x80000BC0) ===\n");
    {
        DWORD inBuf[8] = {0xC0000000, 0, 0x1000, 0, 0, 0, 0, 0}; /* PA=0xC0000000, Size=0x1000 */
        DWORD buf[32] = {0};
        DWORD ret = 0;
        BOOL ok = DeviceIoControl(h, 0x80000BC0, inBuf, sizeof(inBuf), buf, sizeof(buf), &ret, NULL);
        DWORD err = GetLastError();
        Log("  ok=%d  ret=%lu  err=%lu (0x%08X)\n", ok, ret, err, err);
        if (ok) { for (int i = 0; i < (int)(ret/4) && i < 32; i++) Log("  [%d]: 0x%08X\n", i, buf[i]); }
    }

    CloseHandle(h);
    Log("\n=== IOCTL Test Complete ===\n");
    fclose(g);
    printf("Done. Check output\\ioctl-test.log\n");
    return 0;
}
