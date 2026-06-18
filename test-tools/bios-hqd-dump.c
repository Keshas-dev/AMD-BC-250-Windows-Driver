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
    printf("=== BIOS HQD State + GCVM L2 Cache Dump ===\n");
    g_h = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (g_h == INVALID_HANDLE_VALUE) { printf("Cannot open driver\n"); return 1; }

    UCHAR ii[32]={0}; *(UINT64*)ii=0xFE800000ULL; *(UINT32*)(ii+8)=0x80000; *(UINT32*)(ii+12)=1;
    *(UINT64*)(ii+16)=0xC0000000ULL; *(UINT32*)(ii+24)=0x10000000;
    UCHAR io[32]={0}; DWORD br=0;
    DeviceIoControl(g_h, IOCTL_AMDBC250_INIT_HARDWARE, ii, 32, io, 32, &br, NULL);
    printf("INIT_HARDWARE: OK\n");

    /* 1. BIOS HQD state (before GPU_KIQ_TEST destroys it) */
    printf("\n--- 1. BIOS HQD state (GRBM broadcast) ---\n");
    W(0x34D0, 0xE0000000);  /* GRBM_INDEX = broadcast */
    printf("  HQD_ACTIVE   (0x1C00) = 0x%08X\n", R(0x1C00));
    printf("  HQD_PQ_BASE  (0x1C04) = 0x%08X\n", R(0x1C04));
    printf("  HQD_PQ_BASE_HI(0x1C08)= 0x%08X\n", R(0x1C08));
    printf("  HQD_PQ_CNTL  (0x1C0C) = 0x%08X\n", R(0x1C0C));
    printf("  HQD_VMID     (0x1C14) = 0x%08X\n", R(0x1C14));
    printf("  HQD_PERSISTENT(0x1C10)= 0x%08X\n", R(0x1C10));
    printf("  HQD_PQ_RPTR  (0x1C18) = 0x%08X\n", R(0x1C18));
    printf("  HQD_PQ_WPTR_LO(0x1C1C)= 0x%08X\n", R(0x1C1C));

    /* 2. KIQ state */
    printf("\n--- 2. KIQ ring state ---\n");
    printf("  KIQ_BASE_LO (0xE060) = 0x%08X\n", R(0xE060));
    printf("  KIQ_BASE_HI (0xE064) = 0x%08X\n", R(0xE064));
    printf("  KIQ_CNTL    (0xE068) = 0x%08X\n", R(0xE068));
    printf("  KIQ_RPTR    (0xE06C) = 0x%08X\n", R(0xE06C));
    printf("  KIQ_WPTR    (0xE078) = 0x%08X\n", R(0xE078));

    /* 3. Read ALL GCVM L2 TLB cache entries */
    printf("\n--- 3. GCVM L2 TLB cache (0x0B300-0x0B400) ---\n");
    for (UINT32 off = 0x0B300; off <= 0x0B3FC; off += 4) {
        printf("    0x%05X = 0x%08X\n", off, R(off));
    }

    /* 4. Read GCVM context regs */
    printf("\n--- 4. GCVM context (0x0B400-0x0B500) ---\n");
    for (UINT32 off = 0x0B400; off <= 0x0B4FC; off += 4) {
        UINT32 v = R(off);
        printf("    0x%05X = 0x%08X\n", off, v);
    }

    /* 5. Try writing GCVM_L2_CNTL to enable physical mode */
    printf("\n--- 5. GCVM_L2_CNTL experiment ---\n");
    UINT32 l2cntl = R(0x0B360);
    printf("  GCVM_L2_CNTL = 0x%08X\n", l2cntl);

    /* 6. Check GCVM invalidate registers */
    printf("\n--- 6. GCVM invalidate (0x0B500-0x0B600) ---\n");
    for (UINT32 off = 0x0B500; off <= 0x0B5FC; off += 4) {
        UINT32 v = R(off);
        if (v != 0) printf("    0x%05X = 0x%08X\n", off, v);
    }

    /* 7. GFX Ring state */
    printf("\n--- 7. GFX ring state ---\n");
    printf("  RING0_BASE_LO (0xDA60) = 0x%08X\n", R(0xDA60));
    printf("  RING0_CNTL    (0xDA68) = 0x%08X\n", R(0xDA68));
    printf("  RING0_RPTR    (0xDA6C) = 0x%08X\n", R(0xDA6C));
    printf("  RING0_WPTR    (0xDA78) = 0x%08X\n", R(0xDA78));

    CloseHandle(g_h);
    printf("\n=== Done ===\n");
    return 0;
}
