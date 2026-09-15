/* smu-sram-test.c - SMU SRAM write pointer + data via Q3 0x28/0x29. */
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
    printf("=== SMU SRAM test (Q3 0x28/0x29) ===\n\n");

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

    /* Read baseline SMU SRAM via direct SMU SRAM registers if available. */
    printf("--- Baseline SMU SRAM via SMN 0x03B10xxx ---\n");
    uint32_t sram_base = 0x03B10000;
    for (int i = 0; i < 8; i++) {
        uint32_t v = pci_smn_read(h, sram_base + i * 4);
        printf("  SMN[0x%08X] = 0x%08X\n", sram_base + i * 4, v);
    }

    /* Test 1: Q3 0x28 set write pointer to 0x00000000 (low SRAM). */
    printf("\n--- Test 1: Q3 0x28 sec_set_write_ptr addr=0x%08X ---\n", 0x00000000);
    uint32_t resp = 0;
    int r = smu_q3_send(h, 0x28, 0x00000000, &resp);
    printf(" result=%d resp=0x%08X\n", r, resp);

    /* Test 2: Q3 0x29 write data 0xDEADBEEF. */
    printf("\n--- Test 2: Q3 0x29 sec_write_through data=0xDEADBEEF ---\n");
    r = smu_q3_send(h, 0x29, 0xDEADBEEF, &resp);
    printf(" result=%d resp=0x%08X\n", r, resp);

    /* Test 3: Read back via SMN to verify. */
    printf("\n--- Test 3: Read back SMN[0x%08X] ---\n", sram_base);
    for (int i = 0; i < 8; i++) {
        uint32_t v = pci_smn_read(h, sram_base + i * 4);
        printf("  SMN[0x%08X] = 0x%08X\n", sram_base + i * 4, v);
    }

    /* Test 4: SMU wedge check. */
    printf("\n--- Test 4: Q3 0x01 TestMessage (wedge check) ---\n");
    r = smu_q3_send(h, 0x01, 0x12345678, &resp);
    printf(" result=%d resp=0x%08X\n", r, resp);

    CloseHandle(h);
    return 0;
}
