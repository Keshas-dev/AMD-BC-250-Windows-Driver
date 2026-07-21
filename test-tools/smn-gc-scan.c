#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE hDev = INVALID_HANDLE_VALUE;

static BOOL WriteReg(uint32_t offset, uint32_t value) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD returned = 0;
    r.RegisterOffset = offset; r.Value = value;
    return DeviceIoControl(hDev, IOCTL_AMDBC250_WRITE_REG, &r, sizeof(r), &r, sizeof(r), &returned, NULL);
}
static uint32_t ReadReg(uint32_t offset) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD returned = 0;
    r.RegisterOffset = offset; r.Value = 0;
    if (DeviceIoControl(hDev, IOCTL_AMDBC250_READ_REG, &r, sizeof(r), &r, sizeof(r), &returned, NULL)) return r.Value;
    return 0xFFFFFFFF;
}
static uint32_t SmnRead(uint32_t smnAddr) {
    WriteReg(0x38, smnAddr); ReadReg(0x38); return ReadReg(0x3C);
}
static BOOL SmnWrite(uint32_t smnAddr, uint32_t value) {
    if (!WriteReg(0x38, smnAddr)) return FALSE; return WriteReg(0x3C, value);
}

int main() {
    hDev = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hDev == INVALID_HANDLE_VALUE) { printf("FAIL: CreateFile gle=%lu\n", GetLastError()); return 1; }
    printf("OK\n");

    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD ret = 0;
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL;
    ih.MmioSize = 0x80000;
    ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
    BOOL ok = DeviceIoControl(hDev, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &ret, NULL);
    printf("INIT_HW ok=%d gle=%lu\n", ok, GetLastError());

    printf("\n=== Step 1: Read GRBM_STATUS via BAR5 ===\n");
    uint32_t grbmStatus = ReadReg(0x3260);
    printf("GRBM_STATUS (BAR5 0x3260) = 0x%08X\n", grbmStatus);

    printf("\n=== Step 2: Probe SMN for GC registers ===\n");
    uint32_t gcSmnBases[] = {
        0x000A0000, 0x03800000, 0x00000000, 0x00120000,
        0x00100000, 0x00200000, 0x00A00000, 0x30000000,
        0x000A3260, 0x03803260, 0x00003260, 0x03803260
    };
    for (int i = 0; i < sizeof(gcSmnBases)/sizeof(gcSmnBases[0]); i++) {
        uint32_t v = SmnRead(gcSmnBases[i]);
        printf("  SMN[0x%08X] = 0x%08X", gcSmnBases[i], v);
        if (v == grbmStatus) printf("  <--- MATCHES GRBM_STATUS!");
        printf("\n");
    }

    printf("\n=== Step 3: Scan SMN range for GRBM_STATUS match ===\n");
    printf("Scanning SMN 0x000A0000-0x000B0000 step 0x100...\n");
    for (uint32_t smn = 0x000A0000; smn < 0x000B0000; smn += 0x100) {
        uint32_t v = SmnRead(smn);
        if (v == grbmStatus) {
            printf("  MATCH at SMN[0x%08X] = 0x%08X (GRBM_STATUS!)\n", smn, v);
        }
    }

    printf("\n=== Step 4: Check known GC registers via SMN ===\n");
    uint32_t gcRegs[] = {0x3260, 0x3264, 0x3278, 0x32D4, 0x34D0, 0x34FC, 0x5C3C, 0x9C1C, 0x3D64};
    const char *gcNames[] = {"GRBM_STATUS", "CC_CONFIG", "GRBM_SOFT_RST", "SCRATCH0", "GRBM_GFX_IDX", "SPI_MASK", "SPI_PG_ENABLE", "CC_ARRAY", "RLC_PG"};
    for (uint32_t trial = 0; trial <= 1; trial++) {
        uint32_t base = trial == 0 ? 0x000A0000 : 0x03800000;
        printf("\n  --- GC SMN base 0x%08X ---\n", base);
        for (int i = 0; i < sizeof(gcRegs)/sizeof(gcRegs[0]); i++) {
            uint32_t smn = base + gcRegs[i];
            uint32_t bar5 = ReadReg(gcRegs[i]);
            uint32_t smnv = SmnRead(smn);
            printf("    %s: BAR5[0x%04X]=0x%08X  SMN[0x%08X]=0x%08X%s\n",
                gcNames[i], gcRegs[i], bar5, smn, smnv,
                bar5 == smnv ? " (MATCH!)" : "");
        }
    }

    printf("\n=== Step 5: Verify SMN base 0 mapping ===\n");
    uint32_t checkRegs[] = {0x0000, 0x3260, 0x32D4, 0x34D0, 0x9C1C, 0x3D64, 0x5C3C, 0x34FC};
    for (int i = 0; i < sizeof(checkRegs)/sizeof(checkRegs[0]); i++) {
        uint32_t bar5v = ReadReg(checkRegs[i]);
        uint32_t smnv = SmnRead(checkRegs[i]);
        printf("  [0x%04X]: BAR5=0x%08X  SMN[0x%08X]=0x%08X%s\n",
            checkRegs[i], bar5v, checkRegs[i], smnv,
            bar5v == smnv ? " MATCH!" : "");
    }

    printf("\n=== Step 6: Try SPI_PG write via SMN ===\n");
    printf("  SPI_PG (0x5C3C) BAR5 before: 0x%08X\n", ReadReg(0x5C3C));
    printf("  SPI_PG (0x5C3C) SMN  before: 0x%08X\n", SmnRead(0x5C3C));
    printf("  SMN write 0x1F to 0x5C3C...\n");
    SmnWrite(0x5C3C, 0x1F);
    uint32_t smnAfter = SmnRead(0x5C3C);
    uint32_t bar5After = ReadReg(0x5C3C);
    printf("  SPI_PG SMN  after: 0x%08X\n", smnAfter);
    printf("  SPI_PG BAR5 after: 0x%08X\n", bar5After);
    if (smnAfter == 0x1F || bar5After == 0x1F) printf("  *** SMN SPI WRITE WORKS! ***\n");
    else printf("  SMN SPI write also locked\n");

    CloseHandle(hDev);
    printf("\nDONE\n");
    return 0;
}
