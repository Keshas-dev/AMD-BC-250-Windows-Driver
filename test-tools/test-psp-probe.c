#include <windows.h>
#include <stdio.h>
#include <SetupAPI.h>
#include <devguid.h>
#include "amdbc250_ioctl.h"

static HANDLE OpenDevice(void) {
    return CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
}

static BOOL SendIoctl(HANDLE h, DWORD code, void* in, DWORD inSize, void* out, DWORD outSize) {
    DWORD bytes = 0;
    return DeviceIoControl(h, code, in, inSize, out, outSize, &bytes, NULL);
}

int main(int argc, char* argv[]) {
    printf("AMD BC-250 PSP Probe v1.0\n");
    printf("==========================\n\n");

    HANDLE hDevice = OpenDevice();
    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("ERROR: Cannot open device (error %lu)\n", GetLastError());
        return 1;
    }
    printf("[OK] Device opened\n\n");

    /* Step 1: Init hardware first (needed for MMIO) */
    printf("=== Step 1: Init Hardware ===\n");
    {
        AMDBC250_IOCTL_INIT_HARDWARE init = {0};
        init.MmioPhysicalBase = 0xC0000000;
        init.MmioSize = 0x10000000;
        BOOL ok = SendIoctl(hDevice, 0x80000B80, &init, sizeof(init), NULL, 0);
        printf("InitHardware: %s (error %lu)\n", ok ? "OK" : "FAIL", GetLastError());
    }

    /* Step 2: Read GPU registers to verify MMIO works */
    printf("\n=== Step 2: Verify MMIO ===\n");
    {
        AMDBC250_IOCTL_REG_ACCESS reg;
        DWORD tests[][2] = {
            {0x0000, 0}, {0x0001, 0}, {0x0004, 0}, {0x0008, 0},
            {0x2004, 0}, {0x2008, 0}, {0x229C, 0}, {0x2380, 0},
            {0x2488, 0}, {0x248C, 0}, {0x2580, 0}, {0x2584, 0},
            {0x2680, 0}, {0x2684, 0}
        };
        printf("  %-8s %-10s %s\n", "Offset", "Value", "Status");
        printf("  %-8s %-10s %s\n", "------", "-----", "------");
        for (int i = 0; i < sizeof(tests)/sizeof(tests[0]); i++) {
            reg.RegisterOffset = tests[i][0];
            reg.Value = 0;
            SendIoctl(hDevice, 0x80000B88, &reg, sizeof(reg), &reg, sizeof(reg));
            const char* status = (reg.Value != 0) ? "OK" : "ZERO";
            printf("  0x%04X   0x%08X   %s\n", tests[i][0], reg.Value, status);
        }
    }

    /* Step 3: Init PSP */
    printf("\n=== Step 3: PSP Init ===\n");
    {
        BOOL ok = SendIoctl(hDevice, 0x80000B98, NULL, 0, NULL, 0);
        printf("PspInit: %s (error %lu)\n", ok ? "OK" : "FAIL", GetLastError());
    }

    /* Step 4: Check PSP status */
    printf("\n=== Step 4: PSP Status ===\n");
    {
        AMDBC250_IOCTL_PSP_STATUS status = {0};
        BOOL ok = SendIoctl(hDevice, 0x80000BA4, NULL, 0, &status, sizeof(status));
        if (ok) {
            printf("  Initialized:  %s\n", status.Initialized ? "YES" : "NO");
            printf("  SOS Alive:    %s\n", status.SosAlive ? "YES" : "NO");
            printf("  FW Loaded:    %s\n", status.FirmwareLoaded ? "YES" : "NO");
            printf("  MMIO Base:    0x%08X\n", status.MmioBase);
            printf("  SOL Register: 0x%08X\n", status.SolRegister);
            printf("  C2PMSG_64:    0x%08X\n", status.C2pmsg64);
        }
    }

    /* Step 5: PSP Mailbox test */
    printf("\n=== Step 5: PSP Mailbox Test ===\n");
    {
        DWORD testValues[] = {0x00000000, 0xDEADBEEF, 0x12345678, 0xFFFFFFFF, 0x00000001};
        for (int i = 0; i < sizeof(testValues)/sizeof(testValues[0]); i++) {
            AMDBC250_IOCTL_PSP_TEST_MAILBOX test = {0};
            test.WriteValue = testValues[i];
            BOOL ok = SendIoctl(hDevice, 0x80000BA8, &test, sizeof(test), &test, sizeof(test));
            if (ok) {
                printf("  Write=0x%08X -> Read=0x%08X SOL=0x%08X %s\n",
                    test.WriteValue, test.ReadValue, test.SolValue,
                    (test.ReadValue == test.WriteValue) ? "MATCH" : "differs");
            } else {
                printf("  Write=0x%08X -> FAILED (error %lu)\n", test.WriteValue, GetLastError());
            }
        }
    }

    /* Step 6: Probe PSP MMIO space (via KMD read_reg on PSP offsets) */
    printf("\n=== Step 6: Probe PSP MMIO Registers ===\n");
    printf("  (Reading GPU_BAR0+0x10000 range via READ_REG)\n");
    {
        DWORD pspOffsets[] = {
            0x10000, 0x10004, 0x10008, 0x1000C,
            0x10800, 0x10804, 0x10808, 0x1080C,
            0x10880, 0x10884, 0x10888, 0x1088C,
            0x108A0, 0x108A4, 0x108A8, 0x108AC,
            0x10A00, 0x10A04, 0x10A08, 0x10A0C,
            0x10C00, 0x10C04, 0x10C08, 0x10C0C,
            0x10E00, 0x10E04, 0x10E08, 0x10E0C,
            0x11000, 0x11004, 0x11008, 0x1100C,
            0x12000, 0x13000, 0x14000,
            0x10944, 0x10948, 0x1094C
        };
        printf("  %-8s %-10s\n", "Offset", "Value");
        printf("  %-8s %-10s\n", "------", "-----");
        for (int i = 0; i < sizeof(pspOffsets)/sizeof(pspOffsets[0]); i++) {
            AMDBC250_IOCTL_REG_ACCESS reg;
            reg.RegisterOffset = pspOffsets[i];
            reg.Value = 0;
            SendIoctl(hDevice, 0x80000B88, &reg, sizeof(reg), &reg, sizeof(reg));
            if (reg.Value != 0) {
                printf("  0x%05X  0x%08X  ***\n", pspOffsets[i], reg.Value);
            } else {
                printf("  0x%05X  0x%08X\n", pspOffsets[i], reg.Value);
            }
        }
    }

    printf("\n=== Probe Complete ===\n");
    CloseHandle(hDevice);
    return 0;
}
