#include <windows.h>
#include <stdio.h>

#define IOCTL_INIT_HW            0x80000B80
#define IOCTL_READ_REG           0x80000B88
#define IOCTL_WRITE_REG          0x80000B8C
#define IOCTL_SEND_PM4           0x80000B84
#define IOCTL_WRITE_PHYSICAL_MEM 0x80000C10

typedef struct { UINT32 Off; UINT32 Val; } REG_IO;
typedef struct { UINT32 Cmds[64]; UINT32 Cnt; UINT32 Pad; UINT64 Fence; UINT32 Q; UINT32 Pad2; } SPM4;

static HANDLE gH = INVALID_HANDLE_VALUE;
static UINT32 R(UINT32 off) {
    REG_IO in={off,0}, out={0}; DWORD br=0;
    DeviceIoControl(gH, IOCTL_READ_REG, &in, sizeof(in), &out, sizeof(out), &br, NULL);
    return out.Val;
}
static void W(UINT32 off, UINT32 val) {
    REG_IO in={off,val}, out={0}; DWORD br=0;
    DeviceIoControl(gH, IOCTL_WRITE_REG, &in, sizeof(in), &out, sizeof(out), &br, NULL);
}

/* Write data to physical address via driver IOCTL */
static BOOL WritePhys(UINT64 pa, const void* data, ULONG size) {
    UCHAR buf[4096 + 12];
    ULONG hdr_size = sizeof(ULONG) * 3;
    if (hdr_size + size > sizeof(buf)) return FALSE;
    ((PULONG)buf)[0] = (ULONG)(pa & 0xFFFFFFFF);
    ((PULONG)buf)[1] = (ULONG)(pa >> 32);
    ((PULONG)buf)[2] = size;
    memcpy(buf + hdr_size, data, size);
    DWORD br = 0;
    BOOL ok = DeviceIoControl(gH, IOCTL_WRITE_PHYSICAL_MEM, buf, hdr_size + size, NULL, 0, &br, NULL);
    if (!ok) printf("    WRITE_PA 0x%llX sz=%lu err=%lu\n", pa, size, GetLastError());
    return ok;
}

