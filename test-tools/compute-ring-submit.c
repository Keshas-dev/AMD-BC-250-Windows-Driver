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

    printf("=== COMPUTE Ring Direct Submit ===\n\n");

    /* Read initial state */
    printf("Initial COMPUTE ring state:\n");
    printf("  BASE_LO (0xDB60) = 0x%08X\n", R(0xDB60));
    printf("  BASE_HI/SIZE (0xDB64) = 0x%08X\n", R(0xDB64));
    printf("  CNTL (0xDB68) = 0x%08X\n", R(0xDB68));
    printf("  RPTR (0xDB6C) = 0x%08X\n", R(0xDB6C));
    printf("  WPTR (0xDB78) = 0x%08X\n", R(0xDB78));
    printf("  DOORBELL (0xDB7C) = 0x%08X\n", R(0xDB7C));
    printf("  DB80 (0xDB80) = 0x%08X\n", R(0xDB80));

    /* Read VRAM at the ring base */
    UINT32 vramStart[4] = {0};
    ReadPhys(0xC0000000ULL, vramStart, 16);
    UINT32 vramAtWptr[4] = {0};
    ReadPhys(0xC00003FEULL, vramAtWptr, 16);

    printf("\nVRAM at ring base (0xC0000000): %08X %08X %08X %08X\n",
        vramStart[0], vramStart[1], vramStart[2], vramStart[3]);
    printf("VRAM at WPTR (0xC00003FE): %08X %08X %08X %08X\n",
        vramAtWptr[0], vramAtWptr[1], vramAtWptr[2], vramAtWptr[3]);

    /* Backup first 1KB of VRAM */
    UINT8 vramBackup[1024];
    ReadPhys(0xC0000000ULL, vramBackup, sizeof(vramBackup));

    printf("\n--- Test: Write NOP PM4 at VRAM offset 0, submit via WPTR ---\n");

    /* Write a NOP + WRITE_DATA to scratch */
    UINT32 pm4[6];
    pm4[0] = MakeType3Hdr(0x10, 1); /* NOP */
    pm4[1] = MakeType3Hdr(0x37, 5); /* IT_WRITE_DATA, count=5 */
    pm4[2] = 0x00000000; /* engine_sel=ME */
    pm4[3] = 0x000032D4; /* addr low (scratch) */
    pm4[4] = 0x00000000; /* addr hi */
    pm4[5] = 0xCAFEBABE; /* data */
    WritePhys(0xC0000000ULL, pm4, sizeof(pm4));

    /* Backup old RPTR/WPTR */
    UINT32 oldRptr = R(0xDB6C);
    UINT32 oldWptr = R(0xDB78);

    printf("  Old: RPTR=0x%08X WPTR=0x%08X SCRATCH=0x%08X\n",
        oldRptr, oldWptr, R(0x32D4));

    /* Set WPTR = 24 (6 DWORDs = 24 bytes of PM4) */
    UINT32 newWptr = 24;
    UINT32 oldDb80 = R(0xDB80);

    /* Check if DB80 bit 22 needs to be set for ring enable */
    printf("  DB80 = 0x%08X (bit22=%s)\n", oldDb80,
        (oldDb80 & 0x00400000) ? "SET" : "CLEAR");

    /* Try: set WPTR */
    W(0xDB78, newWptr);
    printf("  WPTR set to %u: RPTR=0x%08X WPTR=0x%08X\n",
        newWptr, R(0xDB6C), R(0xDB78));

    /* Poll for RPTR change */
    for (int i = 0; i < 10; i++) {
        Sleep(20);
        UINT32 rptr = R(0xDB6C);
        UINT32 scratch = R(0x32D4);
        printf("  t=%dms: RPTR=0x%08X SCRATCH=0x%08X %s\n",
            (i+1)*20, rptr, scratch,
            (rptr >= newWptr) ? "RPTR ADVANCED!" : "");
        if (rptr >= newWptr) break;
    }

    /* Restore VRAM */
    WritePhys(0xC0000000ULL, vramBackup, sizeof(vramBackup));
    W(0xDB78, oldWptr);

    /* Try different approach: check if ring is at 0xC0100000 (our test area) */
    printf("\n--- Test: Ring at 0xC0100000 VS 0xC0000000 ---\n");

    /* Read what's at 0xC0100000 area */
    UINT32 memTest[4];
    ReadPhys(0xC0100000ULL, memTest, 16);
    printf("  At 0xC0100000: %08X %08X %08X %08X\n",
        memTest[0], memTest[1], memTest[2], memTest[3]);

    /* Check ring enable bit in DB68 */
    printf("\n  DB68 (CNTL) = 0x%08X\n", R(0xDB68));
    printf("  RLC_CP_SCHED (0xECA8) = 0x%08X\n", R(0xECA8));

    /* Check GRBM status */
    printf("  GRBM_STATUS = 0x%08X\n", R(0x3260));

    /* Try to enable ring via CP_MEC_CNTL */
    printf("\n  CP_MEC_CNTL (0x4B14) = 0x%08X\n", R(0x4B14));
    printf("  MEC_ME1_CNTL (0x7A00) = 0x%08X\n", R(0x7A00));

    CloseHandle(gH);
    printf("\nDone\n");
    return 0;
}
