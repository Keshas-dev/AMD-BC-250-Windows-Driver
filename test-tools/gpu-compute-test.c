/* gpu-compute-test.c — Test GPU compute path via KMD IOCTLs.
 * Allocates VRAM via ALLOC_VIDMEM, maps via MAP_VIDMEM, sends PM4.
 * Tests: VRAM alloc, VRAM map, PM4 NOP+EOP, register readback. */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <winioctl.h>

/* IOCTL codes — raw values matching KMD switch cases */
#define IOCTL_AMDBC250_INIT_HARDWARE  0x80000B80
#define IOCTL_AMDBC250_READ_REG       0x80000B88
#define IOCTL_AMDBC250_WRITE_REG      0x80000B8C
#define IOCTL_AMDBC250_SEND_PM4       0x80000B84
#define IOCTL_AMDBC250_ALLOC_VIDMEM   0x80000840
#define IOCTL_AMDBC250_FREE_VIDMEM    0x80000844
#define IOCTL_AMDBC250_MAP_VIDMEM     0x80000848
#define IOCTL_AMDBC250_UNMAP_VIDMEM   0x8000084C
#define IOCTL_AMDBC250_GET_VRAM_INFO  0x80000804

/* SEND_PM4 struct — matches KMD #pragma pack(push,8) layout:
 * [0..255]  Commands[64] (256 bytes)
 * [256..259] CommandCount (4)
 * [260..263] Padding (4)
 * [264..271] FenceValue (8)
 * [272..275] QueueType (4)
 * [276..279] Padding2 (4) = 280 total */
#pragma pack(push, 8)
typedef struct {
    ULONG Commands[64];
    ULONG CommandCount;
    ULONG Padding;
    ULONG64 FenceValue;
    ULONG QueueType;
    ULONG Padding2;
} AMDBC250_SEND_PM4;
#pragma pack(pop)

/* REG_ACCESS struct — matches KMD's AMDBC250_IOCTL_REG_ACCESS exactly (pack(8)):
 * [0..3] RegisterOffset (UINT32)
 * [4..7] Value (UINT32) */
#pragma pack(push, 8)
typedef struct {
    ULONG RegisterOffset;
    ULONG Value;
} AMDBC250_REG_ACCESS;
#pragma pack(pop)

/* PM4 type3 header: (3<<30)|((cnt-1)<<16)|(op<<8) */
#define PM4_TYPE3_HDR(op, cnt)  ((3u << 30) | (((cnt)-1) << 16) | ((op) << 8))
#define PM4_TYPE2_NOP           0x30000000

/* IT opcodes */
#define IT_NOP                  0x10
#define IT_EVENT_WRITE_EOP      0x47
#define IT_EVENT_WRITE_EOS      0x46
#define IT_ACQUIRE_MEM          0x5C

/* GC_BASE shifted registers */
#define REG_GRBM_STATUS         0x3260
#define REG_GRBM_GFX_INDEX      0x34D0
#define REG_SCRATCH_REG0        0x32D4
#define REG_SPI_PG_ENABLE       0x5C3C
#define REG_CC_GC_SHADER_ARRAY  0x9C1C
#define REG_COMPUTE_PGM_LO      0x8110
#define REG_COMPUTE_PGM_HI      0x8114
#define REG_COMPUTE_DISPATCH_INIT 0x80E0
#define REG_CP_ME_CNTL          0x4A74
#define REG_GRBM_SOFT_RESET     0x3278

static HANDLE g_hDev = INVALID_HANDLE_VALUE;

static BOOL OpenKmd(void) {
    g_hDev = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
                         0, NULL, OPEN_EXISTING, 0, NULL);
    return g_hDev != INVALID_HANDLE_VALUE;
}

