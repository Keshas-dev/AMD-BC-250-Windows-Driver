/* vcn-bit-sweep.c - VCN dom6 feature bit sweep via Q3 0x3C. */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "..\inc\amdbc250_ioctl.h"

static uint32_t pci_smn_read(HANDLE h, uint32_t addr) {
    DWORD br = 0;
    AMDBC250_IOCTL_PCI_SMN_ACCESS in, out;
    ZeroMemory(&in, sizeof(in));
    in.SmnAddress = addr;
    in.Bus = 0; in.Device = 0; in.Function = 0;
    ZeroMemory(&out, sizeof(out));
    if (!DeviceIoControl(h, IOCTL_AMDBC250_PCI_SMN_ACCESS,
                         &in, sizeof(in), &out, sizeof(out), &br, NULL)) {
        return 0xFFFFFFFF;
    }
    return out.SmnData;
}

static int pci_smn_write(HANDLE h, uint32_t addr, uint32_t val, uint32_t *result) {
    DWORD br = 0;
    AMDBC250_IOCTL_PCI_SMN_ACCESS in, out;
    ZeroMemory(&in, sizeof(in));
    in.SmnAddress = addr;
    in.SmnData = val;
    in.IsWrite = 1;
    in.Bus = 0; in.Device = 0; in.Function = 0;
    ZeroMemory(&out, sizeof(out));
    if (!DeviceIoControl(h, IOCTL_AMDBC250_PCI_SMN_ACCESS,
                         &in, sizeof(in), &out, sizeof(out), &br, NULL)) {
        return 0;
    }
    if (result) *result = out.Result;
    return 1;
}

static int smu_q3_send(HANDLE h, uint32_t msg, uint32_t arg, uint32_t *resp) {
    static const uint32_t q_cmd = 0x03B10A20;
    static const uint32_t q_rsp = 0x03B10A80;
    static const uint32_t q_arg = 0x03B10A88;
    for (int i = 0; i < 500; i++) {
        uint32_t r = pci_smn_read(h, q_rsp);
        if (r == 1 || r == 0xFF || r == 0xFE || r == 0xFD || r == 0xFC) break;
        Sleep(1);
    }
    pci_smn_write(h, q_rsp, 0, NULL);
    pci_smn_write(h, q_arg, arg, NULL);
    pci_smn_write(h, q_cmd, msg, NULL);
    for (int i = 0; i < 2000; i++) {
        uint32_t r = pci_smn_read(h, q_rsp);
        if (r == 1 || r == 0xFF || r == 0xFE || r == 0xFD || r == 0xFC) {
            if (resp) *resp = pci_smn_read(h, q_arg);
            return (int)r;
        }
        Sleep(1);
    }
    return -100;
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== VCN dom6 feature bit sweep (Q3 0x3C) ===\n\n");

    HANDLE h = CreateFileA("\\\\.\\AMDBC250DreamV43",
        GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE,
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

    /* Baseline VCN registers. */
    uint32_t vcn_ctrl = pci_smn_read(h, 0x02403000);
    uint32_t vcn_doorbell = pci_smn_read(h, 0x02403BCC);
    uint32_t dom6_status = pci_smn_read(h, 0x02403004);
    printf("Baseline:\n");
    printf("  VCN_CTRL    (0x02403000) = 0x%08X\n", vcn_ctrl);
    printf("  VCN_DOORBELL(0x02403BCC) = 0x%08X\n", vcn_doorbell);
    printf("  DOM6_STATUS (0x02403004) = 0x%08X\n", dom6_status);

    printf("\n--- Individual bit sweep (safe bits only, 0x2/0x80/0x100/0x10000 known) ---\n");
    uint32_t safe_bits[] = {0x2, 0x80, 0x100, 0x10000};
    for (int i = 0; i < sizeof(safe_bits)/sizeof(safe_bits[0]); i++) {
        uint32_t r = smu_q3_send(h, 0x3C, safe_bits[i], NULL);
        printf(" bit=0x%08X r=%d", safe_bits[i], r);
        if (r == 1) {
            uint32_t c = pci_smn_read(h, 0x02403000);
            uint32_t d = pci_smn_read(h, 0x02403004);
            printf(" VCN_CTRL=0x%08X DOM6=0x%08X", c, d);
        }
        printf("\n");
    }

    printf("\n--- Combined safe bits ---\n");
    uint32_t combined = 0x2 | 0x80 | 0x100 | 0x10000;
    uint32_t r = smu_q3_send(h, 0x3C, combined, NULL);
    printf(" mask=0x%08X r=%d", combined, r);
    if (r == 1) {
        printf(" VCN_CTRL=0x%08X DOM6=0x%08X",
               pci_smn_read(h, 0x02403000), pci_smn_read(h, 0x02403004));
    }
    printf("\n");

    printf("\n--- After sweep final state ---\n");
    printf("  VCN_CTRL    = 0x%08X\n", pci_smn_read(h, 0x02403000));
    printf("  VCN_DOORBELL= 0x%08X\n", pci_smn_read(h, 0x02403BCC));
    printf("  DOM6_STATUS = 0x%08X\n", pci_smn_read(h, 0x02403004));

    CloseHandle(h);
    return 0;
}
