/* smu-feature-toggle.c
 *
 * Enable/Disable SMU features (GFXCLK/GFXOFF/CG/PG) on BC-250 via Queue 2.
 * Proven 2026-07-08: Q2 msg 0x06 mask 0x1C changed 0xDD602C7D -> 0xDD602C61.
 *
 * Usage:
 *   smu-feature-toggle.exe            -> show current features
 *   smu-feature-toggle.exe off        -> disable GFXOFF|CG|PG (mask 0x1C)
 *   smu-feature-toggle.exe on         -> enable  GFXOFF|CG|PG (mask 0x1C)
 *   smu-feature-toggle.exe off 0x04   -> disable custom mask
 *   smu-feature-toggle.exe on  0x01   -> enable custom mask (e.g. GFXCLK DPM)
 *
 * Feature bits: 0=GFXCLK DPM, 2=GFXOFF, 3=CG, 4=PG
 * WARNING: does NOT unlock WGPs (SPI_PG stays SOS-locked) — verified.
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

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
static uint32_t SmnRead(uint32_t a)  { WriteReg(0x38, a); ReadReg(0x38); return ReadReg(0x3C); }
static BOOL     SmnWrite(uint32_t a, uint32_t v) { WriteReg(0x38, a); return WriteReg(0x3C, v); }

/* --- Queue 0 (queries): cmd/rsp/arg --- */
#define Q0_CMD 0x03B10A08
#define Q0_RSP 0x03B10A68
#define Q0_ARG 0x03B10A48
/* --- Queue 2 (feature enable/disable): cmd/rsp/arg(+4=high) --- */
#define Q2_CMD 0x03B10528
#define Q2_RSP 0x03B10564
#define Q2_ARG 0x03B10998
#define Q2_ARG_HI 0x03B1099C

static int WaitRsp(uint32_t rspAddr, int ms) {
    for (int i = 0; i < ms; i++) {
        uint32_t v = SmnRead(rspAddr);
        if (v != 0 && v != 0xFFFFFFFF) return (int)v;
        Sleep(1);
    }
    return -1;
}

/* Q0 query (bar5-smn-test protocol) */
static uint32_t SmuQuery(uint16_t msg) {
    if (SmnRead(Q0_RSP) == 1) SmnWrite(Q0_RSP, 0);
    SmnWrite(Q0_ARG, 0);
    SmnWrite(Q0_CMD, msg);
    int r = WaitRsp(Q0_RSP, 500);
    if (r < 0) return 0xFFFFFFFF;
    return SmnRead(Q0_ARG);
}

/* Q2 set: returns response status (0x01 OK, 0xFF fail, 0xFE unknown, 0xFD rejected, 0xFC busy) */
static int SmuQ2Set(uint16_t msg, uint32_t maskLow, uint32_t maskHigh) {
    SmnWrite(Q2_RSP, 0);            /* clear response */
    SmnWrite(Q2_ARG, maskLow);      /* arg low */
    SmnWrite(Q2_ARG_HI, maskHigh);  /* arg high */
    SmnWrite(Q2_CMD, msg);          /* kick */
    return WaitRsp(Q2_RSP, 1000);
}

static void PrintFeatures(uint32_t f) {
    printf("Features: 0x%08X (GFXCLK=%s GFXOFF=%s CG=%s PG=%s)\n", f,
        (f & 1) ? "ON" : "OFF", (f & 4) ? "ON" : "OFF",
        (f & 8) ? "ON" : "OFF", (f & 16) ? "ON" : "OFF");
}

int main(int argc, char* argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);
    g_hDev = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (g_hDev == INVALID_HANDLE_VALUE) { printf("FAIL: CreateFile gle=%lu\n", GetLastError()); return 1; }

    typedef struct {
        UINT64 MmioPhysicalBase; UINT32 MmioSize; UINT32 Flags;
        UINT64 FbPhysicalBase; UINT32 FbSize;
    } INIT_HW;
    INIT_HW ih; DWORD ret = 0;
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL; ih.MmioSize = 0x80000; ih.Flags = 1;
    BOOL ok = DeviceIoControl(g_hDev, IOCTL_GPU_INIT, &ih, sizeof(ih), &ih, sizeof(ih), &ret, NULL);
    printf("INIT_HW: ok=%d gle=%lu\n", ok, GetLastError());

    uint32_t before = SmuQuery(0x3D);
    printf("BEFORE: "); PrintFeatures(before);

    if (argc < 2) { CloseHandle(g_hDev); return 0; }

    int disable = (_stricmp(argv[1], "off") == 0);
    int enable  = (_stricmp(argv[1], "on") == 0);
    if (!disable && !enable) { printf("Usage: exe [off|on] [hexmask]\n"); CloseHandle(g_hDev); return 1; }

    uint32_t mask = (argc >= 3) ? (uint32_t)strtoul(argv[2], NULL, 0) : 0x1Cu;

    /* If enabling a subset, OR it with what must stay on? No — Enable sets bits,
       Disable clears bits. Mask selects WHICH bits to act on. */
    int rsp = SmuQ2Set(disable ? 0x06 : 0x05, mask, 0);
    printf("%s mask 0x%X -> Q2 resp: ", disable ? "DISABLE" : "ENABLE", mask);
    switch (rsp) {
    case 1:    printf("OK\n"); break;
    case -1:   printf("TIMEOUT\n"); break;
    case 0xFF: printf("FAILED\n"); break;
    case 0xFE: printf("UNKNOWN\n"); break;
    case 0xFD: printf("REJECTED\n"); break;
    case 0xFC: printf("BUSY\n"); break;
    default:   printf("0x%X\n", rsp); break;
    }

    Sleep(200);
    uint32_t after = SmuQuery(0x3D);
    printf("AFTER:  "); PrintFeatures(after);

    /* Sanity: GFX freq still readable */
    printf("GfxFreq: %u MHz\n", SmuQuery(0x37));

    CloseHandle(g_hDev);
    return (rsp == 1) ? 0 : 1;
}
