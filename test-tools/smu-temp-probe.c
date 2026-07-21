#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE h;
static BOOL W32(uint32_t o, uint32_t v) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=v; return DeviceIoControl(h,IOCTL_AMDBC250_WRITE_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL); }
static uint32_t R32(uint32_t o) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=0; if(DeviceIoControl(h,IOCTL_AMDBC250_READ_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL)) return r.Value; return 0xFFFFFFFF; }
static uint32_t SR(uint32_t a) { W32(0x38,a); R32(0x38); return R32(0x3C); }

int main() {
    h = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { printf("FAIL: Open device\n"); return 1; }
    AMDBC250_IOCTL_INIT_HARDWARE ih = {0}; ih.MmioPhysicalBase = 0xFE800000ULL; ih.MmioSize = 0x80000; ih.Flags = 1; DWORD b;
    if (!DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &b, NULL)) {
        printf("FAIL: INIT_HARDWARE err=%lu\n", GetLastError()); CloseHandle(h); return 1;
    }
    printf("INIT_HARDWARE OK\n\n");

    // SMU mailbox Q0 status after boot
    printf("=== SMU C2PMSG registers ===\n");
    printf("  Q0_CMD(0x03B10A08): 0x%08X\n", SR(0x03B10A08));
    printf("  Q0_ARG(0x03B10A48): 0x%08X\n", SR(0x03B10A48));
    printf("  Q0_RSP(0x03B10A68): 0x%08X\n", SR(0x03B10A68));
    printf("  Q1_CMD(0x03B10A00): 0x%08X\n", SR(0x03B10A00));
    printf("  Q1_RSP(0x03B10A60): 0x%08X\n", SR(0x03B10A60));
    printf("  Q3_CMD(0x03B10A20): 0x%08X\n", SR(0x03B10A20));
    printf("  Q3_RSP(0x03B10A80): 0x%08X\n", SR(0x03B10A80));
    printf("  Q4_CMD(0x03B10A24): 0x%08X\n", SR(0x03B10A24));
    printf("  Q4_RSP(0x03B10A84): 0x%08X\n", SR(0x03B10A84));

    // SMU firmware status registers (Linux psp_v11_0 SMU interface)
    printf("\n=== SMU firmware status (0x03B10000 range) ===\n");
    uint32_t fwRegs[] = {0x03B10000,0x03B10004,0x03B10008,0x03B1000C,0x03B10010,0x03B10014,0x03B10018,0x03B1001C,0x03B10020,0x03B10024,0x03B10028,0x03B1002C,0x03B10030,0x03B10034,0x03B10038,0x03B1003C};
    uint32_t fwRegs2[] = {0x03B10080,0x03B10084,0x03B10088,0x03B1008C,0x03B10090,0x03B10094,0x03B10098,0x03B1009C};
    for (int i = 0; i < sizeof(fwRegs)/sizeof(fwRegs[0]); i++) {
        uint32_t v = SR(fwRegs[i]);
        if (v != 0 && v != 0xFFFFFFFF) printf("  0x%08X: 0x%08X\n", fwRegs[i], v);
    }
    for (int i = 0; i < sizeof(fwRegs2)/sizeof(fwRegs2[0]); i++) {
        uint32_t v = SR(fwRegs2[i]);
        if (v != 0 && v != 0xFFFFFFFF) printf("  0x%08X: 0x%08X\n", fwRegs2[i], v);
    }

    // SMU feature registers (bc250-collective Queue 2 interface)
    printf("\n=== SMU Queue 2 interface (0x03B10500-0x03B10A00) ===\n");
    uint32_t q2Addrs[] = {0x03B10500,0x03B10504,0x03B10508,0x03B1050C,0x03B10510,0x03B10514,0x03B10518,0x03B1051C,0x03B10520,0x03B10524,0x03B10528,0x03B1052C,0x03B10530,0x03B10534,0x03B10538,0x03B1053C,0x03B10540,0x03B10544,0x03B10548,0x03B1054C,0x03B10550,0x03B10554,0x03B10558,0x03B1055C,0x03B10560,0x03B10564,0x03B10568,0x03B1056C,0x03B10570,0x03B10574,0x03B10578,0x03B1057C};
    for (int i = 0; i < sizeof(q2Addrs)/sizeof(q2Addrs[0]); i++) {
        uint32_t v = SR(q2Addrs[i]);
        if (v != 0 && v != 0xFFFFFFFF) printf("  0x%08X: 0x%08X\n", q2Addrs[i], v);
    }

    // THM temperature sensor registers (Linux IP map: THM = 0x03B16600)
    printf("\n=== THM temperature registers (0x03B16600+) ===\n");
    for (uint32_t off = 0; off <= 0x100; off += 4) {
        uint32_t v = SR(0x03B16600 + off);
        if (v != 0 && v != 0xFFFFFFFF) {
            printf("  0x%08X: 0x%08X", 0x03B16600 + off, v);
            if (v < 0x10000) printf(" (raw=%u)", v);
            printf("\n");
        }
    }

    // SMUIO registers (Linux IP map: SMUIO = 0x03B16800, 0x03B16A00)
    printf("\n=== SMUIO range (0x03B16800, 0x03B16A00) ===\n");
    for (uint32_t off = 0; off <= 0x80; off += 4) {
        uint32_t v = SR(0x03B16800 + off);
        if (v != 0 && v != 0xFFFFFFFF) printf("  0x%08X: 0x%08X\n", 0x03B16800 + off, v);
        v = SR(0x03B16A00 + off);
        if (v != 0 && v != 0xFFFFFFFF) printf("  0x%08X: 0x%08X\n", 0x03B16A00 + off, v);
    }

    // MP0 PSP mailbox SMN area (where C2PMSG lives)
    printf("\n=== MP0 PSP C2PMSG via SMN (0x03B10A00-0x03B10B00) ===\n");
    for (uint32_t addr = 0x03B10A00; addr <= 0x03B10B00; addr += 4) {
        uint32_t v = SR(addr);
        if (v != 0 && v != 0xFFFFFFFF) {
            uint32_t regNum = (addr - 0x03B10A00) / 4;
            printf("  SMN 0x%08X [C2PMSG_%u?]: 0x%08X\n", addr, regNum, v);
        }
    }

    CloseHandle(h);
    return 0;
}
