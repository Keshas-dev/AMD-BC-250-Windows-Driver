/* fresh-verification.c - Re-verify all "locked/dead" claims with fresh driver build. */
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

static int smu_cpu_msg(HANDLE h, uint32_t q, uint32_t msg, uint32_t arg,
                       AMDBC250_IOCTL_SMU_CPU_MSG *sm) {
    DWORD br = 0;
    ZeroMemory(sm, sizeof(*sm));
    sm->Queue = q;
    sm->Message = msg;
    sm->Argument = arg;
    if (!DeviceIoControl(h, IOCTL_AMDBC250_SMU_CPU_MSG,
                         sm, sizeof(*sm), sm, sizeof(*sm), &br, NULL)) {
        printf("  FAIL: SMU_CPU_MSG q=%u msg=0x%X arg=0x%X (gle=%lu)\n",
               q, msg, arg, GetLastError());
        return 0;
    }
    return 1;
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

static int pci_smn_write(HANDLE h, uint32_t addr, uint32_t val, uint32_t *result) {
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
    if (result) *result = out.Result;
    return 1;
}

static uint32_t read_reg(HANDLE h, uint32_t offset) {
    AMDBC250_IOCTL_REG_ACCESS ra;
    DWORD br = 0;
    ZeroMemory(&ra, sizeof(ra));
    ra.RegisterOffset = offset;
    if (!DeviceIoControl(h, IOCTL_AMDBC250_READ_REG, &ra, sizeof(ra), &ra, sizeof(ra), &br, NULL)) {
        return 0xFFFFFFFF;
    }
    return ra.Value;
}

static int write_reg(HANDLE h, uint32_t offset, uint32_t val, ...) {
    AMDBC250_IOCTL_REG_ACCESS ra;
    DWORD br = 0;
    ZeroMemory(&ra, sizeof(ra));
    ra.RegisterOffset = offset;
    ra.Value = val;
    if (!DeviceIoControl(h, IOCTL_AMDBC250_WRITE_REG, &ra, sizeof(ra), &ra, sizeof(ra), &br, NULL)) {
        return 0;
    }
    return 1;
}

/* ==================== */
/* TEST 1: SPI_PG 0x5C3C */
/* ==================== */
static void test_spi_pg(HANDLE h) {
    printf("\n=== TEST 1: SPI_PG 0x5C3C ===\n");
    printf("Using corrected GRBM_GFX_INDEX broadcast 0x15000000\n\n");

    uint32_t spi_before = read_reg(h, 0x5C3C);
    printf("SPI_PG before any writes: 0x%08X\n", spi_before);

    printf("\n--- Test 1a: broadcast write (0x15000000) ---\n");
    write_reg(h, 0x34D0, 0x15000000);
    write_reg(h, 0x9C1C, 0xFFE00000);
    uint32_t spi_after_bcast = read_reg(h, 0x5C3C);
    printf("SPI_PG after broadcast CC=0xFFE00000: 0x%08X\n", spi_after_bcast);

    printf("\n--- Test 1b: per-bank writes ---\n");
    uint32_t bank_sel[4] = {0x00000000, 0x00000100, 0x00010000, 0x00010100};
    for (int b = 0; b < 4; b++) {
        write_reg(h, 0x34D0, bank_sel[b]);
        write_reg(h, 0x9C1C, 0x00000000);
        uint32_t spi_perbank = read_reg(h, 0x5C3C);
        uint32_t cc_perbank = read_reg(h, 0x9C1C);
        printf("  Bank 0x%08X: SPI_PG=0x%08X CC=0x%08X\n", bank_sel[b], spi_perbank, cc_perbank);
    }

    printf("\n--- Test 1c: RLC_PG 0x3D64 ---\n");
    write_reg(h, 0x34D0, 0x15000000);
    uint32_t rlc_before = read_reg(h, 0x3D64);
    printf("RLC_PG before: 0x%08X\n", rlc_before);
    write_reg(h, 0x3D64, 0x0000001F);
    uint32_t rlc_after = read_reg(h, 0x3D64);
    printf("RLC_PG after write 0x1F: 0x%08X\n", rlc_after);

    write_reg(h, 0x34D0, 0x15000000);

    printf("\n=== SPI_PG CONCLUSION ===\n");
    if (spi_after_bcast == 0 && rlc_after == 0xFFFFFFFF) {
        printf("SPI_PG STUCK at 0, RLC_PG READ-ONLY — SOS-locked confirmed\n");
    } else if (spi_after_bcast != 0) {
        printf("SPI_PG WRITABLE! Value=0x%08X — WGP unlock possible!\n", spi_after_bcast);
    } else {
        printf("SPI_PG=0 but RLC_PG changed — partial unlock?\n");
    }
}

/* ==================== */
/* TEST 2: KIQ_SIZE 0xE068 */
/* ==================== */
static void test_kiq_size(HANDLE h) {
    printf("\n=== TEST 2: KIQ_SIZE 0xE068 ===\n");
    printf("Testing with ME=1 select (0x00010000) and broadcast\n\n");

    printf("--- Test 2a: default GRBM ---\n");
    uint32_t kiq_size_default = read_reg(h, 0xE068);
    printf("KIQ_SIZE default: 0x%08X\n", kiq_size_default);

    printf("\n--- Test 2b: ME=1 select (0x00010000) ---\n");
    write_reg(h, 0x34D0, 0x00010000);
    uint32_t kiq_size_me1 = read_reg(h, 0xE068);
    printf("KIQ_SIZE with ME=1: 0x%08X\n", kiq_size_me1);

    printf("\n--- Test 2c: write test ---\n");
    write_reg(h, 0x34D0, 0x00010000);
    uint32_t kiq_written = 0x00000100;
    write_reg(h, 0xE068, kiq_written);
    uint32_t kiq_readback = read_reg(h, 0xE068);
    printf("KIQ_SIZE wrote 0x%08X, readback 0x%08X\n", kiq_written, kiq_readback);

    printf("\n--- Test 2d: KIQ_BASE writability ---\n");
    write_reg(h, 0x34D0, 0x00010000);
    uint32_t base_lo_before = read_reg(h, 0xE060);
    uint32_t base_hi_before = read_reg(h, 0xE064);
    printf("KIQ_BASE_LO before: 0x%08X\n", base_lo_before);
    printf("KIQ_BASE_HI before: 0x%08X\n", base_hi_before);

    write_reg(h, 0xE060, 0x12345678);
    write_reg(h, 0xE064, 0x87654321);
    uint32_t base_lo_after = read_reg(h, 0xE060);
    uint32_t base_hi_after = read_reg(h, 0xE064);
    printf("KIQ_BASE_LO after write: 0x%08X\n", base_lo_after);
    printf("KIQ_BASE_HI after write: 0x%08X\n", base_hi_after);

    write_reg(h, 0x34D0, 0x15000000);

    printf("\n=== KIQ CONCLUSION ===\n");
    if (kiq_size_default == 0 && kiq_size_me1 == 0 && kiq_readback == 0) {
        printf("KIQ_SIZE=0 on all selects — hardware read-only confirmed\n");
    } else if (kiq_readback != 0) {
        printf("KIQ_SIZE WRITABLE! Value=0x%08X — KIQ init possible!\n", kiq_readback);
    } else {
        printf("KIQ_SIZE stuck but BASE writable — partial KIQ?\n");
    }
}

/* ==================== */
/* TEST 3: RLC firmware */
/* ==================== */
static void test_rlc_firmware(HANDLE h) {
    printf("\n=== TEST 3: RLC firmware via PSP ring ===\n");
    printf("Scanning known firmware types...\n\n");

    uint32_t fw_types[] = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12
    };
    const char *fw_names[] = {
        "CP_ME", "CP_PFP", "CP_CE", "CP_MEC", "CP_MEC2",
        "RLC_G", "SDMA0", "SDMA1", "VCN", "DMCU",
        "ATHUB", "MMHUB"
    };

    for (int i = 0; i < sizeof(fw_types)/sizeof(fw_types[0]); i++) {
        AMDBC250_IOCTL_PSP_LOAD_IP_FW fwReq;
        DWORD br = 0;
        ZeroMemory(&fwReq, sizeof(fwReq));
        fwReq.FwType = fw_types[i];
        fwReq.FwSize = 0; /* no blob, just probe type support */

        printf("  Type %2u (%s): ", fw_types[i], fw_names[i]);
        if (DeviceIoControl(h, IOCTL_AMDBC250_PSP_LOAD_IP_FW,
                            &fwReq, sizeof(fwReq), &fwReq, sizeof(fwReq), &br, NULL)) {
            printf("Result=%u C2P35=0x%08X C2P81=0x%08X\n",
                   fwReq.Result, fwReq.C2Pmsg35After, fwReq.C2Pmsg81After);
        } else {
            printf("IOCTL failed (gle=%lu)\n", GetLastError());
        }
    }

    printf("\n=== RLC FIRMWARE CONCLUSION ===\n");
    printf("If all types return UNKNOWN_CMD/ITEM_NOT_FOUND, PSP ring on BC-250\n");
    printf("does NOT support IP firmware loading via this path.\n");
}

