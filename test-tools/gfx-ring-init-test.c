#include <windows.h>
#include <stdio.h>
#pragma pack(push, 1)
typedef struct { UINT32 Off; UINT32 Val; } REG_IO;
#pragma pack(pop)
static HANDLE gH;

static UINT32 R(UINT32 off) {
    REG_IO in={off,0}, out={0}; DWORD br=0;
    DeviceIoControl(gH, 0x80000B88, &in, 8, &out, 8, &br, NULL);
    return out.Val;
}
static void W(UINT32 off, UINT32 val) {
    REG_IO in={off,val}, out={0}; DWORD br=0;
    DeviceIoControl(gH, 0x80000B8C, &in, 8, &out, 8, &br, NULL);
}
static void WritePhys(UINT64 pa, const void* data, ULONG size) {
    UCHAR buf[4096 + 12];
    ((UINT32*)buf)[0] = (UINT32)(pa & 0xFFFFFFFF);
    ((UINT32*)buf)[1] = (UINT32)(pa >> 32);
    ((UINT32*)buf)[2] = size;
    memcpy(buf + 12, data, size);
    DWORD br = 0;
    DeviceIoControl(gH, 0x80000C10, buf, 12 + size, NULL, 0, &br, NULL);
}
static ULONG ReadPhys(UINT64 pa, void* data, ULONG size) {
    UCHAR inbuf[24], outbuf[4096];
    ((UINT32*)inbuf)[0] = (UINT32)(pa & 0xFFFFFFFF);
    ((UINT32*)inbuf)[1] = (UINT32)(pa >> 32);
    ((UINT32*)inbuf)[2] = size;
    DWORD br = 0;
    DeviceIoControl(gH, 0x80000C14, inbuf, 12, outbuf, sizeof(outbuf), &br, NULL);
    if (br > 0) memcpy(data, outbuf, min(br, size));
    return br;
}

/* PM4 packets for ring submission */
static UINT32 MakeType3Hdr(UINT32 op, UINT32 count) {
    return (3 << 30) | ((count - 1) << 16) | (op << 8);
}

