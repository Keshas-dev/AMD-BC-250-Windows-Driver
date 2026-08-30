/* wgp-persist-check.c
 *
 * Checks whether WGP unlock values (SPI_PG=0x1F, CC=0, RLC=0x1F) PERSISTED
 * into Windows. Run this AFTER either:
 *   A) EFI Shell -> WGP_unlock.nsh -> boot Windows
 *   B) Linux (CachyOS with CU unlock active) -> warm reboot -> Windows
 *
 * Verdict logic:
 *   SPI_PG reads 0x1F (any bank)  -> unlock PERSISTED (huge news!)
 *   SPI_PG reads 0x00             -> lock returned (expected per theory)
 *   ActiveWgp > 0                 -> WGPs actually POWERED (even bigger news)
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

static HANDLE g_hDev = INVALID_HANDLE_VALUE;

#define IOCTL_GPU_READ     0x80000B88
#define IOCTL_GPU_WRITE    0x80000B8C
#define IOCTL_GPU_INIT     0x80000B80

typedef struct { UINT32 RegisterOffset; UINT32 Value; } REG_IO;

static BOOL WriteReg(uint32_t offset, uint32_t value) {
    REG_IO r; DWORD returned = 0;
    r.RegisterOffset = offset; r.Value = value;
    return DeviceIoControl(g_hDev, IOCTL_GPU_WRITE, &r, sizeof(r), &r, sizeof(r), &returned, NULL);
}
static uint32_t ReadReg(uint32_t offset) {
    REG_IO r; DWORD returned = 0;
    r.RegisterOffset = offset; r.Value = 0;
    if (DeviceIoControl(g_hDev, IOCTL_GPU_READ, &r, sizeof(r), &r, sizeof(r), &returned, NULL)) return r.Value;
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
    SmnWrite(C2PMSG_82, 0);
    SmnWrite(C2PMSG_66, msg);
    if (!WaitC2p90(1, 500)) return 0xFFFFFFFF;
    return SmnRead(C2PMSG_82);
}

/* GRBM_GFX_INDEX (0x34D0) bank selects — Linux gfx10 layout */
#define GRBM_GFX_INDEX   0x34D0
#define BANK_SE0SH0      0x00000000
#define BANK_SE0SH1      0x00000100
#define BANK_SE1SH0      0x00010000
#define BANK_SE1SH1      0x00010100
#define BANK_BCAST       0x15000000

#define SPI_PG           0x5C3C
#define RLC_PG           0x3D64
#define CC_ARRAY         0x9C1C

static const char* BankName(int b) {
    static const char* names[] = { "SE0/SH0", "SE0/SH1", "SE1/SH0", "SE1/SH1", "BCAST" };
    return names[b];
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    g_hDev = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (g_hDev == INVALID_HANDLE_VALUE) { printf("FAIL: CreateFile gle=%lu\n", GetLastError()); return 1; }

    /* MUST match driver's AMDBC250_IOCTL_INIT_HARDWARE exactly (32 bytes):
       driver writes back the FULL struct to the output buffer — a smaller
       stack buffer gets overflowed -> /GS fastfail at exit. */
    typedef struct {
        UINT64 MmioPhysicalBase;
        UINT32 MmioSize;
        UINT32 Flags;
        UINT64 FbPhysicalBase;
        UINT32 FbSize;
    } INIT_HW;
    INIT_HW ih; DWORD ret = 0;
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL;
    ih.MmioSize = 0x80000;
    ih.Flags = 1;
    BOOL ok = DeviceIoControl(g_hDev, IOCTL_GPU_INIT, &ih, sizeof(ih), &ih, sizeof(ih), &ret, NULL);
    printf("INIT_HW: ok=%d gle=%lu\n", ok, GetLastError());

    printf("=== WGP Persist Check (run AFTER EFI unlock or Linux warm reboot) ===\n\n");

    printf("GPU_ID (0x0000): 0x%08X\n", ReadReg(0x0000));
    printf("GRBM_STATUS (0x3260): 0x%08X\n\n", ReadReg(0x3260));

    /* Per-bank register dump — NO writes, pure readback */
    uint32_t banks[5] = { BANK_SE0SH0, BANK_SE0SH1, BANK_SE1SH0, BANK_SE1SH1, BANK_BCAST };
    int spiUnlocked = 0;

    printf("%-9s %-12s %-12s %-12s\n", "Bank", "SPI_PG", "RLC_PG", "CC_ARRAY");
    for (int b = 0; b < 5; b++) {
        WriteReg(GRBM_GFX_INDEX, banks[b]);
        uint32_t spi = ReadReg(SPI_PG);
        uint32_t rlc = ReadReg(RLC_PG);
        uint32_t cc  = ReadReg(CC_ARRAY);
        printf("%-9s 0x%08X   0x%08X   0x%08X", BankName(b), spi, rlc, cc);
        if ((spi & 0x1F) == 0x1F) { printf("  <-- UNLOCKED!"); spiUnlocked++; }
        printf("\n");
    }
    WriteReg(GRBM_GFX_INDEX, BANK_BCAST); /* restore broadcast */

    /* SMU queries */
    printf("\n=== SMU State ===\n");
    uint32_t v;
    v = SmuQuery(0x2);  printf("SmuVersion:    0x%08X (%u.%u.%u)\n", v, (v>>16)&0xFF, (v>>8)&0xFF, v&0xFF);
    v = SmuQuery(0x3D); printf("Features:      0x%08X (GFXCLK=%s GFXOFF=%s CG=%s PG=%s)\n", v,
        (v&1)?"ON":"OFF", (v&4)?"ON":"OFF", (v&8)?"ON":"OFF", (v&16)?"ON":"OFF");
    v = SmuQuery(0x37); printf("GfxFreq:       %u MHz\n", v);
    v = SmuQuery(0x1E); printf("ActiveWgp:     %u %s\n", v, (v > 0 && v != 0xFFFFFFFF) ? "<-- WGPs POWERED!" : "(0 = off)");

    /* Verdict */
    printf("\n=== VERDICT ===\n");
    if (spiUnlocked > 0) {
        printf("SPI_PG = 0x1F in %d bank(s): UNLOCK PERSISTED into Windows!\n", spiUnlocked);
        printf("Next: try creating a GFX ring / dispatching PM4.\n");
    } else {
        printf("SPI_PG = 0 everywhere: lock returned after reboot (expected).\n");
    }
    v = SmuQuery(0x1E);
    if (v > 0 && v != 0xFFFFFFFF) {
        printf("ActiveWgp=%u: WGPs are POWERED — compute may be possible!\n", v);
    } else {
        printf("ActiveWgp=0: WGPs powered down.\n");
    }

    CloseHandle(g_hDev);
    return 0;
}
