#define INITGUID
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

#define IOCTL_INIT_HARDWARE  0x80000B80
#define IOCTL_READ_REG       0x80000B88
#define IOCTL_WRITE_REG      0x80000B8C
#define IOCTL_DISCOVER_PCI   0x80000B74
#define IOCTL_GET_HW_STATUS  0x80000B90

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

BOOL WriteReg(HANDLE h, UINT32 offset, UINT32 value) {
    UINT32 buf[2] = { offset, value };
    DWORD br = 0;
    return DeviceIoControl(h, IOCTL_WRITE_REG, buf, sizeof(buf), NULL, 0, &br, NULL);
}

UINT32 ReadReg(HANDLE h, UINT32 offset) {
    UINT32 buf[2] = { offset, 0 };
    DWORD br = 0;
    DeviceIoControl(h, IOCTL_READ_REG, buf, sizeof(buf), buf, sizeof(buf), &br, NULL);
    return buf[1];
}

int main() {
    g_log = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\basic-display-test.log", "w");
    if (!g_log) { printf("Cannot open log\n"); return 1; }

    Log("=== BASIC DISPLAY REG TEST ===\n\n");

    HANDLE h = OpenMyDriver();
    if (h == INVALID_HANDLE_VALUE) {
        Log("Driver not found! Using Microsoft Basic Display.\n");
        fclose(g_log);
        return 1;
    }
    Log("Driver opened successfully\n");

    DWORD br = 0;
    BOOL ok;

    /* Step 0: Check HW status first */
    Log("\n0. Checking HW status...\n");
    {
        UINT32 statusBuf[8] = {0};
        ok = DeviceIoControl(h, IOCTL_GET_HW_STATUS,
            NULL, 0, statusBuf, sizeof(statusBuf), &br, NULL);
        if (ok) {
            Log("   MmioMapped=%u, RingsInit=%u, FenceInit=%u\n",
                statusBuf[0], statusBuf[1], statusBuf[2]);
            if (statusBuf[0] == 0) {
                Log("   MMIO not mapped, need INIT_HARDWARE\n");
            }
        } else {
            Log("   GET_HW_STATUS failed (err=%u)\n", GetLastError());
        }
    }

    /* Step 1: Call INIT_HARDWARE with NBIO_MAP flag (safe: just maps MMIO, no GPU init) */
    Log("\n1. Calling INIT_HARDWARE (NBIO_MAP, BAR5 PA=0xFE800000, Size=0x80000)...\n");
    {
        struct {
            UINT64 MmioPhysicalBase;
            UINT32 MmioSize;
            UINT32 Flags;
            UINT64 FbPhysicalBase;
            UINT32 FbSize;
            UINT32 Pad;
        } initHw;
        RtlZeroMemory(&initHw, sizeof(initHw));
        initHw.MmioPhysicalBase = 0xFE800000ULL;
        initHw.MmioSize = 0x80000;  /* 512KB */
        initHw.Flags = 1;           /* AMDBC250_INIT_FLAG_NBIO_MAP — MMIO map only, no HwInitialize */

        ok = DeviceIoControl(h, IOCTL_INIT_HARDWARE,
            &initHw, sizeof(initHw), NULL, 0, &br, NULL);
        if (ok) {
            Log("   INIT_HARDWARE NBIO_MAP succeeded\n");
        } else {
            Log("   INIT_HARDWARE failed (err=%u)\n", GetLastError());
            Log("   Cannot map MMIO — register reads will fail\n");
        }
    }

    /* Step 2: Read GPU registers (GC_BASE=0x1260 shifted offsets) */
    Log("\n2. Testing GPU register access (GC_BASE-shifted offsets)...\n");
    {
        UINT32 gpuRegs[] = {
            0x3260,  /* GRBM_STATUS */
            0x3264,  /* CC_UCONFIG */
            0x32D4,  /* SCRATCH_REG */
            0x34FC,  /* SPI_WGP_CNTL */
            0x8000,  /* THM */
        };
        for (int i = 0; i < 5; i++) {
            UINT32 val = ReadReg(h, gpuRegs[i]);
            Log("   GPU[0x%04X] = 0x%08X\n", gpuRegs[i], val);
        }
    }

    /* Step 3: Write SCRATCH_REG and read back */
    Log("\n3. Testing register write (SCRATCH_REG 0x32D4)...\n");
    {
        UINT32 scratchVal = 0xDEADBEEF;
        ok = WriteReg(h, 0x32D4, scratchVal);
        if (ok) {
            UINT32 readback = ReadReg(h, 0x32D4);
            Log("   Wrote 0x%08X, read back 0x%08X\n", scratchVal, readback);
            if (readback == scratchVal) {
                Log("   -> Write/Read MATCH!\n");
            } else {
                Log("   -> Mismatch (write may be ignored by HW)\n");
            }
        } else {
            Log("   Write failed (err=%u)\n", GetLastError());
        }
    }

    /* Step 4: Wider register scan */
    Log("\n4. Reading wider GPU register range...\n");
    {
        UINT32 wideRegs[] = {
            0x2000, 0x2004, 0x2008, 0x200C,
            0x2100, 0x2104, 0x2108, 0x210C,
            0x2600, 0x2604, 0x2608, 0x260C,
            0x3000, 0x3004, 0x3008, 0x300C,
            0x3200, 0x3204, 0x3208, 0x320C,
            0x3210, 0x3214, 0x3218, 0x321C,
            0x3260, 0x3264, 0x3268, 0x326C,
            0x3270, 0x3274, 0x3278, 0x327C,
            0x3280, 0x3284, 0x3288, 0x328C,
            0x3290, 0x3294, 0x3298, 0x329C,
            0x32A0, 0x32A4, 0x32A8, 0x32AC,
            0x32B0, 0x32B4, 0x32B8, 0x32BC,
            0x32C0, 0x32C4, 0x32C8, 0x32CC,
            0x32D0, 0x32D4, 0x32D8, 0x32DC,
            0x3400, 0x3404, 0x3408, 0x340C,
            0x34F0, 0x34F4, 0x34F8, 0x34FC,
            0x3500, 0x3504, 0x3508, 0x350C,
            0x5000, 0x5004, 0x5008, 0x500C,
            0x5400, 0x5404, 0x5408, 0x540C,
            0xA000, 0xA004, 0xA008, 0xA00C,
            0xA200, 0xA204, 0xA208, 0xA20C,
        };
        int nonzero = 0, total = sizeof(wideRegs)/sizeof(wideRegs[0]);
        for (int i = 0; i < total; i++) {
            UINT32 val = ReadReg(h, wideRegs[i]);
            if (val != 0x00000000 && val != 0xFFFFFFFF) {
                Log("   GPU[0x%04X] = 0x%08X\n", wideRegs[i], val);
                nonzero++;
            }
        }
        Log("   %d non-zero registers found out of %d\n", nonzero, total);
    }

    CloseHandle(h);

    Log("\n=== BASIC DISPLAY REG TEST COMPLETE ===\n");
    fclose(g_log);

    printf("Done. Check output\\basic-display-test.log\n");
    return 0;
}
