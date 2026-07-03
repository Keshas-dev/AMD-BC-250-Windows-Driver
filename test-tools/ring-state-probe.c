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

    printf("=== GFX Ring State Investigation ===\n\n");

    /* Read initial register state multiple times to detect changes */
    printf("1. Reading ring registers 3 times (checking for changes):\n");
    for (int t = 0; t < 3; t++) {
        printf("   t=%d:", t);
        for (UINT32 a = 0xDA60; a <= 0xDA80; a += 4) {
            printf(" [0x%04X=0x%08X]", a, R(a));
        }
        printf("\n");
        Sleep(100);
    }

    /* Read RPTR_ADDR target memory (physical 0x6000) */
    printf("\n2. RPTR_ADDR (0xDA70) = 0x%08X\n", R(0xDA70));
    UINT32 rptrWbMem[4] = {0};
    ReadPhys(0x6000ULL, rptrWbMem, 16);
    printf("   Memory at 0x6000: %08X %08X %08X %08X\n",
        rptrWbMem[0], rptrWbMem[1], rptrWbMem[2], rptrWbMem[3]);

    /* Check what's at 0x0000 (ring base if BASE=0) */
    UINT32 memZero[4] = {0};
    ReadPhys(0x0000ULL, memZero, 16);
    printf("\n3. Memory at physical 0x0000: %08X %08X %08X %08X\n",
        memZero[0], memZero[1], memZero[2], memZero[3]);

    /* Scan for writable base register near 0xDA60 */
    printf("\n4. Scanning for writable BASE register (0xDA00-0xDB00):\n");
    for (UINT32 a = 0xDA00; a <= 0xDB00; a += 4) {
        UINT32 v = R(a);
        if (v != 0xFFFFFFFF && v != 0) {
            W(a, 0x12345678);
            UINT32 v2 = R(a);
            W(a, v);
            printf("   0x%04X: init=0x%08X -> wrote 0x12345678 -> 0x%08X %s\n",
                a, v, v2, (v2 == 0x12345678) ? "WRITABLE!" : "");
        }
    }

    /* Try setting up ring at 0xC0100000 + writing WPTR to see if CP processes it */
    printf("\n5. Attempting ring setup at 0xC0100000:\n");

    /* Clear 64 DWORDS of ring buffer */
    UINT8 ringZero[256] = {0};
    WritePhys(0xC0100000ULL, ringZero, sizeof(ringZero));

    /* Write a scratch-write PM4: SET_SH_REG to write 0xCAFEBABE to scratch reg 0 */
    /* SET_SH_REG (0x76): write to shader register (MMIO at GC_BASE + 0x2074 = 0x32D4) */
    /* HDR (count=2): (3<<30) | ((2-1)<<16) | (0x76<<8) = 0xC0017600 */
    /* Reg addr: (0x32D4 - 0x3000) >> 2 ... actually SH_REG format: offset from SH_REG_START */
    /* Let me just use NOP + WRITE_DATA to scratch instead */

    /* WRITE_DATA PM4: write 0xCAFEBABE to 0x32D4 (scratch) */
    /* IT_WRITE_DATA = 0x37, count=5 (hdr, dst_sel+engine, addr_lo, addr_hi, data) */
    /* HDR: (3<<30) | ((5-1)<<16) | (0x37<<8) = 0xC0043700 */
    UINT32 pm4[5];
    pm4[0] = (3 << 30) | ((5 - 1) << 16) | (0x37 << 8); /* IT_WRITE_DATA */
    pm4[1] = 0x00000200; /* WRITE_CONFIRM | engine_sel=ME */
    pm4[2] = 0x000032D4; /* addr low */
    pm4[3] = 0x00000000; /* addr high */
    pm4[4] = 0xCAFEBABE; /* data */
    WritePhys(0xC0100000ULL, pm4, sizeof(pm4));

    /* Also write NOP */
    UINT32 nop = MakeType3Hdr(0x10, 1);
    WritePhys(0xC0100014ULL, &nop, 4); /* at offset 0x14 */

    /* Write WPTR + ring base guess */
    UINT32 rptrPrev = R(0xDA6C);
    UINT32 wptrPrev = R(0xDA78);

    /* Try doorbell write */
    W(0xDA80, 0); /* doorbell = 0 to try to reset WPTR */
    printf("   After doorbell=0: WPTR=0x%08X RPTR=0x%08X\n", R(0xDA78), R(0xDA6C));

    /* Now try submitting command via WPTR:
     * WPTR = offset of first valid PM4 in ring buffer
     * Since we don't know the actual ring buffer address, we're guessing
     * But let's try WPTR=4 (4 bytes offset = 1 DWORD NOP at start) */
    W(0xDA78, 4);
    printf("   WPTR=4: RPTR=0x%08X WPTR=0x%08X\n", R(0xDA6C), R(0xDA78));
    Sleep(50);
    printf("   After 50ms: RPTR=0x%08X SCRATCH=0x%08X\n", R(0xDA6C), R(0x32D4));

    /* Restore */
    W(0xDA78, wptrPrev);
    printf("   Restored WPTR=0x%08X\n", wptrPrev);

    /* Try DOORBELL write with WPTR-like value */
    printf("\n6. DOORBELL (0xDA80) vs WPTR (0xDA78) readback comparison:\n");
    printf("   Reading both 5 times:\n");
    for (int t = 0; t < 5; t++) {
        printf("   t=%d: WPTR=0x%08X DOORBELL=0x%08X RPTR=0x%08X\n",
            t, R(0xDA78), R(0xDA80), R(0xDA6C));
        Sleep(50);
    }

    /* Check VRAM for any pre-existing PM4 patterns */
    printf("\n7. Checking VRAM for ring buffer signatures:\n");
    for (UINT64 pa = 0xC0000000ULL; pa < 0xC0100000ULL; pa += 0x100000) {
        UINT32 tmp[4];
        ReadPhys(pa, tmp, 16);
        /* Look for PM4 type3 headers: bit 31:30 = 11 */
        UINT32 hdr = tmp[0];
        if ((hdr >> 30) == 3) {
            printf("   PM4 at 0x%08llX: 0x%08X 0x%08X 0x%08X 0x%08X\n",
                pa, tmp[0], tmp[1], tmp[2], tmp[3]);
        }
    }

    /* Check low VRAM addresses */
    printf("\n8. VRAM low ranges (10 regions):\n");
    for (UINT64 pa = 0xC0000000ULL; pa < 0xC0100000ULL; pa += 0x200000) {
        UINT32 tmp[4];
        ReadPhys(pa, tmp, 16);
        printf("   0x%08llX: %08X %08X %08X %08X\n",
            pa, tmp[0], tmp[1], tmp[2], tmp[3]);
    }

    CloseHandle(gH);
    printf("\nDone\n");
    return 0;
}
