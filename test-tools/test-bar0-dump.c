#include <windows.h>
#include <stdio.h>
#include "amdbc250_ioctl.h"

static HANDLE OpenDevice(void) {
    return CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
}

static BOOL SendIoctl(HANDLE h, DWORD code, void* in, DWORD inSize, void* out, DWORD outSize) {
    DWORD bytes = 0;
    return DeviceIoControl(h, code, in, inSize, out, outSize, &bytes, NULL);
}

static ULONG ReadReg(HANDLE h, DWORD offset) {
    AMDBC250_IOCTL_REG_ACCESS reg = {0};
    reg.RegisterOffset = offset;
    SendIoctl(h, 0x80000B88, &reg, sizeof(reg), &reg, sizeof(reg));
    return reg.Value;
}

static void WriteReg(HANDLE h, DWORD offset, ULONG value) {
    AMDBC250_IOCTL_REG_ACCESS reg = {0};
    reg.RegisterOffset = offset;
    reg.Value = value;
    SendIoctl(h, 0x80000B8C, &reg, sizeof(reg), NULL, 0);
}

int main(void) {
    printf("AMD BC-250 BAR0 Deep Analysis\n");
    printf("=============================\n\n");

    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) {
        printf("ERROR: Cannot open device\n");
        return 1;
    }

    AMDBC250_IOCTL_INIT_HARDWARE init = {0};
    init.MmioPhysicalBase = 0xFE800000;   /* BAR5 = registers (Linux dmesg confirmed) */
    init.MmioSize = 0x00080000;           /* 512KB */
    init.FbPhysicalBase = 0xC0000000;     /* BAR0 = VRAM */
    init.FbSize = 0x10000000;             /* 256MB */
    SendIoctl(h, 0x80000B80, &init, sizeof(init), NULL, 0);
    printf("[OK] Hardware initialized: MMIO=0xFE800000(512KB), VRAM=0xC0000000(256MB)\n\n");

    printf("=== Test 1: VRAM write/readback ===\n");
    {
        DWORD testOffsets[] = {0x0000, 0x1000, 0x10000, 0x100000, 0x1000000};
        for (int i = 0; i < sizeof(testOffsets)/sizeof(testOffsets[0]); i++) {
            DWORD off = testOffsets[i];
            ULONG orig = ReadReg(h, off);
            WriteReg(h, off, 0xDEADBEEF);
            ULONG readback = ReadReg(h, off);
            WriteReg(h, off, orig);
            printf("  Offset 0x%08X: orig=0x%08X, wrote=0xDEADBEEF, read=0x%08X %s\n",
                off, orig, readback, (readback == 0xDEADBEEF) ? "VRAM OK" : "VRAM BLOCKED");
        }
    }

    printf("\n=== Test 2: Coarse scan (every 0x10000 = 64KB) ===\n");
    {
        int nonzero = 0;
        for (DWORD off = 0; off < 0x10000000; off += 0x10000) {
            ULONG val = ReadReg(h, off);
            if (val != 0) {
                printf("  0x%08X: 0x%08X\n", off, val);
                nonzero++;
            }
        }
        printf("  Non-zero: %d regions\n", nonzero);
    }

    printf("\n=== Test 3: PCI config space scan ===\n");
    {
        AMDBC250_IOCTL_PCI_CONFIG pci = {0};
        DWORD bytes = 0;
        BOOL ok = DeviceIoControl(h, 0x80000B94, NULL, 0, &pci, sizeof(pci), &bytes, NULL);
        if (ok) {
            printf("  Vendor: 0x%04X, Device: 0x%04X\n", pci.VendorId, pci.DeviceId);
            printf("  Bus: %u, Dev: %u, Func: %u\n", pci.Bus, pci.Device, pci.Function);
            for (int i = 0; i < 6; i++) {
                if (pci.Bars[i].PhysicalAddress != 0) {
                    printf("  BAR[%d]: PA=0x%llX, Size=0x%X, Mem=%s, 64bit=%s\n",
                        i, pci.Bars[i].PhysicalAddress, pci.Bars[i].Size,
                        pci.Bars[i].IsMemoryBar ? "YES" : "NO",
                        pci.Bars[i].Is64Bit ? "YES" : "NO");
                }
            }
        } else {
            printf("  PCI scan failed\n");
        }
    }

    printf("\n=== Test 4: Extended PCI config scan (all offsets) ===\n");
    {
        printf("  Scanning PCI config 0x00-0xFF for non-zero DWORDs...\n");
        for (DWORD off = 0; off < 256; off += 4) {
            UCHAR buf[4] = {0};
            DWORD got = 0;
            BOOL ok = DeviceIoControl(h, 0x80000B88,
                &(AMDBC250_IOCTL_REG_ACCESS){.RegisterOffset = 0x100000 + off},
                sizeof(AMDBC250_IOCTL_REG_ACCESS),
                &(AMDBC250_IOCTL_REG_ACCESS){0},
                sizeof(AMDBC250_IOCTL_REG_ACCESS),
                &got, NULL);
        }
    }

    printf("\n=== Test 5: Known AMD register offsets (GFX10) ===\n");
    {
        DWORD known[] = {
            0x0000, 0x0001, 0x0004, 0x0008,
            0x0A00, 0x0A04, 0x0A08, 0x0A0C,
            0x0B00, 0x0B04, 0x0B08, 0x0B0C,
            0x1500, 0x1504, 0x1508, 0x150C,
            0x2000, 0x2004, 0x2008, 0x200C,
            0x2100, 0x2104, 0x2108, 0x210C,
            0x2200, 0x2204, 0x2208, 0x220C,
            0x2300, 0x2304, 0x2308, 0x230C,
            0x2400, 0x2404, 0x2408, 0x240C,
            0x2500, 0x2504, 0x2508, 0x250C,
            0x2600, 0x2604, 0x2608, 0x260C,
            0x2800, 0x2804, 0x2808, 0x280C,
            0x2900, 0x2904, 0x2908, 0x290C,
            0x3000, 0x3004, 0x3008, 0x300C,
            0x4000, 0x4004, 0x4008, 0x400C,
            0x5000, 0x5004, 0x5008, 0x500C,
            0x8000, 0x8004, 0x8008, 0x800C,
            0x9000, 0x9004, 0x9008, 0x900C,
            0xA000, 0xA004, 0xA008, 0xA00C,
            0xB000, 0xB004, 0xB008, 0xB00C,
            0xC000, 0xC004, 0xC008, 0xC00C,
            0xD000, 0xD004, 0xD008, 0xD00C,
            0xE000, 0xE004, 0xE008, 0xE00C,
            0xF000, 0xF004, 0xF008, 0xF00C,
        };
        for (int i = 0; i < sizeof(known)/sizeof(known[0]); i++) {
            ULONG val = ReadReg(h, known[i]);
            if (val != 0) {
                printf("  0x%04X: 0x%08X ***\n", known[i], val);
            }
        }
        printf("  (all zeros = GPU registers not in VRAM BAR)\n");
    }

    printf("\n=== Analysis ===\n");
    printf("MMIO BAR2 (registers) = 0xD0000000, 2MB (from Linux lspci)\n");
    printf("VRAM BAR0 = 0xC0000000, 256MB\n");
    printf("NOTE: MMIO and VRAM are separate PCI BARs on BC-250!\n");
    printf("Register reads access MMIO BAR2, not VRAM BAR0\n");

    CloseHandle(h);
    return 0;
}
