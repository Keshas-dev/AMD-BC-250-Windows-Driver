#include <windows.h>
#include <stdio.h>

#define IOCTL_INIT_HW       0x80000B80
#define IOCTL_READ_REG      0x80000B88
#define IOCTL_WRITE_REG     0x80000B8C
#define IOCTL_SEND_PM4      0x80000B84

#define IT_WRITE_DATA       0x37
#define IT_NOP              0x10
#define IT_EVENT_WRITE_EOP  0x47
#define PM4_TYPE3_HDR(op, cnt)  ((3<<30)|(((cnt)-1)<<16)|((op)<<8))

typedef struct { UINT32 Cmds[64]; UINT32 Cnt; UINT32 Pad; UINT64 Fence; UINT32 Q; UINT32 Pad2; } SPM4;
typedef struct { UINT32 Off; UINT32 Val; } REG_IO;

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

int main(void) {
    printf("=== Software PM4 Executor Test ===\n\n");

    gH = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (gH == INVALID_HANDLE_VALUE) {
        printf("ERROR: Cannot open GPU driver (err=%lu)\n", GetLastError());
        return 1;
    }
    printf("GPU driver opened\n");

    /* Init hardware (NBIO_MAP flag=1 — no GfxRing, just MMIO mapping) */
    UCHAR initBuf[32] = {0};
    *(UINT64*)(initBuf+0)  = 0xFE800000ULL;
    *(UINT32*)(initBuf+8)  = 0x00080000;
    *(UINT32*)(initBuf+12) = 1;
    *(UINT64*)(initBuf+16) = 0xC0000000ULL;
    *(UINT32*)(initBuf+24) = 0x20000000;  /* 512MB VRAM per BIOS */
    DWORD br = 0;
    BOOL ok = DeviceIoControl(gH, IOCTL_INIT_HW, initBuf, sizeof(initBuf), NULL, 0, &br, NULL);
    printf("INIT_HARDWARE (NBIO_MAP): %s\n", ok ? "OK" : "FAILED");

    /* Read initial state */
    UINT32 scratchBefore = R(0x32D4);
    printf("SCRATCH before = 0x%08X\n", scratchBefore);

    /* Test 1: Direct register write (baseline) */
    printf("\n--- Test 1: Direct Register Write ---\n");
    W(0x32D4, 0xDEADBEEF);
    UINT32 scratchAfter = R(0x32D4);
    printf("  Direct write SCRATCH = 0x%08X %s\n", scratchAfter,
           scratchAfter == 0xDEADBEEF ? "WRITABLE" : "READONLY/DIFFERENT");

    /* Test 2: Software PM4 - IT_WRITE_DATA to SCRATCH */
    printf("\n--- Test 2: Software PM4 IT_WRITE_DATA (SCRATCH=0xCAFEBABE) ---\n");
    scratchBefore = R(0x32D4);
    SPM4 spm4 = {0};
    spm4.Cmds[0] = PM4_TYPE3_HDR(IT_WRITE_DATA, 4);  /* IT_WRITE_DATA count=4 (4 payload DWORDs) */
    spm4.Cmds[1] = 0x10100000;  /* CONTROL: DST_SEL=register, WR_CONFIRM */
    spm4.Cmds[2] = 0x000032D4;  /* ADDR_LO = SCRATCH */
    spm4.Cmds[3] = 0x00000000;  /* ADDR_HI */
    spm4.Cmds[4] = 0xCAFEBABE;  /* DATA */
    spm4.Cnt = 5;
    ok = DeviceIoControl(gH, IOCTL_SEND_PM4, &spm4, sizeof(spm4), NULL, 0, &br, NULL);
    scratchAfter = R(0x32D4);
    printf("  SEND_PM4 (SW PM4): %s (err=%lu)\n", ok ? "OK" : "FAILED", GetLastError());
    printf("  SCRATCH: 0x%08X -> 0x%08X\n", scratchBefore, scratchAfter);
    /* HW masks top nibble [31:28] — compare lower 28 bits */
    if ((scratchAfter & 0x0FFFFFFF) == (0xCAFEBABE & 0x0FFFFFFF)) {
        printf("  *** SOFTWARE PM4 EXECUTED! ***\n");
    } else {
        printf("  PM4 NOT executed\n");
    }

    /* Test 3: Software PM4 - IT_NOP only (lightest test) */
    printf("\n--- Test 3: Software PM4 IT_NOP ---\n");
    spm4.Cmds[0] = PM4_TYPE3_HDR(IT_NOP, 1);  /* NOP, count=1 (just header) */
    spm4.Cnt = 1;
    ok = DeviceIoControl(gH, IOCTL_SEND_PM4, &spm4, sizeof(spm4), NULL, 0, &br, NULL);
    printf("  SEND_PM4 (NOP): %s\n", ok ? "OK" : "FAILED");

    /* Test 4: Software PM4 - multiple IT_WRITE_DATA + fence */
    printf("\n--- Test 4: Software PM4 with Fence ---\n");
    scratchBefore = R(0x32D4);
    spm4.Cmds[0] = PM4_TYPE3_HDR(IT_WRITE_DATA, 4);
    spm4.Cmds[1] = 0x10100000;
    spm4.Cmds[2] = 0x000032D4;  /* SCRATCH */
    spm4.Cmds[3] = 0x00000000;
    spm4.Cmds[4] = 0x12345678;  /* value */
    spm4.Cnt = 5;
    spm4.Fence = 0x42;  /* Request fence write */
    ok = DeviceIoControl(gH, IOCTL_SEND_PM4, &spm4, sizeof(spm4), NULL, 0, &br, NULL);
    scratchAfter = R(0x32D4);
    printf("  SEND_PM4 + fence: %s\n", ok ? "OK" : "FAILED");
    printf("  SCRATCH: 0x%08X -> 0x%08X %s\n", scratchBefore, scratchAfter,
           (scratchAfter & 0x0FFFFFFF) == (0x12345678 & 0x0FFFFFFF) ? "MATCH" : "DIFF");

    /* Test 5: Software PM4 - PM4_TYPE_0 register write */
    printf("\n--- Test 5: Software PM4 Type0 register ---\n");
    spm4.Cnt = 2;
    spm4.Cmds[0] = ((2-1)<<16) | (0x32D4>>2);  /* PM4_TYPE0: write 2 regs starting at SCRATCH */
    spm4.Cmds[1] = 0xAABBCCDD;                  /* DATA for SCRATCH */
    /* Note: type0 also writes the next register (SCRATCH+4), but we don't care */
    DeviceIoControl(gH, IOCTL_SEND_PM4, &spm4, sizeof(spm4), NULL, 0, &br, NULL);
    scratchAfter = R(0x32D4);
    printf("  SEND_PM4 (Type0): SCRATCH=0x%08X\n", scratchAfter);

    /* Scan: find COMPUTE registers at SEG1 */
    printf("\n--- Scan: COMPUTE registers at SEG1 (0xDC60-0xDC80) ---\n");
    {
        struct { UINT32 off; const char* name; } regs[] = {
            {0x3C60, "DISPATCH_DIRECT SEG0 (dead)"},
            {0xDC60, "DISPATCH_DIRECT SEG1"},
            {0xDC64, "DISPATCH_START"},
            {0xDC68, "PGM_RSRC1"},
            {0xDC6C, "PGM_RSRC2"},
            {0xDC70, "PGM_LO"},
            {0xDC74, "PGM_HI"},
            {0xDC78, "RESOURCE_LIMITS"},
            {0xDC7C, "STATIC_THREAD_MGMT_SE0"},
            {0xDC80, "MISC_BASE"},
        };
        for (int i = 0; i < sizeof(regs)/sizeof(regs[0]); i++) {
            UINT32 v = R(regs[i].off);
            if (v == 0xFFFFFFFF)
                printf("  0x%04X %-28s 0x%08X (DEAD)\n", regs[i].off, regs[i].name, v);
            else
                printf("  0x%04X %-28s 0x%08X\n", regs[i].off, regs[i].name, v);
        }
    }

    /* Test 6a: Verify COMPUTE register writability */
    printf("\n--- Test 6a: COMPUTE PGM writability check ---\n");
    {
        UINT32 savePgmLo = R(0xDC70);
        UINT32 savePgmHi = R(0xDC74);
        UINT32 saveRsrc1 = R(0xDC68);
        UINT32 saveRsrc2 = R(0xDC6C);
        UINT32 saveLimits = R(0xDC78);
        printf("  Saving: PGM_LO=0x%08X PGM_HI=0x%08X RSRC1=0x%08X RSRC2=0x%08X LIMITS=0x%08X\n",
               savePgmLo, savePgmHi, saveRsrc1, saveRsrc2, saveLimits);
        
        W(0xDC70, 0xCAFEBABE);
        W(0xDC74, 0x0000BEEF);
        W(0xDC68, 0x12345678);
        W(0xDC6C, 0x87654321);
        W(0xDC78, 0x000000FF);
        {
            UINT32 vLo = R(0xDC70), vHi = R(0xDC74), vR1 = R(0xDC68), vR2 = R(0xDC6C), vLim = R(0xDC78);
            printf("  Write verify:\n");
            printf("    PGM_LO: 0xCAFEBABE -> 0x%08X %s\n", vLo, vLo == 0xCAFEBABE ? "MATCH" : "DIFFERENT");
            printf("    PGM_HI: 0x0000BEEF -> 0x%08X %s\n", vHi, vHi == 0x0000BEEF ? "MATCH" : "DIFFERENT");
            printf("    RSRC1:  0x12345678 -> 0x%08X %s\n", vR1, vR1 == 0x12345678 ? "MATCH" : "DIFFERENT");
            printf("    RSRC2:  0x87654321 -> 0x%08X %s\n", vR2, vR2 == 0x87654321 ? "MATCH" : "DIFFERENT");
            printf("    LIMITS: 0x000000FF -> 0x%08X %s\n", vLim, vLim == 0x000000FF ? "MATCH" : "DIFFERENT");
        }
        
        /* Restore */
        W(0xDC70, savePgmLo);
        W(0xDC74, savePgmHi);
        W(0xDC68, saveRsrc1);
        W(0xDC6C, saveRsrc2);
        W(0xDC78, saveLimits);
        printf("  Restored to original values\n");
        
        /* After restore, try DISPATCH_DIRECT with PGM pointing to a safe NOP region */
        printf("\n  PGM now points to 0. DISPATCH with VALID=1 will NOP or fault silently.\n");
        printf("  Next: load shader code to known address, set PGM, then dispatch.\n");
    }

    /* Test 6: Software PM4 - IT_DISPATCH_DIRECT (register write, no trigger) */
    printf("\n--- Test 6: IT_DISPATCH_DIRECT (SEG1 0xDC60/0xDC64) ---\n");
    {
        /* COMPUTE regs are in SEG1: GC_BASE(0x1260) + SEG1(0xA000) + 0x2A00 = 0xDC60 */
        UINT32 ddBefore = R(0xDC60);
        UINT32 dsBefore = R(0xDC64);
        printf("  COMPUTE_DISPATCH_DIRECT (0xDC60) before = 0x%08X\n", ddBefore);
        printf("  COMPUTE_DISPATCH_START  (0xDC64) before = 0x%08X\n", dsBefore);
        
        SPM4 dspm4 = {0};
        /* IT_DISPATCH_DIRECT opcode=0x15, count=4 */
        dspm4.Cmds[0] = (3<<30)|((4-1)<<16)|(0x15<<8);  /* TYPE3 HDR: DISPATCH_DIRECT */
        dspm4.Cmds[1] = 64;   /* dim_x = 64 */
        dspm4.Cmds[2] = 1;    /* dim_y = 1 */
        dspm4.Cmds[3] = 1;    /* dim_z = 1 */
        dspm4.Cmds[4] = 0;    /* initiator = 0 (no VALID trigger - safe without shader) */
        dspm4.Cnt = 5;
        ok = DeviceIoControl(gH, IOCTL_SEND_PM4, &dspm4, sizeof(dspm4), NULL, 0, &br, NULL);
        printf("  SEND_PM4 (DISPATCH_DIRECT): %s (err=%lu)\n", ok ? "OK" : "FAILED", GetLastError());
        
        UINT32 ddAfter = R(0xDC60);
        UINT32 dsAfter = R(0xDC64);
        printf("  COMPUTE_DISPATCH_DIRECT after  = 0x%08X", ddAfter);
        UINT32 expDims = (64 & 0xFFF) | ((1 & 0xFFF) << 12) | ((1 & 0xFF) << 24);
        if (ddAfter == expDims) printf(" *** MATCH (expected 0x%08X)", expDims);
        printf("\n");
        printf("  COMPUTE_DISPATCH_START after   = 0x%08X%s\n", dsAfter, dsAfter == 0 ? "" : " (expected 0)");
    }

    /* Test 7: IT_DISPATCH_DIRECT with VALID=1 (triggers compute) */
    printf("\n--- Test 7: IT_DISPATCH_DIRECT with VALID=1 ---\n");
    {
        UINT32 grbmBefore = R(0x3260);
        printf("  GRBM_STATUS before = 0x%08X\n", grbmBefore);
        
        SPM4 dspm4 = {0};
        dspm4.Cmds[0] = (3<<30)|((4-1)<<16)|(0x15<<8);  /* TYPE3 HDR: DISPATCH_DIRECT */
        dspm4.Cmds[1] = 64;   /* dim_x */
        dspm4.Cmds[2] = 1;    /* dim_y */
        dspm4.Cmds[3] = 1;    /* dim_z */
        dspm4.Cmds[4] = 0x80000000;  /* initiator with VALID=bit31 */
        dspm4.Cnt = 5;
        ok = DeviceIoControl(gH, IOCTL_SEND_PM4, &dspm4, sizeof(dspm4), NULL, 0, &br, NULL);
        printf("  SEND_PM4 (DISPATCH_DIRECT+VALID): %s (err=%lu)\n", ok ? "OK" : "FAILED", GetLastError());
        
        UINT32 ddAfter = R(0xDC60);
        UINT32 dsAfter = R(0xDC64);
        printf("  COMPUTE_DISPATCH_DIRECT after  = 0x%08X\n", ddAfter);
        printf("  COMPUTE_DISPATCH_START after   = 0x%08X", dsAfter);
        if (dsAfter == 0) printf(" (VALID cleared by HW)");
        printf("\n");
        
        /* Poll GRBM_STATUS for compute busy (bit17=ME_BUSY, bit16=CP_BUSY, bit22=GUI_ACTIVE) */
        printf("  Polling GRBM_STATUS...\n");
        for (int i = 0; i < 50; i++) {
            UINT32 gs = R(0x3260);
            if (gs == 0) {
                printf("  GRBM_STATUS idle after %d polls\n", i + 1);
                break;
            }
            printf("  GRBM_STATUS[%d] = 0x%08X\n", i, gs);
            Sleep(10);
        }
        UINT32 grbmAfter = R(0x3260);
        printf("  GRBM_STATUS after  = 0x%08X\n", grbmAfter);
    }

    /* Read final HW state */
    printf("\n--- Final HW State ---\n");
    printf("  GPU_ID(0x0000)  = 0x%08X\n", R(0x0000));
    printf("  SCRATCH(0x32D4) = 0x%08X\n", R(0x32D4));
    printf("  ME_CNTL(0x4A74) = 0x%08X\n", R(0x4A74));
    printf("  GRBM_STAT(0x3260) = 0x%08X\n", R(0x3260));

    CloseHandle(gH);
    printf("\n=== Done ===\n");
    return 0;
}
