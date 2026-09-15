/* vcn-full-dump.c - comprehensive VCN MMIO dump via DF Q3 0x2A mem64.
 * Reads VCN slice registers + dom6 sequencer.
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "..\inc\amdbc250_ioctl.h"

typedef struct _AMDBC250_PCI_SMN_IN {
    UINT32 SmnAddress;
    UINT32 SmnData;
    UINT32 IsWrite;
    UINT32 Result;
    UINT32 Bus;
    UINT32 Device;
    UINT32 Function;
} AMDBC250_PCI_SMN_IN, *PAMDBC250_PCI_SMN_IN;

typedef struct _AMDBC250_PCI_SMN_OUT {
    UINT32 SmnAddress;
    UINT32 SmnData;
    UINT32 IsWrite;
    UINT32 Result;
    UINT32 Bus;
    UINT32 Device;
    UINT32 Function;
    UINT32 Method;
    UINT32 Bar5SmnData;
} AMDBC250_PCI_SMN_OUT, *PAMDBC250_PCI_SMN_OUT;

static int pci_smn_read(HANDLE h, uint32_t addr, uint32_t *val) {
    DWORD br = 0;
    AMDBC250_IOCTL_PCI_SMN_ACCESS in, out;
    ZeroMemory(&in, sizeof(in));
    in.SmnAddress = addr;
    ZeroMemory(&out, sizeof(out));
    if (!DeviceIoControl(h, IOCTL_AMDBC250_PCI_SMN_ACCESS,
                         &in, sizeof(in), &out, sizeof(out), &br, NULL)) {
        return 0;
    }
    *val = out.SmnData;
    return 1;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== BC-250 VCN full MMIO dump via DF ===\n\n");

    HANDLE h = CreateFileA("\\\\.\\AMDBC250DreamV43",
        GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("FAIL: CreateFile gle=%lu\n", GetLastError());
        return 1;
    }

    AMDBC250_IOCTL_INIT_HARDWARE ih;
    DWORD br = 0;
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL;
    ih.MmioSize = 0x80000;
    ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
    DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &br, NULL);

    printf("--- dom6 sequencer ---\n");
    uint32_t dom6[] = {0x0006D190, 0x0006D0F8, 0x0006D17C, 0x0006D184};
    const char *d6name[] = {"STATUS", "CTRL", "CMD", "RAIL"};
    for (int i = 0; i < 4; i++) {
        uint32_t v = 0;
        if (pci_smn_read(h, dom6[i], &v))
            printf("  %s 0x%06X = 0x%08X\n", d6name[i], dom6[i], v);
    }

    printf("\n--- VCN slice (0x02403000-0x02403FFF) ---\n");
    for (uint32_t addr = 0x02403000; addr < 0x02404000; addr += 4) {
        uint32_t v = 0;
        if (pci_smn_read(h, addr, &v)) {
            if (v != 0xFFFFFFFF && v != 0x00000000)
                printf("  VCN 0x%08X = 0x%08X\n", addr, v);
        }
    }

    printf("\n--- VCN key regs (renoir VCN2 map) ---\n");
    uint32_t vcn_key[] = {
        0x02403000, 0x02403004, 0x02403008, 0x0240300C,
        0x02403010, 0x02403014, 0x02403018, 0x0240301C,
        0x02403400, 0x02403404, 0x02403408, 0x0240340C,
        0x02403800, 0x02403804, 0x02403808, 0x0240380C,
        0x02403C00, 0x02403C04, 0x02403C08, 0x02403C0C,
    };
    const char *vcn_name[] = {
        "VCN_CTRL", "VCN_STATUS", "VCN_CMD", "VCN_RAIL",
        "VCN_UNK10", "VCN_UNK14", "VCN_UNK18", "VCN_UNK1C",
        "VCN_FW_LO", "VCN_FW_HI", "VCN_TMR_LO", "VCN_TMR_HI",
        "VCN_IB_LO", "VCN_IB_HI", "VCN_IB_CNTL", "VCN_IB_STATUS",
        "VCN_DECODE", "VCN_ENCODE", "VCN_UNKC8", "VCN_UNKCC",
    };
    for (int i = 0; i < sizeof(vcn_key)/sizeof(vcn_key[0]); i++) {
        uint32_t v = 0;
        if (pci_smn_read(h, vcn_key[i], &v))
            printf("  %s 0x%08X = 0x%08X\n", vcn_name[i], vcn_key[i], v);
    }

    printf("\n--- GC slice (0x02402C00-0x02402FFF) ---\n");
    for (uint32_t addr = 0x02402C00; addr < 0x02403000; addr += 4) {
        uint32_t v = 0;
        if (pci_smn_read(h, addr, &v)) {
            if (v != 0xFFFFFFFF && v != 0x00000000)
                printf("  GC  0x%08X = 0x%08X\n", addr, v);
        }
    }

    CloseHandle(h);
    return 0;
}
