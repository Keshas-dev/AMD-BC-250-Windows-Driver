/*
 * g3d-wr-control - calibration: does a full 32-bit register write even work?
 *
 * Testing CP_RB0_BASE showed only bits [7:0] ever survive, on every encoding
 * tried. Before concluding "the ring base is locked", the write path itself has
 * to be calibrated. If every register behaves the same way, the limitation is
 * in our write path or in how this hardware handles MMIO writes, and the ring
 * conclusion would be wrong.
 *
 * GRBM_GFX_INDEX is a known-good control: previous tests wrote 0x15000000 and
 * read it back intact, and it is only used as a bank selector so touching it is
 * harmless. CP_ME_CNTL is a second control that we know holds a real value.
 *
 * Every register is restored.
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE g_hDev = INVALID_HANDLE_VALUE;

static uint32_t ReadReg(uint32_t off) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD ret = 0;
    r.RegisterOffset = off; r.Value = 0;
    if (DeviceIoControl(g_hDev, IOCTL_AMDBC250_READ_REG, &r, sizeof(r),
                        &r, sizeof(r), &ret, NULL)) return r.Value;
    return 0xFFFFFFFF;
}
static BOOL WriteReg(uint32_t off, uint32_t v) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD ret = 0;
    r.RegisterOffset = off; r.Value = v;
    return DeviceIoControl(g_hDev, IOCTL_AMDBC250_WRITE_REG, &r, sizeof(r),
                           &r, sizeof(r), &ret, NULL);
}

/* Reports how many of the 32 bits we wrote actually survived. */
static void Trial(const char *name, uint32_t addr, uint32_t probe) {
    uint32_t orig = ReadReg(addr);
    WriteReg(addr, probe);
    uint32_t got = ReadReg(addr);
    WriteReg(addr, orig);
    uint32_t back = ReadReg(addr);

    int bitsKept = 0;
    for (int i = 0; i < 32; i++)
        if (got & (1u << i)) bitsKept++;

    const char *v;
    if      (got == probe)   v = "FULL WRITE OK";
    else if (got == orig)    v = "write ignored (RO)";
    else                     v = "PARTIAL";

    printf("  %-22s 0x%05X  orig=0x%08X  wrote=0x%08X  got=0x%08X  bits=%2d/32  %s  restore=%s\n",
           name, addr, orig, probe, got, bitsKept, v,
           back == orig ? "OK" : "MISMATCH");
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);

    g_hDev = CreateFileA("\\\\.\\AMDBC250DreamV43",
        GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, 0, NULL);
    if (g_hDev == INVALID_HANDLE_VALUE) {
        printf("FAIL: cannot open GPU device (err=%lu)\n", GetLastError());
        return 1;
    }
    {
        AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br = 0;
        memset(&ih, 0, sizeof(ih));
        ih.MmioPhysicalBase = 0xFE800000ULL;
        ih.MmioSize = 0x80000;
        ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
        if (!DeviceIoControl(g_hDev, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih),
                             NULL, 0, &br, NULL)) {
            printf("FAIL: INIT_HARDWARE (err=%lu)\n", GetLastError());
            return 1;
        }
    }

    /* Probe pattern: alternating bits, so a stuck bit is unambiguous. */
    const uint32_t P = 0xA5C35E7Du;

    printf("\n=== CONTROL: known-writable register ===\n");
    Trial("GRBM_GFX_INDEX",  0x34D0, 0x15000000u);
    Trial("GRBM_GFX_INDEX2", 0x34D0, P);

    printf("\n=== CONTROL: register holding a known real value ===\n");
    Trial("CP_ME_CNTL(0x4FB8)", 0x4FB8, P);
    Trial("SCRATCH_REG0",     0x32D4, 0x5EADBEEFu);

    printf("\n=== SUBJECT: GFX ring block ===\n");
    Trial("CP_RB0_BASE_LO",   0x89E0, P);
    Trial("CP_RB0_BASE_LO2",  0x89E0, 0xFFFFFFFFu);
    Trial("CP_RB0_CNTL",      0x89E4, 0x00030001u);
    Trial("CP_RB0_WPTR",      0x8A30, P);
    Trial("CP_RB0_BASE_HI",   0x8BA4, P);

    printf("\n=== bit-by-bit map of CP_RB0_BASE_LO ===\n");
    {
        uint32_t orig = ReadReg(0x89E0);
        for (int b = 0; b < 32; b += 8) {
            uint32_t v = 0xFFu << b;
            WriteReg(0x89E0, v);
            printf("  bits[%2d:%2d] write 0x%08X -> 0x%08X\n", b, b + 7, v, ReadReg(0x89E0));
        }
        WriteReg(0x89E0, orig);
        printf("  restored -> 0x%08X\n", ReadReg(0x89E0));
    }

    printf("\nDone.\n");
    CloseHandle(g_hDev);
    return 0;
}