#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

int main() {
    HANDLE hDev = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hDev == INVALID_HANDLE_VALUE) {
        printf("CreateFile FAILED: %lu\n", GetLastError());
        return 1;
    }
    printf("CreateFile OK\n\n");

    /* Test READ_REG */
    AMDBC250_IOCTL_REG_ACCESS r; DWORD ret = 0;
    r.RegisterOffset = 0x2000; r.Value = 0;
    BOOL ok = DeviceIoControl(hDev, IOCTL_AMDBC250_READ_REG, &r, sizeof(r), &r, sizeof(r), &ret, NULL);
    printf("READ_REG(0x2000): ok=%d GetLastError=%lu Value=0x%08X\n", ok, GetLastError(), r.Value);

    /* Test DISCOVER_PCI */
    AMDBC250_IOCTL_DISCOVER_PCI d;
    ZeroMemory(&d, sizeof(d));
    ok = DeviceIoControl(hDev, IOCTL_AMDBC250_DISCOVER_PCI, NULL, 0, &d, sizeof(d), &ret, NULL);
    if (ok) {
        printf("\nDISCOVER_PCI: method=%d vendor=%d bus=%u dev=%u func=%u\n",
            d.MethodUsed, d.VendorFound, d.FoundBus, d.FoundDevice, d.FoundFunction);
        for (int i = 0; i < 6; i++) {
            printf("  BAR%d: addr=0x%08I64x size=0x%08x mem=%d 64bit=%d\n", i,
                d.PciConfig.Bars[i].PhysicalAddress, d.PciConfig.Bars[i].Size,
                d.PciConfig.Bars[i].IsMemoryBar, d.PciConfig.Bars[i].Is64Bit);
        }
    } else {
        printf("DISCOVER_PCI FAILED: %lu\n", GetLastError());
    }

    CloseHandle(hDev);
    return 0;
}
