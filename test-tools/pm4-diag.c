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
    printf("=== GCVM PT_BASE Persistence Test ===\n");
    g_h = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (g_h == INVALID_HANDLE_VALUE) { printf("Cannot open driver\n"); return 1; }

    /* INIT_HARDWARE */
    UCHAR ii[32] = {0}; *(UINT64*)(ii)=0xFE800000ULL; *(UINT32*)(ii+8)=0x80000; *(UINT32*)(ii+12)=1;
    *(UINT64*)(ii+16)=0xC0000000ULL; *(UINT32*)(ii+24)=0x10000000;
    UCHAR io[32]={0}; DWORD br=0;
    DeviceIoControl(g_h, IOCTL_AMDBC250_INIT_HARDWARE, ii, 32, io, 32, &br, NULL);
    printf("INIT_HARDWARE: OK\n");

    /* Read baseline */
    printf("\n--- Baseline ---\n");
    printf("  GCVM_CNTL  (0xB460) = 0x%08X\n", R(0x0B460));
    printf("  PT_BASE_LO (0xB608) = 0x%08X\n", R(0x0B608));
    printf("  PT_BASE_HI (0xB60C) = 0x%08X\n", R(0x0B60C));
    printf("  ME_CNTL    (0x4A74) = 0x%08X\n", R(0x4A74));

    /* Test 1: Write PT_BASE and read back immediately */
    printf("\n--- Test 1: Write PT_BASE LO/HI and read back ---\n");
    W(0x0B608, 0xDEADBEEF);
    W(0x0B60C, 0x12345678);
    printf("  Wrote PT_BASE = 0x12345678DEADBEEF\n");
    printf("  Readback LO = 0x%08X\n", R(0x0B608));
    printf("  Readback HI = 0x%08X\n", R(0x0B60C));

    /* Test 2: Read GCVM_CNTL bits */
    printf("\n--- Test 2: GCVM_CNTL bits ---\n");
    UINT32 cntl = R(0x0B460);
    printf("  GCVM_CNTL = 0x%08X\n", cntl);
    printf("  bit0 (ENABLE_L1_TLB) = %u\n", (cntl >> 0) & 1);
    printf("  bit1 = %u\n", (cntl >> 1) & 1);
    printf("  bit2 = %u\n", (cntl >> 2) & 1);
    printf("  bit3 = %u\n", (cntl >> 3) & 1);
    printf("  bit4 = %u\n", (cntl >> 4) & 1);
    printf("  bit5 = %u\n", (cntl >> 5) & 1);
    printf("  bit6 = %u\n", (cntl >> 6) & 1);
    printf("  bit7 = %u\n", (cntl >> 7) & 1);
    printf("  bits[15:8] = 0x%02X\n", (cntl >> 8) & 0xFF);
    printf("  bits[23:16] = 0x%02X\n", (cntl >> 16) & 0xFF);
    printf("  bits[31:24] = 0x%02X\n", (cntl >> 24) & 0xFF);

    /* Test 3: Try different CNTL values */
    printf("\n--- Test 3: Write different CNTL values ---\n");
    W(0x0B460, 0x01);
    printf("  Wrote 0x01, readback = 0x%08X\n", R(0x0B460));
    W(0x0B460, cntl);  /* restore */
    printf("  Restored 0x%08X\n", R(0x0B460));

    /* Test 4: Run GPU_KIQ_TEST and check PT_BASE afterwards */
    printf("\n--- Test 4: Run GPU_KIQ_TEST then check PT_BASE ---\n");
    typedef struct { UINT32 a,b,c,d,e,f,g,h; } KIQ_OUT;
    KIQ_OUT kiqOut = {0};
    DeviceIoControl(g_h, IOCTL_AMDBC250_GPU_KIQ_TEST, NULL, 0, &kiqOut, sizeof(kiqOut), &br, NULL);
    printf("  GPU_KIQ_TEST: Mmio=%u Ring=%u Hqd=%u Pm4=%u\n", kiqOut.d, kiqOut.e, kiqOut.f, kiqOut.g);
    printf("  SCRATCH: 0x%08X -> 0x%08X\n", kiqOut.b, kiqOut.c);
    printf("  WPTR: 0x%08X\n", kiqOut.h);
    printf("  After GPU_KIQ_TEST:\n");
    printf("  GCVM_CNTL  (0xB460) = 0x%08X\n", R(0x0B460));
    printf("  PT_BASE_LO (0xB608) = 0x%08X\n", R(0x0B608));
    printf("  PT_BASE_HI (0xB60C) = 0x%08X\n", R(0x0B60C));
    printf("  ME_CNTL    (0x4A74) = 0x%08X\n", R(0x4A74));

    /* Test 5: Write PT_BASE AFTER GPU_KIQ_TEST, then check if it persists across writes */
    printf("\n--- Test 5: PT_BASE persistence after writing ---\n");
    W(0x0B608, 0x11111111);
    W(0x0B60C, 0x22222222);
    printf("  Wrote 0x2222222211111111\n");
    printf("  Readback LO = 0x%08X HI = 0x%08X\n", R(0x0B608), R(0x0B60C));

    /* Test 6: Check what SCRATCH and ME_CNTL look like */
    printf("\n--- Test 6: Hardware state ---\n");
    printf("  SCRATCH    (0x32D4) = 0x%08X\n", R(0x32D4));
    printf("  GRBM_STATUS(0x3260) = 0x%08X\n", R(0x3260));
    printf("  GPU_ID     (0x0000) = 0x%08X\n", R(0x0000));

    CloseHandle(g_h);
    printf("\n=== Done ===\n");
    return 0;
}
