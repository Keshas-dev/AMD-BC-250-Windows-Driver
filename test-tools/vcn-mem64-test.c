/* vcn-mem64-test.c - VCN power/fabric test via SMU mem64 (Q3 0x3C + 0x2A/0x2B). */
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

/* SMU Q3 send via PCI SMN. */
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

/* Enable dom6 via Q3 0x3C feature mask. */
static int vcn_enable_dom6(HANDLE h, uint32_t mask) {
    return smu_q3_send(h, 0x3C, mask, NULL);
}

/* SMU mem64 write via Q3 0x2A/0x2B. */
static int vcn_mem64_write(HANDLE h, uint32_t addr, uint32_t val, uint32_t *resp) {
    int r1 = smu_q3_send(h, 0x2A, addr, NULL);
    if (r1 != 1) return r1;
    return smu_q3_send(h, 0x2B, val, resp);
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== VCN mem64/fabric test (SMU Q3 0x3C + 0x2A/0x2B) ===\n\n");

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

    /* Read baseline VCN MMIO via PCI SMN. */
    printf("--- Baseline VCN MMIO via PCI SMN ---\n");
    uint32_t vcn_ctrl = pci_smn_read(h, 0x02403000);
    uint32_t vcn_doorbell = pci_smn_read(h, 0x02403BCC);
    uint32_t dom6_status = pci_smn_read(h, 0x02403004);
    printf("VCN_CTRL   (0x02403000) = 0x%08X\n", vcn_ctrl);
    printf("VCN_DOORBELL(0x02403BCC)= 0x%08X\n", vcn_doorbell);
    printf("DOM6_STATUS(0x02403004)= 0x%08X\n", dom6_status);

    /* Step 1: Try enabling dom6 features via Q3 0x3C. */
    printf("\n--- Step 1: Q3 0x3C enable_features mask sweep ---\n");
    uint32_t masks[] = {0x2, 0x80, 0x100, 0x10000, 0x20000, 0xDD613DFF};
    for (int i = 0; i < sizeof(masks)/sizeof(masks[0]); i++) {
        uint32_t r = smu_q3_send(h, 0x3C, masks[i], NULL);
        printf(" mask=0x%08X r=%d\n", masks[i], r);
    }
    printf(" after mask sweep:\n");
    printf(" VCN_CTRL   = 0x%08X\n", pci_smn_read(h, 0x02403000));
    printf(" VCN_DOORBELL=0x%08X\n", pci_smn_read(h, 0x02403BCC));
    printf(" DOM6_STATUS= 0x%08X\n", pci_smn_read(h, 0x02403004));

    /* Step 2: SMU mem64 write to VCN_CTRL via Q3 0x2A/0x2B. */
    printf("\n--- Step 2: Q3 0x2A/0x2B mem64 write VCN_CTRL=0x00000001 ---\n");
    uint32_t mem64_resp = 0;
    int mr = vcn_mem64_write(h, 0x02403000, 0x00000001, &mem64_resp);
    printf(" mem64 write result=%d resp=0x%08X\n", mr, mem64_resp);
    printf(" VCN_CTRL after = 0x%08X\n", pci_smn_read(h, 0x02403000));

    /* Step 3: SMU mem64 write to VCN doorbell. */
    printf("\n--- Step 3: Q3 0x2A/0x2B mem64 write VCN_DOORBELL=0x00000001 ---\n");
    mr = vcn_mem64_write(h, 0x02403BCC, 0x00000001, &mem64_resp);
    printf(" mem64 write result=%d resp=0x%08X\n", mr, mem64_resp);
    printf(" VCN_DOORBELL after = 0x%08X\n", pci_smn_read(h, 0x02403BCC));

    /* Step 4: Check SMU wedge via Q3 0x2C. */
    printf("\n--- Step 4: SMU wedge check Q3 0x2C ---\n");
    uint32_t wedge_r = smu_q3_send(h, 0x2C, 0, NULL);
    printf(" Q3 0x2C response: 0x%02X\n", wedge_r);

    CloseHandle(h);
    return 0;
}
