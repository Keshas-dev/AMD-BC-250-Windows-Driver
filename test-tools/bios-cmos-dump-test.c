/* bios-cmos-dump-test.c - read-only dump of the BC-250 CMOS "APCB" memory config.
 * Port of fanoush/bc250_memcfg (Linux, ports 0x72/0x73) to our Windows driver via
 * IOCTL_AMDBC250_PORT_IO (0x80000BC8).
 *
 * The BIOS keeps a MemConf_t blob at CMOS offset 0x90 with signature "APCB"
 * (0x42435041). Fields: ClockSpeed, tCL..tRFC memory timings, and UMA_SIZE
 * (VRAM/UMA frame buffer in MB, 16M aligned) at 0xAA. This lets us see what the
 * BIOS actually programmed (user reports 512 MB, Linux/driver dump said 256M).
 *
 * READ-ONLY: never writes CMOS.
 *
 * Usage: bios-cmos-dump-test.exe
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "..\inc\amdbc250_ioctl.h"

#define CMOS_INDEX_PORT 0x72   /* same as bc250_memcfg */
#define CMOS_DATA_PORT  0x73
#define CMOS0_INDEX_PORT 0x70  /* classic bank: RTC, password area, boot config */
#define CMOS0_DATA_PORT  0x71

static HANDLE h;

static int port_io(uint32_t port, uint32_t width, uint32_t isWrite, uint32_t value, uint32_t *out) {
    AMDBC250_IOCTL_PORT_IO p; DWORD b;
    ZeroMemory(&p, sizeof(p));
    p.Port = port; p.Width = width; p.IsWrite = isWrite; p.Value = value;
    if (!DeviceIoControl(h, IOCTL_AMDBC250_PORT_IO, &p, sizeof(p), &p, sizeof(p), &b, NULL)) return 0;
    if (!p.Result) return 0;
    if (out) *out = p.Value;
    return 1;
}

/* Standard CMOS index/data byte read */
static int cmos_read_byte(uint32_t offset, uint8_t *val) {
    uint32_t idx, dat;
    if (!port_io(CMOS_INDEX_PORT, 1, 1, offset, &idx)) return 0;
    if (!port_io(CMOS_DATA_PORT, 1, 0, 0, &dat)) return 0;
    *val = (uint8_t)dat;
    return 1;
}

static uint16_t rd16(const uint8_t *b) { return (uint16_t)(b[0] | (b[1] << 8)); }
static uint32_t rd32(const uint8_t *b) { return (uint32_t)(b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24)); }

