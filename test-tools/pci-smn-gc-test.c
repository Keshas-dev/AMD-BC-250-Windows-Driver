/* pci-smn-gc-test.c - Try writing GC registers via PCI config SMN path (DF 00:00.0 B8/BC). */
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

/* SMU mailbox send via raw PCI SMN (Linux-style Bc250PciTransport) */
static int smu_send_via_pci(HANDLE h, uint32_t q, uint32_t msg, uint32_t arg,
                            uint32_t *resp) {
    static const uint32_t q_cmd[] = {0x03B10A08, 0x03B10A00, 0x03B10528, 0x03B10A20, 0x03B10A24};
    static const uint32_t q_rsp[] = {0x03B10A68, 0x03B10A60, 0x03B10564, 0x03B10A80, 0x03B10A84};
    static const uint32_t q_arg[] = {0x03B10A48, 0x03B10A40, 0x03B10998, 0x03B10A88, 0x03B10A8C};
    if (q >= sizeof(q_cmd)/sizeof(q_cmd[0])) return 0;
    for (int i = 0; i < 500; i++) {
        uint32_t r = pci_smn_read(h, q_rsp[q]);
        if (r == 1 || r == 0xFF || r == 0xFE || r == 0xFD || r == 0xFC) break;
        Sleep(1);
    }
    pci_smn_write(h, q_rsp[q], 0, NULL);
    pci_smn_write(h, q_arg[q], arg, NULL);
    pci_smn_write(h, q_cmd[q], msg, NULL);
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

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== PCI SMN path: GC register write test ===\n\n");

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

    /* Test 1: Can PCI SMN read GC registers? */
    printf("--- Test 1: GC register reads via PCI SMN ---\n");
    uint32_t spi_pg = pci_smn_read(h, 0x02405C3C);
    uint32_t kiq_size = pci_smn_read(h, 0x0240E068);
    uint32_t rlc_pg = pci_smn_read(h, 0x02403D64);
    uint32_t vcn_ctrl = pci_smn_read(h, 0x02403000);
    printf("SPI_PG  (0x02405C3C) = 0x%08X\n", spi_pg);
    printf("KIQ_SIZE(0x0240E068) = 0x%08X\n", kiq_size);
    printf("RLC_PG  (0x02403D64) = 0x%08X\n", rlc_pg);
    printf("VCN_CTRL(0x02403000) = 0x%08X\n", vcn_ctrl);

    /* Test 2: Try writing SPI_PG via PCI SMN */
    printf("\n--- Test 2: Write SPI_PG=0x1F via PCI SMN ---\n");
    uint32_t result = 0;
    pci_smn_write(h, 0x02405C3C, 0x1F, &result);
    uint32_t spi_after = pci_smn_read(h, 0x02405C3C);
    printf("Write result=%u, readback=0x%08X\n", result, spi_after);

    /* Test 3: Try writing KIQ_SIZE via PCI SMN */
    printf("\n--- Test 3: Write KIQ_SIZE=0x100 via PCI SMN ---\n");
    pci_smn_write(h, 0x0240E068, 0x100, &result);
    uint32_t kiq_after = pci_smn_read(h, 0x0240E068);
    printf("Write result=%u, readback=0x%08X\n", result, kiq_after);

    /* Test 4: Try writing RLC_PG via PCI SMN */
    printf("\n--- Test 4: Write RLC_PG=0x1F via PCI SMN ---\n");
    pci_smn_write(h, 0x02403D64, 0x1F, &result);
    uint32_t rlc_after = pci_smn_read(h, 0x02403D64);
    printf("Write result=%u, readback=0x%08X\n", result, rlc_after);

    /* Test 5: SMU Q3 0x98 via PCI SMN to SPI_PG address */
    printf("\n--- Test 5: SMU Q3 0x98 -> 0x02405C3C via PCI SMN ---\n");
    uint32_t q3_resp = 0;
    int smu_r = smu_send_via_pci(h, 3, 0x98, 0x02405C3C, &q3_resp);
    printf("Q3 0x98 response: 0x%02X, resp=0x%08X\n", smu_r, q3_resp);
    uint32_t spi_final = pci_smn_read(h, 0x02405C3C);
    printf("SPI_PG after Q3 0x98: 0x%08X\n", spi_final);

    CloseHandle(h);
    return 0;
}