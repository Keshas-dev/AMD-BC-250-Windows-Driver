/* sdma-wptrtest.c — Test SDMA WPTR writability and ring base layout */

#include <windows.h>
#include <stdio.h>

#define IOCTL_GPU_READ     0x80000B88
#define IOCTL_GPU_WRITE    0x80000B8C
#define IOCTL_GPU_INIT     0x80000B80

static HANDLE h;

static ULONG R(ULONG off) {
    UCHAR buf[8] = {0};
    *(ULONG*)(buf+0) = off;
    *(ULONG*)(buf+4) = 0xBAD0C0DE;
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
    printf("=== SDMA WPTR Writability Test ===\n");

    h = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
                    0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("FAIL: Cannot open (err=%lu)\n", GetLastError());
        return 1;
    }

    Init();
    printf("Init OK\n\n");

    /* Read current values */
    printf("--- SDMA Regs Before Write Test ---\n");
    printf("  RB_BASE_LO (0xE000) = 0x%08X\n", R(0xE000));
    printf("  RB_BASE_HI (0xE004) = 0x%08X\n", R(0xE004));
    printf("  RB_CNTL    (0xE008) = 0x%08X\n", R(0xE008));
    printf("  RB_RPTR    (0xE00C) = 0x%08X\n", R(0xE00C));
    printf("  RB_WPTR    (0xE010) = 0x%08X\n", R(0xE010));

    /* Test 1: Write WPTR with test pattern */
    printf("\n--- Test 1: Write WPTR = 0x12345678 ---\n");
    if (W(0xE010, 0x12345678)) {
        printf("  Write OK\n");
    } else {
        printf("  Write FAILED\n");
    }
    printf("  RB_WPTR = 0x%08X\n", R(0xE010));

    /* Test 2: Write BASE_LO low test pattern */
    printf("\n--- Test 2: Write BASE_LO = 0xDEAD0000 ---\n");
    if (W(0xE000, 0xDEAD0000)) {
        printf("  Write OK\n");
    }
    printf("  RB_BASE_LO = 0x%08X\n", R(0xE000));

    /* Test 3: Disable ring, then write BASE_LO */
    printf("\n--- Test 3: Disable ring -> write BASE_LO -> re-enable ---\n");
    ULONG cntl = R(0xE008);
    printf("  RB_CNTL before = 0x%08X\n", cntl);
    W(0xE008, 0);  /* Disable */
    printf("  RB_CNTL after disable = 0x%08X\n", R(0xE008));
    W(0xE000, 0xCAFE0000);  /* Write BASE_LO */
    printf("  RB_BASE_LO after write = 0x%08X\n", R(0xE000));
    W(0xE008, cntl);  /* Restore CNTL */
    printf("  RB_CNTL after restore = 0x%08X\n", R(0xE008));
    printf("  RB_BASE_LO final = 0x%08X\n", R(0xE000));

    /* Test 4: Write RPTR */
    printf("\n--- Test 4: Write RPTR = 0xAAAAAAAA ---\n");
    W(0xE00C, 0xAAAAAAAA);
    printf("  RB_RPTR = 0x%08X\n", R(0xE00C));

    /* Restore WPTR to 0 */
    W(0xE010, 0);

    printf("\n=== Done ===\n");
    CloseHandle(h);
    return 0;
}