static BOOL InitHardware(void) {
    /* KMD checks inputLen >= sizeof(AMDBC250_IOCTL_INIT_HARDWARE).
     * Header uses #pragma pack(push,8):
     *   [0..7]  MmioPhysicalBase (UINT64)
     *   [8..11] MmioSize (UINT32)
     *   [12..15] Flags (UINT32)
     *   [16..23] FbPhysicalBase (UINT64)
     *   [24..27] FbSize (UINT32)
     *   [28..31] padding (to 8-byte boundary) = 32 total */
    UCHAR buf[32] = {0};
    *(ULONG64*)(buf + 0)  = 0xFE800000ULL;  /* BAR5 */
    *(ULONG*)  (buf + 8)  = 0x80000;         /* 512KB */
    *(ULONG*)  (buf + 12) = 1;               /* NBIO_MAP */
    /* fbBase=0, fbSize=0 for auto-detect */
    DWORD br = 0;
    printf("  sizeof(buf)=%zu (expect 32)\n", sizeof(buf));
    BOOL ok = DeviceIoControl(g_hDev, IOCTL_AMDBC250_INIT_HARDWARE, buf, sizeof(buf), NULL, 0, &br, NULL);
    printf("  INIT_HARDWARE: %s (gle=%lu)\n", ok ? "OK" : "FAIL", GetLastError());
    return ok;
}

static ULONG ReadReg(ULONG offset) {
    AMDBC250_REG_ACCESS req = { offset, 0 };
    DWORD br = 0;
    BOOL ok = DeviceIoControl(g_hDev, IOCTL_AMDBC250_READ_REG, &req, sizeof(req),
                               &req, sizeof(req), &br, NULL);
    return ok ? req.Value : 0xDEADBEEF;
}

static BOOL WriteReg(ULONG offset, ULONG value) {
    AMDBC250_REG_ACCESS req = { offset, value };
    DWORD br = 0;
    return DeviceIoControl(g_hDev, IOCTL_AMDBC250_WRITE_REG, &req, sizeof(req),
                           NULL, 0, &br, NULL);
}

static BOOL SendPm4(ULONG* cmds, ULONG count, ULONG64 fence) {
    AMDBC250_SEND_PM4 req;
    memset(&req, 0, sizeof(req));
    if (count > 64) count = 64;
    memcpy(req.Commands, cmds, count * sizeof(ULONG));
    req.CommandCount = count;
    req.FenceValue = fence;
    req.QueueType = 0;
    DWORD br = 0;
    BOOL ok = DeviceIoControl(g_hDev, IOCTL_AMDBC250_SEND_PM4, &req, sizeof(req),
                               NULL, 0, &br, NULL);
    return ok;
}

/* PM4 NOP + EOP fence */
static BOOL SendNopEop(ULONG64 fenceValue) {
    ULONG cmds[8];
    int n = 0;
    cmds[n++] = PM4_TYPE2_NOP;
    cmds[n++] = PM4_TYPE3_HDR(IT_EVENT_WRITE_EOP, 5);
    cmds[n++] = 0x00000000; /* CONTROL: data_sel=0 */
    cmds[n++] = 0x00000000; /* ADDR_LO */
    cmds[n++] = 0x00000000; /* ADDR_HI */
    cmds[n++] = (ULONG)fenceValue;
    cmds[n++] = (ULONG)(fenceValue >> 32);
    cmds[n++] = PM4_TYPE2_NOP;
    return SendPm4(cmds, n, fenceValue);
}

/* PM4 NOP + ACQUIRE_MEM */
static BOOL SendNopAcquireMem(void) {
    ULONG cmds[16];
    int n = 0;
    cmds[n++] = PM4_TYPE2_NOP;
    /* ACQUIRE_MEM: op=0x5C, cnt=5, sync=1, tc=1,CBC=1, DB=1 */
    cmds[n++] = PM4_TYPE3_HDR(IT_ACQUIRE_MEM, 5);
    cmds[n++] = 0x00000000; /* CP_COHER_CNTL: all bits = 0 (no flush) */
    cmds[n++] = 0x00000000; /* CP_COHER_CNTL_HI */
    cmds[n++] = 0x00000000; /* CP_COHER_SIZE_LO */
    cmds[n++] = 0x00000000; /* CP_COHER_SIZE_HI */
    cmds[n++] = 0x00000000; /* CP_COHER_CNTL2 */
    cmds[n++] = PM4_TYPE2_NOP;
    return SendPm4(cmds, n, 0);
}

