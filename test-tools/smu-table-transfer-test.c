/* smu-table-transfer-test.c - SMU DPM/table transfer via Q0 0x04-0x07. */
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

/* SMU Q0 send via PCI SMN. */
static int smu_q0_send(HANDLE h, uint32_t msg, uint32_t arg, uint32_t *resp) {
    static const uint32_t q_cmd = 0x03B10A08;
    static const uint32_t q_rsp = 0x03B10A68;
    static const uint32_t q_arg = 0x03B10A48;
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
    printf("=== SMU table transfer test (Q0 0x04-0x07) ===\n\n");

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

    /* Step 0: Allocate 4KB DMA buffer. */
    printf("--- Step 0: Allocate 4KB DMA buffer ---\n");
    ULONG64 dmaPa = 0, dmaVa = 0;
    ULONG allocSize = 0x1000;
    ULONG64 outBuf[2] = {0};
    if (!DeviceIoControl(h, 0x80000930,
                         &allocSize, sizeof(allocSize),
                         outBuf, sizeof(outBuf), &br, NULL)) {
        printf("FAIL: alloc gle=%lu\n", GetLastError());
        CloseHandle(h);
        return 1;
    }
    dmaPa = outBuf[0];
    dmaVa = outBuf[1];
    printf(" DMA PA=0x%llX VA=0x%llX\n", dmaPa, dmaVa);

    /* Step 1: Set DRAM address high via Q0 0x04. */
    printf("\n--- Step 1: Q0 0x04 SetDrvTblAddrHi=0x%llX ---\n", dmaPa >> 32);
    uint32_t resp = 0;
    int r = smu_q0_send(h, 0x04, (uint32_t)(dmaPa >> 32), &resp);
    printf(" result=%d resp=0x%08X\n", r, resp);

    /* Step 2: Set DRAM address low via Q0 0x05. */
    printf("\n--- Step 2: Q0 0x05 SetDrvTblAddrLo=0x%llX ---\n", dmaPa & 0xFFFFFFFF);
    r = smu_q0_send(h, 0x05, (uint32_t)(dmaPa & 0xFFFFFFFF), &resp);
    printf(" result=%d resp=0x%08X\n", r, resp);

    /* Step 3: Transfer table DRAM→SMU via Q0 0x07. */
    printf("\n--- Step 3: Q0 0x07 TransferTable Dram2Smu ---\n");
    r = smu_q0_send(h, 0x07, 0, &resp);
    printf(" result=%d resp=0x%08X\n", r, resp);

    /* Step 3b: Reverse transfer SMU→DRAM via Q0 0x06. */
    printf("\n--- Step 3b: Q0 0x06 TransferTable Smu2Dram ---\n");
    r = smu_q0_send(h, 0x06, 0, &resp);
    printf(" result=%d resp=0x%08X\n", r, resp);

    /* Step 3c: Retry 0x07 with 0x1000-aligned address fallback. */
    ULONG64 dmaPa2 = (dmaPa > 0x1000) ? (dmaPa - 0x1000) : 0;
    if (dmaPa2 == 0) dmaPa2 = 0x1000;
    printf("\n--- Step 3c: Q0 0x04/0x05 with fallback PA=0x%llX ---\n", dmaPa2);
    r = smu_q0_send(h, 0x04, (uint32_t)(dmaPa2 >> 32), &resp);
    printf("  Q0 0x04 result=%d resp=0x%08X\n", r, resp);
    r = smu_q0_send(h, 0x05, (uint32_t)(dmaPa2 & 0xFFFFFFFF), &resp);
    printf("  Q0 0x05 result=%d resp=0x%08X\n", r, resp);
    r = smu_q0_send(h, 0x07, 0, &resp);
    printf("  Q0 0x07 result=%d resp=0x%08X\n", r, resp);

    /* Step 4: Check SMU wedge via Q0 0x01 (TestMessage). */
    printf("\n--- Step 4: Q0 0x01 TestMessage (wedge check) ---\n");
    r = smu_q0_send(h, 0x01, 0x12345678, &resp);
    printf(" result=%d resp=0x%08X\n", r, resp);

    /* Free DMA buffer. */
    DeviceIoControl(h, 0x80000934,
                    &dmaVa, sizeof(dmaVa), NULL, 0, &br, NULL);

    CloseHandle(h);
    return 0;
}