static const char *sig_name(uint32_t s) {
    switch (s) {
    case 0x42435041UL: return "LINUX_TOOL_SIGNATURE (APCB)";
    case 0x4C424124UL: return "ABL_SIGNATURE";
    case 0x42534D43UL: return "CMOS_BAD_ERROR_CODE";
    case 0x46544457UL: return "WATCH_DOG_TIMER_FIRED";
    case 0x454B4843UL: return "CHECKSUM_ERROR_CODE";
    case 0x45474953UL: return "SIGNATURE_ERROR_CODE";
    default:           return "UNKNOWN";
    }
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== BC-250 CMOS (APCB) memory config dump (READ-ONLY, ports 0x72/0x73) ===\n\n");

    h = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("FAIL: CreateFile gle=%lu\n", GetLastError());
        return 1;
    }

    /* PORT_IO does not need BAR5, but init anyway so the driver is in a known state. */
    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br = 0;
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL;
    ih.MmioSize = 0x80000;
    ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
    DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &br, NULL);

    /* Read the whole 256-byte CMOS page */
    uint8_t buf[256];
    int ok = 0, fail_at = -1;
    for (int i = 0; i < 256; i++) {
        if (!cmos_read_byte((uint32_t)i, &buf[i])) { fail_at = i; break; }
        ok++;
    }
    if (ok < 0xA0) {
        printf("FAIL: could only read %d CMOS bytes (fail@0x%02X) - is PORT_IO working?\n", ok, fail_at);
        CloseHandle(h);
        return 1;
    }
    printf("Read %d/256 CMOS bytes OK\n\n", ok);

    /* Full hex dump */
    printf("--- CMOS 0x00-0xFF ---\n");
    for (int row = 0; row < 256; row += 16) {
        printf("%04X: ", row);
        for (int c = 0; c < 16; c++) printf("%02X ", buf[row + c]);
        printf("\n");
    }

    /* Decode MemConf_t at 0x90 (packed, little-endian) */
    const uint8_t *m = buf + 0x90;
    uint32_t sig = rd32(m + 0x00);   /* 0x90 */
    uint16_t cks = rd16(m + 0x04);   /* 0x94 */
    printf("\n--- MemConf_t @0x90 ---\n");
    printf("Signature : 0x%08X (%s)\n", sig, sig_name(sig));
    printf("Checksum  : 0x%04X\n", cks);

    if (sig != 0x42435041UL && sig != 0x4C424124UL) {
        printf("NOTE: signature is not APCB/ABL - decoding fields anyway "
               "(error-code signatures still carry the last programmed values).\n\n");
    }

    printf("ClockSpeed: %u (0x%04X) [450..1750]\n", rd16(m + 0x06), rd16(m + 0x06));
    printf("tCL       : %u\n", m[0x08]);
    printf("tRAS      : %u\n", m[0x09]);
    printf("tRCDRD    : %u\n", m[0x0A]);
    printf("tRCDWR    : %u\n", m[0x0B]);
    printf("tRCAb     : %u\n", m[0x0C]);
    printf("tRCPb     : %u\n", m[0x0D]);
    printf("tRPAb     : %u\n", m[0x0E]);
    printf("tRPPb     : %u\n", m[0x0F]);
    printf("tRRDS     : %u\n", m[0x10]);
    printf("tRRDL     : %u\n", m[0x11]);
    printf("tRTP      : %u\n", m[0x12]);
    printf("tFAW      : %u\n", m[0x13]);
    printf("tREF      : %u (0x%04X)\n", rd16(m + 0x14), rd16(m + 0x14));
    printf("RFCPb     : %u (0x%04X)\n", rd16(m + 0x16), rd16(m + 0x16));
    printf("tRFC      : %u (0x%04X)\n", rd16(m + 0x18), rd16(m + 0x18));
    printf("UMA_SIZE  : %u MB (0x%04X)  <-- VRAM/UMA frame buffer\n", rd16(m + 0x1A), rd16(m + 0x1A));

    /* Verify checksum: byte sum of offsets 0x96..0xAB = m[0x06..0x1B] (20 bytes) */
    uint16_t sum = 0;
    for (int i = 0x06; i <= 0x1B; i++) sum += m[i];
    printf("\nChecksum check: stored 0x%04X, computed(0x96..0xAB) 0x%04X -> %s\n",
        cks, sum, (cks == sum) ? "MATCH" : "MISMATCH (may use different range)");

    /* Classic bank 0x70/0x71 (offsets 0x00-0x7F only: bit7 of index = NMI disable).
     * Holds RTC clock, legacy password/checksum area, boot config. Read-only. */
    {
        uint8_t c0[128];
        int ok0 = 0;
        for (int i = 0; i < 128; i++) {
            uint32_t idx, dat;
            if (!port_io(CMOS0_INDEX_PORT, 1, 1, (uint32_t)i, &idx)) break;
            if (!port_io(CMOS0_DATA_PORT, 1, 0, 0, &dat)) break;
            c0[i] = (uint8_t)dat;
            ok0++;
        }
        /* re-enable NMI path state: rewrite index 0 (bit7 clear) */
        {
            uint32_t dummy;
            port_io(CMOS0_INDEX_PORT, 1, 1, 0, &dummy);
        }
        printf("\n--- CMOS classic bank 0x70/0x71 (0x00-0x7F): %d/128 bytes ---\n", ok0);
        for (int row = 0; row < ok0; row += 16) {
            printf("%04X: ", row);
            for (int c = 0; c < 16 && row + c < ok0; c++) printf("%02X ", c0[row + c]);
            printf("\n");
        }
        if (ok0 >= 0x40) {
            printf("\nRTC: %02X%02X-%02X-%02X %02X:%02X:%02X (raw BCD-ish)\n",
                c0[0x09], c0[0x08], c0[0x07], c0[0x06], c0[0x04], c0[0x02], c0[0x00]);
        }
    }

    CloseHandle(h);
    return 0;
}
