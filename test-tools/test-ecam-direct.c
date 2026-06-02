#include <windows.h>
#include <stdio.h>
#include "amdbc250_ioctl.h"

static HANDLE OpenDevice(void) {
    return CreateFileA("\\\\.\\AMDBC250DreamV43",
        GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
}

int main(void) {
    printf("AMD BC-250 Direct MMIO Test\n");
    printf("============================\n\n");
    printf("Known BARs from Linux lspci:\n");
    printf("  GPU BAR0 (VRAM):    0xC0000000 (256MB, prefetchable)\n");
    printf("  GPU BAR2 (MMIO reg): 0xD0000000 (2MB, non-prefetchable)\n");
    printf("  GPU BAR5:            0xFE880000 (128KB)\n");
    printf("  ECAM base:           0xE0000000 (256MB)\n");
    printf("  PSP BAR2:            0xFE700000 (1MB)\n\n");

    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) {
        printf("ERROR: Cannot open device (run as Admin?)\n");
        return 1;
    }

    /* Test 1: Try ECAM at bus 0 (should see host bridge 0x14EC) */
    printf("=== Test 1: ECAM at 0xE0000000 (read bus 0 config) ===\n");
    {
        AMDBC250_IOCTL_INIT_HARDWARE init = {0};
        init.MmioPhysicalBase = 0xE0000000ULL;
        init.MmioSize = 0x1000;
        init.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;

        DWORD bytes = 0;
        BOOL ok = DeviceIoControl(h, 0x80000B80,
            &init, sizeof(init), NULL, 0, &bytes, NULL);
        if (ok) {
            printf("  ECAM mapped! Reading B0:D0:F0 config...\n");
            for (DWORD off = 0; off <= 0x40; off += 4) {
                AMDBC250_IOCTL_REG_ACCESS reg = {0};
                reg.RegisterOffset = off;
                if (DeviceIoControl(h, 0x80000B88, &reg, sizeof(reg), &reg, sizeof(reg), &bytes, NULL)) {
                    printf("    +0x%02X: 0x%08X", off, reg.Value);
                    if (off == 0) {
                        UINT16 vendor = (UINT16)(reg.Value & 0xFFFF);
                        UINT16 device = (UINT16)((reg.Value >> 16) & 0xFFFF);
                        printf(" (Vendor=0x%04X, Device=0x%04X)", vendor, device);
                    }
                    if (off == 4) {
                        printf(" (Cmd=0x%04X, Status=0x%04X)",
                            (UINT16)(reg.Value & 0xFFFF), (UINT16)((reg.Value >> 16) & 0xFFFF));
                    }
                    printf("\n");
                }
            }
        } else {
            printf("  ECAM map FAILED (error %lu)\n", GetLastError());
        }
    }

    /* Test 2: Try ECAM at bus 1 offset (read GPU at B1:D0:F0) */
    printf("\n=== Test 2: ECAM at B1 offset 0xE0100000 ===\n");
    {
        AMDBC250_IOCTL_INIT_HARDWARE init = {0};
        init.MmioPhysicalBase = 0xE0100000ULL;  /* 0xE0000000 + (1 << 20) */
        init.MmioSize = 0x1000;
        init.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;

        DWORD bytes = 0;
        BOOL ok = DeviceIoControl(h, 0x80000B80,
            &init, sizeof(init), NULL, 0, &bytes, NULL);
        if (ok) {
            printf("  Bus 1 ECAM mapped! Reading B1:D0:F0 config...\n");
            AMDBC250_IOCTL_REG_ACCESS reg = {0};
            reg.RegisterOffset = 0;
            ok = DeviceIoControl(h, 0x80000B88, &reg, sizeof(reg), &reg, sizeof(reg), &bytes, NULL);
            if (ok) {
                UINT16 vendor = (UINT16)(reg.Value & 0xFFFF);
                UINT16 device = (UINT16)((reg.Value >> 16) & 0xFFFF);
                printf("  Vendor/Device (offset 0): 0x%08X\n", reg.Value);
                printf("  Vendor=0x%04X, Device=0x%04X\n", vendor, device);
                if (vendor == 0x1002) printf("  *** AMD VENDOR FOUND! ***\n");
                if (device == 0x13FE) printf("  *** BC-250 FOUND! ***\n");
            } else {
                printf("  READ_REG error %lu\n", GetLastError());
            }
        } else {
            printf("  Bus 1 ECAM map FAILED (error %lu)\n", GetLastError());
        }
    }

    /* Test 3: Try GPU BAR2 (MMIO registers at 0xD0000000) */
    printf("\n=== Test 3: GPU BAR2 MMIO at 0xD0000000 ===\n");
    {
        AMDBC250_IOCTL_INIT_HARDWARE init = {0};
        init.MmioPhysicalBase = 0xD0000000ULL;
        init.MmioSize = 0x200000; /* 2MB */
        init.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;

        DWORD bytes = 0;
        BOOL ok = DeviceIoControl(h, 0x80000B80,
            &init, sizeof(init), NULL, 0, &bytes, NULL);
        if (ok) {
            printf("  GPU MMIO mapped! Reading registers...\n");
            /* Read GPU_ID at offset 0, CC_CONFIG at 0x2004, etc */
            DWORD testOffsets[] = {0x0000, 0x0004, 0x0008, 0x0BBC, 0x2004, 0x229C};
            for (int i = 0; i < sizeof(testOffsets)/sizeof(testOffsets[0]); i++) {
                AMDBC250_IOCTL_REG_ACCESS reg = {0};
                reg.RegisterOffset = testOffsets[i];
                ok = DeviceIoControl(h, 0x80000B88, &reg, sizeof(reg), &reg, sizeof(reg), &bytes, NULL);
                if (ok) {
                    printf("  [0x%04X] = 0x%08X", testOffsets[i], reg.Value);
                    if (testOffsets[i] == 0x0000) {
                        printf(" <- GPU_ID = 0x%08X", reg.Value);
                    }
                    printf("\n");
                } else {
                    printf("  [0x%04X] = FAILED (error %lu)\n", testOffsets[i], GetLastError());
                }
            }
        } else {
            printf("  GPU BAR2 map FAILED (error %lu)\n", GetLastError());
        }
    }

    /* Test 4: Try GPU BAR5 (0xFE880000) */
    printf("\n=== Test 4: GPU BAR5 at 0xFE880000 ===\n");
    {
        AMDBC250_IOCTL_INIT_HARDWARE init = {0};
        init.MmioPhysicalBase = 0xFE880000ULL;
        init.MmioSize = 0x20000; /* 128KB */
        init.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;

        DWORD bytes = 0;
        BOOL ok = DeviceIoControl(h, 0x80000B80,
            &init, sizeof(init), NULL, 0, &bytes, NULL);
        if (ok) {
            printf("  BAR5 mapped! Reading registers...\n");
            for (DWORD off = 0; off <= 0x40; off += 4) {
                AMDBC250_IOCTL_REG_ACCESS reg = {0};
                reg.RegisterOffset = off;
                if (DeviceIoControl(h, 0x80000B88, &reg, sizeof(reg), &reg, sizeof(reg), &bytes, NULL)) {
                    printf("    +0x%02X: 0x%08X\n", off, reg.Value);
                }
            }
        } else {
            printf("  BAR5 map FAILED (error %lu)\n", GetLastError());
        }
    }

    CloseHandle(h);
    printf("\nDone.\n");
    return 0;
}
