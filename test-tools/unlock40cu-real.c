/* unlock40cu-real.c - call UNLOCK_40CU with proper struct, then read registers */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"
#pragma warning(disable: 4996)

#define IOCTL_AMDBC250_UNLOCK_40CU  (0x80000980)

#pragma pack(push,1)
typedef struct {
    UINT32 SpiWgpMaskBefore;
    UINT32 SpiWgpMaskAfter;
    UINT32 ActiveWgpBefore;
    UINT32 ActiveWgpAfter;
} UNLOCK40CU;
#pragma pack(pop)

static uint32_t readReg(HANDLE h, uint32_t off) {
    AMDBC250_IOCTL_REG_ACCESS ra;
    DWORD b;
    ra.RegisterOffset = off;
    ra.Value = 0;
    if (DeviceIoControl(h, IOCTL_AMDBC250_READ_REG, &ra, sizeof(ra), &ra, sizeof(ra), &b, NULL))
        return ra.Value;
    return 0xFFFFFFFF;
}

int main(void) {
    HANDLE h = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { printf("open FAIL gle=%lu\n", GetLastError()); return 1; }
    printf("device OK\n");

    ULONG code = IOCTL_AMDBC250_UNLOCK_40CU;

    printf("\n=== BEFORE UNLOCK ===\n");
    printf("  SPI_PG (0x5C3C)   = 0x%08X\n", readReg(h, 0x5C3C));
    printf("  CC_ARRAY (0x9C1C) = 0x%08X\n", readReg(h, 0x9C1C));
    printf("  RLC_PG (0x3D64)   = 0x%08X\n", readReg(h, 0x3D64));
    printf("  GRBM_STATUS       = 0x%08X\n", readReg(h, 0x2000));

    /* Pass 1: enable=1 */
    ULONG enable = 1;
    UNLOCK40CU out;
    DWORD br = 0;
    BOOL r = DeviceIoControl(h, code, &enable, sizeof(enable), &out, sizeof(out), &br, NULL);
    printf("\nIOCTL(enable=1) -> %s (gle=%lu, br=%lu)\n", r ? "OK" : "FAIL", GetLastError(), br);

    printf("\n=== AFTER UNLOCK (enable=1) ===\n");
    printf("  SPI_PG (0x5C3C)   = 0x%08X\n", readReg(h, 0x5C3C));
    printf("  CC_ARRAY (0x9C1C) = 0x%08X\n", readReg(h, 0x9C1C));
    printf("  RLC_PG (0x3D64)   = 0x%08X\n", readReg(h, 0x3D64));
    printf("  GRBM_STATUS       = 0x%08X\n", readReg(h, 0x2000));

    /* Pass 2: enable=0 (restore) */
    enable = 0;
    memset(&out, 0, sizeof(out));
    br = 0;
    r = DeviceIoControl(h, code, &enable, sizeof(enable), &out, sizeof(out), &br, NULL);
    printf("\nIOCTL(enable=0) -> %s (gle=%lu, br=%lu)\n", r ? "OK" : "FAIL", GetLastError(), br);

    printf("\n=== AFTER RESTORE (enable=0) ===\n");
    printf("  SPI_PG (0x5C3C)   = 0x%08X\n", readReg(h, 0x5C3C));
    printf("  CC_ARRAY (0x9C1C) = 0x%08X\n", readReg(h, 0x9C1C));
    printf("  RLC_PG (0x3D64)   = 0x%08X\n", readReg(h, 0x3D64));

    CloseHandle(h);
    return 0;
}
