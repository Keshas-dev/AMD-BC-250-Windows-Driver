#include <windows.h>
#include <stdio.h>

#define IOCTL_AMDBC250_INIT_HARDWARE  0x80000B80
#define IOCTL_AMDBC250_READ_REG       0x80000B88
#define IOCTL_AMDBC250_WRITE_REG      0x80000B8C
#define IOCTL_AMDBC250_GPU_KIQ_TEST   0x80000BD0

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
    printf("=== VM Path Investigation (post-reboot) ===\n");
    g_h = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (g_h == INVALID_HANDLE_VALUE) { printf("Cannot open driver\n"); return 1; }

    UCHAR ii[32]={0}; *(UINT64*)ii=0xFE800000ULL; *(UINT32*)(ii+8)=0x80000; *(UINT32*)(ii+12)=1;
    *(UINT64*)(ii+16)=0xC0000000ULL; *(UINT32*)(ii+24)=0x10000000;
    UCHAR io[32]={0}; DWORD br=0;
    DeviceIoControl(g_h, IOCTL_AMDBC250_INIT_HARDWARE, ii, 32, io, 32, &br, NULL);
    printf("INIT_HARDWARE: OK\n\n");

    /* 1. GCVM state after clean boot */
    printf("--- 1. GCVM registers (fresh boot) ---\n");
    printf("  GCVM_CNTL  (0xB460) = 0x%08X\n", R(0x0B460));
    printf("  PT_BASE_LO (0xB608) = 0x%08X\n", R(0x0B608));
    printf("  PT_BASE_HI (0xB60C) = 0x%08X\n", R(0x0B60C));
    printf("  GCVM_L2_CNTL(0xB360)= 0x%08X\n", R(0x0B360));

    /* 2. Test PT_BASE writability on fresh boot */
    printf("\n--- 2. PT_BASE writability test ---\n");
    W(0x0B608, 0xDEADBEEF);
    W(0x0B60C, 0x12345678);
    printf("  Wrote 0x12345678DEADBEEF\n");
    printf("  Readback LO = 0x%08X HI = 0x%08X\n", R(0x0B608), R(0x0B60C));

    /* 3. Try clearing GCVM_CNTL bit 0 before writing PT_BASE */
    printf("\n--- 3. Disable GCVM, write PT_BASE, re-enable ---\n");
    UINT32 cntl = R(0x0B460);
    W(0x0B460, cntl & ~1u);  /* clear bit 0 */
    printf("  CNTL after clear bit0 = 0x%08X\n", R(0x0B460));
    W(0x0B608, 0xBBBBBBBB);
    W(0x0B60C, 0xAAAAAAAA);
    printf("  Wrote PT_BASE while disabled\n");
    printf("  Readback LO = 0x%08X HI = 0x%08X\n", R(0x0B608), R(0x0B60C));
    W(0x0B460, cntl);  /* restore */

    /* 4. Scan HDP MC_VM registers for flat mapping */
    printf("\n--- 4. HDP MC_VM registers (0x0500-0x05FF) ---\n");
    UINT32 hdp_alive[64];
    int nalive = 0;
    for (UINT32 off = 0x0500; off <= 0x05FC; off += 4) {
        UINT32 v = R(off);
        if (v != 0 && v != 0xFFFFFFFF) {
            hdp_alive[nalive*2] = off;
            hdp_alive[nalive*2+1] = v;
            nalive++;
        }
    }
    printf("  HDP alive registers: %d\n", nalive);
    for (int i = 0; i < nalive; i++) {
        printf("    0x%04X = 0x%08X\n", hdp_alive[i*2], hdp_alive[i*2+1]);
    }

    /* 5. Test HDP MC_VM writability */
    printf("\n--- 5. HDP MC_VM writability test ---\n");
    for (int i = 0; i < nalive; i++) {
        UINT32 off = hdp_alive[i*2];
        UINT32 orig = hdp_alive[i*2+1];
        W(off, 0xDEADBEEF);
        UINT32 readback = R(off);
        if (readback != orig) {
            printf("    0x%04X: 0x%08X -> wrote DEADBEEF -> 0x%08X %s\n",
                off, orig, readback, (readback == 0xDEADBEEF) ? "WRITABLE!" : "modified");
        }
    }

    /* 6. Check MMHUB VM registers (0x1A000-0x1BFFF) */
    printf("\n--- 6. MMHUB VM registers (0x1A000-0x1BFFF) ---\n");
    int mmh_alive = 0;
    for (UINT32 off = 0x1A000; off <= 0x1BFFC; off += 4) {
        UINT32 v = R(off);
        if (v != 0 && v != 0xFFFFFFFF) {
            if (mmh_alive < 80) printf("    0x%05X = 0x%08X\n", off, v);
            mmh_alive++;
        }
    }
    printf("  MMHUB alive registers: %d\n", mmh_alive);

    /* 7. Check MMHUB VM registers specifically (0x1B400-0x1B600) */
    printf("\n--- 7. MMHUB VM detail (0x1B400-0x1B600) ---\n");
    for (UINT32 off = 0x1B400; off <= 0x1B5FC; off += 4) {
        UINT32 v = R(off);
        if (v != 0) printf("    0x%05X = 0x%08X\n", off, v);
    }

    /* 8. Check for GCVM lock registers */
    printf("\n--- 8. GCVM near-registers scan (0x0B300-0x0B4FF) ---\n");
    for (UINT32 off = 0x0B300; off <= 0x0B4FC; off += 4) {
        UINT32 v = R(off);
        if (v != 0 && v != 0xFFFFFFFF)
            printf("    0x%05X = 0x%08X\n", off, v);
    }

    /* 9. Check GCVM registers above PT_BASE (0x0B610-0x0B700) */
    printf("\n--- 9. GCVM past PT_BASE (0x0B610-0x0B700) ---\n");
    for (UINT32 off = 0x0B610; off <= 0x0B700; off += 4) {
        UINT32 v = R(off);
        if (v != 0 && v != 0xFFFFFFFF)
            printf("    0x%05X = 0x%08X\n", off, v);
    }

    /* 10. ME_CNTL and CP state */
    printf("\n--- 10. CP state ---\n");
    printf("  ME_CNTL (0x4A74)   = 0x%08X\n", R(0x4A74));
    printf("  SCRATCH  (0x32D4)   = 0x%08X\n", R(0x32D4));
    printf("  GPU_ID   (0x0000)   = 0x%08X\n", R(0x0000));

    CloseHandle(g_h);
    printf("\n=== Done ===\n");
    return 0;
}
