/* pt-base-test.c — Test if PT_BASE is writable after disabling GCVM context */
/* Step 1: Disable CTX0_CNTL, Step 2: Try write PT_BASE, Step 3: Restore */

#include <windows.h>
#include <stdio.h>

#define IOCTL_GPU_READ  0x80000B88
#define IOCTL_GPU_WRITE 0x80000B8C
#define IOCTL_GPU_INIT  0x80000B80

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
    printf("=== PT_BASE Unlock Test ===\n");

    h = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
                    0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("FAIL: Cannot open (err=%lu)\n", GetLastError());
        return 1;
    }

    Init();
    printf("Init OK\n\n");

    /* Save original state */
    ULONG origCtx0Cntl  = R(0x0B460);
    ULONG origPtBaseLo  = R(0x0B608);
    ULONG origPtBaseHi  = R(0x0B60C);
    
    printf("=== INITIAL STATE ===\n");
    printf("  CTX0_CNTL  (0x0B460) = 0x%08X (ENABLE=%d, DEFAULT_PAGE=%d)\n",
           origCtx0Cntl, origCtx0Cntl & 1, (origCtx0Cntl >> 1) & 1);
    printf("  PT_BASE_LO (0x0B608) = 0x%08X\n", origPtBaseLo);
    printf("  PT_BASE_HI (0x0B60C) = 0x%08X\n", origPtBaseHi);

    /* Step 1: Disable context by writing 0 to CTX0_CNTL */
    printf("\n=== Step 1: Disable CTX0_CNTL ===\n");
    printf("  Writing 0x00000000 to CTX0_CNTL...\n");
    if (!W(0x0B460, 0)) {
        printf("  WRITE FAILED!\n");
        return 1;
    }
    ULONG ctxAfter = R(0x0B460);
    printf("  CTX0_CNTL now = 0x%08X (ENABLE=%d)\n", ctxAfter, ctxAfter & 1);
    
    /* Step 2: Try reading PT_BASE again */
    printf("\n=== Step 2: Check PT_BASE after disable ===\n");
    ULONG ptLoAfterDisable = R(0x0B608);
    ULONG ptHiAfterDisable = R(0x0B60C);
    printf("  PT_BASE_LO = 0x%08X\n", ptLoAfterDisable);
    printf("  PT_BASE_HI = 0x%08X\n", ptHiAfterDisable);
    
    if (ptLoAfterDisable == origPtBaseLo && ptHiAfterDisable == origPtBaseHi) {
        printf("  -> Unchanged (may be HW-locked or read-only)\n");
    } else {
        printf("  -> CHANGED! PT_BASE is software-gated!\n");
    }

    /* Step 3: Try WRITING a test pattern to PT_BASE */
    printf("\n=== Step 3: Write test pattern to PT_BASE ===\n");
    ULONG testPattern = 0x12345678;
    printf("  Writing 0x%08X to PT_BASE_LO...\n", testPattern);
    if (!W(0x0B608, testPattern)) {
        printf("  WRITE FAILED! PT_BASE is truly HW locked.\n");
    } else {
        ULONG ptAfterWrite = R(0x0B608);
        printf("  Read back: 0x%08X\n", ptAfterWrite);
        if (ptAfterWrite == testPattern) {
            printf("  -> PT_BASE IS WRITABLE after disabling context!\n");
            printf("  *** MAJOR FINDING: Gemini was correct! ***\n");
        } else {
            printf("  -> PT_BASE did NOT accept write (still locked)\n");
        }
    }

    /* Step 4: Restore original values */
    printf("\n=== Step 4: Restore ===\n");
    printf("  Writing back PT_BASE_LO = 0x%08X... ", origPtBaseLo);
    W(0x0B608, origPtBaseLo);
    printf("OK\n  Writing back PT_BASE_HI = 0x%08X... ", origPtBaseHi);
    W(0x0B60C, origPtBaseHi);
    printf("OK\n  Writing back CTX0_CNTL = 0x%08X... ", origCtx0Cntl);
    W(0x0B460, origCtx0Cntl);
    printf("OK\n");

    /* Verify restore */
    ULONG finalPtLo = R(0x0B608);
    ULONG finalPtHi = R(0x0B60C);
    ULONG finalCtx = R(0x0B460);
    printf("\n=== VERIFY RESTORE ===\n");
    printf("  CTX0_CNTL  = 0x%08X (%s)\n", finalCtx, finalCtx == origCtx0Cntl ? "MATCH" : "DIFF");
    printf("  PT_BASE_LO = 0x%08X (%s)\n", finalPtLo, finalPtLo == origPtBaseLo ? "MATCH" : "DIFF");
    printf("  PT_BASE_HI = 0x%08X (%s)\n", finalPtHi, finalPtHi == origPtBaseHi ? "MATCH" : "DIFF");

    CloseHandle(h);
    printf("\n=== Test Complete ===\n");
    return 0;
}