/* smu-init-test.c — extended from working bar5-smn-test */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE hGpu;

static BOOL WriteReg(uint32_t offset, uint32_t value) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD returned = 0;
    r.RegisterOffset = offset; r.Value = value;
    return DeviceIoControl(hGpu, IOCTL_AMDBC250_WRITE_REG, &r, sizeof(r), &r, sizeof(r), &returned, NULL);
}
static uint32_t ReadReg(uint32_t offset) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD returned = 0;
    r.RegisterOffset = offset; r.Value = 0;
    if (DeviceIoControl(hGpu, IOCTL_AMDBC250_READ_REG, &r, sizeof(r), &r, sizeof(r), &returned, NULL)) return r.Value;
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
#define SmuSend SmuQueryParam

int main() {
    hGpu = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hGpu == INVALID_HANDLE_VALUE) { printf("FAIL: CreateFile gle=%lu\n", GetLastError()); return 1; }
    printf("CreateFile OK\n");

    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD ret = 0;
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL;
    ih.MmioSize = 0x80000;
    ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
    BOOL ok = DeviceIoControl(hGpu, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &ret, NULL);
    printf("INIT_HW(0xFE800000/0x80000): ok=%d gle=%lu\n", ok, GetLastError());

    printf("SCRATCH=0x%08X  GRBM_STATUS=0x%08X\n\n", ReadReg(0x32D4), ReadReg(0x3260));

    printf("=== Pre-init SMU state ===\n");
    uint32_t v;
    printf("  Features:  0x%08X\n", SmuQuery(0x3D));
    printf("  GfxFreq:   %u MHz\n", SmuQuery(0x37));
    printf("  GfxVid:    0x%08X\n", SmuQuery(0x38));
    printf("  ActiveWgp: %u\n",   SmuQuery(0x1E));

    printf("\n=== Step 1: SetDriverDramAddr (PPSMC 0x04/0x05) ===\n");
    printf("  *** WARNING: These require DRAM table setup, will likely fail ***\n");
    uint64_t pa = 0xC0000000ULL;
    printf("  Using VRAM base PA=0x%016llX\n", pa);
    v = SmuSend(0x4, (uint32_t)(pa>>32));
    printf("  SetDriverDramAddrHigh: 0x%08X %s\n", v, v==1?"OK":"FAIL");
    v = SmuSend(0x5, (uint32_t)(pa&0xFFFFFFFF));
    printf("  SetDriverDramAddrLow:  0x%08X %s\n", v, v==1?"OK":"FAIL");

    printf("\n=== Step 2: TransferTableDram2Smu (PPSMC 0x07) ===\n");
    printf("  *** WARNING: Known to hang SMN bus! ***\n");
    v = SmuSend(0x7, 0);
    printf("  TransferTableDram2Smu: 0x%08X %s\n", v, v==1?"OK":"FAIL");

    printf("\n=== Step 3: RequestGfxclk (PPSMC 0x0E) ===\n");
    printf("  *** WARNING: RequestGfxclk(0x0E) can CRASH the SMU! ***\n");
    printf("  *** Use ForceGfxFreq(0x39) with governor sequence instead ***\n");
    uint32_t freqs[] = {50000, 100000, 150000, 200000};
    const char* labels[] = {"500 MHz", "1000 MHz", "1500 MHz", "2000 MHz"};
    for (int i = 0; i < 4; i++) {
        v = SmuSend(0x0E, freqs[i]);
        printf("  RequestGfxclk(%s): 0x%08X %s\n", labels[i], v,
            v==1?"OK":v==0xFFFFFFFF?"TIMEOUT":"ACK");
        Sleep(100);
    }

    Sleep(200);

    printf("\n=== Post-init SMU state ===\n");
    uint32_t feat = SmuQuery(0x3D);
    v = SmuQuery(0x37);
    printf("  Features:  0x%08X (GFXCLK=%s GFXOFF=%s)\n", feat,
        (feat&1)?"ON":"OFF", (feat&4)?"ON":"OFF");
    printf("  GfxFreq:   %u MHz\n", v);
    printf("  GfxVid:    0x%08X\n", SmuQuery(0x38));
    printf("  ActiveWgp: %u\n",   SmuQuery(0x1E));

    if (v >= 500) printf("\n  *** GFX FREQUENCY INCREASED! DPM WORKING! ***\n");
    else if (v > 15) printf("\n  ** Partial wake: freq=%u MHz\n", v);
    else printf("\n  GFX still at 15 MHz idle\n");

    CloseHandle(hGpu);
    return 0;
}
