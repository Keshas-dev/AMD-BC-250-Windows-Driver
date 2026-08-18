#include <windows.h>
#include <stdio.h>
#include "..\inc\amdbc250_ioctl.h"

int main() {
    HANDLE hDev = CreateFileA("\\\\.\\AMDBC250Reg", GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hDev == INVALID_HANDLE_VALUE) { printf("CreateFile FAIL: %lu\n", GetLastError()); return 1; }
    printf("CreateFile OK\n\n");

    AMDBC250_IOCTL_REG_ACCESS r; DWORD ret; BOOL ok;
    #define READ(off) r.RegisterOffset=off; r.Value=0; ok=DeviceIoControl(hDev,IOCTL_AMDBC250_READ_REG,&r,sizeof(r),&r,sizeof(r),&ret,NULL); printf("READ  0x%04X: val=0x%08X ok=%d\n", off, r.Value, ok);
    #define WRITE(off,val) r.RegisterOffset=off; r.Value=val; ok=DeviceIoControl(hDev,IOCTL_AMDBC250_WRITE_REG,&r,sizeof(r),&r,sizeof(r),&ret,NULL); printf("WRITE 0x%04X=0x%X: ok=%d\n", off, val, ok);
    #define READAFTER(off) r.RegisterOffset=off; r.Value=0; ok=DeviceIoControl(hDev,IOCTL_AMDBC250_READ_REG,&r,sizeof(r),&r,sizeof(r),&ret,NULL); printf("READ  0x%04X after: val=0x%08X\n", off, r.Value);

    printf("=== SPI_PG_ENABLE_STATIC_WGP_MASK ===\n");
    READ(0x5C3C); WRITE(0x5C3C, 0x1F); READAFTER(0x5C3C);
    READ(0x24D7); WRITE(0x24D7, 0x1F); READAFTER(0x24D7);
    READ(0xFB3C); WRITE(0xFB3C, 0x1F); READAFTER(0xFB3C);
    printf("\n");

    printf("=== CC_GC_SHADER_ARRAY_CONFIG ===\n");
    READ(0x9C1C); WRITE(0x9C1C, 0); READAFTER(0x9C1C);
    READ(0x529C); WRITE(0x529C, 0); READAFTER(0x529C);
    printf("\n");

    printf("=== RLC_PG_ALWAYS_ON_WGP_MASK ===\n");
    READ(0x3D64); WRITE(0x3D64, 0x1F); READAFTER(0x3D64);
    READ(0x5EB3); WRITE(0x5EB3, 0x1F); READAFTER(0x5EB3);
    READ(0x1E3AC); WRITE(0x1E3AC, 0x1F); READAFTER(0x1E3AC);
    printf("\n");

    printf("=== GRBM (test if writable) ===\n");
    READ(0x34D0); WRITE(0x34D0, 0x100); READAFTER(0x34D0);
    READ(0x9A60); WRITE(0x9A60, 0x100); READAFTER(0x9A60);
    READ(0x13A60); WRITE(0x13A60, 0x100); READAFTER(0x13A60);

    CloseHandle(hDev);
    printf("\nDONE\n");
    return 0;
}
