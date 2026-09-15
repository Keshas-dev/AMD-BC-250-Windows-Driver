/* smu-all-msgs-test.c — verify ALL whitelisted SMU messages. */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "..\inc\amdbc250_ioctl.h"

static uint32_t pci_smn_read(HANDLE h, uint32_t addr) {
    DWORD br = 0;
    AMDBC250_IOCTL_PCI_SMN_ACCESS in, out;
    ZeroMemory(&in, sizeof(in));
    in.SmnAddress = addr; in.Bus = 0; in.Device = 0; in.Function = 0;
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
    in.SmnAddress = addr; in.SmnData = val; in.IsWrite = 1;
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

static int g_passed = 0, g_failed = 0;

static void run_msg(HANDLE h, int queue, uint32_t msg, uint32_t arg,
                     const char *name, int is_write) {
    uint32_t resp = 0;
    int r;
    if (is_write) {
        uint32_t wedge;
        r = smu_q3_send(h, 0x01, 0x12345678, &wedge);
        if (r != 1) { printf("  WEDGE before %s!\n", name); g_failed++; return; }
    }
    r = (queue == 0) ? smu_q0_send(h, msg, arg, &resp)
                     : smu_q3_send(h, msg, arg, &resp);
    if (is_write) {
        uint32_t wedge;
        r = smu_q3_send(h, 0x01, 0x12345678, &wedge);
        if (r != 1) { printf("  WEDGE after %s! r=%d\n", name, r); g_failed++; return; }
    }
    if (r == 1) {
        printf("  %-28s OK resp=0x%08X\n", name, resp);
        g_passed++;
    } else {
        printf("  %-28s FAIL r=%d\n", name, r);
        g_failed++;
    }
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== ALL whitelisted SMU CPU messages ===\n\n");

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
    ih.MmioPhysicalBase = 0xFE800000ULL; ih.MmioSize = 0x80000;
    ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
    DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &br, NULL);

    printf("--- READS (read-only queries) ---\n");
    run_msg(h, 0, 0x02, 0, "Q0 0x02 GetSmuVersion", 0);
    run_msg(h, 0, 0x03, 0, "Q0 0x03 GetDriverIfVersion", 0);
    run_msg(h, 0, 0x3D, 0, "Q0 0x3D GetEnabledFeatures", 0);
    run_msg(h, 3, 0x36, 0, "Q3 0x36 GetCpuVoltage", 0);
    run_msg(h, 3, 0x43, 0, "Q3 0x43 GetCoreFreq[0]", 0);
    run_msg(h, 3, 0x43, 7, "Q3 0x43 GetCoreFreq[7]", 0);
    run_msg(h, 3, 0x1E, 0, "Q3 0x1E QueryActiveWgp", 0);

    printf("\n--- WRITES (with wedge check) ---\n");
    run_msg(h, 3, 0x50, 0, "Q3 0x50 ScaleFvidCurve(0)", 1);
    run_msg(h, 3, 0x8B, 80, "Q3 0x8B SetCpuMaxTemp(80)", 1);
    run_msg(h, 3, 0x8C, 80, "Q3 0x8C SetGpuMaxTemp(80)", 1);
    run_msg(h, 3, 0x8F, 3500, "Q3 0x8F SetMaxBoostClk(3500)", 1);
    run_msg(h, 3, 0x9A, 1, "Q3 0x9A DisableExtraVolt(1)", 1);
    run_msg(h, 3, 0x98, 0x0115A870, "Q3 0x98 UngatedSMN(0x0115A870)", 1);
    run_msg(h, 3, 0x28, 0x0000776C, "Q3 0x28 SecSetWritePtr(0x776C)", 1);
    run_msg(h, 3, 0x29, 0x0000DEAD, "Q3 0x29 SecWriteThrough(0xDEAD)", 1);
    run_msg(h, 3, 0x3C, 0x2, "Q3 0x3C EnableFeatures(0x2)", 1);

    printf("\n=== Results: %d passed, %d failed ===\n", g_passed, g_failed);

    CloseHandle(h);
    return 0;
}
