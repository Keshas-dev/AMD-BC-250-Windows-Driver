/* vcn-fabric-test.c - test if VCN fabric is truly writable after Q3 0x3C sweep.
 * Writes test patterns to VCN_CTRL (0x02403000) and reads back.
 * Also probes dom6 CTRL bits.
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

static int pci_smn_write(HANDLE h, uint32_t addr, uint32_t val) {
    DWORD br = 0;
    AMDBC250_IOCTL_PCI_SMN_ACCESS in, out;
    ZeroMemory(&in, sizeof(in));
    in.SmnAddress = addr;
    in.SmnData = val;
    in.IsWrite = 1;
    ZeroMemory(&out, sizeof(out));
    if (!DeviceIoControl(h, IOCTL_AMDBC250_PCI_SMN_ACCESS,
                         &in, sizeof(in), &out, sizeof(out), &br, NULL)) {
        return 0;
    }
    return out.Result == 1 ? 1 : 0;
}

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
    printf("=== BC-250 VCN fabric write test ===\n\n");

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

    printf("--- VCN_CTRL write/readback test ---\n");
    uint32_t vcn_ctrl = 0x02403000;
    uint32_t before = 0;
    if (!pci_smn_read(h, vcn_ctrl, &before)) {
        printf("  FAIL: read VCN_CTRL\n");
        return 1;
    }
    printf("  VCN_CTRL before = 0x%08X\n", before);

    uint32_t test_vals[] = {0x00000001, 0x00000002, 0x00000000, 0xFFFFFFFF, 0x12345678};
    for (int i = 0; i < sizeof(test_vals)/sizeof(test_vals[0]); i++) {
        uint32_t written = test_vals[i];
        printf("  write 0x%08X -> ", written);
        if (!pci_smn_write(h, vcn_ctrl, written)) {
            printf("WRITE FAIL\n");
            continue;
        }
        uint32_t after = 0;
        if (!pci_smn_read(h, vcn_ctrl, &after)) {
            printf("READ FAIL\n");
            continue;
        }
        printf("readback 0x%08X (%s)\n", after,
               (after == written) ? "MATCH" : "MISMATCH");
    }

    printf("\n--- dom6 CTRL bit decode ---\n");
    uint32_t dom6_ctrl = 0x0006D0F8;
    uint32_t ctrl = 0;
    if (pci_smn_read(h, dom6_ctrl, &ctrl)) {
        printf("  CTRL 0x%08X = 0x%08X\n", dom6_ctrl, ctrl);
        printf("  bit0 (enable): %s\n", (ctrl & 1) ? "SET" : "CLEAR");
        printf("  bit1 (ready?): %s\n", (ctrl & 2) ? "SET" : "CLEAR");
        printf("  bit2 (reset?): %s\n", (ctrl & 4) ? "SET" : "CLEAR");
        printf("  bits 3-31: 0x%08X\n", ctrl & ~7u);
    }

    printf("\n--- VCN_STATUS bit decode ---\n");
    uint32_t vcn_status = 0x02403004;
    uint32_t status = 0;
    if (pci_smn_read(h, vcn_status, &status)) {
        printf("  VCN_STATUS 0x%08X = 0x%08X\n", vcn_status, status);
    }

    CloseHandle(h);
    return 0;
}
