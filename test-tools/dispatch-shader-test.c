#include <windows.h>
#include <stdio.h>

#define IOCTL_INIT_HW            0x80000B80
#define IOCTL_READ_REG           0x80000B88
#define IOCTL_WRITE_REG          0x80000B8C
#define IOCTL_SEND_PM4           0x80000B84
#define IOCTL_WRITE_PHYSICAL_MEM 0x80000C10
#define IOCTL_READ_PHYSICAL_MEM  0x80000C14

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
    if (!ok) printf("    WRITE_PA err=%lu\n", GetLastError());
    return ok;
}

static BOOL ReadPhys(UINT64 pa, void* data, ULONG size) {
    UCHAR inBuf[12];
    ((PULONG)inBuf)[0] = (ULONG)(pa & 0xFFFFFFFF);
    ((PULONG)inBuf)[1] = (ULONG)(pa >> 32);
    ((PULONG)inBuf)[2] = size;
    DWORD br = 0;
    BOOL ok = DeviceIoControl(gH, IOCTL_READ_PHYSICAL_MEM, inBuf, sizeof(inBuf), data, size, &br, NULL);
    if (!ok) printf("    READ_PA err=%lu br=%lu\n", GetLastError(), br);
    return ok;
}

int main(void) {
    printf("=== COMPUTE DISPATCH + SHADER TEST (v5) ===\n\n");
    gH = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (gH == INVALID_HANDLE_VALUE) {
        printf("ERROR: Cannot open GPU driver (err=%lu)\n", GetLastError());
        return 1;
    }
    printf("Driver opened\n");

    UINT8 initBuf[32] = {0};
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

    /* ========== STEP 0: READ_PHYSICAL_MEM via BAR5 SCRATCH ========== */
    printf("=== Step 0: READ_PHYSICAL_MEM verification (BAR5 SCRATCH) ===\n");
    {
        UINT64 bar5Addr = 0xFE800000ULL + 0x32D4;
        /* First write known value via WRITE_REG (safe, proven to work) */
        W(0x32D4, 0x12345678);
        UINT32 rb = R(0x32D4);
        printf("  WRITE_REG SCRATCH: wrote 0x12345678, read 0x%08X %s\n",
               rb, rb == 0x12345678 ? "MATCH" : "MISMATCH");

        /* Now READ_PHYSICAL_MEM from same address */
        UINT32 readBack = 0;
        BOOL rOK = ReadPhys(bar5Addr, &readBack, sizeof(readBack));
        printf("  READ_PHYSICAL_MEM: %s, value=0x%08X", rOK ? "OK" : "FAILED", readBack);
        if (rOK) printf(" %s", readBack == 0x12345678 ? "MATCH" : "MISMATCH");
        printf("\n");
    }

    /* ========== STEP 1: Save GCVM state ========== */
    UINT32 ctxCntl = R(0x0B460);
    printf("\n=== GCVM State ===\n");
    printf("  CONTEXT0_CNTL (0x0B460) = 0x%08X%s%s\n",
           ctxCntl, (ctxCntl & 1) ? " TRANSLATION ON" : " TRANSLATION OFF",
           (ctxCntl & 2) ? " +DEFAULT_PAGE" : "");
    printf("  PT_BASE_LO (0x6C8C) = 0x%08X\n", R(0x6C8C));
    printf("  PT_BASE_HI (0x6C90) = 0x%08X\n", R(0x6C90));

    /* ========== STEP 2: Write shader to VRAM (0xC0000000 + 0x100000) ========== */
    UINT64 vramAddr = 0xC0000000ULL + 0x100000;
    printf("\n=== Step 2: Write/verify shader at CPU PA 0x%llX (VRAM offset 0x100000) ===\n", vramAddr);
    {
        UINT32 buf[256];
        for (int i = 0; i < 256; i++) buf[i] = 0xBF9F0000; /* GCN s_endpgm */
        WritePhys(vramAddr, buf, sizeof(buf));

        /* Verify via READ_PHYSICAL_MEM (will confirm if VRAM address is valid) */
        UINT32 readBack[4] = {0};
        BOOL rOK = ReadPhys(vramAddr, readBack, sizeof(readBack));
        printf("  READ_PHYSICAL_MEM verify: %s\n", rOK ? "OK" : "FAILED");
        if (rOK) {
            printf("  Data: %08X %08X %08X %08X %s\n",
                   readBack[0], readBack[1], readBack[2], readBack[3],
                   (readBack[0] == 0xBF9F0000) ? "MATCH" : "MISMATCH");
        }
    }

    /* ========== STEP 3: DISPATCH with GCVM OFF ========== */
    printf("\n=== Step 3: DISPATCH with GCVM OFF ===\n");
    UINT32 saveCtxCntl = ctxCntl;
    W(0x0B460, ctxCntl & ~1);
    printf("  GCVM CONTEXT0_CNTL: 0x%08X -> 0x%08X (translation OFF)\n",
           ctxCntl, R(0x0B460));

    /* Set PGM to GPU physical address 0x100000 (VRAM offset).
     * With GCVM OFF, GPU treats PGM as physical address.
     * VRAM starts at GPU physical 0, so offset 0x100000 = our shader. */
    W(0xDC70, (UINT32)(0x100000 >> 8));  /* PGM_LO = 0x1000 */
    W(0xDC74, 0);                         /* PGM_HI = 0 */
    printf("  PGM: GPU_PA 0x100000 (LO=0x%08X HI=0x%08X)\n", R(0xDC70), R(0xDC74));

    /* Set LIMITS - minimal config */
    W(0xDC78, 0x00001001);
    printf("  LIMITS=0x%08X\n", R(0xDC78));

    /* Save RSRC1/RSRC2 defaults, write minimal config */
    UINT32 rsrc1Save = R(0xDC68);
    UINT32 rsrc2Save = R(0xDC6C);
    /* Keep HW defaults for RSRC1/RSRC2 - don't write them */
    printf("  RSRC1(0xDC68)=0x%08X RSRC2(0xDC6C)=0x%08X (HW defaults)\n",
           rsrc1Save, rsrc2Save);

    /* Check state before dispatch */
    printf("  BEFORE: STATUS=0x%08X GUILTY=0x%08X DIR=0x%08X START=0x%08X\n",
           R(0x3260), R(0x3264), R(0xDC60), R(0xDC64));

    /* DISPATCH_DIRECT with VALID=1 */
    {
        SPM4 spm4 = {0};
        spm4.Cmds[0] = (3<<30)|((4-1)<<16)|(0x15<<8);
        spm4.Cmds[1] = 1; spm4.Cmds[2] = 1; spm4.Cmds[3] = 1;
        spm4.Cmds[4] = 0x80000000;
        spm4.Cnt = 5;
        DeviceIoControl(gH, IOCTL_SEND_PM4, &spm4, sizeof(spm4), NULL, 0, &br, NULL);
    }

    printf("  AFTER:  STATUS=0x%08X GUILTY=0x%08X DIR=0x%08X START=0x%08X\n",
           R(0x3260), R(0x3264), R(0xDC60), R(0xDC64));

    /* Poll briefly (no busy loop that can hang) */
    int busy = 0;
    for (int i = 0; i < 200; i++) {
        UINT32 gs = R(0x3260);
        if (gs & 0x1003F000) {  /* Check ME/CP/PFP/QUEUE busy bits */
            printf("  *** BUSY at iter %d: STATUS=0x%08X ***\n", i, gs);
            busy = 1;
            break;
        }
        Sleep(1);
    }
    if (!busy) printf("  GRBM_STATUS idle after dispatch\n");

    /* Verify SCRATCH didn't change (GPU didn't write to random memory) */
    printf("  SCRATCH=0x%08X\n", R(0x32D4));

    /* ========== Restore ========== */
    W(0xDC70, 0); W(0xDC74, 0); W(0xDC78, 0);
    W(0x0B460, saveCtxCntl);
    printf("\n  GCVM restored to 0x%08X\n", R(0x0B460));

    printf("\n=== Done ===\n");
    CloseHandle(gH);
    return 0;
}
