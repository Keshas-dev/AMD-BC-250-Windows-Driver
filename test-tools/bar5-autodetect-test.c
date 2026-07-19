#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE g_hDev = INVALID_HANDLE_VALUE;

static uint32_t ReadReg(uint32_t offset) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD returned = 0;
    r.RegisterOffset = offset; r.Value = 0;
    if (DeviceIoControl(g_hDev, IOCTL_AMDBC250_READ_REG, &r, sizeof(r), &r, sizeof(r), &returned, NULL)) return r.Value;
    return 0xFFFFFFFF;
}
static BOOL WriteReg(uint32_t offset, uint32_t value) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD returned = 0;
    r.RegisterOffset = offset; r.Value = value;
    return DeviceIoControl(g_hDev, IOCTL_AMDBC250_WRITE_REG, &r, sizeof(r), &r, sizeof(r), &returned, NULL);
}

#define GRBM_GFX_INDEX 0x34D0
#define GRBM_GFX_INDEX_BROADCAST 0xE0000000

int main(void) {
    g_hDev = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (g_hDev == INVALID_HANDLE_VALUE) {
        printf("FAIL: cannot open GPU device (err=%lu)\n", GetLastError());
        return 1;
    }

    printf("=== INIT_HARDWARE with MmioPhysicalBase=0 (auto-detect BAR5) ===\n");
    {
        AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br = 0;
        memset(&ih, 0, sizeof(ih));
        ih.MmioPhysicalBase = 0;          /* trigger PCIe auto-detect */
        ih.MmioSize = 0;                  /* driver fills default 512KB */
        ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
        if (!DeviceIoControl(g_hDev, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), NULL, 0, &br, NULL)) {
            printf("FAIL: INIT_HARDWARE (err=%lu)\n", GetLastError());
            return 1;
        }
        printf("OK\n");
    }

    printf("\n=== Verify BAR5 is physically mapped (GRBM_GFX_INDEX=0x34D0) ===\n");
    uint32_t before = ReadReg(GRBM_GFX_INDEX);
    WriteReg(GRBM_GFX_INDEX, GRBM_GFX_INDEX_BROADCAST);
    uint32_t after = ReadReg(GRBM_GFX_INDEX);
    WriteReg(GRBM_GFX_INDEX, before);
    printf("GRBM_GFX_INDEX: before=0x%08X wrote=0x%08X read=0x%08X %s\n",
           before, GRBM_GFX_INDEX_BROADCAST, after,
           (after == GRBM_GFX_INDEX_BROADCAST) ? "*** BAR5 LIVE (auto-detect worked) ***"
                                               : (after == 0xFFFFFFFF ? "[MMIO NULL?]" : "[RO]"));

    if (after == GRBM_GFX_INDEX_BROADCAST) {
        printf("\n*** SUCCESS: BAR5 auto-detected via HalGetBusData + MmMapIoSpace. No hard-coded 0xFE800000 needed. ***\n");
        return 0;
    }
    printf("\n*** Auto-detect mapped but register not live (SOS/offset issue). ***\n");
    return 2;
}