/* ==================== */
/* TEST 4: SMU DPM scan */
/* ==================== */
static void test_smu_dpm(HANDLE h) {
    printf("\n=== TEST 4: SMU DPM tables ===\n");

    AMDBC250_IOCTL_SMU_CPU_MSG sm;
    if (!smu_cpu_msg(h, 0, 0x02, 0, &sm)) return;
    printf("SMU version: 0x%08X (build 0x%02X)\n", sm.Response, (sm.Response>>16)&0xFF);

    if (!smu_cpu_msg(h, 0, 0x3D, 0, &sm)) return;
    printf("Enabled features: 0x%08X\n", sm.Response);

    printf("\nScanning SMN 0x03B10000-0x03B10FFF for DPM tables...\n");
    for (uint32_t addr = 0x03B10000; addr < 0x03B11000; addr += 4) {
        uint32_t val = 0;
        if (pci_smn_read(h, addr, &val)) {
            if (val != 0 && val != 0xFFFFFFFF) {
                printf("  SMN[0x%08X] = 0x%08X\n", addr, val);
            }
        }
    }

    printf("\nTrying DPM table transfer messages...\n");
    struct {
        uint32_t msg;
        const char *name;
    } dpm_msgs[] = {
        {0x04, "SetDriverTableDramAddrHigh"},
        {0x05, "SetDriverTableDramAddrLow"},
        {0x06, "TransferTableSmu2Dram"},
        {0x07, "TransferTableDram2Smu"},
    };

    for (int i = 0; i < sizeof(dpm_msgs)/sizeof(dpm_msgs[0]); i++) {
        if (smu_cpu_msg(h, 0, dpm_msgs[i].msg, 0, &sm)) {
            printf("  %s: RespStatus=0x%02X (%s)\n",
                   dpm_msgs[i].name, sm.ResponseStatus, resp_name(sm.ResponseStatus));
        }
    }

    printf("\n=== DPM CONCLUSION ===\n");
    printf("If TransferTable* returns UNKNOWN_CMD, DPM tables are NOT supported\n");
    printf("on this SMU firmware (expected for cyan_skillfish).\n");
}

