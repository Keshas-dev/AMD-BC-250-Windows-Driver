/* pci-smn-smu-test.c - Test SMU via PCI config 0xB8/0xBC (DF 00:00.0).
 * This is the Linux amdgpu primary SMN path.
 *
 * Usage:
 *   pci-smn-smu-test.exe              - SMU version + features via PCI SMN
 *   pci-smn-smu-test.exe q0 0x3D      - Q0 GetEnabledFeatures
 *   pci-smn-smu-test.exe q3 0x98 <addr> - Q3 ungated write
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "..\inc\amdbc250_ioctl.h"

static const char *resp_name(uint32_t s) {
    switch (s) {
        case 1: return "OK";
        case 0xFF: return "FAIL";
        case 0xFE: return "UNKNOWN_CMD";
        case 0xFD: return "REJECTED";
        case 0xFC: return "BUSY";
        case 0: return "TIMEOUT";
        default: return "?";
    }
}

/* Raw PCI config SMN read via DF 00:00.0 B8/BC */
static uint32_t pci_smn_read(HANDLE h, uint32_t addr) {
    DWORD br = 0;
    AMDBC250_IOCTL_PCI_SMN_ACCESS in, out;
    ZeroMemory(&in, sizeof(in));
    in.SmnAddress = addr;
    in.Bus = 0;
    in.Device = 0;
    in.Function = 0;
    ZeroMemory(&out, sizeof(out));
    if (!DeviceIoControl(h, IOCTL_AMDBC250_PCI_SMN_ACCESS,
                         &in, sizeof(in), &out, sizeof(out), &br, NULL)) {
        return 0xFFFFFFFF;
    }
    return out.SmnData;
}

/* Raw PCI config SMN write via DF 00:00.0 B8/BC */
static int pci_smn_write(HANDLE h, uint32_t addr, uint32_t val, uint32_t *result) {
    DWORD br = 0;
    AMDBC250_IOCTL_PCI_SMN_ACCESS in, out;
    ZeroMemory(&in, sizeof(in));
    in.SmnAddress = addr;
    in.SmnData = val;
    in.IsWrite = 1;
    in.Bus = 0;
    in.Device = 0;
    in.Function = 0;
    ZeroMemory(&out, sizeof(out));
    if (!DeviceIoControl(h, IOCTL_AMDBC250_PCI_SMN_ACCESS,
                         &in, sizeof(in), &out, sizeof(out), &br, NULL)) {
        return 0;
    }
    if (result) *result = out.Result;
    return 1;
}

/* SMU mailbox send via raw PCI SMN (Linux-style Bc250PciTransport) */
static int smu_send_via_pci(HANDLE h, uint32_t q, uint32_t msg, uint32_t arg,
                            uint32_t *resp) {
    /* Queue addresses from bc250_smu_oc Linux library */
    static const uint32_t q_cmd[] = {0x03B10A08, 0x03B10A00, 0x03B10528, 0x03B10A20, 0x03B10A24};
    static const uint32_t q_rsp[] = {0x03B10A68, 0x03B10A60, 0x03B10564, 0x03B10A80, 0x03B10A84};
    static const uint32_t q_arg[] = {0x03B10A48, 0x03B10A40, 0x03B10998, 0x03B10A88, 0x03B10A8C};

    if (q >= sizeof(q_cmd)/sizeof(q_cmd[0])) return 0;

    /* Wait for ready */
    for (int i = 0; i < 500; i++) {
        uint32_t r = pci_smn_read(h, q_rsp[q]);
        if (r == 1 || r == 0xFF || r == 0xFE || r == 0xFD || r == 0xFC) break;
        Sleep(1);
    }

    /* Write args: RSP=0, ARG, CMD */
    pci_smn_write(h, q_rsp[q], 0, NULL);
    pci_smn_write(h, q_arg[q], arg, NULL);
    pci_smn_write(h, q_cmd[q], msg, NULL);

    /* Poll for response */
    for (int i = 0; i < 2000; i++) {
        uint32_t r = pci_smn_read(h, q_rsp[q]);
        if (r == 1 || r == 0xFF || r == 0xFE || r == 0xFD || r == 0xFC) {
            if (resp) *resp = pci_smn_read(h, q_arg[q]);
            return (int)r;
        }
        Sleep(1);
    }
    return -100;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== BC-250 SMU via PCI config 0xB8/0xBC (DF 00:00.0) ===\n\n");

    HANDLE h = CreateFileA("\\\\.\\AMDBC250DreamV43",
        GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("FAIL: CreateFile gle=%lu\n", GetLastError());
        return 1;
    }

    /* Init BAR5 for MMIO fallback */
    AMDBC250_IOCTL_INIT_HARDWARE ih;
    DWORD br = 0;
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL;
    ih.MmioSize = 0x80000;
    ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
    DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &br, NULL);

    /* Compare: PCI SMN vs BAR5 SMN for basic registers */
    printf("--- Register read comparison ---\n");
    uint32_t pci_smu_ver = pci_smn_read(h, 0x03B10050);
    uint32_t pci_features = pci_smn_read(h, 0x03B10998);
    printf("PCI SMN: SMU_VER=0x%08X FEATURES=0x%08X\n", pci_smu_ver, pci_smu_ver ? pci_features : 0);

    /* SMU mailbox via PCI SMN */
    printf("\n--- SMU Q0 0x02 GetSmuVersion via PCI SMN ---\n");
    uint32_t resp = 0;
    int r = smu_send_via_pci(h, 0, 0x02, 0, &resp);
    printf("Response: 0x%02X (%s), resp=0x%08X\n", r, resp_name(r), resp);

    printf("\n--- SMU Q0 0x3D GetEnabledFeatures via PCI SMN ---\n");
    r = smu_send_via_pci(h, 0, 0x3D, 0, &resp);
    printf("Response: 0x%02X (%s), features=0x%08X\n", r, resp_name(r), resp);

    /* Command mode: arbitrary message */
    if (argc >= 3) {
        uint32_t q = (uint32_t)strtoul(argv[1], NULL, 0);
        uint32_t m = (uint32_t)strtoul(argv[2], NULL, 0);
        uint32_t a = (argc >= 4) ? (uint32_t)strtoul(argv[3], NULL, 0) : 0;
        printf("\n--- PCI SMN send q=%u msg=0x%X arg=0x%X ---\n", q, m, a);
        r = smu_send_via_pci(h, q, m, a, &resp);
        printf("Response: 0x%02X (%s), resp=0x%08X\n", r, resp_name(r), resp);
    }

    CloseHandle(h);
    return 0;
}