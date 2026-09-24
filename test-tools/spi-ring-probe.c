/* spi-ring-probe.c — PURE SPI_PG write probe (no SMU, no CC, no RLC touches).
 * Run AFTER psp-ring-submit-test.exe (GPCOM ring persists in driver).
 * Question: do BAR5 SPI_PG writes stick once the PSP ring exists?
 * Uses canonical Linux GRBM layout (SH=bit8, SE=bit16, bcast=0x15000000). */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define IOCTL_GPU_READ  0x80000B88
#define IOCTL_GPU_WRITE 0x80000B8C
#define IOCTL_GPU_INIT  0x80000B80

#pragma pack(push, 8)
typedef struct { UINT32 RegisterOffset; UINT32 Value; } REG_IO;
typedef struct {
    UINT64 MmioPhysicalBase;
    UINT32 MmioSize;
    UINT32 Flags;
    UINT64 FbPhysicalBase;
    UINT32 FbSize;
} INIT_HW;
#pragma pack(pop)

static HANDLE g_hDev = INVALID_HANDLE_VALUE;

static BOOL WriteReg(UINT32 o, UINT32 v) {
    REG_IO r; DWORD ret = 0;
    r.RegisterOffset = o; r.Value = v;
    return DeviceIoControl(g_hDev, IOCTL_GPU_WRITE, &r, sizeof(r), &r, sizeof(r), &ret, NULL);
}

static UINT32 ReadReg(UINT32 o) {
    REG_IO r; DWORD ret = 0;
    r.RegisterOffset = o; r.Value = 0;
    if (DeviceIoControl(g_hDev, IOCTL_GPU_READ, &r, sizeof(r), &r, sizeof(r), &ret, NULL))
        return r.Value;
    return 0xFFFFFFFF;
}

int main(void) {
    static const UINT32 banks[4] = {0x00000000, 0x00000100, 0x00010000, 0x00010100};
    static const char *names[4] = {"SE0/SH0", "SE0/SH1", "SE1/SH0", "SE1/SH1"};
    INIT_HW ih; DWORD ret = 0;
    int i, hits = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== SPI_PG ring-gate probe (WRITE-ONLY test, no SMU) ===\n");
    printf("Run psp-ring-submit-test.exe FIRST so the GPCOM ring exists.\n\n");

    g_hDev = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (g_hDev == INVALID_HANDLE_VALUE) {
        printf("FAIL open gle=%lu\n", GetLastError());
        return 1;
    }
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL;
    ih.MmioSize = 0x80000;
    ih.Flags = 1; /* NBIO_MAP */
    DeviceIoControl(g_hDev, IOCTL_GPU_INIT, &ih, sizeof(ih), &ih, sizeof(ih), &ret, NULL);

    printf("GPU_ID=0x%08X (expect 0x9FFF9700)\n", ReadReg(0x0000));
    for (i = 0; i < 4; i++) {
        UINT32 before, after;
        WriteReg(0x34D0, banks[i]);
        before = ReadReg(0x5C3C);
        WriteReg(0x5C3C, 0x1F);
        after = ReadReg(0x5C3C);
        printf("%s: SPI_PG before=0x%08X after=0x%08X %s\n",
               names[i], before, after, (after == 0x1F) ? "*** STUCK ***" : "[locked]");
        if (after == 0x1F) hits++;
    }
    WriteReg(0x34D0, 0x15000000); /* restore broadcast */
    printf("broadcast restored. banks stuck: %d/4\n", hits);
    CloseHandle(g_hDev);
    return 0;
}