static void PrintReg(const char* name, ULONG addr) {
    ULONG val = ReadReg(addr);
    printf("  %-35s (0x%04X) = 0x%08X\n", name, addr, val);
}

int main(int argc, char** argv) {
    printf("=== GPU Compute Test ===\n\n");

    /* Step 1: Open + INIT */
    printf("[1] Open KMD\n");
    if (!OpenKmd()) {
        printf("  FAIL: CreateFile gle=%lu\n", GetLastError());
        return 1;
    }
    printf("  Opened OK\n");

    printf("[2] INIT_HARDWARE\n");
    InitHardware();

    /* Step 2: Read key registers */
    printf("\n[3] Register dump\n");
    PrintReg("GRBM_STATUS", REG_GRBM_STATUS);
    PrintReg("GRBM_GFX_INDEX", REG_GRBM_GFX_INDEX);
    PrintReg("SCRATCH_REG0", REG_SCRATCH_REG0);
    PrintReg("SPI_PG_ENABLE_STATIC_WGP_MASK", REG_SPI_PG_ENABLE);
    PrintReg("CC_GC_SHADER_ARRAY_CONFIG", REG_CC_GC_SHADER_ARRAY);
    PrintReg("COMPUTE_PGM_LO", REG_COMPUTE_PGM_LO);
    PrintReg("COMPUTE_DISPATCH_INITIATOR", REG_COMPUTE_DISPATCH_INIT);
    PrintReg("CP_ME_CNTL", REG_CP_ME_CNTL);

    /* Step 3: VRAM info */
    printf("\n[4] VRAM Info\n");
    {
        ULONG64 vramInfo[4] = {0};
        DWORD br = 0;
        BOOL ok = DeviceIoControl(g_hDev, IOCTL_AMDBC250_GET_VRAM_INFO, NULL, 0,
                                   vramInfo, sizeof(vramInfo), &br, NULL);
        if (ok) {
            printf("  VRAM total: %llu MB (0x%llX)\n", vramInfo[0] / (1024*1024), vramInfo[0]);
            printf("  VRAM free:  %llu MB\n", vramInfo[1] / (1024*1024));
        } else {
            printf("  GET_VRAM_INFO failed (gle=%lu)\n", GetLastError());
        }
    }

    /* Step 4: Allocate VRAM via KMD ALLOC_VIDMEM */
    printf("\n[4] VRAM Allocation (ALLOC_VIDMEM)\n");
    {
        ULONG inData[3] = { 65536, 0, 4096 }; /* 64KB, aligned */
        ULONG64 outData[2] = {0};
        DWORD br = 0;
        BOOL ok = DeviceIoControl(g_hDev, IOCTL_AMDBC250_ALLOC_VIDMEM,
                                   inData, sizeof(inData),
                                   outData, sizeof(outData), &br, NULL);
        if (ok && outData[0] != 0) {
            printf("  ALLOC OK: PA=0x%llX, KernelVA=%p (%llu bytes)\n",
                   outData[0], (void*)outData[1], 65536ULL);
            printf("  Note: MAP_VIDMEM returns kernel VA (unusable from user mode)\n");
            printf("  Note: ICD uses VirtualAlloc for buffer memory (UMA)\n");
        } else {
            printf("  ALLOC failed (gle=%lu)\n", GetLastError());
        }
    }

    /* Step 5: PM4 NOP+EOP fence test */
    printf("\n[5] PM4 NOP+EOP send\n");
    {
        BOOL pm4ok = SendNopEop(42);
        printf("  NOP+EOP: %s (gle=%lu)\n", pm4ok ? "OK" : "FAIL", GetLastError());
    }

    /* Step 6: PM4 ACQUIRE_MEM */
    printf("\n[6] PM4 ACQUIRE_MEM send\n");
    {
        BOOL acqOk = SendNopAcquireMem();
        printf("  ACQUIRE_MEM: %s (gle=%lu)\n", acqOk ? "OK" : "FAIL", GetLastError());
    }

    /* Step 7: SCRATCH_REG0 write from CPU */
    printf("\n[7] SCRATCH_REG0 CPU write test\n");
    {
        WriteReg(REG_SCRATCH_REG0, 0x434F4D50); /* "COMP" */
        ULONG rb = ReadReg(REG_SCRATCH_REG0);
        printf("  Wrote 0x434F4D50, read 0x%08X — %s\n",
               rb, rb == 0x434F4D50 ? "PASS (CPU can write SCRATCH)" : "FAIL");
    }

    /* Step 8: GPU-initiated write via PM4 WRITE_DATA to SCRATCH */
    printf("\n[8] PM4 WRITE_DATA → SCRATCH_REG0\n");
    {
        /* WRITE_DATA: op=0x37, cnt=5, CONFIRM=1
         * DST_SEL=SYSTEM (0x05), SRC_SEL=IMM_DATA (0x01)
         * Control: 0x00000102 = DST_SEL=REG|WR_CONFIRM
         * ADDR: GC_OFFSET(SCRATCH) = 0x32D4 → shift by GC_BASE 0x1260: already shifted
         * Data: 0x47505550 ("GPUP") */
        ULONG cmds[8];
        int n = 0;
        cmds[n++] = PM4_TYPE2_NOP;
        cmds[n++] = PM4_TYPE3_HDR(0x37, 5);  /* IT_WRITE_DATA, 5 DWORDs */
        cmds[n++] = 0x00000102;               /* CONFIRM + DST_SEL=REG */
        cmds[n++] = REG_SCRATCH_REG0;          /* DST_ADDR (register offset) */
        cmds[n++] = 0x00000000;               /* DST_ADDR_HI */
        cmds[n++] = 0x47505550;               /* DATA: "GPUP" */
        cmds[n++] = PM4_TYPE2_NOP;
        BOOL wrok = SendPm4(cmds, n, 99);
        printf("  WRITE_DATA: %s (gle=%lu)\n", wrok ? "OK" : "FAIL", GetLastError());

        /* Verify GPU wrote to SCRATCH */
        ULONG rb = ReadReg(REG_SCRATCH_REG0);
        printf("  SCRATCH after PM4: 0x%08X — %s\n", rb,
               rb == 0x47505550 ? "PASS (GPU wrote via PM4!)" :
               "NO CHANGE (CP halted or ring not processing)");
    }

    /* Step 8b: Unhalt CP (ME_CNTL = 0) + retry PM4 */
    printf("\n[8b] CP unhalt + retry PM4 WRITE_DATA\n");
    {
        /* Save current ME_CNTL */
        ULONG savedMeCntl = ReadReg(REG_CP_ME_CNTL);
        printf("  CP_ME_CNTL before unhalt: 0x%08X\n", savedMeCntl);

        /* Unhalt ME + PFP (clear bits 28,30) */
        WriteReg(REG_CP_ME_CNTL, 0);
        ULONG afterUnhalt = ReadReg(REG_CP_ME_CNTL);
        printf("  CP_ME_CNTL after unhalt:  0x%08X — %s\n", afterUnhalt,
               afterUnhalt == 0 ? "UNHALTED OK" : "STILL HALTED (SOS-locked?)");

        /* Retry PM4 WRITE_DATA */
        {
            ULONG cmds[8];
            int n = 0;
            cmds[n++] = PM4_TYPE2_NOP;
            cmds[n++] = PM4_TYPE3_HDR(0x37, 5);  /* IT_WRITE_DATA */
            cmds[n++] = 0x00000102;
            cmds[n++] = REG_SCRATCH_REG0;
            cmds[n++] = 0x00000000;
            cmds[n++] = 0x50415550;               /* DATA: "PAUP" (different pattern) */
            cmds[n++] = PM4_TYPE2_NOP;
            BOOL wrok = SendPm4(cmds, n, 99);
            printf("  WRITE_DATA after unhalt: %s (gle=%lu)\n", wrok ? "OK" : "FAIL", GetLastError());

            ULONG rb = ReadReg(REG_SCRATCH_REG0);
            printf("  SCRATCH after retry: 0x%08X — %s\n", rb,
                   rb == 0x50415550 ? "PASS (GPU wrote via PM4 after CP unhalt!)" :
                   rb == 0x47505550 ? "STILL OLD PATTERN (CP consumed previous but not this)" :
                   "NO CHANGE (CP still not processing)");
        }

        /* Restore ME_CNTL */
        if (savedMeCntl != 0) {
            WriteReg(REG_CP_ME_CNTL, savedMeCntl);
            printf("  CP_ME_CNTL restored to 0x%08X\n", savedMeCntl);
        }
    }

    /* Step 9: GRBM_SOFT_RESET (reset GFX engine) + retry PM4 */
    printf("\n[9] GRBM_SOFT_RESET + retry PM4\n");
    {
        /* Read GRBM_SOFT_RESET */
        ULONG softReset = ReadReg(REG_GRBM_SOFT_RESET);
        printf("  GRBM_SOFT_RESET before: 0x%08X\n", softReset);

        /* Write SOFT_RESET: bits 0-2 = ME/CE/REQ (reset GFX/CP/HRQ) */
        /* Set bits 0,1,2 to reset ME, CE, and all requesters */
        WriteReg(REG_GRBM_SOFT_RESET, 0x7);
        /* Small delay for reset to take effect */
        for (volatile int d = 0; d < 100000; d++) {}
        /* Clear reset bits */
        WriteReg(REG_GRBM_SOFT_RESET, 0x0);

        ULONG afterReset = ReadReg(REG_GRBM_SOFT_RESET);
        printf("  GRBM_SOFT_RESET after:  0x%08X — %s\n", afterReset,
               afterReset == 0 ? "RESET OK" : "STUCK");

        /* Unhalt CP again (reset may have re-halted) */
        WriteReg(REG_CP_ME_CNTL, 0);
        ULONG meCntl = ReadReg(REG_CP_ME_CNTL);
        printf("  CP_ME_CNTL after reset+unhalt: 0x%08X — %s\n", meCntl,
               meCntl == 0 ? "UNHALTED" : "STILL HALTED");

        /* Retry PM4 WRITE_DATA */
        {
            ULONG cmds[8];
            int n = 0;
            cmds[n++] = PM4_TYPE2_NOP;
            cmds[n++] = PM4_TYPE3_HDR(0x37, 5);
            cmds[n++] = 0x00000102;
            cmds[n++] = REG_SCRATCH_REG0;
            cmds[n++] = 0x00000000;
            cmds[n++] = 0x52455345;               /* DATA: "RESE" */
            cmds[n++] = PM4_TYPE2_NOP;
            BOOL wrok = SendPm4(cmds, n, 99);
            printf("  WRITE_DATA after reset: %s (gle=%lu)\n", wrok ? "OK" : "FAIL", GetLastError());

            ULONG rb = ReadReg(REG_SCRATCH_REG0);
            printf("  SCRATCH after reset+PM4: 0x%08X — %s\n", rb,
                   rb == 0x52455345 ? "PASS (GPU wrote via PM4 after reset!)" :
                   "NO CHANGE (ring still not processing)");
        }

        /* Final GRBM_STATUS */
        PrintReg("GRBM_STATUS after reset", REG_GRBM_STATUS);
        PrintReg("GRBM_GFX_INDEX after reset", REG_GRBM_GFX_INDEX);
    }

    /* Step 10: Final register dump */
    printf("\n[10] Register dump after compute tests\n");
    PrintReg("GRBM_STATUS", REG_GRBM_STATUS);
    PrintReg("GRBM_GFX_INDEX", REG_GRBM_GFX_INDEX);
    PrintReg("SCRATCH_REG0", REG_SCRATCH_REG0);
    PrintReg("SPI_PG_ENABLE_STATIC_WGP_MASK", REG_SPI_PG_ENABLE);
    PrintReg("CC_GC_SHADER_ARRAY_CONFIG", REG_CC_GC_SHADER_ARRAY);
    PrintReg("COMPUTE_PGM_LO", REG_COMPUTE_PGM_LO);
    PrintReg("COMPUTE_DISPATCH_INITIATOR", REG_COMPUTE_DISPATCH_INIT);
    PrintReg("CP_ME_CNTL", REG_CP_ME_CNTL);

    CloseHandle(g_hDev);
    printf("\n=== Done ===\n");
    return 0;
}
