#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE h;
static BOOL W32(uint32_t o, uint32_t v) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=v; return DeviceIoControl(h,IOCTL_AMDBC250_WRITE_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL); }
static uint32_t R32(uint32_t o) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=0; if(DeviceIoControl(h,IOCTL_AMDBC250_READ_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL)) return r.Value; return 0xFFFFFFFF; }
static uint32_t SR(uint32_t a) { W32(0x38,a); R32(0x38); return R32(0x3C); }

int main() {
    h = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { printf("FAIL: Open %lu\n", GetLastError()); return 1; }
    AMDBC250_IOCTL_INIT_HARDWARE ih = {0}; ih.MmioPhysicalBase = 0xFE800000ULL; ih.MmioSize = 0x80000; ih.Flags = 1; DWORD b;
    if (!DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &b, NULL)) {
        printf("FAIL: INIT_HARDWARE err=%lu\n", GetLastError()); CloseHandle(h); return 1;
    }

    printf("=== BAR5 accessibility ===\n");
    printf("GRBM_STATUS (0x3260): 0x%08X\n", R32(0x3260));
    printf("CC_CONFIG (0x9C1C): 0x%08X\n", R32(0x9C1C));
    printf("SPI_PG_MASK (0x34FC): 0x%08X\n", R32(0x34FC));

    printf("\n=== SMU SMN bridge health check ===\n");
    printf("Q0_RSP(0x03B10A68): 0x%08X\n", SR(0x03B10A68));
    printf("Q0_CMD(0x03B10A08): 0x%08X\n", SR(0x03B10A08));
    printf("Q0_ARG(0x03B10A48): 0x%08X\n", SR(0x03B10A48));
    printf("Q3_RSP(0x03B10A80): 0x%08X\n", SR(0x03B10A80));
    printf("Q3_CMD(0x03B10A20): 0x%08X\n", SR(0x03B10A20));
    printf("Q2_RSP(0x03B10564): 0x%08X\n", SR(0x03B10564));
    printf("Q2_CMD(0x03B10528): 0x%08X\n", SR(0x03B10528));
    printf("SMU FW_FLAGS(0x03B10024): 0x%08X\n", SR(0x03B10024));

    printf("\n=== SMU direct register health ===\n");
    printf("C2PMSG_90(0x03B10A68): 0x%08X\n", SR(0x03B10A68));
    printf("C2PMSG_66(0x03B10A08): 0x%08X\n", SR(0x03B10A08));
    printf("C2PMSG_82(0x03B10A48): 0x%08X\n", SR(0x03B10A48));

    CloseHandle(h);
    return 0;
}
