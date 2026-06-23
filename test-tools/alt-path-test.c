/* alt-path-test.c — Test alternative GPU access paths */
/* Tests RLCG, SDMA, direct register write patterns, and other IP blocks */

#include <windows.h>
#include <stdio.h>

#define IOCTL_GPU_READ  0x80000B88
#define IOCTL_GPU_WRITE 0x80000B8C
#define IOCTL_GPU_INIT  0x80000B80
#define IOCTL_GPU_KIQ   0x80000BDC

static HANDLE h;

static ULONG R(ULONG off) {
    UCHAR buf[8] = {0};
    *(ULONG*)(buf+0) = off;
    *(ULONG*)(buf+4) = 0xDEADBEEF;
    DWORD br = 0;
    DeviceIoControl(h, IOCTL_GPU_READ, buf, 8, buf, 8, &br, NULL);
    return *(ULONG*)(buf+4);
}

static BOOL W(ULONG off, ULONG val) {
    UCHAR buf[8] = {0};
    *(ULONG*)(buf+0) = off;
    *(ULONG*)(buf+4) = val;
    DWORD br = 0;
    return DeviceIoControl(h, IOCTL_GPU_WRITE, buf, 8, buf, 8, &br, NULL);
}

static void InitDriver(void) {
    UCHAR init[32] = {0};
    *(unsigned __int64*)(init+0) = 0xFE800000ULL;
    *(unsigned*)(init+8) = 0x00080000;
    *(unsigned*)(init+12) = 1;
    *(unsigned __int64*)(init+16) = 0xC0000000ULL;  
    *(unsigned*)(init+24) = 0x10000000;
    DWORD br = 0;
    DeviceIoControl(h, IOCTL_GPU_INIT, init, sizeof(init), NULL, 0, &br, NULL);
}

/* Test RLCG register paths — Ring Local CS Global */
static void TestRlcg(void) {
    printf("\n=== RLCG Register Access ===\n");
    
    /* RLCG uses GRBM_GFX_INDEX (0x34D0) to select ME/PIPE/QUEUE */
    ULONG grbmIndex = R(0x34D0);
    printf("  GRBM_GFX_INDEX (0x34D0) = 0x%08X\n", grbmIndex);
    
    /* Try switching to KIQ (ME=1) */
    W(0x34D0, 0x00010000);  /* ME=1 for KIQ */
    ULONG kiqGrbm = R(0x34D0);
    W(0x34D0, 0xE0000000);  /* Broadcast */
    ULONG bcGrbm = R(0x34D0);
    printf("  After KIQ select: 0x%08X\n", kiqGrbm);
    printf("  After Broadcast:  0x%08X\n", bcGrbm);
}

/* Test HQD registers directly via GPU driver */
static void TestHqdRegs(void) {
    printf("\n=== HQD Register Access (GPU driver) ===\n");
    
    /* First switch to KIQ engine */
    W(0x34D0, 0x00010000);
    
    ULONG regs[] = {
        0xDAC0, 0xDAC4, 0xDAC8, 0xDB90, 0xDB94,
        0xDAD0, 0xDAD4, 0xDAD8, 0xDADC, 0xDAE0
    };
    const char* names[] = {
        "HQD_ACTIVE", "HQD_VMID", "HQD_PERSISTENT", 
        "HQD_PQ_WPTR_LO", "HQD_PQ_WPTR_HI",
        "HQD_PQ_RPTR", "HQD_PQ_WPTR", 
        "HQD_PQ_BASE_LO", "HQD_PQ_BASE_HI", "HQD_PQ_CNTL"
    };
    
    for (int i = 0; i < 10; i++) {
        ULONG val = R(regs[i]);
        printf("  %s [0x%04X] = 0x%08X\n", names[i], regs[i], val);
    }
    
    /* Restore broadcast */
    W(0x34D0, 0xE0000000);
}

/* Test KIQ registers via GPU driver */
static void TestKiqRegs(void) {
    printf("\n=== KIQ Register Access (GPU driver) ===\n");
    
    ULONG regs[] = {0xE060, 0xE064, 0xE068, 0xE06C, 0xE070, 0xE074, 0xE078, 0xE07C};
    const char* names[] = {
        "KIQ_BASE_LO", "KIQ_BASE_HI", "KIQ_CNTL", 
        "KIQ_RPTR", "KIQ_RPTR_HI", "KIQ_WPTR_HI", 
        "KIQ_WPTR", "KIQ_DOORBELL"
    };
    
    for (int i = 0; i < 8; i++) {
        printf("  %s [0x%04X] = 0x%08X\n", names[i], regs[i], R(regs[i]));
    }
}

