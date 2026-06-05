#define INITGUID
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

static FILE *gLog = NULL;

static void Log(const char *fmt, ...) {
    va_list a;
    va_start(a, fmt);
    vfprintf(stdout, fmt, a);
    va_end(a);
    if (gLog) {
        va_start(a, fmt);
        vfprintf(gLog, fmt, a);
        va_end(a);
        fflush(gLog);
    }
}

static HANDLE OpenMyDriver(void) {
    return CreateFileW(L"\\\\.\\AMDBC250DreamV43",
        GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
}

static BOOL InitHardware(HANDLE h) {
    UCHAR in[32] = {0}, out[32] = {0};
    DWORD br = 0;
    *(UINT64*)(in + 0)  = 0xFE800000ULL;  /* MMIO BAR5 */
    *(UINT32*)(in + 8)  = 0x00080000;      /* 512KB */
    *(UINT32*)(in + 12) = 1;                /* Flags=1: map only, no GPU init */
    *(UINT64*)(in + 16) = 0xC0000000ULL;  /* VRAM BAR0 */
    *(UINT32*)(in + 24) = 0x10000000;      /* 256MB */
    return DeviceIoControl(h, 0x80000B80, in, sizeof(in), out, sizeof(out), &br, NULL);
}

static BOOL ReadReg(HANDLE h, UINT32 offset, UINT32 *val) {
    UINT32 ra[2] = {offset, 0xDEADBEEF};
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, 0x80000B88, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
    *val = ra[1];
    return ok;
}

static BOOL WriteReg(HANDLE h, UINT32 offset, UINT32 val) {
    UINT32 ra[2] = {offset, val};
    DWORD br = 0;
    return DeviceIoControl(h, 0x80000B8C, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
}

static BOOL ReadPciConfig(HANDLE h, UINT32 bus, UINT32 dev, UINT32 func,
                          UCHAR out[256], UINT32 *bytesRead) {
    struct { UINT32 Bus, Device, Function, BytesRead; UCHAR Data[256]; } r = {0};
    r.Bus = bus; r.Device = dev; r.Function = func;
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, 0x80000BAC, &r, sizeof(r), &r, sizeof(r), &br, NULL);
    if (ok && bytesRead) *bytesRead = r.BytesRead;
    if (ok) memcpy(out, r.Data, 256);
    return ok;
}

static BOOL WritePciConfig(HANDLE h, UINT32 bus, UINT32 dev, UINT32 func,
                           UINT32 offset, UINT32 value) {
    struct { UINT32 Bus, Device, Function, Offset, Value; } w = {0};
    w.Bus = bus; w.Device = dev; w.Function = func;
    w.Offset = offset; w.Value = value;
    DWORD br = 0;
    return DeviceIoControl(h, 0x80000BB0, &w, sizeof(w), &w, sizeof(w), &br, NULL);
}

static void CheckBlockedRegs(HANDLE h, const char *label) {
    UINT32 v;
    Log("  [%s] Blocked register status:\n", label);
    ReadReg(h, 0x2004, &v); Log("    GRBM_STATUS[0x2004] = 0x%08X%s\n", v,
        (v != 0xFFFFFFFF && v != 0x00000000) ? "  *** UNBLOCKED! ***" : "");
    ReadReg(h, 0x2000, &v); Log("    CP[0x2000]          = 0x%08X%s\n", v,
        (v != 0xFFFFFFFF && v != 0x00000000) ? "  *** UNBLOCKED! ***" : "");
    ReadReg(h, 0x0D00, &v); Log("    CLK[0x0D00]         = 0x%08X%s\n", v,
        (v != 0xFFFFFFFF && v != 0x00000000) ? "  *** UNBLOCKED! ***" : "");
    ReadReg(h, 0x2074, &v); Log("    Scratch[0x2074]     = 0x%08X%s\n", v,
        (v != 0xFFFFFFFF && v != 0x00000000) ? "  *** UNBLOCKED! ***" : "");
    ReadReg(h, 0x2600, &v); Log("    SDMA[0x2600]        = 0x%08X%s\n", v,
        (v != 0xFFFFFFFF && v != 0x00000000) ? "  *** UNBLOCKED! ***" : "");
    ReadReg(h, 0xA000, &v); Log("    RSMU[0xA000]        = 0x%08X%s\n", v,
        (v != 0xFFFFFFFF && v != 0x00000000) ? "  *** UNBLOCKED! ***" : "");
}

int main(void) {
    gLog = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\nbio-pci-root.log", "w");
    if (!gLog) { printf("Cannot open log file\n"); return 1; }

    Log("=== NBIO Unlock via Root Complex PCI Config 0xB8 ===\n\n");

    HANDLE h = OpenMyDriver();
    if (h == INVALID_HANDLE_VALUE) {
        Log("ERROR: Cannot open driver (err=%lu)\n", GetLastError());
        fclose(gLog);
        return 1;
    }

    /* Initialize hardware */
    if (!InitHardware(h)) {
        Log("ERROR: INIT_HARDWARE failed (err=%lu)\n", GetLastError());
        fclose(gLog);
        return 1;
    }
    Log("Hardware initialized (MMIO mapped)\n\n");

    /* Baseline - check blocked registers before any PCI writes */
    Log("=== BASELINE (before any PCI config writes) ===\n");
    CheckBlockedRegs(h, "baseline");

    /* Read current root complex PCI config */
    UCHAR pciCfg[256];
    UINT32 br = 0;
    Log("\n=== Reading Root Complex (B0:D0:F0) PCI Config ===\n");
    if (ReadPciConfig(h, 0, 0, 0, pciCfg, &br)) {
        Log("  Bytes read: %lu\n", br);
        if (br >= 0xBC) {
            UINT32 valB8 = *(UINT32*)(pciCfg + 0xB8);
            UINT32 valBC = *(UINT32*)(pciCfg + 0xBC);
            Log("  Offset 0xB8 = 0x%08X\n", valB8);
            Log("  Offset 0xBC = 0x%08X\n", valBC);
        } else {
            Log("  WARNING: only %lu bytes read, cannot see 0xB8\n", br);
        }

        /* Also dump some known offsets */
        Log("\n  PCI Config dump (relevant offsets):\n");
        for (int off = 0xB0; off < 0xC0 && off < (int)br; off += 4) {
            UINT32 v = *(UINT32*)(pciCfg + off);
            Log("    [0x%02X] = 0x%08X\n", off, v);
        }
    } else {
        Log("  ERROR: Failed to read PCI config (err=%lu)\n", GetLastError());
    }

    /* Save original value */
    UINT32 origB8 = 0, origBC = 0;
    if (br >= 0xBC) {
        origB8 = *(UINT32*)(pciCfg + 0xB8);
        origBC = *(UINT32*)(pciCfg + 0xBC);
    }

    /* Test values to write to offset 0xB8 */
    UINT32 testVals[] = {
        0x00000000,
        0xFFFFFFFF,
        0x00000001,
        0x80000000,
        0x00010000,
        0x000000FF,
        0x01000000,
        origB8,  /* try original value again */
        0x13B102E0,  /* the exact original we read */
    };
    const char *testNames[] = {
        "zero",
        "all-ones",
        "bit0",
        "bit31",
        "bit16",
        "low-byte-FF",
        "bit24",
        "original-as-is",
        "original-again",
    };

    /* ---- TEST ROOT COMPLEX (B0:D0:F0) ---- */
    for (int i = 0; i < (int)(sizeof(testVals)/sizeof(testVals[0])); i++) {
        Log("\n=== TEST %d: Write 0x%08X (%s) to B0:D0:F0 offset 0xB8 ===\n",
            i + 1, testVals[i], testNames[i]);

        if (WritePciConfig(h, 0, 0, 0, 0xB8, testVals[i])) {
            Log("  Write OK\n");

            if (ReadPciConfig(h, 0, 0, 0, pciCfg, &br) && br >= 0xBC) {
                UINT32 readBack = *(UINT32*)(pciCfg + 0xB8);
                Log("  Read back: 0x%08X (write %s)\n", readBack,
                    (readBack == testVals[i]) ? "PERSISTED" : "IGNORED/MODIFIED");
            }

            CheckBlockedRegs(h, testNames[i]);
        } else {
            Log("  ERROR: Write failed (err=%lu)\n", GetLastError());
        }
    }

    /* ---- TEST GPU DEVICE (B1:D0:F0) ---- */
    Log("\n\n=== TESTING GPU DEVICE (B1:D0:F0) offset 0xB8 ===\n");
    if (ReadPciConfig(h, 1, 0, 0, pciCfg, &br) && br >= 0xBC) {
        UINT32 gpuB8 = *(UINT32*)(pciCfg + 0xB8);
        Log("  GPU [0xB8] = 0x%08X\n", gpuB8);
        for (int i = 0; i < 3; i++) {
            UINT32 v = (i == 0) ? 0x00000000 : (i == 1) ? 0xFFFFFFFF : 0x80000000;
            const char *nm = (i == 0) ? "zero" : (i == 1) ? "all-ones" : "bit31";
            Log("\n  GPU test %d: write 0x%08X (%s)\n", i + 1, v, nm);
            if (WritePciConfig(h, 1, 0, 0, 0xB8, v)) {
                Log("    Write OK\n");
                CheckBlockedRegs(h, nm);
            } else {
                Log("    Write FAILED (err=%lu)\n", GetLastError());
            }
            WritePciConfig(h, 1, 0, 0, 0xB8, gpuB8);
        }
    } else {
        Log("  Cannot read GPU PCI config\n");
    }

    /* ---- TEST PSP DEVICE (B1:D0:F2) ---- */
    Log("\n\n=== TESTING PSP DEVICE (B1:D0:F2) offset 0xB8 ===\n");
    if (ReadPciConfig(h, 1, 0, 2, pciCfg, &br) && br >= 0xBC) {
        UINT32 pspB8 = *(UINT32*)(pciCfg + 0xB8);
        Log("  PSP [0xB8] = 0x%08X\n", pspB8);
        for (int i = 0; i < 3; i++) {
            UINT32 v = (i == 0) ? 0x00000000 : (i == 1) ? 0xFFFFFFFF : 0x80000000;
            const char *nm = (i == 0) ? "zero" : (i == 1) ? "all-ones" : "bit31";
            Log("\n  PSP test %d: write 0x%08X (%s)\n", i + 1, v, nm);
            if (WritePciConfig(h, 1, 0, 2, 0xB8, v)) {
                Log("    Write OK\n");
                CheckBlockedRegs(h, nm);
            } else {
                Log("    Write FAILED (err=%lu)\n", GetLastError());
            }
            WritePciConfig(h, 1, 0, 2, 0xB8, pspB8);
        }
    } else {
        Log("  Cannot read PSP PCI config\n");
    }

    /* ---- TEST: Write to 0xBC as well ---- */
    Log("\n\n=== TESTING 0xBC (root complex) ===\n");
    if (ReadPciConfig(h, 0, 0, 0, pciCfg, &br) && br >= 0xC0) {
        UINT32 bcVal = *(UINT32*)(pciCfg + 0xBC);
        Log("  Root [0xBC] = 0x%08X\n", bcVal);
        for (int i = 0; i < 3; i++) {
            UINT32 v = (i == 0) ? 0x00000000 : (i == 1) ? 0xFFFFFFFF : 0xFDD00101;
            const char *nm = (i == 0) ? "zero" : (i == 1) ? "all-ones" : "FDD00101";
            Log("\n  0xBC test %d: write 0x%08X (%s)\n", i + 1, v, nm);
            if (WritePciConfig(h, 0, 0, 0, 0xBC, v)) {
                Log("    Write OK\n");
                CheckBlockedRegs(h, nm);
            } else {
                Log("    Write FAILED (err=%lu)\n", GetLastError());
            }
            WritePciConfig(h, 0, 0, 0, 0xBC, bcVal);
        }
    }

    /* Restore original root complex 0xB8 */
    if (origB8) {
        Log("\n=== RESTORING root 0xB8 to 0x%08X ===\n", origB8);
        WritePciConfig(h, 0, 0, 0, 0xB8, origB8);
    }

    /* Final blocked register check */
    Log("\n=== FINAL STATUS ===\n");
    CheckBlockedRegs(h, "final");

    CloseHandle(h);
    Log("\n=== Test Complete ===\n");
    if (gLog) fclose(gLog);
    printf("Done. Check output\\nbio-pci-root.log\n");
    return 0;
}
