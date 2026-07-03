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

static UINT32 MakeType3Hdr(UINT32 op, UINT32 count) {
    return (3 << 30) | ((count - 1) << 16) | (op << 8);
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

    printf("=== MQD + Doorbell Ring Test ===\n\n");

    /* Check current state */
    printf("Initial state:\n");
    printf("  CP_MQD_BASE_ADDR (0x9104): 0x%08X\n", R(0x9104));
    printf("  CP_MQD_BASE_ADDR_HI (0x9108): 0x%08X\n", R(0x9108));
    printf("  CP_HQD_ACTIVE (0x910C): 0x%08X\n", R(0x910C));
    printf("  CP_HQD_PQ_BASE_LO (0x9124): 0x%08X\n", R(0x9124));
    printf("  CP_HQD_PQ_CONTROL (0x9148): 0x%08X\n", R(0x9148));

    /* === Approach 1: Use 0xDA78 WPTR doorbell with VRAM ring buffer ===
     * Maybe the ring base is implicit (starts at VRAM base 0xC0000000)
     * and WPTR/DOORBELL writes trigger processing.
     * Let's try: write PM4 NOP at offset 0x1000 in VRAM + set WPTR to 0x1000 */
    printf("\n--- Approach 1: Fixed VRAM offset ---\n");

    /* Write PM4 NOP + scratch write at VRAM offset 0x1000 */
    UINT32 pm4Nop = MakeType3Hdr(0x10, 1);
    WritePhys(0xC0001000ULL, &pm4Nop, 4);

    /* Also write a scratch-write PM4 there */
    UINT32 pm4WrData[5] = {
        (3 << 30) | ((5 - 1) << 16) | (0x37 << 8), /* IT_WRITE_DATA */
        0x00000000, /* engine_sel=ME, no WRITE_CONFIRM */
        0x000032D4, /* addr (scratch reg 0) */
        0x00000000, /* addr hi */
        0xDEADBEEF  /* data */
    };
    WritePhys(0xC0001004ULL, pm4WrData, sizeof(pm4WrData));

    /* Submit via doorbell */
    UINT32 rptrBefore = R(0xDA6C);
    UINT32 wptrBefore = R(0xDA78);
    printf("  Before: RPTR=0x%08X WPTR=0x%08X SCRATCH=0x%08X\n",
        rptrBefore, wptrBefore, R(0x32D4));

    W(0xDA78, 4); /* WPTR=4 byte offset = NOP at VRAM+0x1000 */
    printf("  WPTR=4: RPTR=0x%08X WPTR=0x%08X\n", R(0xDA6C), R(0xDA78));

    for (int i = 0; i < 5; i++) {
        Sleep(20);
        UINT32 r = R(0xDA6C);
        UINT32 s = R(0x32D4);
        printf("  t=%dms: RPTR=0x%08X SCRATCH=0x%08X\n", (i+1)*20, r, s);
    }

    /* Restore */
    W(0xDA78, wptrBefore);

    /* === Approach 2: Set up MQD and use writeback ===
     * GFX10 MQD format for compute queue (from Linux v10_compute_mqd):
     *   Offset 0: PGM_LO
     *   Offset 4: PGM_HI
     *   Offset 8: PGM_RSRC1
     *   Offset 12: PGM_RSRC2
     *   Offset 16: ... 
     *   Offset 64+: ring buffer fields */
    printf("\n--- Approach 2: MQD setup ---\n");

    /* Allocate MQD in VRAM */
    UINT64 mqdPhys = 0xC0200000ULL;
    UINT8 mqdZero[256] = {0};
    WritePhys(mqdPhys, mqdZero, sizeof(mqdZero));

    /* Set up MQD fields for ring:
     * MQD offset 0x50: CP_HQD_PQ_BASE_LO (ring buffer base)
     * MQD offset 0x54: CP_HQD_PQ_BASE_HI
     * MQD offset 0x58: CP_HQD_PQ_CONTROL 
     * MQD offset 0x60: CP_HQD_PQ_WPTR_POLL_ADDR
     * etc.
     * 
     * Actually, the ring base in MQD is at different offsets.
     * Let me just try writing the MQD base register. */

    /* Try: write CP_MQD_BASE_ADDR to point to our MQD */
    UINT32 origMqdBase = R(0x9104);
    W(0x9104, (UINT32)(mqdPhys & 0xFFFFFFFF));
    printf("  CP_MQD_BASE_ADDR -> 0x%08X (was 0x%08X)\n",
        (UINT32)(mqdPhys & 0xFFFFFFFF), origMqdBase);

    /* Try activating HQD */
    UINT32 origHqdActive = R(0x910C);
    W(0x910C, 1);
    printf("  CP_HQD_ACTIVE=1 -> reads 0x%08X\n", R(0x910C));

    /* Check if MQD loaded by reading PGM_LO */
    printf("  PGM_LO (0x8110) = 0x%08X\n", R(0x8110));

    /* Restore */
    W(0x9104, origMqdBase);
    W(0x910C, origHqdActive);

    /* === Approach 3: Try direct doorbell with full offset ===
     * Maybe the doorbell at 0xDA80 accepts (ring_base_hi << 16) | wptr */
    printf("\n--- Approach 3: Doorbell with ring base ---\n");

    /* Try writing doorbell with ring base address in upper bits */
    W(0xDA80, 0xC0000000); /* try with ring base suggestion */
    printf("  Doorbell=0xC0000000: WPTR=0x%08X RPTR=0x%08X\n", R(0xDA78), R(0xDA6C));

    W(0xDA80, 0x00000000); /* reset */
    printf("  Doorbell=0: WPTR=0x%08X RPTR=0x%08X\n", R(0xDA78), R(0xDA6C));

    /* === Approach 4: Check if 0xDA60 registers are for SDMA or another engine ===
     * What if these are SDMA ring registers, not GFX CP? */
    printf("\n--- Approach 4: Check SDMA ---\n");
    printf("  SDMA0_RB_BASE_LO (0xE000): 0x%08X\n", R(0xE000));
    printf("  SDMA0_CNTL (0xE018): 0x%08X\n", R(0xE018));

    /* Check 0xDC60 (old COMPUTE range) — what changed? */
    printf("\n--- Approach 5: Re-check 0xDC60 area ---\n");
    for (UINT32 a = 0xDC60; a <= 0xDC80; a += 4) {
        printf("  0x%04X = 0x%08X\n", a, R(a));
    }

    CloseHandle(gH);
    printf("\nDone\n");
    return 0;
}
