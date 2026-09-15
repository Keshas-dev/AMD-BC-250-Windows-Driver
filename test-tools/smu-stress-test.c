/* smu-stress-test.c — repeatedly run SMU messages to check driver stability. */
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

static int run_iteration(HANDLE h, int iter, int *failures) {
    int fails = 0;
    uint32_t resp = 0;
    int r;

    /* Q0 0x02 GetSmuVersion */
    r = smu_q0_send(h, 0x02, 0, &resp);
    if (r != 1) { printf("[%d] Q0 0x02 FAIL r=%d\n", iter, r); fails++; }

    /* Q0 0x0F QueryGfxclk */
    r = smu_q0_send(h, 0x0F, 0, &resp);
    if (r != 1) { printf("[%d] Q0 0x0F FAIL r=%d\n", iter, r); fails++; }

    /* Q0 0x3D GetEnabledSmuFeatures */
    r = smu_q0_send(h, 0x3D, 0, &resp);
    if (r != 1) { printf("[%d] Q0 0x3D FAIL r=%d resp=0x%08X\n", iter, r, resp); fails++; }

    /* Q3 0x36 GetCurrentCpuVoltage */
    r = smu_q3_send(h, 0x36, 0, &resp);
    if (r != 1) { printf("[%d] Q3 0x36 FAIL r=%d\n", iter, r); fails++; }

    /* Q3 0x43 GetCoreFreq core0 */
    r = smu_q3_send(h, 0x43, 0, &resp);
    if (r != 1) { printf("[%d] Q3 0x43 FAIL r=%d\n", iter, r); fails++; }

    /* Wedge check: Q3 0x01 TestMessage */
    r = smu_q3_send(h, 0x01, 0x12345678, &resp);
    if (r != 1) { printf("[%d] WEDGE CHECK FAIL r=%d\n", iter, r); fails++; }

    /* Q3 0x8B SetCpuMaxTemp (safe value) */
    r = smu_q3_send(h, 0x8B, 80, &resp);
    if (r != 1) { printf("[%d] Q3 0x8B FAIL r=%d\n", iter, r); fails++; }

    /* Q3 0x8C SetGpuMaxTemp (safe value) */
    r = smu_q3_send(h, 0x8C, 80, &resp);
    if (r != 1) { printf("[%d] Q3 0x8C FAIL r=%d\n", iter, r); fails++; }

    /* Q3 0x8F SetMaxCpuBoostClk (safe value) */
    r = smu_q3_send(h, 0x8F, 3500, &resp);
    if (r != 1) { printf("[%d] Q3 0x8F FAIL r=%d\n", iter, r); fails++; }

    /* Q0 0x3D re-check features (should still be valid) */
    r = smu_q0_send(h, 0x3D, 0, &resp);
    if (r != 1) { printf("[%d] Q0 0x3D retry FAIL r=%d\n", iter, r); fails++; }

    /* Verify SMU alive: Q0 0x02 again */
    r = smu_q0_send(h, 0x02, 0, &resp);
    if (r != 1 || resp != 0x00580600) {
        printf("[%d] SMU VERSION MISMATCH r=%d resp=0x%08X\n", iter, r, resp);
        fails++;
    }

    return fails;
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    int iterations = 50;
    int total_fails = 0;

    printf("=== SMU driver stress test (%d iterations) ===\n\n", iterations);

    HANDLE h = CreateFileA("\\\\.\\AMDBC250DreamV43",
        GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("FAIL: CreateFile gle=%lu\n", GetLastError());
        return 1;
    }

    /* Re-init hardware (maps BAR5). */
    AMDBC250_IOCTL_INIT_HARDWARE ih;
    DWORD br = 0;
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL;
    ih.MmioSize = 0x80000;
    ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
    if (!DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE,
                          &ih, sizeof(ih), &ih, sizeof(ih), &br, NULL)) {
        printf("INIT_HARDWARE FAILED gle=%lu\n", GetLastError());
        CloseHandle(h);
        return 1;
    }
    printf("INIT_HARDWARE OK (iter=0)\n\n");

    for (int i = 1; i <= iterations; i++) {
        int f = run_iteration(h, i, &total_fails);
        total_fails += f;
        if (i % 10 == 0 || f > 0) {
            printf("[iter %d/%d] fails=%d cumulative=%d\n", i, iterations, f, total_fails);
        }
        if (f > 2) {
            printf("!!! Multiple failures in one iteration — stopping.\n");
            break;
        }
        Sleep(50);
    }

    printf("\n=== Results: %d total failures across %d iterations ===\n", total_fails, iterations);

    CloseHandle(h);
    return 0;
}
