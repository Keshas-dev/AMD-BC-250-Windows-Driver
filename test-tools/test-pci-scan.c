#include <windows.h>
#include <stdio.h>
#include "amdbc250_ioctl.h"

static HANDLE h;

static BOOL Ioctl(DWORD code, void* in, DWORD inSize, void* out, DWORD outSize) {
    DWORD bytes = 0;
    return DeviceIoControl(h, code, in, inSize, out, outSize, &bytes, NULL);
}

static void DumpPciConfig(ULONG bus, ULONG dev, ULONG func, const char* label) {
    AMDBC250_IOCTL_READ_PCI_CONFIG pci = {0};
    pci.Bus = bus;
    pci.Device = dev;
    pci.Function = func;
    BOOL ok = Ioctl(0x80000BAC, &pci, sizeof(pci), &pci, sizeof(pci));
    printf("%s B%lu:D%lu:F%lu: %s", label, bus, dev, func, ok ? "OK" : "FAIL");
    if (ok && pci.BytesRead >= 64) {
        UINT16 ven = *(UINT16*)(pci.ConfigData + 0);
        UINT16 devid = *(UINT16*)(pci.ConfigData + 2);
        UINT8 rev = pci.ConfigData[8];
        UINT8 progif = pci.ConfigData[9];
        UINT8 subcls = pci.ConfigData[10];
        UINT8 basecls = pci.ConfigData[11];
        UINT16 cmd = *(UINT16*)(pci.ConfigData + 4);
        printf(" VEN=%04X DEV=%04X REV=%02X CLASS=%02X%02X%02X CMD=%04X", ven, devid, rev, basecls, subcls, progif, cmd);
        for (int i = 0; i < 6; i++) {
            UINT32 bar = *(UINT32*)(pci.ConfigData + 0x10 + i * 4);
            if (bar & 1) {
                printf(" BAR%d=IO-%08X", i, bar & ~3);
            } else if (bar) {
                UINT32 barLow = bar & ~0xF;
                printf(" BAR%d=MEM-%08X", i, barLow);
            }
        }
    }
    printf("\n");
}

int main(void) {
    printf("AMD BC-250 PCI Config Space Scanner\n");
    printf("=====================================\n\n");

    h = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("ERROR: Cannot open device\n"); return 1;
    }
    printf("[OK] Device opened\n\n");

    printf("=== Scan AMD devices on all buses ===\n");
    ULONG foundBus = 0, foundDev = 0, foundFunc = 0;
    int found = 0;
    for (ULONG bus = 0; bus < 256; bus++) {
        for (ULONG dev = 0; dev < 32; dev++) {
            for (ULONG func = 0; func < 8; func++) {
                AMDBC250_IOCTL_READ_PCI_CONFIG pci = {0};
                pci.Bus = bus; pci.Device = dev; pci.Function = func;
                if (!Ioctl(0x80000BAC, &pci, sizeof(pci), &pci, sizeof(pci))) continue;
                if (pci.BytesRead < 4) continue;
                UINT16 ven = *(UINT16*)(pci.ConfigData + 0);
                if (ven == 0xFFFF || ven == 0x0000) continue;
                UINT16 devid = *(UINT16*)(pci.ConfigData + 2);
                printf("  B%02lu:D%02lu:F%lu: VEN=%04X DEV=%04X", bus, dev, func, ven, devid);
                UINT8 basecls = pci.ConfigData[11];
                UINT8 subcls = pci.ConfigData[10];
                printf(" CLASS=%02X%02X", basecls, subcls);
                if (ven == 0x1002 && devid == 0x13FE) {
                    printf(" <<< BC-250 GPU");
                    foundBus = bus; foundDev = dev; foundFunc = func; found = 1;
                }
                printf("\n");
            }
        }
    }

    if (found) {
        printf("\n=== BC-250 GPU found at B%lu:D%lu:F%lu ===\n", foundBus, foundDev, foundFunc);
        printf("\n=== Full PCI config space (256 bytes) ===\n");
        AMDBC250_IOCTL_READ_PCI_CONFIG pci = {0};
        pci.Bus = foundBus; pci.Device = foundDev; pci.Function = foundFunc;
        Ioctl(0x80000BAC, &pci, sizeof(pci), &pci, sizeof(pci));
        for (int i = 0; i < 64; i++) {
            printf("%02X ", pci.ConfigData[i]);
            if ((i + 1) % 16 == 0) printf("\n");
        }
        printf("\n\n=== BAR values ===\n");
        for (int i = 0; i < 6; i++) {
            UINT32 bar = *(UINT32*)(pci.ConfigData + 0x10 + i * 4);
            UINT32 barLow = bar & ~0xF;
            if (bar & 1) printf("  BAR%d: IO  base=0x%08X\n", i, barLow);
            else if (bar) printf("  BAR%d: MEM base=0x%08X%s\n", i, barLow, (bar & 4) ? " (64-bit)" : "");
        }
    } else {
        printf("\nBC-250 GPU NOT FOUND via HalGetBusDataByOffset!\n");
    }

    CloseHandle(h);
    return 0;
}