int main(void) {
    printf("=== COMPUTE DISPATCH + SHADER TEST (v3) ===\n\n");
    gH = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (gH == INVALID_HANDLE_VALUE) {
        printf("ERROR: Cannot open GPU driver (err=%lu)\n", GetLastError());
        return 1;
    }
    printf("Driver opened\n");

    UCHAR initBuf[32] = {0};
    *(UINT64*)(initBuf+0)  = 0xFE800000ULL;
    *(UINT32*)(initBuf+8)  = 0x00080000;
    *(UINT32*)(initBuf+12) = 1;
    *(UINT64*)(initBuf+16) = 0xC0000000ULL;
    *(UINT32*)(initBuf+24) = 0x20000000;
    DWORD br = 0;
    BOOL ok = DeviceIoControl(gH, IOCTL_INIT_HW, initBuf, sizeof(initBuf), NULL, 0, &br, NULL);
    printf("INIT_HARDWARE: %s\n", ok ? "OK" : "FAILED");
    if (!ok) { CloseHandle(gH); return 1; }
    printf("GRBM_STATUS = 0x%08X\n\n", R(0x3260));

    /* Check GCVM state */
    printf("=== GCVM State ===\n");
    UINT32 ctxCntl = R(0x0B460);
    printf("  GCVM_CONTEXT0_CNTL (0x0B460) = 0x%08X", ctxCntl);
    if (ctxCntl & 1) printf(" (TRANSLATION ON)");
    else printf(" (TRANSLATION OFF)");
    if (ctxCntl & 2) printf(" +DEFAULT_PAGE");
    printf("\n");
    printf("  PT_BASE_LO (0x6C8C) = 0x%08X\n", R(0x6C8C));
    printf("  PT_BASE_HI (0x6C90) = 0x%08X\n\n", R(0x6C90));

    /* Test WRITE_PHYSICAL_MEM with known-good BAR5 address first */
    printf("=== Step 0: Test WRITE_PHYSICAL_MEM with BAR5 ===\n");
    /* BAR5 base = 0xFE800000. SCRATCH offset = 0x32D4. Write "TEST" */
    {
        UINT64 bar5Addr = 0xFE800000ULL + 0x32D4;
        UINT32 testVal = 0xDEADBEEF;
        ok = WritePhys(bar5Addr, &testVal, sizeof(testVal));
        printf("  Write to BAR5 SCRATCH (0x%llX): %s\n", bar5Addr, ok ? "OK" : "FAILED");
        if (ok) {
            UINT32 rb = R(0x32D4);
            printf("  SCRATCH now = 0x%08X %s\n", rb, rb == 0xDEADBEEF ? "MATCH" : "");
        }
    }

    /* Step 1: Write shader to VRAM via WRITE_PHYSICAL_MEM IOCTL */
    /* VRAM is at 0xC0000000, size 512MB. Use 0xC0100000 (1MB in, safe) */
    UINT64 vramAddr = 0xC0100000ULL;
    printf("\n=== Step 1: Write shader to VRAM @ 0x%llX ===\n", vramAddr);
    static const UINT32 candidates[] = {
        0xBF9F0000,  /* GCN s_endpgm (might work on RDNA too) */
        0x9F800000,  /* RDNA SOPP s_endpgm (bit31:30=10, op=0x3F) */
        0x00000000,  /* all zeros (s_nop or illegal instr -> fault) */
        0xBF800000,  /* s_nop on GCN */
    };
    const char* names[] = {
        "GCN s_endpgm",
        "RDNA s_endpgm",
        "ZERO (NOP)",
        "s_nop (GCN)",
    };

    for (int trial = 0; trial < 4; trial++) {
        printf("\n  --- Trial %d: %s (0x%08X) ---\n",
               trial+1, names[trial], candidates[trial]);

        /* Write the shader code: 256 repeats of the candidate instruction */
        UINT32 shaderCode[256];
        for (int i = 0; i < 256; i++) shaderCode[i] = candidates[trial];
        ok = WritePhys(vramAddr, shaderCode, sizeof(shaderCode));
        printf("  WRITE_PHYSICAL_MEM: %s\n", ok ? "OK" : "FAILED");
        if (!ok) { printf("  (skip trial)\n"); continue; }

        /* Step 2: Set PGM_LO/HI */
        /* If GCVM is ON: PGM uses GPU virtual address
         * If GCVM is OFF: PGM uses physical address */
        if (ctxCntl & 1) {
            /* Translation ON -- use VRAM physical address directly? */
            /* Without page tables, physical address will fault.
             * Try anyway with PGM_LO = addr >> 8 with upper bits masked */
            printf("  GCVM ON: PGM = 0x%llX (physical)\n", vramAddr);
        } else {
            printf("  GCVM OFF: PGM = 0x%llX (physical)\n", vramAddr);
        }
        W(0xDC70, (UINT32)(vramAddr >> 8));  /* PGM_LO = addr[39:8] */
        W(0xDC74, (UINT32)(vramAddr >> 40));  /* PGM_HI = addr[47:40] */

        printf("  PGM_LO = 0x%08X, PGM_HI = 0x%08X\n", R(0xDC70), R(0xDC74));

        /* Step 3: DISPATCH with VALID=1 */
        printf("  COMPUTE_START before = 0x%08X\n", R(0xDC64));
        printf("  GRBM_STATUS before = 0x%08X\n", R(0x3260));

        SPM4 spm4 = {0};
        spm4.Cmds[0] = (3<<30)|((4-1)<<16)|(0x15<<8);
        spm4.Cmds[1] = 1;  /* dim_x */
        spm4.Cmds[2] = 1;  /* dim_y */
        spm4.Cmds[3] = 1;  /* dim_z */
        spm4.Cmds[4] = 0x80000000;  /* VALID=bit31 */
        spm4.Cnt = 5;
        ok = DeviceIoControl(gH, IOCTL_SEND_PM4, &spm4, sizeof(spm4), NULL, 0, &br, NULL);
        printf("  DISPATCH: %s\n", ok ? "OK" : "FAILED");

        printf("  COMPUTE_START after = 0x%08X\n", R(0xDC64));

        /* Poll GRBM_STATUS */
        int busyFound = 0;
        for (int i = 0; i < 100; i++) {
            UINT32 gs = R(0x3260);
            if (gs != 0) {
                printf("  GRBM_STATUS[%d] = 0x%08X *** BUSY ***\n", i, gs);
                busyFound = 1;
            }
            Sleep(10);
        }
        if (busyFound) {
            printf("  >>> COMPUTE ENGINE RAN! <<<\n");
            break;
        }
        printf("  GRBM_STATUS final = 0x%08X (idle)\n", R(0x3260));
    }

    /* Cleanup */
    W(0xDC70, 0);
    W(0xDC74, 0);

    printf("\n=== Final ===\n");
    printf("  GRBM_STATUS = 0x%08X\n", R(0x3260));
    printf("  SCRATCH     = 0x%08X\n", R(0x32D4));
    printf("  PGM_LO      = 0x%08X\n", R(0xDC70));

    CloseHandle(gH);
    printf("\n=== Done ===\n");
    return 0;
}
