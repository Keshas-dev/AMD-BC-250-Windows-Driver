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
static BOOL SmnWrite(uint32_t smnAddr, uint32_t value) {
    if (!WriteReg(0x38, smnAddr)) return FALSE; return WriteReg(0x3C, value);
}

#define C2PMSG_66 0x03B10A08
#define C2PMSG_82 0x03B10A48
#define C2PMSG_90 0x03B10A68

static int WaitC2p90(uint32_t exp, int ms) {
    for (int i = 0; i < ms; i++) { if (SmnRead(C2PMSG_90) == exp) return 1; Sleep(1); }
    return 0;
}

static uint32_t SmuQuery(uint16_t msg) {
    uint32_t c90 = SmnRead(C2PMSG_90);
    if (c90 == 1) SmnWrite(C2PMSG_90, 0);
    SmnWrite(C2PMSG_66, msg);
    if (!WaitC2p90(1, 500)) return 0xFFFFFFFF;
    return SmnRead(C2PMSG_82);
}
static uint32_t SmuQueryParam(uint16_t msg, uint32_t param) {
    uint32_t c90 = SmnRead(C2PMSG_90);
    if (c90 == 1) SmnWrite(C2PMSG_90, 0);
    SmnWrite(C2PMSG_82, param);
    SmnWrite(C2PMSG_66, msg);
    if (!WaitC2p90(1, 500)) return 0xFFFFFFFF;
    return SmnRead(C2PMSG_82);
}

int main() {
    g_hDev = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (g_hDev == INVALID_HANDLE_VALUE) { printf("FAIL\n"); return 1; }
    printf("OK\n\n");

    printf("=== GPU Basic ===\n");
    printf("GRBM_STATUS  (0x2000): 0x%08X\n", ReadReg(0x2000));
    printf("SCRATCH      (0x32D4): 0x%08X\n", ReadReg(0x32D4));

    printf("\n=== SMU Queries ===\n");
    uint32_t v;
    printf("FW_FLAGS:           0x%08X\n", SmnRead(0x03b10024));
    printf("PUB_CTRL:           0x%08X\n", SmnRead(0x03B10B14));
    v = SmuQuery(0x1);  printf("TestMessage:         0x%08X\n", v);
    v = SmuQuery(0x2);  printf("GetSmuVersion:       0x%08X (%u.%u.%u)\n", v, (v>>16)&0xFF, (v>>8)&0xFF, v&0xFF);
    v = SmuQuery(0x3);  printf("GetDriverIfVersion:  0x%08X (%u)\n", v, v);
    v = SmuQuery(0x3D); printf("Features:            0x%08X (GFXCLK=%s GFXOFF=%s)\n", v,
        (v&1)?"ON":"OFF", (v&4)?"ON":"OFF");
    v = SmuQuery(0x37); printf("GfxFreq:             %u MHz\n", v/100);
    v = SmuQuery(0x0F); printf("GfxClk:              %u MHz\n", v/100);
    v = SmuQuery(0x38); printf("GfxVid:              0x%08X\n", v);
    v = SmuQuery(0x1E); printf("ActiveWgp:           %u\n", v);
    v = SmuQuery(0x0C); printf("CorePstate:          0x%08X\n", v);
    v = SmuQuery(0x13); printf("DfPstate:            0x%08X\n", v);

    // Try UnForceGfxFreq (0x3A) in case a stale force is stuck from crash
    printf("\n=== UnForce (safe cleanup) ===\n");
    SmuQuery(0x3A);
    v = SmuQuery(0x37); printf("GfxFreq after UnForce: %u MHz\n", v/100);

    CloseHandle(g_hDev);
    printf("\nDONE\n");
    return 0;
}