/* ==================== */
/* TEST 5: SMU Q3 0x98 */
/* ==================== */
static void test_smu_q3_98(HANDLE h) {
    printf("\n=== TEST 5: SMU Q3 0x98 ungated SMN write ===\n");
    printf("This writes 0x00FF to ANY SMN address passed as arg.\n");
    printf("Only safe address to test: 0x0115A870 (CPU core mask).\n\n");

    uint32_t core_mask_before = 0;
    if (pci_smn_read(h, 0x0115A870, &core_mask_before)) {
        printf("CPU core mask before: 0x%08X\n", core_mask_before);
    }

    AMDBC250_IOCTL_SMU_CPU_MSG sm;
    if (smu_cpu_msg(h, 3, 0x98, 0x0115A870, &sm)) {
        printf("Q3 0x98 response: 0x%02X (%s)\n", sm.ResponseStatus, resp_name(sm.ResponseStatus));
    }

    uint32_t core_mask_after = 0;
    if (pci_smn_read(h, 0x0115A870, &core_mask_after)) {
        printf("CPU core mask after:  0x%08X\n", core_mask_after);
        if (core_mask_after != core_mask_before) {
            printf("*** CORE MASK CHANGED! SMU 0x98 WORKS for ungated writes! ***\n");
        } else {
            printf("Core mask unchanged (already 0xFF or write rejected)\n");
        }
    }
}

/* ==================== */
/* MAIN */
/* ==================== */
int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== BC-250 Fresh Verification Suite ===\n");
    printf("Driver: atikmdag.sys (post-audit build)\n\n");

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
    if (!DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &br, NULL)) {
        printf("FAIL: INIT_HARDWARE gle=%lu\n", GetLastError());
        CloseHandle(h);
        return 1;
    }
    printf("Init OK (FB=0x%llX)\n\n", ih.FbPhysicalBase);

    if (argc <= 1 || strcmp(argv[1], "all") == 0) {
        test_spi_pg(h);
        test_kiq_size(h);
        test_rlc_firmware(h);
        test_smu_dpm(h);
        test_smu_q3_98(h);
    } else if (strcmp(argv[1], "spi") == 0) {
        test_spi_pg(h);
    } else if (strcmp(argv[1], "kiq") == 0) {
        test_kiq_size(h);
    } else if (strcmp(argv[1], "rlc") == 0) {
        test_rlc_firmware(h);
    } else if (strcmp(argv[1], "dpm") == 0) {
        test_smu_dpm(h);
    } else if (strcmp(argv[1], "q3-98") == 0) {
        test_smu_q3_98(h);
    } else {
        printf("Usage: %s [all|spi|kiq|rlc|dpm|q3-98]\n", argv[0]);
    }

    CloseHandle(h);
    return 0;
}