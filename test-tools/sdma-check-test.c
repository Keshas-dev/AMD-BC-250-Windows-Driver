/* sdma-check-test.c — Check SDMA ring status and test SDMA copy/fill IOCTLs */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define IOCTL_GPU_READ     0x80000B88
#define IOCTL_GPU_WRITE    0x80000B8C
#define IOCTL_GPU_INIT     0x80000B80
#define IOCTL_SDMA_COPY    0x80000940
#define IOCTL_SDMA_FILL    0x80000944

static HANDLE h;

static ULONG R(ULONG off) {
    UCHAR buf[8] = {0};
    *(ULONG*)(buf+0) = off;
    *(ULONG*)(buf+4) = 0xDEADBEEF;
    DWORD br = 0;
    if (!DeviceIoControl(h, IOCTL_GPU_READ, buf, 8, buf, 8, &br, NULL))
        return 0xBAD0C0DE;
    return *(ULONG*)(buf+4);
}

static BOOL W(ULONG off, ULONG val) {
    UCHAR buf[8] = {0};
    *(ULONG*)(buf+0) = off;
    *(ULONG*)(buf+4) = val;
    DWORD br = 0;
    return DeviceIoControl(h, IOCTL_GPU_WRITE, buf, 8, buf, 8, &br, NULL);
}

static void Init(void) {
    UCHAR init[32] = {0};
    *(unsigned __int64*)(init+0) = 0xFE800000ULL;
    *(unsigned*)(init+8) = 0x00080000;
    *(unsigned*)(init+12) = 1;
    *(unsigned __int64*)(init+16) = 0xC0000000ULL;
    *(unsigned*)(init+24) = 0x10000000;
    DWORD br = 0;
    DeviceIoControl(h, IOCTL_GPU_INIT, init, sizeof(init), NULL, 0, &br, NULL);
}

int main(void) {
    printf("=== SDMA Check Test ===\n");

    h = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
                    0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("FAIL: Cannot open (err=%lu)\n", GetLastError());
        return 1;
    }

    Init();
    printf("Init OK\n\n");

    /* Check SDMA ring register status */
    printf("--- SDMA GFX Ring Registers ---\n");
    ULONG baseLo  = R(0xE000);  /* AMDBC250_REG_SDMA0_GFX_RB_BASE_LO */
    ULONG baseHi  = R(0xE004);  /* AMDBC250_REG_SDMA0_GFX_RB_BASE_HI */
    ULONG cntl    = R(0xE008);  /* AMDBC250_REG_SDMA0_GFX_RB_CNTL */
    ULONG rptr    = R(0xE00C);  /* AMDBC250_REG_SDMA0_GFX_RB_RPTR */
    ULONG wptr    = R(0xE010);  /* AMDBC250_REG_SDMA0_GFX_RB_WPTR */
    ULONG sdmaCntl = R(0xE018); /* AMDBC250_REG_SDMA0_CNTL */
    
    printf("  RB_BASE_LO  (0xE000) = 0x%08X\n", baseLo);
    printf("  RB_BASE_HI  (0xE004) = 0x%08X\n", baseHi);
    printf("  RB_CNTL     (0xE008) = 0x%08X\n", cntl);
    printf("  RB_RPTR     (0xE00C) = 0x%08X\n", rptr);
    printf("  RB_WPTR     (0xE010) = 0x%08X\n", wptr);
    printf("  CNTL        (0xE018) = 0x%08X\n", sdmaCntl);
    
    ULONG64 ringPa = ((ULONG64)baseHi << 32) | (ULONG64)(baseLo << 8);
    printf("  Ring PA (reconstructed) = 0x%llX\n", ringPa);
    printf("  Ring initialized: %s\n", (baseLo != 0 || baseHi != 0) ? "YES (by BIOS)" : "NO");

    /* Try SDMA copy IOCTL if ring seems initialized */
    if (baseLo != 0 || baseHi != 0) {
        printf("\n--- SDMA Copy Test ---\n");
        UCHAR buf[128] = {0};
        /* SDMA Copy struct: src_pa, dst_pa, size */
        *(ULONG64*)(buf+0) = 0;   /* src (unused) */
        *(ULONG64*)(buf+8) = 0;   /* dst (unused) */
        *(ULONG*)(buf+16) = 64;   /* size */
        DWORD br = 0;
        BOOL ok = DeviceIoControl(h, IOCTL_SDMA_COPY, buf, sizeof(buf), NULL, 0, &br, NULL);
        printf("  SDMA_COPY IOCTL: %s\n", ok ? "OK" : "FAILED");
    }

    CloseHandle(h);
    printf("\n=== Done ===\n");
    return 0;
}