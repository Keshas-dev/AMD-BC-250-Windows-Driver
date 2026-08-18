#include <windows.h>
#include <stdio.h>
#include "..\inc\amdbc250_ioctl.h"

int main() {
    HANDLE hDev = CreateFileA("\\\\.\\AMDBC250Reg", GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hDev == INVALID_HANDLE_VALUE) {
        printf("CreateFile FAIL: %lu\n", GetLastError());
        return 1;
    }
    printf("CreateFile OK\n\n");

    AMDBC250_IOCTL_REG_ACCESS r;
    DWORD ret = 0;
    BOOL ok;

    // ===== CC_GC_SHADER_ARRAY_CONFIG =====
    // Linux address: 0x529C (GC_BASE + 0x100f*4)
    // Our hw.h: 0x9C1C (GC_BASE + 0x226F*4)
    printf("=== CC_GC_SHADER_ARRAY_CONFIG ===\n");
    
    r.RegisterOffset = 0x529C; r.Value = 0;
    ok = DeviceIoControl(hDev, IOCTL_AMDBC250_READ_REG, &r, sizeof(r), &r, sizeof(r), &ret, NULL);
    printf("READ  0x529C (Linux): val=0x%08X ok=%d\n", r.Value, ok);
    
    r.RegisterOffset = 0x9C1C; r.Value = 0;
    ok = DeviceIoControl(hDev, IOCTL_AMDBC250_READ_REG, &r, sizeof(r), &r, sizeof(r), &ret, NULL);
    printf("READ  0x9C1C (hw.h):  val=0x%08X ok=%d\n", r.Value, ok);

    // Write 0 to both
    r.RegisterOffset = 0x529C; r.Value = 0;
    ok = DeviceIoControl(hDev, IOCTL_AMDBC250_WRITE_REG, &r, sizeof(r), &r, sizeof(r), &ret, NULL);
    printf("WRITE 0x529C = 0:      ok=%d\n", ok);
    r.RegisterOffset = 0x529C; r.Value = 0;
    DeviceIoControl(hDev, IOCTL_AMDBC250_READ_REG, &r, sizeof(r), &r, sizeof(r), &ret, NULL);
    printf("READ  0x529C after:    val=0x%08X\n", r.Value);
    
    r.RegisterOffset = 0x9C1C; r.Value = 0;
    ok = DeviceIoControl(hDev, IOCTL_AMDBC250_WRITE_REG, &r, sizeof(r), &r, sizeof(r), &ret, NULL);
    printf("WRITE 0x9C1C = 0:      ok=%d\n", ok);
    r.RegisterOffset = 0x9C1C; r.Value = 0;
    DeviceIoControl(hDev, IOCTL_AMDBC250_READ_REG, &r, sizeof(r), &r, sizeof(r), &ret, NULL);
    printf("READ  0x9C1C after:    val=0x%08X\n", r.Value);

    printf("\n");

    // ===== SPI_PG_ENABLE_STATIC_WGP_MASK =====
    // Linux: 0x5C3C (GC_BASE + 0x1277*4)
    printf("=== SPI_PG_ENABLE_STATIC_WGP_MASK ===\n");
    
    r.RegisterOffset = 0x5C3C; r.Value = 0;
    ok = DeviceIoControl(hDev, IOCTL_AMDBC250_READ_REG, &r, sizeof(r), &r, sizeof(r), &ret, NULL);
    printf("READ  0x5C3C:          val=0x%08X ok=%d\n", r.Value, ok);
    
    r.RegisterOffset = 0x5C3C; r.Value = 0x1F;
    ok = DeviceIoControl(hDev, IOCTL_AMDBC250_WRITE_REG, &r, sizeof(r), &r, sizeof(r), &ret, NULL);
    printf("WRITE 0x5C3C = 0x1F:   ok=%d\n", ok);
    r.RegisterOffset = 0x5C3C; r.Value = 0;
    DeviceIoControl(hDev, IOCTL_AMDBC250_READ_REG, &r, sizeof(r), &r, sizeof(r), &ret, NULL);
    printf("READ  0x5C3C after:    val=0x%08X\n", r.Value);

    printf("\n");

    // ===== RLC_PG_ALWAYS_ON_WGP_MASK =====
    // Our hw.h: 0x3D64 (GC_BASE + 0x0B04) — need Linux address
    printf("=== RLC_PG_ALWAYS_ON_WGP_MASK ===\n");
    
    r.RegisterOffset = 0x3D64; r.Value = 0;
    ok = DeviceIoControl(hDev, IOCTL_AMDBC250_READ_REG, &r, sizeof(r), &r, sizeof(r), &ret, NULL);
    printf("READ  0x3D64:          val=0x%08X ok=%d\n", r.Value, ok);
    
    r.RegisterOffset = 0x3D64; r.Value = 0x1F;
    ok = DeviceIoControl(hDev, IOCTL_AMDBC250_WRITE_REG, &r, sizeof(r), &r, sizeof(r), &ret, NULL);
    printf("WRITE 0x3D64 = 0x1F:   ok=%d\n", ok);
    r.RegisterOffset = 0x3D64; r.Value = 0;
    DeviceIoControl(hDev, IOCTL_AMDBC250_READ_REG, &r, sizeof(r), &r, sizeof(r), &ret, NULL);
    printf("READ  0x3D64 after:    val=0x%08X\n", r.Value);

    CloseHandle(hDev);
    printf("\nDONE\n");
    return 0;
}