/* Test one address range for ring functionality */
static int TestRingRange(const char* label,
    UINT32 baseAddr,    /* CP_RB0_BASE */
    UINT32 baseHiAddr,  /* CP_RB0_BASE_HI */
    UINT32 cntlAddr,    /* CP_RB0_CNTL */
    UINT32 wptrAddr,    /* CP_RB0_WPTR */
    UINT32 rptrAddr,    /* CP_RB0_RPTR (CP_STAT, RO status) */
    UINT32 rptrWbAddr,  /* CP_RB0_RPTR_ADDR (writeback addr low) */
    UINT32 rptrWbHiAddr /* CP_RB0_RPTR_ADDR_HI (writeback addr high) */
) {
    printf("\n=== Testing %s ===\n", label);

    /* Check if base register is alive */
    UINT32 baseVal = R(baseAddr);
    UINT32 cntlVal = R(cntlAddr);
    printf("  Base=0x%08X CNTL=0x%08X WPTR=0x%08X\n",
        baseVal, cntlVal, R(wptrAddr));

    if (baseVal == 0xFFFFFFFF) {
        printf("  -> SKIP (dead range)\n");
        return 0;
    }

    /* Check if base is writable */
    W(baseAddr, 0x5A5A5A5A);
    UINT32 baseAfter = R(baseAddr);
    W(baseAddr, baseVal); /* restore */
    if (baseAfter == 0x5A5A5A5A) {
        printf("  Base IS writable!\n");
    } else {
        printf("  Base is RO (read-back=0x%08X)\n", baseAfter);
        /* Still try if CNTL is writable */
    }

    /* Set up ring buffer in VRAM at 0xC0100000 (safe offset) */
    UINT64 ringPhys = 0xC0100000ULL;
    UINT32 ringSize = 4096; /* bytes */

    /* Clear the ring buffer */
    UINT32 zero[1024] = {0}; /* 4096 bytes */
    WritePhys(ringPhys, zero, ringSize);

    /* Write a NOP PM4 packet at the start of the ring */
    UINT32 pm4Nop = MakeType3Hdr(0x10, 1); /* IT_NOP, count=1 */
    WritePhys(ringPhys, &pm4Nop, 4);
    printf("  Ring at 0x%016llX, NOP packet written\n", ringPhys);

    /* Set up RPTR writeback in system RAM */
    UINT64 rptrWbPhys = 0x7E512000ULL; /* known-working system RAM addr */
    UINT32 rptrWbInit = 0;
    WritePhys(rptrWbPhys, &rptrWbInit, 4); /* clear writeback */
    printf("  RPTR writeback at 0x%016llX\n", rptrWbPhys);

    /* Check GRBM status for engine activity */
    UINT32 grbmStatus = R(0x3260);
    printf("  GRBM_STATUS before=0x%08X\n", grbmStatus);

    /* Step 1: Write WPTR_DELAY = 0 (only for Linux-range, skip for hw.h range) */
    /* Step 2: Program ring control */
    /* CNTL format: buf_sz(7:0) | blk_sz(15:8) | RPTR_WB(22) */
    UINT32 rbCntl = 0x0C | (0x04 << 8) | (1 << 22); /* 4KB ring, 16DW blk, WB */
    W(cntlAddr, rbCntl);
    printf("  CNTL <- 0x%08X (size=4KB blk=16DW wb=1)\n", rbCntl);
    Sleep(1);

    /* Step 3: Program ring base (phys_addr >> 8) */
    UINT64 addrShifted = ringPhys >> 8;
    W(baseAddr, (UINT32)(addrShifted & 0xFFFFFFFF));
    W(baseHiAddr, (UINT32)(addrShifted >> 32));
    printf("  BASE <- 0x%08X (0x%016llX >> 8)\n",
        (UINT32)(addrShifted & 0xFFFFFFFF), ringPhys);
    Sleep(1);

    /* Step 4: Program RPTR writeback address */
    W(rptrWbAddr, (UINT32)(rptrWbPhys & 0xFFFFFFFF));
    W(rptrWbHiAddr, (UINT32)(rptrWbPhys >> 32));
    printf("  RPTR_WB <- 0x%016llX\n", rptrWbPhys);
    Sleep(1);

    /* Step 5: Verify setup */
    UINT32 baseReadback = R(baseAddr);
    UINT32 cntlReadback = R(cntlAddr);
    printf("  Verify: BASE=0x%08X CNTL=0x%08X\n", baseReadback, cntlReadback);

    /* Step 6: Fetch WPTR to process the NOP */
    /* WPTR = 4 (offset of next DWORD after the NOP packet = 4 bytes) */
    UINT32 wptrTarget = 4; /* 1 DWORD NOP */
    UINT32 rptrBefore = R(rptrAddr);
    printf("  RPTR before WPTR write: 0x%08X\n", rptrBefore);
    W(wptrAddr, wptrTarget);
    printf("  WPTR <- %u (NOP at offset 0)\n", wptrTarget);

    /* Step 7: Poll for processing */
    printf("  Polling RPTR for 100ms...\n");
    UINT32 rptrFinal = R(rptrAddr);
    UINT32 wbFinal = 0;
    ReadPhys(rptrWbPhys, &wbFinal, 4);
    UINT32 grbmAfter = R(0x3260);

    printf("  RPTR(reg)=0x%08X RPTR(wb)=0x%08X WPTR=0x%08X\n",
        rptrFinal, wbFinal, R(wptrAddr));
    printf("  GRBM_STATUS after=0x%08X\n", grbmAfter);

    /* Wait a bit and track RPTR movement */
    UINT32 rptrMax = rptrFinal;
    for (int i = 0; i < 10; i++) {
        Sleep(10);
        UINT32 newRptr = R(rptrAddr);
        ReadPhys(rptrWbPhys, &wbFinal, 4);
        if (newRptr != rptrMax) {
            printf("  RPTR CHANGED to 0x%08X at %dms!\n", newRptr, (i+1)*10);
            rptrMax = newRptr;
        }
        printf("  RPTR=0x%08X (WB=%u) at %dms\n", newRptr, wbFinal, (i+1)*10);
    }

    /* Check scratch register for any GPU activity */
    UINT32 scratch = R(0x32D4);
    printf("  SCRATCH=0x%08X\n", scratch);

    /* Determine if processing occurred: check if RPTR advanced */
    UINT32 rptrAfter = R(rptrAddr);
    UINT32 rptrAdvance = rptrAfter - rptrBefore;
    int processed = (rptrAdvance > 0) ? 1 : 0;

    /* Cleanup: zero out the registers */
    W(baseAddr, 0);
    W(cntlAddr, 0);
    W(rptrWbAddr, 0);
    W(wptrAddr, 0);

    printf("  %s: %s\n", label,
        processed ? "RING WORKS!" : "RING NOT PROCESSED");
    printf("  ===========================\n");
    return processed;
}

