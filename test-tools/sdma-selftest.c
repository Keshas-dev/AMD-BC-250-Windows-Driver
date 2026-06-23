/* sdma-selftest.c — Test SDMA self-test IOCTL (0x80000988) */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define IOCTL_GPU_READ     0x80000B88
#define IOCTL_GPU_WRITE    0x80000B8C
#define IOCTL_GPU_INIT     0x80000B80
#define IOCTL_SDMA_SELFTEST 0x80000988
#define IOCTL_SDMA_COPY    0x80000940

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
    printf("=== SDMA Self-Test ===\n");

    h = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
                    0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("FAIL: Cannot open (err=%lu)\n", GetLastError());
        return 1;
    }

    Init();
    printf("Init OK\n\n");

    /* Check SDMA ring */
    printf("--- SDMA Regs Before ---\n");
    printf("  RB_BASE_LO = 0x%08X\n", R(0xE000));
    printf("  RB_CNTL    = 0x%08X\n", R(0xE008));
    printf("  RB_WPTR    = 0x%08X\n", R(0xE010));

    /* Call SDMA self-test */
    printf("\n--- SDMA Self-Test IOCTL ---\n");
    ULONG result = 0;
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_SDMA_SELFTEST, NULL, 0, &result, sizeof(result), &br, NULL);
    printf("  IOCTL: %s (br=%lu)\n", ok ? "OK" : "FAILED", br);

    if (ok && br >= sizeof(ULONG)) {
        printf("  Result: 0x%08X\n", result);
        if (result == 0x600DCAFE) {
            printf("\n*** SDMA SELF-TEST PASSED! ***\n");
        } else if (result == 0xDEAD0001) {
            printf("  FAIL: source allocation failed\n");
        } else if (result == 0xDEAD0002) {
            printf("  FAIL: destination allocation failed\n");
        } else if (result == 0xDEADBEEF) {
            printf("  FAIL: data mismatch (SDMA copied but data corrupt)\n");
        } else {
            printf("  FAIL: SDMA copy returned NTSTATUS=0x%08X\n", result);
        }
    } else {
        printf("  FAIL: IOCTL call error (err=%lu)\n", GetLastError());
    }

    /* Check SDMA registers after */
    printf("\n--- SDMA Regs After ---\n");
    printf("  RB_WPTR    = 0x%08X\n", R(0xE010));
    printf("  RB_RPTR    = 0x%08X\n", R(0xE00C));

    CloseHandle(h);
    printf("\n=== Done ===\n");
    return 0;
}
