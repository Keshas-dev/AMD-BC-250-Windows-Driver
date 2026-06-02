#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

static FILE *g = NULL;
static void Log(const char *fmt, ...) {
    va_list a; va_start(a, fmt);
    vfprintf(stdout, fmt, a); va_end(a); fflush(stdout);
    if (g) { va_start(a, fmt); vfprintf(g, fmt, a); va_end(a); fflush(g); }
}

#define IOCTL_AMDBC250_GET_CAPS             0x80000800
#define IOCTL_AMDBC250_GET_VRAM_INFO        0x80000804
#define IOCTL_AMDBC250_GET_RESOURCE_BARS    0x80000BB8
#define IOCTL_AMDBC250_DISCOVER_PCI         0x80000BB4
#define IOCTL_AMDBC250_READ_REG             0x80000B88
#define IOCTL_AMDBC250_READ_PCI_CONFIG      0x80000BAC
#define IOCTL_AMDBC250_GET_HW_STATUS        0x80000B90
#define IOCTL_AMDBC250_GET_CU_STATUS        0x80000984
#define IOCTL_AMDBC250_READ_PCI_BAR         0x80000B94
#define IOCTL_AMDBC250_MMIO_TEST            0x80000BC0

static HANDLE OpenDevice(void) {
    const wchar_t *paths[] = {
        L"\\\\.\\AMDBC250DreamV43",
        L"\\\\.\\AMDBC250-DREAM-V4.3",
        NULL
    };
    for (int i = 0; paths[i]; i++) {
        HANDLE h = CreateFileW(paths[i], GENERIC_READ | GENERIC_WRITE, 0, NULL,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            Log("  Opened device: %ls\n", paths[i]);
            return h;
        }
        Log("  Open %ls: error %lu\n", paths[i], GetLastError());
    }
    return INVALID_HANDLE_VALUE;
}

static BOOL Ioctl(HANDLE h, DWORD code, void *in, DWORD inSz, void *out, DWORD outSz, DWORD *ret) {
    BOOL ok = DeviceIoControl(h, code, in, inSz, out, outSz, ret, NULL);
    if (!ok) {
        DWORD err = GetLastError();
        Log("  GetLastError: %lu (0x%08X)\n", err, err);
    }
    return ok;
}