int main(void) {
    gH = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL);
    if (gH == INVALID_HANDLE_VALUE) { printf("FAIL: open\n"); return 1; }

    UINT8 initBuf[32] = {0};
    *(UINT64*)(initBuf+0)=0xFE800000; *(UINT32*)(initBuf+8)=0x80000;
    *(UINT32*)(initBuf+12)=1; *(UINT64*)(initBuf+16)=0xC0000000;
    *(UINT32*)(initBuf+24)=0x20000000;
    DWORD br=0; DeviceIoControl(gH, 0x80000B80, initBuf, 32, NULL, 0, &br, NULL);

    printf("=== GFX10 Ring Init Test ===\n");
    printf("Date: 2026-07-03\n");
    printf("Checking both address ranges for GFX ring functionality\n\n");

    /* Check VRAM writeability */
    UINT32 testVal = 0xDEADBEEF;
    UINT32 readBack = 0;
    WritePhys(0xC0100000ULL, &testVal, 4);
    ReadPhys(0xC0100000ULL, &readBack, 4);
    printf("VRAM test: wrote 0x%08X read 0x%08X %s\n",
        testVal, readBack, (readBack == testVal) ? "OK" : "FAIL");

    if (readBack != testVal) {
        printf("VRAM not writable! Trying system RAM instead.\n");
        CloseHandle(gH);
        return 1;
    }

    /* ============================================================
     * Test Range B: Linux-corrected addresses (0x89E0+, 0x4FE0+)
     * ============================================================ */
    int resultB = TestRingRange(
        "Linux-corrected (0x89E0+ / 0x4FE0+)",
        0x89E0,  /* CP_RB0_BASE     */
        0x8BA4,  /* CP_RB0_BASE_HI  */
        0x89E4,  /* CP_RB0_CNTL     */
        0x8A30,  /* CP_RB0_WPTR     */
        0x4FE0,  /* CP_RB0_RPTR     */
        0x89EC,  /* CP_RB0_RPTR_ADDR  */
        0x89F0   /* CP_RB0_RPTR_ADDR_HI */
    );

    /* ============================================================
     * Test Range A: Current hw.h addresses (0xDA60+)
     * ============================================================ */
    int resultA = TestRingRange(
        "hw.h legacy (0xDA60+)",
        0xDA60,  /* CP_RING0_BASE_LO  */
        0xDA64,  /* CP_RING0_BASE_HI  */
        0xDA68,  /* CP_RING0_CNTL     */
        0xDA78,  /* CP_RING0_WPTR     */
        0xDA6C,  /* CP_RING0_RPTR     */
        0xDA70,  /* CP_RING0_RPTR_ADDR_LO */
        0xDA74   /* CP_RING0_RPTR_ADDR_HI */
    );

    printf("\n=== RESULTS ===\n");
    printf("Linux-corrected: %s\n", resultB ? "WORKS!" : "FAILED");
    printf("hw.h legacy:     %s\n", resultA ? "WORKS!" : "FAILED");

    CloseHandle(gH);
    printf("\nDone\n");
    return (resultA || resultB) ? 0 : 1;
}
