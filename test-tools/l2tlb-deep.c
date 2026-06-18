#include <windows.h>
#include <stdio.h>

#define IOCTL_AMDBC250_INIT_HARDWARE  0x80000B80
#define IOCTL_AMDBC250_READ_REG       0x80000B88
#define IOCTL_AMDBC250_WRITE_REG      0x80000B8C

typedef struct { UINT32 Offset; UINT32 Value; } REG_OP;
static HANDLE g_h = INVALID_HANDLE_VALUE;

static UINT32 R(UINT32 off) {
    REG_OP in = {off,0}, out = {0}; DWORD br = 0;
    DeviceIoControl(g_h, IOCTL_AMDBC250_READ_REG, &in, sizeof(in), &out, sizeof(out), &br, NULL);
    return out.Value;
}

static void W(UINT32 off, UINT32 val) {
    REG_OP in = {off,val}, out = {0}; DWORD br = 0;
    DeviceIoControl(g_h, IOCTL_AMDBC250_WRITE_REG, &in, sizeof(in), &out, sizeof(out), &br, NULL);
}

int main() {
    printf("=== GCVM L2 TLB Deep Investigation ===\n");
    g_h = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (g_h == INVALID_HANDLE_VALUE) { printf("Cannot open driver\n"); return 1; }

    UCHAR ii[32]={0}; *(UINT64*)ii=0xFE800000ULL; *(UINT32*)(ii+8)=0x80000; *(UINT32*)(ii+12)=1;
    *(UINT64*)(ii+16)=0xC0000000ULL; *(UINT32*)(ii+24)=0x10000000;
    UCHAR io[32]={0}; DWORD br=0;
    DeviceIoControl(g_h, IOCTL_AMDBC250_INIT_HARDWARE, ii, 32, io, 32, &br, NULL);
    printf("INIT_HARDWARE: OK\n\n");

    /* 1. Dump entire GCVM block (0x0B300-0x0B700) with writability */
    printf("--- 1. GCVM block (0x0B300-0x0B700) — writability scan ---\n");
    printf("%-8s %-10s %-10s %-10s %s\n", "Offset", "Read", "Write", "After", "Status");
    for (UINT32 off = 0x0B300; off <= 0x0B700; off += 4) {
        UINT32 orig = R(off);
        if (orig == 0 && off < 0x0B31C) continue;  /* skip leading zeros */
        if (orig == 0 && off > 0x0B370 && off < 0x0B404) continue;
        if (orig == 0 && off > 0x0B4B0 && off < 0x0B4C0) continue;
        if (orig == 0 && off > 0x0B4D8 && off < 0x0B51C) continue;
        if (orig == 0 && off > 0x0B570 && off < 0x0B600) continue;
        if (orig == 0 && off > 0x0B610) continue;

        /* Try writing DEADBEEF */
        W(off, 0xDEADBEEF);
        UINT32 after = R(off);
        
        const char *status = "RO";
        if (after == 0xDEADBEEF) status = "FULL_WRITABLE";
        else if (after != orig) status = "MASKED";
        
        printf("0x%05X   0x%08X   DEADBEEF  0x%08X  %s\n", off, orig, after, status);
        
        /* Restore original */
        if (after != orig && after != 0xDEADBEEF) {
            W(off, orig);
        }
    }

    /* 2. Try GCVM_L2_CNTL changes */
    printf("\n--- 2. GCVM_L2_CNTL (0x0B360) experiments ---\n");
    UINT32 l2orig = R(0x0B360);
    printf("  Original: 0x%08X\n", l2orig);
    
    /* Try enabling L2 cache (bit 0) */
    W(0x0B360, l2orig | 1);
    printf("  Set bit0: 0x%08X\n", R(0x0B360));
    W(0x0B360, l2orig);

    /* 3. Check GCVM_CONTEXT0_CNTL bit fields */
    printf("\n--- 3. GCVM_CONTEXT0_CNTL (0x0B460) bit manipulation ---\n");
    UINT32 cntl = R(0x0B460);
    printf("  Original: 0x%08X\n", cntl);
    
    /* Try clearing all bits except bit 0 */
    W(0x0B460, 1);
    printf("  Set only bit0: 0x%08X\n", R(0x0B460));
    W(0x0B460, cntl);
    
    /* Try setting bit 31 (maybe a lock/disable bit?) */
    W(0x0B460, cntl | 0x80000000);
    printf("  Set bit31: 0x%08X\n", R(0x0B460));
    W(0x0B460, cntl);

    /* 4. Look for identity mapping registers in 0x0B400+ */
    printf("\n--- 4. GCVM identity/context registers (0x0B400-0x0B500) writability ---\n");
    for (UINT32 off = 0x0B400; off <= 0x0B500; off += 4) {
        UINT32 orig = R(off);
        if (orig == 0) continue;
        W(off, 0xDEADBEEF);
        UINT32 after = R(off);
        const char *status = "RO";
        if (after == 0xDEADBEEF) status = "FULL_WRITABLE";
        else if (after != orig) status = "MASKED";
        printf("  0x%05X: 0x%08X -> DEADBEEF -> 0x%08X [%s]\n", off, orig, after, status);
        if (after != orig && after != 0xDEADBEEF) W(off, orig);
    }

    /* 5. Check for flat mapping / identity mapping control */
    printf("\n--- 5. Search for flat mapping control ---\n");
    /* Try GCVM_CONTEXT0_CNTL with different flat mapping bits */
    for (int bit = 8; bit < 32; bit++) {
        UINT32 test = cntl | (1u << bit);
        W(0x0B460, test);
        UINT32 readback = R(0x0B460);
        if (readback != cntl) {
            printf("  bit%d: wrote 0x%08X, read 0x%08X %s\n", 
                bit, test, readback, (readback & (1u<<bit)) ? "STUCK" : "CLEARED");
        }
        W(0x0B460, cntl);
    }

    /* 6. Final: try to write PT_BASE after modifying CNTL */
    printf("\n--- 6. PT_BASE after CNTL modification ---\n");
    /* Try disabling context first */
    W(0x0B460, 0);  /* disable all */
    W(0x0B608, 0xDEADBEEF);
    printf("  CNTL=0: PT_BASE_LO = 0x%08X\n", R(0x0B608));
    W(0x0B460, cntl);  /* restore */

    /* 7. Check if PT_BASE is in a protected range */
    printf("\n--- 7. PT_BASE protection test ---\n");
    /* Try various patterns */
    UINT32 patterns[] = {0x00000000, 0x00001000, 0x00100000, 0x10000000, 0x40000000};
    for (int i = 0; i < 5; i++) {
        W(0x0B608, patterns[i]);
        UINT32 rb = R(0x0B608);
        if (rb != 0) printf("  Wrote 0x%08X -> 0x%08X\n", patterns[i], rb);
    }

    CloseHandle(g_h);
    printf("\n=== Done ===\n");
    return 0;
}
