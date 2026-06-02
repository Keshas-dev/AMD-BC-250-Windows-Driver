#include <windows.h>
#include <stdio.h>
#include "amdbc250_ioctl.h"

static HANDLE OpenDevice(void) {
    return CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
}

static BOOL Ioctl(HANDLE h, DWORD code, void* in, DWORD inSize, void* out, DWORD outSize) {
    DWORD bytes = 0;
    return DeviceIoControl(h, code, in, inSize, out, outSize, &bytes, NULL);
}

int main(void) {
    printf("AMD BC-250 PCI Discovery Tool\n");
    printf("==============================\n\n");

    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) {
        printf("ERROR: Cannot open device (run as Admin?)\n");
        return 1;
    }

    /* Method 1: READ_PCI_BAR (0x80000B94) with IO port fallback */
    printf("=== Method 1: READ_PCI_BAR (IO ports + HAL) ===\n");
    {
        AMDBC250_IOCTL_PCI_CONFIG pci = {0};
        DWORD bytes = 0;
        BOOL ok = DeviceIoControl(h, 0x80000B94, NULL, 0, &pci, sizeof(pci), &bytes, NULL);
        if (ok) {
            printf("  Vendor: 0x%04X, Device: 0x%04X\n", pci.VendorId, pci.DeviceId);
            printf("  Bus: %u, Dev: %u, Func: %u\n", pci.Bus, pci.Device, pci.Function);
            printf("  Command: 0x%04X, Status: 0x%04X\n", pci.Command, pci.Status);
            for (int i = 0; i < 6; i++) {
                if (pci.Bars[i].PhysicalAddress != 0 || pci.Bars[i].Size != 0) {
                    printf("  BAR[%d]: PA=0x%llX, Size=0x%X (%s%s)\n",
                        i, pci.Bars[i].PhysicalAddress, pci.Bars[i].Size,
                        pci.Bars[i].IsMemoryBar ? "MEM" : "IO",
                        pci.Bars[i].Is64Bit ? " 64-bit" : " 32-bit");
                }
            }
        } else {
            printf("  FAILED (error %lu)\n", GetLastError());
        }
    }

    /* Method 2: DISCOVER_PCI (0x80000BB4) */
    printf("\n=== Method 2: DISCOVER_PCI (ECAM + IO ports) ===\n");
    {
        AMDBC250_IOCTL_DISCOVER_PCI d = {0};
        BOOL ok = Ioctl(h, 0x80000BB4, NULL, 0, &d, sizeof(d));
        if (ok && d.VendorFound) {
            printf("  Found at PCI %lu:%lu.%lu (method %lu)\n",
                d.FoundBus, d.FoundDevice, d.FoundFunction, d.MethodUsed);
            printf("  Vendor: 0x%04X, Device: 0x%04X\n", d.PciConfig.VendorId, d.PciConfig.DeviceId);
            printf("  Command: 0x%04X\n", d.PciConfig.Command);
            for (int i = 0; i < 6; i++) {
                if (d.PciConfig.Bars[i].PhysicalAddress != 0 || d.PciConfig.Bars[i].Size != 0) {
                    printf("  BAR[%d]: PA=0x%llX, Size=0x%X (%s%s)\n",
                        i, d.PciConfig.Bars[i].PhysicalAddress, d.PciConfig.Bars[i].Size,
                        d.PciConfig.Bars[i].IsMemoryBar ? "MEM" : "IO",
                        d.PciConfig.Bars[i].Is64Bit ? " 64-bit" : " 32-bit");
                }
            }

            /* Try enabling Memory Space if not already enabled */
            if ((d.PciConfig.Command & 0x02) == 0) {
                printf("\n  *** Memory Space NOT enabled (Command=0x%04X) ***\n", d.PciConfig.Command);
                printf("  Attempting to enable Memory Space + Bus Master...\n");
                AMDBC250_IOCTL_WRITE_PCI_CONFIG w = {0};
                w.Bus = d.FoundBus;
                w.Device = d.FoundDevice;
                w.Function = d.FoundFunction;
                w.Offset = 0x04;
                w.Value = d.PciConfig.Command | 0x07; /* Enable I/O + Memory + BusMaster */
                BOOL wok = Ioctl(h, 0x80000BB0, &w, sizeof(w), NULL, 0);
                if (wok) {
                    printf("  Write OK - Memory Space should now be enabled\n");
                    /* Verify */
                    AMDBC250_IOCTL_READ_PCI_CONFIG r = {0};
                    r.Bus = d.FoundBus;
                    r.Device = d.FoundDevice;
                    r.Function = d.FoundFunction;
                    Ioctl(h, 0x80000BAC, &r, sizeof(r), &r, sizeof(r));
                    UINT16 cmdNew = *(UINT16*)(r.ConfigData + 4);
                    printf("  Command register now: 0x%04X\n", cmdNew);
                } else {
                    printf("  Write FAILED (error %lu)\n", GetLastError());
                }
            } else {
                printf("\n  Memory Space already enabled (Command=0x%04X)\n", d.PciConfig.Command);
            }
        } else {
            printf("  FAILED: BC-250 not found\n");
        }
    }

    /* Method 3: GET_RESOURCE_BARS from StartDevice (most reliable on this platform) */
    printf("\n=== Method 3: GET_RESOURCE_BARS (StartDevice resource list) ===\n");
    {
        AMDBC250_IOCTL_RESOURCE_BARS bars = {0};
        BOOL ok = Ioctl(h, 0x80000BB8, NULL, 0, &bars, sizeof(bars));
        printf("  DeviceStarted=%u, MmioMapped=%u\n", bars.DeviceStarted, bars.MmioMapped);
        printf("  MMIO: PA=0x%llX, Size=0x%X (%uKB), VA=0x%llX\n",
            bars.MmioPhysicalBase, bars.MmioSize, (UINT32)(bars.MmioSize / 1024), bars.MmioVirtualBase);
        printf("  VRAM: PA=0x%llX, Size=0x%X (%uMB)\n",
            bars.FbPhysicalBase, bars.FbSize, (UINT32)(bars.FbSize / (1024*1024)));

        if (bars.DeviceStarted && bars.MmioMapped && bars.MmioVirtualBase != 0) {
            printf("\n  Register reads from mapped MMIO (without INIT_HARDWARE):\n");
            for (DWORD off = 0; off <= 0x10; off += 4) {
                AMDBC250_IOCTL_REG_ACCESS reg = {0};
                reg.RegisterOffset = off;
                Ioctl(h, 0x80000B88, &reg, sizeof(reg), &reg, sizeof(reg));
                printf("    [0x%04X] = 0x%08X\n", off, reg.Value);
            }
        } else {
            printf("\n  StartDevice did not run or MMIO not mapped!\n");
            printf("  Trying INIT_HARDWARE with discovered addresses...\n");
        }
    }

    /* Method 4: Try to INIT_HARDWARE with discovered BAR addresses */
    printf("\n=== Test: INIT_HARDWARE with discovered BARs ===\n");
    {
        AMDBC250_IOCTL_DISCOVER_PCI d = {0};
        BOOL ok = Ioctl(h, 0x80000BB4, NULL, 0, &d, sizeof(d));
        if (ok && d.VendorFound) {
            UINT64 mmioBase = 0;
            UINT32 mmioSize = 0;
            UINT64 fbBase = 0;
            UINT32 fbSize = 0;

            for (int i = 0; i < 6; i++) {
                if (d.PciConfig.Bars[i].IsMemoryBar &&
                    d.PciConfig.Bars[i].PhysicalAddress != 0 &&
                    d.PciConfig.Bars[i].Size < 0x1000000 && /* < 16MB = MMIO */
                    d.PciConfig.Bars[i].Size >= 0x10000 &&   /* >= 64KB */
                    mmioBase == 0) {
                    mmioBase = d.PciConfig.Bars[i].PhysicalAddress;
                    mmioSize = d.PciConfig.Bars[i].Size;
                }
                if (d.PciConfig.Bars[i].IsMemoryBar &&
                    d.PciConfig.Bars[i].PhysicalAddress != 0 &&
                    d.PciConfig.Bars[i].Size >= 0x1000000 && /* >= 16MB = VRAM */
                    fbBase == 0) {
                    fbBase = d.PciConfig.Bars[i].PhysicalAddress;
                    fbSize = d.PciConfig.Bars[i].Size;
                }
            }

            printf("  Discovered: MMIO=0x%llX(%uMB), VRAM=0x%llX(%uMB)\n",
                mmioBase, mmioSize / (1024*1024),
                fbBase, fbSize / (1024*1024));

            if (mmioBase != 0) {
                /* Enable Memory Space if needed */
                if ((d.PciConfig.Command & 0x02) == 0) {
                    AMDBC250_IOCTL_WRITE_PCI_CONFIG w = {0};
                    w.Bus = d.FoundBus;
                    w.Device = d.FoundDevice;
                    w.Function = d.FoundFunction;
                    w.Offset = 0x04;
                    w.Value = d.PciConfig.Command | 0x07;
                    Ioctl(h, 0x80000BB0, &w, sizeof(w), NULL, 0);
                    Sleep(10);
                }

                AMDBC250_IOCTL_INIT_HARDWARE init = {0};
                init.MmioPhysicalBase = mmioBase;
                init.MmioSize = mmioSize;
                init.FbPhysicalBase = fbBase;
                init.FbSize = fbSize;
                ok = Ioctl(h, 0x80000B80, &init, sizeof(init), NULL, 0);
                printf("  INIT_HARDWARE: %s\n", ok ? "OK" : "FAIL");

                if (ok) {
                    printf("  Register reads after INIT:\n");
                    for (DWORD off = 0; off <= 0x10; off += 4) {
                        AMDBC250_IOCTL_REG_ACCESS reg = {0};
                        reg.RegisterOffset = off;
                        Ioctl(h, 0x80000B88, &reg, sizeof(reg), &reg, sizeof(reg));
                        printf("    [0x%04X] = 0x%08X\n", off, reg.Value);
                    }
                }
            }
        }
    }

    CloseHandle(h);
    printf("\nDone.\n");
    return 0;
}