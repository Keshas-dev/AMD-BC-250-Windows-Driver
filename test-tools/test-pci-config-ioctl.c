#include <windows.h>
#include <stdio.h>
#include "amdbc250_ioctl.h"

static HANDLE OpenDevice(void) {
    return CreateFileA("\\\\.\\AMDBC250DreamV43",
        GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
}

int main(void) {
    printf("AMD BC-250 PCI Config Test via IOCTL\n");
    printf("=====================================\n\n");

    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) {
        printf("ERROR: Cannot open device (error %lu)\n", GetLastError());
        return 1;
    }

    DWORD bytes = 0;

    /* Test IOCTL_AMDBC250_READ_PCI_CONFIG via ECAM + IO ports fallback */
    /* This reads raw PCI config space, falling back to IO ports if ECAM fails */

    struct {
        UINT32 bus; UINT32 dev; UINT32 func; const char* desc;
    } targets[] = {
        {0, 0, 0, "Host bridge (B0:D0:F0)"},
        {0, 8, 1, "PCI bridge (B0:D8:F1)"},
        {0, 0, 2, "NBIO func 2"},
        {1, 0, 0, "GPU (B1:D0:F0) - BC-250"},
        {1, 0, 1, "HDMI audio (B1:D0:F1)"},
        {1, 0, 2, "PSP/CCP (B1:D0:F2)"},
    };

    for (int t = 0; t < sizeof(targets)/sizeof(targets[0]); t++) {
        printf("=== %s ===\n", targets[t].desc);

        AMDBC250_IOCTL_READ_PCI_CONFIG r = {0};
        r.Bus = targets[t].bus;
        r.Device = targets[t].dev;
        r.Function = targets[t].func;

        BOOL ok = DeviceIoControl(h, 0x80000BAC,
            &r, sizeof(r), &r, sizeof(r), &bytes, NULL);

        if (ok && r.BytesRead > 0) {
            UINT16 vendor = *(UINT16*)(r.ConfigData + 0);
            UINT16 device = *(UINT16*)(r.ConfigData + 2);
            UINT16 cmd = *(UINT16*)(r.ConfigData + 4);
            UINT32 bar0 = *(UINT32*)(r.ConfigData + 0x10);
            UINT32 bar2 = *(UINT32*)(r.ConfigData + 0x18);
            UINT32 bar5 = *(UINT32*)(r.ConfigData + 0x24);

            printf("  Vendor=0x%04X, Device=0x%04X, Cmd=0x%04X\n", vendor, device, cmd);
            printf("  BAR0=0x%08X, BAR2=0x%08X, BAR5=0x%08X\n", bar0, bar2, bar5);

            if (vendor == 0x1002) printf("  *** AMD VENDOR ***\n");
            if (vendor == 0xFFFF || vendor == 0x0000) printf("  (no device or invalid)\n");

            /* Print first 64 bytes of config space */
            printf("  Config dump (+0x00..+0x3F):\n");
            for (int off = 0; off < 64; off += 16) {
                printf("    +0x%02X:", off);
                for (int i = 0; i < 16; i++) {
                    printf(" %02X", r.ConfigData[off + i]);
                }
                printf("\n");
            }
        } else {
            printf("  FAILED (error %lu, BytesRead=%lu)\n",
                ok ? 0 : GetLastError(), r.BytesRead);
        }
        printf("\n");
    }

    CloseHandle(h);
    printf("Done.\n");
    return 0;
}
