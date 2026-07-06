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

/* SMU v11.8 PPSMC header (Linux kernel, smu_v11_8_ppsmc.h):
   PPSMC_Message_Count = 0x3E — valid messages are 0x01 through 0x3D only.
   0x3E/0x3F are NOT valid commands (return 0xFFFFFFFF).
   There is NO DisableSmuFeatures/EnableSmuFeatures in v11.8.
   GFXOFF control is baked into firmware, not externally controllable. */

int main() {
    g_hDev = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (g_hDev == INVALID_HANDLE_VALUE) { printf("FAIL: CreateFile gle=%lu\n", GetLastError()); return 1; }
    printf("CreateFile OK\n");

    /* Step 1: INIT_HARDWARE to map BAR5 (required on Win11 26100 WDM fallback) */
    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD ret = 0;
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL;
    ih.MmioSize = 0x80000;
    ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
    BOOL ok = DeviceIoControl(g_hDev, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &ret, NULL);
    printf("INIT_HW(0xFE800000/0x80000): ok=%d gle=%lu\n", ok, GetLastError());

    /* Step 2: Read GPU basic registers */
    printf("\n=== GPU Basic ===\n");
    printf("SCRATCH_REG0 (0x32D4): 0x%08X\n", ReadReg(0x32D4));
    printf("GRBM_STATUS  (0x3260): 0x%08X\n", ReadReg(0x3260));
    printf("GRBM_STATUS2 (0x326C): 0x%08X\n", ReadReg(0x326C));
    printf("GRBM_SOFT_RST(0x3278): 0x%08X\n", ReadReg(0x3278));
    printf("GPU_ID       (0x0000): 0x%08X\n", ReadReg(0x0000));

    /* Step 3: SMU queries */
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

    /* Step 4: Try safe clock changes */
    printf("\n=== Safe Clock Changes ===\n");

    /* SetSoftMinCclk (0x35) — set minimum GFX clock */
    printf("SetSoftMinCclk(0x35, 20000 = 200 MHz)...\n");
    v = SmuQueryParam(0x35, 20000);
    printf("  Response: 0x%08X\n", v);
    Sleep(100);
    v = SmuQuery(0x37); printf("  GfxFreq after: %u MHz\n", v/100);
    v = SmuQuery(0x1E); printf("  ActiveWgp: %u\n", v);

    /* SetSoftMaxCclk (0x36) — set maximum GFX clock */
    printf("\nSetSoftMaxCclk(0x36, 40000 = 400 MHz)...\n");
    v = SmuQueryParam(0x36, 40000);
    printf("  Response: 0x%08X\n", v);
    Sleep(500);
    v = SmuQuery(0x37); printf("  GfxFreq after: %u MHz\n", v/100);
    v = SmuQuery(0x1E); printf("  ActiveWgp: %u\n", v);

    /* Try SetSoftMinDeepSleepGfxclkFreq (0x19) with 0 = disable deep sleep */
    printf("\n=== Deep Sleep Disable ===\n");
    printf("SetSoftMinDeepSleepGfxclkFreq(0x19, 0)...\n");
    v = SmuQueryParam(0x19, 0);
    printf("  Response: 0x%08X\n", v);
    Sleep(500);
    v = SmuQuery(0x37); printf("  GfxFreq: %u MHz\n", v/100);
    v = SmuQuery(0x1E); printf("  ActiveWgp: %u\n", v);

    /* NOTE: SMU v11.8 has NO DisableSmuFeatures/EnableSmuFeatures messages.
       PPSMC_Message_Count = 0x3E, so valid messages are 0x01-0x3D only.
       Messages 0x3E/0x3F are INVALID and return 0xFFFFFFFF. */

    /* SetMinDeepSleepGfxclkFreq (0x19) with a moderate value to reduce idle depth */
    printf("\nSetMinDeepSleepGfxclkFreq(0x19, 5000 = 50 MHz min)...\n");
    v = SmuQueryParam(0x19, 5000);
    printf("  Response: 0x%08X\n", v);
    Sleep(300);
    v = SmuQuery(0x37); printf("  GfxFreq: %u MHz\n", v/100);

    /* SetMaxDeepSleepDfllGfxDiv (0x1A) = minimize divider during deep sleep */
    printf("\nSetMaxDeepSleepDfllGfxDiv(0x1A, 1)...\n");
    v = SmuQueryParam(0x1A, 1);
    printf("  Response: 0x%08X\n", v);
    Sleep(300);
    v = SmuQuery(0x37); printf("  GfxFreq: %u MHz\n", v/100);

    /* Try RequestActiveWgp (0x18) — valid v11.8 msg, may wake GFX */
    printf("\nRequestActiveWgp(0x18)...\n");
    v = SmuQueryParam(0x18, 0);
    printf("  Response: 0x%08X\n", v);
    Sleep(100);
    v = SmuQuery(0x37); printf("  GfxFreq: %u MHz\n", v/100);
    v = SmuQuery(0x1E); printf("  ActiveWgp: %u\n", v);

    /* NOTE: RequestGfxclk(0xE, 1000) set freq to 10 MHz (side effect).
       This is the overdrive message from Linux — only works in full DPM context.
       Not useful without proper init sequence. */

    /* UnForceGfxFreq (0x3A) — release any forced frequency */
    printf("\nUnForceGfxFreq(0x3A)...\n");
    v = SmuQuery(0x3A);
    printf("  Response: 0x%08X\n", v);
    Sleep(100);

    /* Restore soft limits (0 = unlimit) */
    printf("\n=== Cleanup ===\n");
    SmuQueryParam(0x35, 0);  /* Reset soft min */
    SmuQueryParam(0x36, 0);  /* Reset soft max */
    v = SmuQuery(0x37); printf("GfxFreq after reset: %u MHz\n", v/100);

    CloseHandle(g_hDev);
    printf("\nDONE\n");
    return 0;
}
