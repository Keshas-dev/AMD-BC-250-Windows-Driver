#include <windows.h>
#include <stdio.h>
#include "amdbc250_ioctl.h"

int main(void) {
    printf("AMD BC-250 Force-Enable MMIO Tester\n");
    printf("====================================\n\n");

    HANDLE hDev = CreateFileA("\\\\.\\AMDBC250DreamV43",
        GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hDev == INVALID_HANDLE_VALUE) {
        printf("ERROR: Cannot open device (code %d)\n", (int)GetLastError());
        return 1;
    }

    AMDBC250_IOCTL_FORCE_ENABLE_MMIO f = {0};
    f.Bus = 1;
    f.Device = 0;
    f.Function = 0;
    f.MmioPhysicalBase = 0xFE800000; /* BAR5 confirmed by BootConfig */
    f.MmioSize = 0x80000;            /* 512KB */
    f.ScratchOffset = 0x0C00;        /* mmSCRATCH_REG0 (safe R/W) */
    f.ScratchWriteVal = 0xDEADBEEF;

    DWORD bytes = 0;
    BOOL ok = DeviceIoControl(hDev, 0x80000BBC,
        &f, sizeof(f), &f, sizeof(f), &bytes, NULL);

    printf("PCI bus B1:D0:F0 (confirmed by LocationInformation registry):\n\n");

    printf("--- PCI Command Register ---\n");
    printf("  Command before: 0x%04X (I/O=%d Mem=%d BusMaster=%d)\n",
        (UINT16)f.CommandBefore,
        (f.CommandBefore & 2) ? 1 : 0,
        (f.CommandBefore & 2) ? 1 : 0,
        (f.CommandBefore & 4) ? 1 : 0);
    printf("  Command after:  0x%04X\n", (UINT16)f.CommandAfter);

    printf("\n--- PCI Config Write Methods ---\n");
    printf("  HalSetBusDataByOffset: %s\n", f.HalSetBusResult ? "SUCCESS" : "FAILED");
    printf("  IO port write (0xCF8/0xCFC): %s\n", f.IoPortWriteResult ? "SUCCESS" : "FAILED");

    printf("\n--- GPU Register Test at 0x%08llX (BAR5) ---\n", f.MmioPhysicalBase);
    printf("  GPU_ID[0x0000] before: 0x%08X\n", f.GpuIdBefore);
    printf("  GPU_ID[0x0000] after:  0x%08X\n", f.GpuIdAfter);

    printf("  Scratch[0x%X] wrote: 0x%08X\n", f.ScratchOffset, f.ScratchWriteVal);
    printf("  Scratch[0x%X] read:  0x%08X\n", f.ScratchOffset, f.ScratchReadVal);
    if (f.ScratchReadVal == f.ScratchWriteVal) {
        printf("  *** SCRATCH R/W OK - MMIO IS WORKING! ***\n");
    } else if (f.ScratchReadVal == 0) {
        printf("  Scratch returns 0 - MMIO reads blocked\n");
    }

    printf("\n--- Interpretation ---\n");
    if (f.CommandBefore == 0 || f.CommandBefore == 0xFFFF) {
        printf("  PCI Command register reads as 0x%04X - IO ports may not work on this platform.\n",
            (UINT16)f.CommandBefore);
    }
    if ((f.CommandBefore & 2) == 0 && f.CommandAfter != f.CommandBefore) {
        printf("  Memory Space was disabled but was successfully enabled!\n");
        printf("  Try running test-mmio-scan-ext.exe or test-pci-discover.exe again.\n");
    }
    if (f.GpuIdBefore == 0 && f.GpuIdAfter == 0 && f.CommandAfter != f.CommandBefore) {
        printf("  Memory Space was changed but GPU still returns 0.\n");
        printf("  GPU is likely power-gated (SMU needs init).\n");
    }
    if (f.GpuIdBefore == 0 && f.GpuIdAfter == 0 && f.CommandAfter == f.CommandBefore) {
        printf("  Memory Space cannot be changed - IO ports/HAL are blocked.\n");
        printf("  Need alternative method to enable PCI memory access.\n");
    }

    CloseHandle(hDev);
    printf("\nDone.\n");
    return 0;
}