/* Test CP firmware status registers */
static void TestCpFwStatus(void) {
    printf("\n=== CP Firmware Status ===\n");
    
    /* ME_IC_BASE (IC base for ME firmware) */
    ULONG meIcLo = R(0x17370);
    ULONG meIcHi = R(0x17374);
    printf("  ME_IC_BASE  = 0x%08X%08X\n", meIcHi, meIcLo);
    
    /* PFP_IC_BASE */
    ULONG pfpIcLo = R(0x17360);
    ULONG pfpIcHi = R(0x17364);
    printf("  PFP_IC_BASE = 0x%08X%08X\n", pfpIcHi, pfpIcLo);
    
    /* CE_IC_BASE */
    ULONG ceIcLo = R(0x17380);
    ULONG ceIcHi = R(0x17384);
    printf("  CE_IC_BASE  = 0x%08X%08X\n", ceIcHi, ceIcLo);
    
    /* ME_CNTL halt bits */
    ULONG meCntl = R(0x4A74);
    printf("  ME_CNTL     = 0x%08X\n", meCntl);
    printf("    ME_HALT(28)  = %d\n", (meCntl >> 28) & 1);
    printf("    PFP_HALT(30) = %d\n", (meCntl >> 30) & 1);
    printf("    CE_HALT(29)  = %d\n", (meCntl >> 29) & 1);
}

/* Test GCVM context registers */
static void TestGcvm(void) {
    printf("\n=== GCVM Register Access ===\n");
    
    ULONG gcvmCtx0Cntl = R(0x0B460);
    ULONG gcvmPtBaseLo = R(0x0B608);
    ULONG gcvmPtBaseHi = R(0x0B60C);
    ULONG gcvmL2Cntl = R(0x0B360);
    
    printf("  GCVM_CTX0_CNTL    = 0x%08X\n", gcvmCtx0Cntl);
    printf("  GCVM_PT_BASE_LO   = 0x%08X (should be 0 if locked)\n", gcvmPtBaseLo);
    printf("  GCVM_PT_BASE_HI   = 0x%08X\n", gcvmPtBaseHi);
    printf("  GCVM_L2_CNTL      = 0x%08X\n", gcvmL2Cntl);
    
    if (gcvmPtBaseLo == 0 && gcvmPtBaseHi == 0) {
        printf("  -> PT_BASE is HARDWARE LOCKED (confirmed)\n");
    }
    
    /* Test GCVM_CONTEXT0 registers for writability */
    ULONG ctx0Regs[] = {0x0B408, 0x0B40C, 0x0B410, 0x0B414, 0x0B418, 0x0B41C};
    for (int i = 0; i < 6; i++) {
        printf("  GCVM_CTX0_REGS[%d] (0x%04X) = 0x%08X\n", i, ctx0Regs[i], R(ctx0Regs[i]));
    }
}

/* Test SDMA registers */
static void TestSdma(void) {
    printf("\n=== SDMA Register Access ===\n");
    
    /* SDMA0 base is typically at 0x1260 + offsets */
    ULONG sdmaAddr[] = {0x3460, 0x3464, 0x3468, 0x346C, 0x3470};
    for (int i = 0; i < 5; i++) {
        printf("  SDMA[%d] (0x%04X) = 0x%08X\n", i, sdmaAddr[i], R(sdmaAddr[i]));
    }
}

/* Test direct register write patterns */
static void TestScratchPatterns(void) {
    printf("\n=== SCRATCH Write Pattern Test ===\n");
    
    ULONG patterns[] = {
        0x00000000, 0xFFFFFFFF, 0xAAAAAAAA, 0x55555555,
        0x12345678, 0x87654321, 0xCAFEBABE, 0xDEADBEEF,
        0x80000000, 0x7FFFFFFF
    };
    
    ULONG before = R(0x32D4);
    printf("  Initial SCRATCH = 0x%08X\n", before);
    
    for (int i = 0; i < 10; i++) {
        W(0x32D4, patterns[i]);
        ULONG after = R(0x32D4);
        ULONG expected = patterns[i];
        ULONG actual = after;
        BOOL match = (actual == expected);
        
        /* Check which bits differ */
        ULONG diff = expected ^ actual;
        printf("  Write 0x%08X -> Read 0x%08X %s (diff=0x%08X)\n",
               expected, actual, match ? "OK" : "MASKED", diff);
        
        /* If masked, show which bits */
        if (!match && diff) {
            for (int b = 0; b < 32; b++) {
                if (diff & (1 << b)) {
                    printf("    Bit %d is %s\n", b, 
                           (actual & (1 << b)) ? "STUCK 1" : "STUCK 0");
                }
            }
        }
    }
    
    /* Restore */
    W(0x32D4, before);
}

int main(void) {
    printf("=== Alternative Access Path Test Suite ===\n\n");
    
    h = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
                    0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("Cannot open GPU driver (err=%lu)\n", GetLastError());
        return 1;
    }
    
    InitDriver();
    
    TestKiqRegs();
    TestHqdRegs();
    TestRlcg();
    TestCpFwStatus();
    TestGcvm();
    TestSdma();
    TestScratchPatterns();
    
    CloseHandle(h);
    printf("\n=== Done ===\n");
    return 0;
}