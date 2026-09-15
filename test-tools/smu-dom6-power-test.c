/* smu-dom6-power-test.c - test Q3 0x3C feature-bitmask for VCN/dom6 power.
 * Uses IOCTL_AMDBC250_SMU_CPU_MSG (0x80000C2C) for Q3 0x3C writes and
 * IOCTL_AMDBC250_PCI_SMN_ACCESS (0x80000C38) for dom6 status reads via DF.
 *
 * Usage:
 *   smu-dom6-power-test.exe                 - baseline dom6 readback
 *   smu-dom6-power-test.exe <mask_hex>      - Q3 0x3C write + dom6 readback
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

static const char *resp_name(uint32_t s) {
    switch (s) {
    case 1:         return "OK";
    case 0xFF:      return "FAIL";
    case 0xFE:      return "UNKNOWN_CMD";
    case 0xFD:      return "REJECTED";
    case 0xFC:      return "BUSY";
    case 0:         return "TIMEOUT";
    default:        return "?";
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

static void dump_dom6(HANDLE h) {
    uint32_t dom6_addrs[] = {0x0006D190, 0x0006D0F8, 0x0006D17C, 0x0006D184};
    const char *dom6_names[] = {"STATUS", "CTRL", "CMD", "RAIL"};
    printf("--- dom6 readback ---\n");
    for (int i = 0; i < 4; i++) {
        uint32_t v = 0;
        if (pci_smn_read(h, dom6_addrs[i], &v))
            printf("  %s 0x%06X = 0x%08X\n", dom6_names[i], dom6_addrs[i], v);
    }
    printf("\n");
}

static void decode_features(uint32_t cur, uint32_t old) {
    uint32_t delta = cur ^ old;
    printf("--- Feature delta: old=0x%08X cur=0x%08X delta=0x%08X ---\n", old, cur, delta);
    for (int b = 0; b < 32; b++) {
        if (delta & (1u << b)) {
            const char *name = "unknown";
            switch (b) {
                case 0:  name = "GFXCLK_DPM"; break;
                case 1:  name = "? (delta bit1)"; break;
                case 2:  name = "GFXOFF"; break;
                case 3:  name = "CG"; break;
                case 4:  name = "PG"; break;
                case 5:  name = "? (delta bit5)"; break;
                case 6:  name = "? (delta bit6)"; break;
                case 7:  name = "? (delta bit7)"; break;
                case 8:  name = "? (delta bit8)"; break;
                case 12: name = "? (delta bit12)"; break;
                case 16: name = "? (delta bit16)"; break;
            }
            printf("  bit %2d (%s): %s -> %s\n", b, name,
                   (old & (1u<<b)) ? "ON" : "OFF",
                   (cur & (1u<<b)) ? "ON" : "OFF");
        }
    }
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== BC-250 dom6 power test (Q3 0x3C feature-bitmask) ===\n\n");

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

    dump_dom6(h);

    AMDBC250_IOCTL_SMU_CPU_MSG sm;
    uint32_t old_features = 0xDD602C7D;

    if (argc >= 2) {
        uint32_t mask = (uint32_t)strtoul(argv[1], NULL, 0);
        printf("--- Q3 0x3C enable_features mask=0x%08X ---\n", mask);
        if (smu_cpu_msg(h, 3, AMDBC250_SMU_Q3_ENABLE_FEATURES, mask, &sm)) {
            printf("  Response      : 0x%08X\n", sm.Response);
            printf("  ResponseStatus: 0x%02X (%s)\n", sm.ResponseStatus, resp_name(sm.ResponseStatus));
            printf("  Result        : %u\n", sm.Result);
        }
        dump_dom6(h);
    }

    printf("--- Combined mask sweep (bits 1/7/8/16 combos) ---\n");
    uint32_t combo_masks[] = {
        0x00000102,
        0x00000182,
        0x00000102,
        0x00010102,
        0x00010182,
        0x01000102,
        0x01000182,
        0x01010182,
    };
    for (size_t i = 0; i < sizeof(combo_masks)/sizeof(combo_masks[0]); i++) {
        uint32_t mask = combo_masks[i];
        if (smu_cpu_msg(h, 3, AMDBC250_SMU_Q3_ENABLE_FEATURES, mask, &sm)) {
            printf("  mask=0x%08X -> st=%s res=%u resp=0x%08X\n",
                   mask, resp_name(sm.ResponseStatus), sm.Result, sm.Response);
        }
    }
    dump_dom6(h);

    printf("--- Focused test: mask=0x182 (bits 1+7+8) ---\n");
    if (smu_cpu_msg(h, 3, AMDBC250_SMU_Q3_ENABLE_FEATURES, 0x182, &sm)) {
        printf("  mask=0x182 -> st=%s res=%u resp=0x%08X\n",
               resp_name(sm.ResponseStatus), sm.Result, sm.Response);
    }
    dump_dom6(h);

    printf("--- Read current SMU features (Q0 0x3D) ---\n");
    uint32_t cur_features = 0;
    if (smu_cpu_msg(h, 0, AMDBC250_SMU_Q0_GET_ENABLED_FEATURES, 0, &sm)) {
        cur_features = sm.Response;
        printf("  Q0 0x3D enabled features: 0x%08X\n", cur_features);
    }

    decode_features(cur_features, old_features);

    printf("--- VCN MMIO probe via DF Q3 0x2A mem64 ---\n");
    uint32_t vcn_slice = 0;
    if (pci_smn_read(h, 0x02403000, &vcn_slice)) {
        printf("  VCN_SLICE 0x02403000 = 0x%08X (%s)\n", vcn_slice,
               vcn_slice == 0xFFFFFFFF ? "DEAD/fabric closed" :
               vcn_slice == 0x00000000 ? "zeroed" : "LIVE");
    }

    CloseHandle(h);
    return 0;
}