int main(void) {
    g = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\ioctl-test.log", "w");
    Log("=== AMDBC250 IOCTL Test ===\n\n");

    /* S1: Open device */
    Log("=== S1: Open Device ===\n");
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) {
        Log("FATAL: Cannot open any device path\n");
        fclose(g);
        return 1;
    }

    /* S2: GET_CAPS */
    Log("\n=== S2: IOCTL_AMDBC250_GET_CAPS ===\n");
    {
        UCHAR buf[256] = {0};
        DWORD ret = 0;
        BOOL ok = Ioctl(h, IOCTL_AMDBC250_GET_CAPS, NULL, 0, buf, sizeof(buf), &ret);
        Log("  Result: %s  BytesReturned=%lu\n", ok ? "OK" : "FAIL", ret);
        if (ok && ret >= 4) {
            DWORD *vals = (DWORD*)buf;
            for (int i = 0; i < (int)(ret/4) && i < 16; i++) {
                Log("  [%d]: 0x%08X\n", i, vals[i]);
            }
        }
    }

    /* S3: GET_VRAM_INFO */
    Log("\n=== S3: IOCTL_AMDBC250_GET_VRAM_INFO ===\n");
    {
        UCHAR buf[256] = {0};
        DWORD ret = 0;
        BOOL ok = Ioctl(h, IOCTL_AMDBC250_GET_VRAM_INFO, NULL, 0, buf, sizeof(buf), &ret);
        Log("  Result: %s  BytesReturned=%lu\n", ok ? "OK" : "FAIL", ret);
        if (ok && ret >= 4) {
            DWORD *vals = (DWORD*)buf;
            for (int i = 0; i < (int)(ret/4) && i < 16; i++) {
                Log("  [%d]: 0x%08X (%u)\n", i, vals[i], vals[i]);
            }
        }
    }

    /* S4: DISCOVER_PCI */
    Log("\n=== S4: IOCTL_AMDBC250_DISCOVER_PCI ===\n");
    {
        UCHAR buf[1024] = {0};
        DWORD ret = 0;
        BOOL ok = Ioctl(h, IOCTL_AMDBC250_DISCOVER_PCI, NULL, 0, buf, sizeof(buf), &ret);
        Log("  Result: %s  BytesReturned=%lu\n", ok ? "OK" : "FAIL", ret);
        if (ok && ret >= 4) {
            DWORD *vals = (DWORD*)buf;
            int count = vals[0];
            Log("  DeviceCount: %u\n", count);
            for (int i = 0; i < count && i < 8; i++) {
                DWORD *dev = &vals[1 + i * 8];
                Log("  Device[%d]: VID=0x%04X DID=0x%04X BDF=%u:%u.%u Class=0x%08X\n",
                    i, dev[0] & 0xFFFF, (dev[0] >> 16) & 0xFFFF,
                    dev[1], (dev[2] >> 5) & 0xFF, dev[2] & 7, dev[3]);
            }
        }
    }

    /* S5: GET_RESOURCE_BARS */
    Log("\n=== S5: IOCTL_AMDBC250_GET_RESOURCE_BARS ===\n");
    {
        UCHAR buf[512] = {0};
        DWORD ret = 0;
        BOOL ok = Ioctl(h, IOCTL_AMDBC250_GET_RESOURCE_BARS, NULL, 0, buf, sizeof(buf), &ret);
        Log("  Result: %s  BytesReturned=%lu\n", ok ? "OK" : "FAIL", ret);
        if (ok && ret >= 4) {
            DWORD *vals = (DWORD*)buf;
            int count = vals[0];
            Log("  BarCount: %u\n", count);
            for (int i = 0; i < count && i < 7; i++) {
                DWORD *bar = &vals[1 + i * 4];
                Log("  BAR[%d]: Base=0x%08X%08X Size=0x%08X%08X Type=%s Prefetch=%s\n",
                    i, bar[1], bar[0], bar[3], bar[2],
                    (bar[0] & 1) ? "I/O" : "MEM",
                    (bar[0] & 8) ? "YES" : "NO");
            }
        }
    }

    /* S6: GET_HW_STATUS */
    Log("\n=== S6: IOCTL_AMDBC250_GET_HW_STATUS ===\n");
    {
        UCHAR buf[256] = {0};
        DWORD ret = 0;
        BOOL ok = Ioctl(h, IOCTL_AMDBC250_GET_HW_STATUS, NULL, 0, buf, sizeof(buf), &ret);
        Log("  Result: %s  BytesReturned=%lu\n", ok ? "OK" : "FAIL", ret);
        if (ok && ret >= 4) {
            DWORD *vals = (DWORD*)buf;
            for (int i = 0; i < (int)(ret/4) && i < 16; i++) {
                Log("  [%d]: 0x%08X\n", i, vals[i]);
            }
        }
    }

    /* S7: GET_CU_STATUS */
    Log("\n=== S7: IOCTL_AMDBC250_GET_CU_STATUS ===\n");
    {
        UCHAR buf[256] = {0};
        DWORD ret = 0;
        BOOL ok = Ioctl(h, IOCTL_AMDBC250_GET_CU_STATUS, NULL, 0, buf, sizeof(buf), &ret);
        Log("  Result: %s  BytesReturned=%lu\n", ok ? "OK" : "FAIL", ret);
        if (ok && ret >= 4) {
            DWORD *vals = (DWORD*)buf;
            for (int i = 0; i < (int)(ret/4) && i < 16; i++) {
                Log("  [%d]: 0x%08X\n", i, vals[i]);
            }
        }
    }

    /* S8: READ_REG - GPU_ID (BAR5+0x000) */
    Log("\n=== S8: IOCTL_AMDBC250_READ_REG (GPU_ID) ===\n");
    {
        UCHAR inBuf[16] = {0};
        DWORD *regIn = (DWORD*)inBuf;
        regIn[0] = 5;      /* BAR index = 5 */
        regIn[1] = 0x000;  /* offset = 0x000 (GPU_ID) */
        UCHAR buf[256] = {0};
        DWORD ret = 0;
        BOOL ok = Ioctl(h, IOCTL_AMDBC250_READ_REG, inBuf, sizeof(inBuf), buf, sizeof(buf), &ret);
        Log("  Result: %s  BytesReturned=%lu\n", ok ? "OK" : "FAIL", ret);
        if (ok && ret >= 4) {
            DWORD *vals = (DWORD*)buf;
            Log("  GPU_ID (BAR5+0x000): 0x%08X\n", vals[0]);
        }
    }

    /* S9: READ_REG - GRBM_STATUS (BAR5+0x2004) */
    Log("\n=== S9: IOCTL_AMDBC250_READ_REG (GRBM_STATUS) ===\n");
    {
        UCHAR inBuf[16] = {0};
        DWORD *regIn = (DWORD*)inBuf;
        regIn[0] = 5;      /* BAR index = 5 */
        regIn[1] = 0x2004; /* offset = 0x2004 (GRBM_STATUS) */
        UCHAR buf[256] = {0};
        DWORD ret = 0;
        BOOL ok = Ioctl(h, IOCTL_AMDBC250_READ_REG, inBuf, sizeof(inBuf), buf, sizeof(buf), &ret);
        Log("  Result: %s  BytesReturned=%lu\n", ok ? "OK" : "FAIL", ret);
        if (ok && ret >= 4) {
            DWORD *vals = (DWORD*)buf;
            Log("  GRBM_STATUS (BAR5+0x2004): 0x%08X\n", vals[0]);
        }
    }

    /* S10: READ_REG - NBIO config (0xFEB00000) */
    Log("\n=== S10: IOCTL_AMDBC250_READ_REG (NBIO+0x00) ===\n");
    {
        UCHAR inBuf[16] = {0};
        DWORD *regIn = (DWORD*)inBuf;
        regIn[0] = 0xFFFFFFFF; /* special: raw physical address */
        regIn[1] = 0xFEB00000; /* NBIO config base */
        UCHAR buf[256] = {0};
        DWORD ret = 0;
        BOOL ok = Ioctl(h, IOCTL_AMDBC250_READ_REG, inBuf, sizeof(inBuf), buf, sizeof(buf), &ret);
        Log("  Result: %s  BytesReturned=%lu\n", ok ? "OK" : "FAIL", ret);
        if (ok && ret >= 4) {
            DWORD *vals = (DWORD*)buf;
            Log("  NBIO[0xFEB00000]: 0x%08X\n", vals[0]);
        }
    }

    /* S11: READ_PCI_CONFIG - GPU B1:D0:F0 offset 0x00 */
    Log("\n=== S11: IOCTL_AMDBC250_READ_PCI_CONFIG (GPU VID:DID) ===\n");
    {
        UCHAR inBuf[16] = {0};
        DWORD *cfgIn = (DWORD*)inBuf;
        cfgIn[0] = 1;   /* Bus 1 */
        cfgIn[1] = 0;   /* Device 0 */
        cfgIn[2] = 0;   /* Function 0 */
        cfgIn[3] = 0x00; /* Config offset 0x00 */
        UCHAR buf[256] = {0};
        DWORD ret = 0;
        BOOL ok = Ioctl(h, IOCTL_AMDBC250_READ_PCI_CONFIG, inBuf, sizeof(inBuf), buf, sizeof(buf), &ret);
        Log("  Result: %s  BytesReturned=%lu\n", ok ? "OK" : "FAIL", ret);
        if (ok && ret >= 8) {
            DWORD *vals = (DWORD*)buf;
            Log("  GPU PCI Config [0x00]: 0x%08X  [0x04]: 0x%08X\n", vals[0], vals[1]);
        }
    }

    /* S12: READ_PCI_BAR - BAR0 */
    Log("\n=== S12: IOCTL_AMDBC250_READ_PCI_BAR ===\n");
    {
        UCHAR inBuf[16] = {0};
        DWORD *barIn = (DWORD*)inBuf;
        barIn[0] = 0; /* BAR index 0 */
        UCHAR buf[256] = {0};
        DWORD ret = 0;
        BOOL ok = Ioctl(h, IOCTL_AMDBC250_READ_PCI_BAR, inBuf, sizeof(inBuf), buf, sizeof(buf), &ret);
        Log("  Result: %s  BytesReturned=%lu\n", ok ? "OK" : "FAIL", ret);
        if (ok && ret >= 8) {
            DWORD *vals = (DWORD*)buf;
            Log("  BAR0 Low: 0x%08X  BAR0 High: 0x%08X\n", vals[0], vals[1]);
        }
    }

    /* S13: MMIO_TEST - VRAM base read */
    Log("\n=== S13: IOCTL_AMDBC250_MMIO_TEST (VRAM) ===\n");
    {
        typedef struct { ULONGLONG PhysicalAddress; ULONGLONG Size; DWORD BarIndex; DWORD Flags; } MMIO_IN;
        typedef struct { ULONGLONG VirtualAddress; DWORD Status; DWORD Reserved; } MMIO_OUT;
        MMIO_IN inBuf = {0};
        MMIO_OUT outBuf = {0};
        DWORD ret = 0;
        inBuf.PhysicalAddress = 0xC0000000; /* VRAM base */
        inBuf.Size = 0x1000;
        inBuf.BarIndex = 0;
        inBuf.Flags = 0; /* read test */
        BOOL ok = Ioctl(h, IOCTL_AMDBC250_MMIO_TEST, &inBuf, sizeof(inBuf), &outBuf, sizeof(outBuf), &ret);
        Log("  Result: %s  BytesReturned=%lu\n", ok ? "OK" : "FAIL", ret);
        if (ok) {
            Log("  VA: 0x%llX  Status: 0x%08X\n", outBuf.VirtualAddress, outBuf.Status);
        }
    }

    CloseHandle(h);
    Log("\n=== IOCTL Test Complete ===\n");
    fclose(g);
    printf("Done. Check output\\ioctl-test.log\n");
    return 0;
}
