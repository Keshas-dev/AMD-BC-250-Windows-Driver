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
