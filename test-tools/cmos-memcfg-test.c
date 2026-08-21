/* cmos-memcfg-test.c - read/write the BC-250 CMOS "APCB" memory config via the
 * kernel IOCTL IOCTL_AMDBC250_CMOS_ACCESS (0x80000C28). Port of
 * fanoush/bc250_memcfg to our Windows driver.
 *
 * The BIOS keeps a MemConf_t blob at CMOS offset 0x90 (signature "APCB" =
 * 0x42435041) with memory timings (ClockSpeed, tCL..tRFC) and UMA_SIZE
 * (VRAM/UMA frame buffer in MB, 16M aligned) at 0xAA. The kernel validates
 * field ranges, recomputes the signature + checksum (sum of 0x96..0xAB), and
 * writes the block back. REBOOT to apply.
 *
 * Usage:
 *   cmos-memcfg-test.exe              - read-only dump of all fields
 *   cmos-memcfg-test.exe UMA_SIZE 512 - set UMA frame buffer to 512 MB
 *   cmos-memcfg-test.exe tCL 24       - set a timing field (expert only)
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "..\inc\amdbc250_ioctl.h"

static const char *field_name(uint32_t f) {
    switch (f) {
    case AMDBC250_CMOS_F_CLOCKSPEED: return "ClockSpeed";
    case AMDBC250_CMOS_F_TCL:        return "tCL";
    case AMDBC250_CMOS_F_TRAS:       return "tRAS";
    case AMDBC250_CMOS_F_TRCDRD:     return "tRCDRD";
    case AMDBC250_CMOS_F_TRCDWR:     return "tRCDWR";
    case AMDBC250_CMOS_F_TRCAB:      return "tRCAb";
    case AMDBC250_CMOS_F_TRCPB:      return "tRCPb";
    case AMDBC250_CMOS_F_TRPAB:      return "tRPAb";
    case AMDBC250_CMOS_F_TRPPB:      return "tRPPb";
    case AMDBC250_CMOS_F_TRRDS:      return "tRRDS";
    case AMDBC250_CMOS_F_TRRDL:      return "tRRDL";
    case AMDBC250_CMOS_F_TRTP:       return "tRTP";
    case AMDBC250_CMOS_F_TFAW:       return "tFAW";
    case AMDBC250_CMOS_F_TREF:       return "tREF";
    case AMDBC250_CMOS_F_RFCPB:      return "RFCPb";
    case AMDBC250_CMOS_F_TRFC:       return "tRFC";
    case AMDBC250_CMOS_F_UMA_SIZE:   return "UMA_SIZE";
    default:                         return "?";
    }
}

static const char *sig_name(uint32_t s) {
    switch (s) {
    case 0x42435041UL: return "APCB (LINUX_TOOL_SIGNATURE)";
    case 0x4C424124UL: return "ABL_SIGNATURE";
    case 0x42534D43UL: return "CMOS_BAD_ERROR_CODE";
    case 0x46544457UL: return "WATCH_DOG_TIMER_FIRED";
    case 0x454B4843UL: return "CHECKSUM_ERROR_CODE";
    case 0x45474953UL: return "SIGNATURE_ERROR_CODE";
    default:           return "UNKNOWN";
    }
}

static int send_ioctl(HANDLE h, uint32_t op, uint32_t field, uint32_t value,
                      AMDBC250_IOCTL_CMOS_ACCESS *cm) {
    DWORD br = 0;
    ZeroMemory(cm, sizeof(*cm));
    cm->Operation = op;
    cm->Field = field;
    cm->Value = value;
    if (!DeviceIoControl(h, IOCTL_AMDBC250_CMOS_ACCESS,
                         cm, sizeof(*cm), cm, sizeof(*cm), &br, NULL)) {
        printf("  FAIL: CMOS_ACCESS IOCTL (err=%lu%s)\n", GetLastError(),
               (GetLastError() == ERROR_INVALID_PARAMETER) ?
                   " = out of range / bad field / bad op" : "");
        return 0;
    }
    return 1;
}

static void print_decoded(const AMDBC250_IOCTL_CMOS_ACCESS *cm) {
    printf("  Signature   : 0x%08X (%s)\n", cm->Signature, sig_name(cm->Signature));
    printf("  Checksum    : stored=0x%04X computed=0x%04X [%s]\n",
           cm->ChecksumStored, cm->ChecksumCalc,
           (cm->ChecksumStored == cm->ChecksumCalc) ? "MATCH" : "MISMATCH");
    printf("  ClockSpeed  : %u MHz\n", cm->ClockSpeed);
    printf("  tCL         : %u\n", cm->tCL);
    printf("  tRAS        : %u\n", cm->tRAS);
    printf("  tRCDRD      : %u\n", cm->tRCDRD);
    printf("  tRCDWR      : %u\n", cm->tRCDWR);
    printf("  tRCAb       : %u\n", cm->tRCAb);
    printf("  tRCPb       : %u\n", cm->tRCPb);
    printf("  tRPAb       : %u\n", cm->tRPAb);
    printf("  tRPPb       : %u\n", cm->tRPPb);
    printf("  tRRDS       : %u\n", cm->tRRDS);
    printf("  tRRDL       : %u\n", cm->tRRDL);
    printf("  tRTP        : %u\n", cm->tRTP);
    printf("  tFAW        : %u\n", cm->tFAW);
    printf("  tREF        : %u\n", cm->tREF);
    printf("  RFCPb       : %u\n", cm->RFCPb);
    printf("  tRFC        : %u\n", cm->tRFC);
    printf("  UMA_SIZE    : %u MB  <-- VRAM/UMA frame buffer\n", cm->UmaSizeMb);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== BC-250 CMOS (APCB memcfg) via kernel IOCTL 0x80000C28 ===\n\n");

    if (argc > 1 && (argc != 3)) {
        printf("Usage:\n  %s\n  %s UMA_SIZE 512\n  %s <FieldName> <Value>\n",
               argv[0], argv[0], argv[0]);
        return 1;
    }

    HANDLE h = CreateFileA("\\\\.\\AMDBC250DreamV43",
        GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("FAIL: CreateFile gle=%lu\n", GetLastError());
        return 1;
    }

    /* Not needed for CMOS (I/O ports only), but leaves the driver in a known state. */
    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br = 0;
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL;
    ih.MmioSize = 0x80000;
    ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
    DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &br, NULL);

    AMDBC250_IOCTL_CMOS_ACCESS cm;

    if (argc == 1) {
        printf("--- READ (no writes) ---\n");
        if (!send_ioctl(h, AMDBC250_CMOS_OP_READ, 0, 0, &cm)) { CloseHandle(h); return 1; }
        if (cm.Result != 1) {
            printf("  Kernel refused the read (Result=%u)\n", cm.Result);
            CloseHandle(h);
            return 1;
        }
        print_decoded(&cm);
        printf("\nRaw 0x90..0xAF: ");
        for (int i = 0; i < 0x20; i++) printf("%02X ", cm.Raw[i]);
        printf("\n");
        printf("\nNOTE: this dump is the LIVE CMOS. VRAM size = UMA_SIZE (must reboot "
               "for changes).\n");
        CloseHandle(h);
        return 0;
    }

    /* --- SET --- */
    uint32_t field = 0xFFFFFFFF, value = (uint32_t)strtoul(argv[2], NULL, 0);
    if (_stricmp(argv[1], "UMA_SIZE") == 0) field = AMDBC250_CMOS_F_UMA_SIZE;
    else if (_stricmp(argv[1], "ClockSpeed") == 0) field = AMDBC250_CMOS_F_CLOCKSPEED;
    else if (_stricmp(argv[1], "tCL") == 0) field = AMDBC250_CMOS_F_TCL;
    else if (_stricmp(argv[1], "tRAS") == 0) field = AMDBC250_CMOS_F_TRAS;
    else if (_stricmp(argv[1], "tRCDRD") == 0) field = AMDBC250_CMOS_F_TRCDRD;
    else if (_stricmp(argv[1], "tRCDWR") == 0) field = AMDBC250_CMOS_F_TRCDWR;
    else if (_stricmp(argv[1], "tRCAb") == 0) field = AMDBC250_CMOS_F_TRCAB;
    else if (_stricmp(argv[1], "tRCPb") == 0) field = AMDBC250_CMOS_F_TRCPB;
    else if (_stricmp(argv[1], "tRPAb") == 0) field = AMDBC250_CMOS_F_TRPAB;
    else if (_stricmp(argv[1], "tRPPb") == 0) field = AMDBC250_CMOS_F_TRPPB;
    else if (_stricmp(argv[1], "tRRDS") == 0) field = AMDBC250_CMOS_F_TRRDS;
    else if (_stricmp(argv[1], "tRRDL") == 0) field = AMDBC250_CMOS_F_TRRDL;
    else if (_stricmp(argv[1], "tRTP") == 0) field = AMDBC250_CMOS_F_TRTP;
    else if (_stricmp(argv[1], "tFAW") == 0) field = AMDBC250_CMOS_F_TFAW;
    else if (_stricmp(argv[1], "tREF") == 0) field = AMDBC250_CMOS_F_TREF;
    else if (_stricmp(argv[1], "RFCPb") == 0) field = AMDBC250_CMOS_F_RFCPB;
    else if (_stricmp(argv[1], "tRFC") == 0) field = AMDBC250_CMOS_F_TRFC;
    if (field == 0xFFFFFFFF) {
        printf("Unknown field '%s'\n", argv[1]);
        CloseHandle(h);
        return 1;
    }

    printf("--- SET %s = %lu ---\n", field_name(field), value);
    if (!send_ioctl(h, AMDBC250_CMOS_OP_SET, field, value, &cm)) { CloseHandle(h); return 1; }
    if (cm.Result != 1) {
        printf("  REFUSED. Range: see AGENTS memory-timings table. "
               "(UMA_SIZE: >=256, 16MB aligned; ClockSpeed 450-1750 MHz; "
               "timings are expert-only, no confirmed gains.)\n");
        CloseHandle(h);
        return 1;
    }

    printf("  %s: %u -> %u  [written]\n", field_name(field),
           cm.FieldValueBefore, cm.FieldValueAfter);
    print_decoded(&cm);
    printf("\nWritten OK. Signature+checksum recomputed. REBOOT to apply.\n");
    if (field == AMDBC250_CMOS_F_UMA_SIZE)
        printf("After reboot, verify with GET_VRAM_INFO / VRAM detection.\n");
    printf("Revert: clear CMOS (jumper/battery) - no software revert.\n");

    CloseHandle(h);
    return 0;
}