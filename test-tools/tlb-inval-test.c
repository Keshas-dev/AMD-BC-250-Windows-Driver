/* tlb-inval-test.c — Test TLB invalidation and shadow copy theory */

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
    printf("=== TLB Invalidation + Shadow Copy Test ===\n");

    h = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
                    0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { printf("FAIL: Cannot open\n"); return 1; }
    Init();

    /* Save originals */
    ULONG origCtx0Cntl   = R(0x0B460);
    ULONG origPtBase     = R(0x0B608);
    ULONG origPtBaseHi   = R(0x0B60C);
    ULONG origPtBase0_lo = R(0x0B408);
    ULONG origPtBase0_hi = R(0x0B40C);
    ULONG origInvLo      = R(0x0B310);
    ULONG origStatus     = R(0x0B318);
    ULONG origInv6C0C    = R(0x6C0C);
    ULONG origInv6C10    = R(0x6C10);

    printf("\n=== BEFORE ===\n");
    printf("  CTX0_CNTL    (0xB460) = 0x%08X\n", origCtx0Cntl);
    printf("  PT_BASE_LO   (0xB608) = 0x%08X\n", origPtBase);
    printf("  PT_BASE_HI   (0xB60C) = 0x%08X\n", origPtBaseHi);
    printf("  PT_BASE0_LO  (0xB408) = 0x%08X\n", origPtBase0_lo);
    printf("  PT_BASE0_HI  (0xB40C) = 0x%08X\n", origPtBase0_hi);
    printf("  INV_LO       (0xB310) = 0x%08X\n", origInvLo);
    printf("  STATUS       (0xB318) = 0x%08X\n", origStatus);
    printf("  INV_6C0C     (0x6C0C) = 0x%08X\n", origInv6C0C);
    printf("  INV_6C10     (0x6C10) = 0x%08X\n", origInv6C10);

    /* Test 1: Write test address to PT_BASE0, then trigger invalidation */
    printf("\n=== TEST 1: Write PT_BASE0 + TLB Inval ===\n");
    printf("  Writing 0x12345000 to PT_BASE0_LO (4KB-aligned test pattern)...\n");
    W(0x0B408, 0x12345000);
    printf("  Read back: 0x%08X\n", R(0x0B408));

    printf("  Writing 0x00000000 to PT_BASE0_HI...\n");
    W(0x0B40C, 0x00000000);
    printf("  Read back: 0x%08X\n", R(0x0B40C));

    /* Disable context first (Gemini's step) */
    printf("  Disabling CTX0_CNTL...\n");
    W(0x0B460, 0);
    printf("  CTX0_CNTL now = 0x%08X\n", R(0x0B460));

    /* Re-enable context for second attempt */
    printf("  Re-enabling CTX0_CNTL (ENABLE=1)...\n");
    W(0x0B460, 1);
    printf("  CTX0_CNTL now = 0x%08X\n", R(0x0B460));

    /* Try TLB invalidation via 0x0B310 (INV_LO) with context enabled */
    printf("\n  TLB invalidation via 0x0B310 = 1 (context enabled)...\n");
    W(0x0B310, 1);
    for (int t = 0; t < 100; t++) { /* poll up to 100ms */
        ULONG st = R(0x0B318);
        if (st & 1) { printf("  STATUS bit 0 set at t=%d\n", t); break; }
        Sleep(1);
    }
    ULONG invLoAfter = R(0x0B310);
    ULONG statusAfter = R(0x0B318);
    printf("  INV_LO  = 0x%08X\n", invLoAfter);
    printf("  STATUS  = 0x%08X\n", statusAfter);

    /* Try TLB invalidation via 0x6C0C (Linux VM_INVALIDATE_ENG0_REQ)
       First clear ACK, then trigger request, then poll for ACK */
    printf("\n  Clearing ACK via 0x6C10 = 1...\n");
    W(0x6C10, 1);
    Sleep(1);
    printf("  INV_6C10 after clear = 0x%08X\n", R(0x6C10));

    printf("  TLB invalidation via 0x6C0C = 1...\n");
    W(0x6C0C, 1);
    for (int t = 0; t < 100; t++) { /* poll for ACK up to 100ms */
        ULONG ack = R(0x6C10);
        if (ack & 1) { printf("  ACK received at t=%d!\n", t); break; }
        Sleep(1);
    }
    ULONG inv6C0C_after = R(0x6C0C);
    ULONG inv6C10_after = R(0x6C10);
    printf("  INV_6C0C = 0x%08X\n", inv6C0C_after);
    printf("  INV_6C10 = 0x%08X\n", inv6C10_after);

    /* Check PT_BASE after invalidation */
    ULONG ptBaseAfter = R(0x0B608);
    ULONG ptBaseHiAfter = R(0x0B60C);
    printf("\n=== AFTER INVAL ===\n");
    printf("  PT_BASE_LO = 0x%08X (was 0x%08X) %s\n",
           ptBaseAfter, origPtBase,
           (ptBaseAfter != origPtBase) ? "** CHANGED! **" : "unchanged");
    printf("  PT_BASE_HI = 0x%08X (was 0x%08X) %s\n",
           ptBaseHiAfter, origPtBaseHi,
           (ptBaseHiAfter != origPtBaseHi) ? "** CHANGED! **" : "unchanged");

    /* Check PT_BASE0 after (did invalidation clear it?) */
    printf("  PT_BASE0_LO = 0x%08X (was 0x%08X)\n", R(0x0B408), 0x12345000UL);
    printf("  PT_BASE0_HI = 0x%08X (was 0x%08X)\n", R(0x0B40C), 0x00000000UL);

    /* Restore */
    printf("\n=== RESTORE ===\n");
    W(0x0B460, origCtx0Cntl);
    W(0x0B408, origPtBase0_lo);
    W(0x0B40C, origPtBase0_hi);
    W(0x0B608, origPtBase);
    W(0x0B60C, origPtBaseHi);

    printf("  CTX0_CNTL restore:  0x%08X %s\n", R(0x0B460),
           R(0x0B460) == origCtx0Cntl ? "OK" : "FAIL");
    printf("  PT_BASE0_LO restore: 0x%08X %s\n", R(0x0B408),
           R(0x0B408) == origPtBase0_lo ? "OK" : "FAIL");

    /* Final PT_BASE check */
    printf("\n  FINAL PT_BASE_LO = 0x%08X\n", R(0x0B608));
    printf("  FINAL PT_BASE_HI = 0x%08X\n", R(0x0B60C));

    CloseHandle(h);
    printf("\n=== Done ===\n");
    return 0;
}