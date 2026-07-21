#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE g_hDev = INVALID_HANDLE_VALUE;

static BOOL WriteReg(uint32_t offset, uint32_t value) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD returned = 0;
    r.RegisterOffset = offset; r.Value = value;
    return DeviceIoControl(g_hDev, IOCTL_AMDBC250_WRITE_REG, &r, sizeof(r), &r, sizeof(r), &returned, NULL);
}
static uint32_t ReadReg(uint32_t offset) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD returned = 0;
    r.RegisterOffset = offset; r.Value = 0;
    if (DeviceIoControl(g_hDev, IOCTL_AMDBC250_READ_REG, &r, sizeof(r), &r, sizeof(r), &returned, NULL)) return r.Value;
    return 0xFFFFFFFF;
}
static uint32_t SmnRead(uint32_t smnAddr) {
    WriteReg(0x38, smnAddr); ReadReg(0x38); return ReadReg(0x3C);
}

/* Candidate registers (verified live on BC-250) */
static const struct { uint32_t off; const char* name; int writable; } REGS[] = {
    { 0x0000, "GRBM_STATUS", 0 },
    { 0x2000, "GRBM_STATUS(alt)", 0 },
    { 0x34D0, "GRBM_GFX_INDEX", 1 },
    { 0x3260, "GRBM_GFX_CNTL", 0 },
    { 0x9C1C, "CC_GC_SHADER_ARRAY_CONFIG", 1 },
    { 0x5C3C, "SPI_PG_ENABLE_STATIC_WGP_MASK", 1 },
    { 0x3D64, "RLC_PG_ALWAYS_ON_WGP_MASK", 1 },
    { 0x32D4, "SCRATCH", 1 },
    { 0xDA60, "CP_RB0_BASE_LO", 0 },
    { 0xDA68, "CP_RB0_CNTL", 1 },
    { 0xDA6C, "CP_RB0_RPTR", 1 },
    { 0xDA78, "CP_RB0_WPTR", 1 },
    { 0x9104, "CP_MQD_BASE_ADDR", 1 },
    { 0x910C, "CP_HQD_ACTIVE", 1 },
    { 0xE018, "SDMA0_CNTL", 0 },
    { 0x80E0, "DISPATCH_INITIATOR", 1 },
    { 0x80E4, "DISPATCH_DIM_X", 1 },
    { 0x8110, "COMPUTE_PGM_LO", 1 },
    { 0x8114, "COMPUTE_PGM_HI", 1 },
    { 0x6300, "DCN_OTG(pipe3)", 0 },
};
#define NREGS (sizeof(REGS)/sizeof(REGS[0]))

int main(void) {
    g_hDev = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (g_hDev == INVALID_HANDLE_VALUE) {
        printf("FAIL: cannot open GPU device (err=%lu)\n", GetLastError());
        return 1;
    }

    printf("=== INIT_HARDWARE (BAR5 + NBIO_MAP) ===\n");
    {
        AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br = 0;
        memset(&ih, 0, sizeof(ih));
        ih.MmioPhysicalBase = 0xFE800000ULL;
        ih.MmioSize = 0x80000;
        ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
        if (!DeviceIoControl(g_hDev, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), NULL, 0, &br, NULL)) {
            printf("FAIL: INIT_HARDWARE (err=%lu)\n", GetLastError());
            return 1;
        }
        printf("OK (BAR5 mapped)\n");
    }

    printf("\n=== Full Register Scan (read + safe write/readback) ===\n");
    int live = 0, dead = 0, locked = 0;
    for (int i = 0; i < NREGS; i++) {
        uint32_t before = ReadReg(REGS[i].off);
        if (before == 0xFFFFFFFF) {
            printf("  [DEAD]    %-32s @0x%04X = 0xFFFFFFFF\n", REGS[i].name, REGS[i].off);
            dead++;
            continue;
        }
        if (!REGS[i].writable) {
            printf("  [RO]      %-32s @0x%04X = 0x%08X\n", REGS[i].name, REGS[i].off, before);
            live++;
            continue;
        }
        /* safe write: save, write ~0, readback, restore */
        uint32_t saved = before;
        WriteReg(REGS[i].off, 0x00000000);
        uint32_t zero = ReadReg(REGS[i].off);
        WriteReg(REGS[i].off, 0xFFFFFFFF);
        uint32_t ones = ReadReg(REGS[i].off);
        WriteReg(REGS[i].off, saved);
        uint32_t restored = ReadReg(REGS[i].off);
        int ok = (restored == saved);
        printf("  [%s] %-32s @0x%04X before=0x%08X zero=0x%08X ones=0x%08X %s\n",
               ok ? "WR " : "LCK", REGS[i].name, REGS[i].off, before, zero, ones,
               ok ? "" : "[LOCKED]");
        if (ok) live++; else locked++;
    }

    printf("\n=== SMU Mailbox (via BAR5+0x38/0x3C) ===\n");
    uint32_t fwFlags = SmnRead(0x03B10024);
    uint32_t smuVer = SmnRead(0x03B10A48); /* C2PMSG_82 after GetSmuVersion */
    printf("  SMN[0x03B10024] FW_FLAGS = 0x%08X\n", fwFlags);
    printf("  SMN[0x03B10A48] (raw)     = 0x%08X\n", smuVer);

    printf("\n=== SUMMARY ===\n");
    printf("  Live/RO: %d  Dead: %d  Locked: %d\n", live, dead, locked);
    printf("  BAR5 mapped: YES (INIT_HARDWARE OK)\n");
    printf("  SMU mailbox: %s\n", (fwFlags != 0xFFFFFFFF) ? "RESPONSIVE" : "NO RESPONSE");
    return 0;
}
