/* gcvm-check-test.c — Read GCVM configuration to understand why DEFAULT_PAGE fails */

#include <windows.h>
#include <stdio.h>

#define IOCTL_GPU_READ  0x80000B88
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

int main(void) {
    printf("=== GCVM Configuration Check ===\n");

    h = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
                    0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("FAIL: Cannot open (err=%lu)\n", GetLastError());
        return 1;
    }

    /* Init with flag=1 (NBIO_MAP, skips DreamV3HwInitialize) */
    UCHAR init[32] = {0};
    *(unsigned __int64*)(init+0) = 0xFE800000ULL;
    *(unsigned*)(init+8) = 0x00080000;
    *(unsigned*)(init+12) = 1;
    *(unsigned __int64*)(init+16) = 0xC0000000ULL;
    *(unsigned*)(init+24) = 0x10000000;
    DWORD br = 0;
    DeviceIoControl(h, IOCTL_GPU_INIT, init, sizeof(init), NULL, 0, &br, NULL);
    printf("Init OK\n\n");

    /* GCVM L2 configuration */
    printf("--- GCVM L2 ---\n");
    ULONG l2Cntl = R(0x0B360);
    ULONG l2Cntl2 = R(0x0B364);
    ULONG l2Cntl3 = R(0x0B368);
    ULONG l2Cntl4 = R(0x0B36C);
    printf("  L2_CNTL    = 0x%08X\n", l2Cntl);
    printf("  L2_CNTL2   = 0x%08X\n", l2Cntl2);
    printf("  L2_CNTL3   = 0x%08X\n", l2Cntl3);
    printf("  L2_CNTL4   = 0x%08X\n", l2Cntl4);

    /* GCVM CONTEXT0 */
    printf("\n--- GCVM CONTEXT0 ---\n");
    ULONG ctx0Cntl = R(0x0B460);
    printf("  CTX0_CNTL  = 0x%08X\n", ctx0Cntl);
    printf("    ENABLE(0)     = %d\n", ctx0Cntl & 1);
    printf("    DEFAULT_PAGE(1) = %d\n", (ctx0Cntl >> 1) & 1);
    printf("    RETRY_FAULT(2)  = %d\n", (ctx0Cntl >> 2) & 1);
    
    /* PT_BASE - known to be hardware locked */
    ULONG ptBaseLo = R(0x0B608);
    ULONG ptBaseHi = R(0x0B60C);
    printf("  PT_BASE_LO = 0x%08X (locked=%s)\n",
           ptBaseLo, ptBaseLo == 0 ? "YES" : "NO");
    printf("  PT_BASE_HI = 0x%08X\n", ptBaseHi);

    /* CONTEXT0 page table registers */
    printf("\n--- CONTEXT0 Page Table Level Registers ---\n");
    ULONG ctx0PtRegs[] = {0x0B404, 0x0B408, 0x0B40C, 0x0B410, 0x0B414, 0x0B418, 0x0B41C};
    const char* ctx0PtNames[] = {
        "CTX0_CNTL_STACK", "CTX0_PT_BASE0_LO", "CTX0_PT_BASE0_HI",
        "CTX0_PT_BASE1_LO", "CTX0_PT_BASE1_HI",
        "CTX0_PT_BASE2_LO", "CTX0_PT_BASE2_HI"
    };
    for (int i = 0; i < 7; i++) {
        printf("  %s [0x%04X] = 0x%08X\n", ctx0PtNames[i], ctx0PtRegs[i], R(ctx0PtRegs[i]));
    }

    /* CONTEXT0 TLB registers */
    printf("\n--- CONTEXT0 TLB / Config Space ---\n");
    ULONG ctx0Cfg[] = {0x0B4C0, 0x0B4C4, 0x0B4C8, 0x0B4CC, 0x0B4D0, 0x0B4D4, 0x0B4D8, 0x0B4DC};
    const char* ctx0CfgNames[] = {
        "CFG0", "CFG1", "CFG2", "CFG3", "CFG4", "CFG5", "CFG6", "CFG7"
    };
    for (int i = 0; i < 8; i++) {
        ULONG val = R(ctx0Cfg[i]);
        if (val != 0xFFFFFFFF)
            printf("  %s [0x%04X] = 0x%08X\n", ctx0CfgNames[i], ctx0Cfg[i], val);
    }

    /* L2 TLB data registers — probably contain actual TLB entries */
    printf("\n--- L2 TLB Data ---\n");
    ULONG l2TlbData[] = {0x0B320, 0x0B324, 0x0B328, 0x0B32C, 0x0B330, 0x0B334, 0x0B338, 0x0B33C,
                          0x0B340, 0x0B344, 0x0B348, 0x0B34C, 0x0B350, 0x0B354, 0x0B358, 0x0B35C};
    for (int i = 0; i < 16; i += 2) {
        ULONG lo = R(l2TlbData[i]);
        ULONG hi = R(l2TlbData[i+1]);
        if (lo != 0xFFFFFFFF || hi != 0xFFFFFFFF)
            printf("  L2_TLB_DATA[%d] = 0x%08X_%08X\n", i/2, hi, lo);
    }

    /* CONTEXT0 TLB (per-context TLB) */
    printf("\n--- Context TLB Entries ---\n");
    /* Context TLB might be at 0x0B408-0x0B4AC or different range */
    for (ULONG off = 0x0B408; off < 0x0B4B0; off += 8) {
        ULONG lo = R(off);
        ULONG hi = R(off + 4);
        if (lo != 0xFFFFFFFF || hi != 0xFFFFFFFF) {
            printf("  CTX0_TLB[0x%04X] = 0x%08X_%08X\n", off, hi, lo);
        }
    }

    /* Check if there's an INVALIDATE or FLUSH register */
    printf("\n--- TLB Control/Status ---\n");
    ULONG inv = R(0x0B310);
    ULONG invHi = R(0x0B314);
    ULONG status = R(0x0B318);
    ULONG tag = R(0x0B31C);
    printf("  INV_LO  (0x0B310) = 0x%08X\n", inv);
    printf("  INV_HI  (0x0B314) = 0x%08X\n", invHi);
    printf("  STATUS  (0x0B318) = 0x%08X\n", status);
    printf("  TAG     (0x0B31C) = 0x%08X\n", tag);

    /* SYSTEM APERTURE registers - key for flat mapping */
    printf("\n--- System Aperture ---\n");
    /* These offsets need to be verified - looking for SYSTEM_APERTURE_LOW/HIGH/DEFAULT */
    ULONG sysAptOffsets[] = {0x0B380, 0x0B384, 0x0B388, 0x0B38C, 0x0B390, 0x0B394, 0x0B398, 0x0B39C, 0x0B3A0};
    const char* sysAptNames[] = {
        "LOW_ADDR", "HIGH_ADDR", "DEFAULT_ADDR_SYS", "DEFAULT_ADDR_RET",
        "LOW_ADDR1", "HIGH_ADDR1", "DEFAULT_ADDR1_SYS", "DEFAULT_ADDR1_RET",
        "UNKN"
    };
    for (int i = 0; i < 9; i++) {
        ULONG val = R(sysAptOffsets[i]);
        if (val != 0xFFFFFFFF)
            printf("  SYS_APERTURE_%s [0x%04X] = 0x%08X\n", sysAptNames[i], sysAptOffsets[i], val);
    }

    /* Check L2_CNTL system aperture bits */
    printf("\n--- L2_CNTL System Aperture Bits ---\n");
    printf("  Bit 17 (ENABLE_SYS_APERTURE) = %d\n", (l2Cntl >> 17) & 1);
    printf("  Bits 18-19 (SYS_APERTURE_MODE) = %d\n", (l2Cntl >> 18) & 3);
    printf("  Bits 20-21 (SYS_APERTURE_BYPASS) = %d\n", (l2Cntl >> 20) & 3);

    /* MC_VM registers - framebuffer location */
    printf("\n--- MC VM (Memory Controller) ---\n");
    /* FB location registers - typical Navi10 offsets adjusted for BC-250 */
    ULONG mcVmOffsets[] = {0x0B100, 0x0B104, 0x0B108, 0x0B10C, 0x0B110, 0x0B114, 0x0B118, 0x0B11C};
    const char* mcVmNames[] = {
        "VM_L2_PROTECTION_FAULT_DEFAULT_ADDR_LO32",
        "VM_L2_PROTECTION_FAULT_DEFAULT_ADDR_HI32",
        "VM_L2_PROTECTION_FAULT_CNTL",
        "VM_L2_PROTECTION_FAULT_STATUS",
        "VM_L2_PROTECTION_FAULT_ADDR_LO32",
        "VM_L2_PROTECTION_FAULT_ADDR_HI32",
        "VM_L2_PROTECTION_FAULT_CNTL2",
        "UNKN"
    };
    for (int i = 0; i < 8; i++) {
        ULONG val = R(mcVmOffsets[i]);
        if (val != 0xFFFFFFFF)
            printf("  %s [0x%04X] = 0x%08X\n", mcVmNames[i], mcVmOffsets[i], val);
    }

    CloseHandle(h);
    printf("\n=== Done ===\n");
    return 0;
}