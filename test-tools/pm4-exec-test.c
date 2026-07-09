/*
 * pm4-exec-test.c — PROVE the GPU executes PM4 packets through the KIQ ring.
 *
 * Prior PM4 tests (sw-pm4-test.c, kernel GPU_KIQ_TEST) used BROKEN packets:
 *   - kernel GPU_KIQ_TEST: 0xC0370003 (decodes to type3/count=0x37/op=NOP — a 55-DWORD garbage NOP)
 *   - sw-pm4-test.c:    control 0x10100000 (bit28 garbage) + extra ADDR_HI DWORD
 * So "GPU does not execute" was an artifact of corrupt PM4, NOT hardware.
 *
 * This test sends a CORRECT WRITE_DATA (register) PM4 that writes a marker to
 * SCRATCH (0x32D4). If SCRATCH changes -> the ring executes -> compute is reachable.
 *
 * PM4 Type3 header macro (from hw_extra.h):
 *   PM4_HDR(op, count, type) = (type<<30) | ((count&0x3FFF)<<16) | ((op&0xFF)<<8)
 *   WRITE_DATA opcode = 0x37
 *   WRITE_DATA register packet: 4 DWORDs total = [header, control, addr, data]
 *     -> count field = number of DWORDs AFTER header = 3
 *   Header = (3<<30) | (3<<16) | (0x37<<8) = 0xC0033700
 *   CONTROL (register dest): DST_SEL=register(bits[23:21]=1 -> 0x00200000)
 *                            | WR_CONFIRM(bit20 -> 0x00100000) = 0x00300000
 *   ADDR = register byte offset in GC aperture (same as MMIO offset)
 *   DATA = marker
 */

#include <windows.h>
#include <stdio.h>
#include <stdint.h>

#define IOCTL_INIT_HW    0x80000B80
#define IOCTL_READ_REG   0x80000B88
#define IOCTL_WRITE_REG  0x80000B8C
#define IOCTL_SEND_PM4  0x80000B84

/* Match AMDBC250_IOCTL_SEND_PM4 exactly (inc/amdbc250_ioctl.h) */
typedef struct _SEND_PM4 {
    UINT32 Commands[64];
    UINT32 CommandCount;
    UINT32 Padding;
    UINT64 FenceValue;
    UINT32 QueueType;
    UINT32 Padding2;
} SEND_PM4;

static HANDLE gH = INVALID_HANDLE_VALUE;

static UINT32 R(UINT32 off) {
    struct { UINT32 off; UINT32 val; } in = { off, 0 }, out = { 0, 0 };
    DWORD br = 0;
    DeviceIoControl(gH, IOCTL_READ_REG, &in, sizeof(in), &out, sizeof(out), &br, NULL);
    return out.val;
}

static void W(UINT32 off, UINT32 val) {
    struct { UINT32 off; UINT32 val; } in = { off, val };
    DWORD br = 0;
    DeviceIoControl(gH, IOCTL_WRITE_REG, &in, sizeof(in), NULL, 0, &br, NULL);
}

static BOOL send_pm4(const UINT32* cmds, UINT32 count) {
    SEND_PM4 s = { 0 };
    if (count > 64) count = 64;
    for (UINT32 i = 0; i < count; i++) s.Commands[i] = cmds[i];
    s.CommandCount = count;
    s.FenceValue = 0; /* no EOP fence — keep packet minimal */
    DWORD br = 0;
    return DeviceIoControl(gH, IOCTL_SEND_PM4, &s, sizeof(s), NULL, 0, &br, NULL);
}

int main(void) {
    printf("=== PM4 EXECUTION PROOF TEST ===\n\n");

    gH = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
                      0, NULL, OPEN_EXISTING, 0, NULL);
    if (gH == INVALID_HANDLE_VALUE) {
        printf("ERROR: cannot open GPU driver (err=%lu)\n", GetLastError());
        return 1;
    }
    printf("[ok] GPU driver opened\n");

    /* INIT_HARDWARE NBIO_MAP (same layout as sw-pm4-test.c, which works) */
    UCHAR initBuf[32] = { 0 };
    *(UINT64*)(initBuf + 0)  = 0xFE800000ULL;  /* MmioPhysicalBase */
    *(UINT32*)(initBuf + 8)  = 0x00080000;     /* MmioSize */
    *(UINT32*)(initBuf + 12) = 1;                /* Flags = NBIO_MAP */
    *(UINT64*)(initBuf + 16) = 0xC0000000ULL;
    *(UINT32*)(initBuf + 24) = 0x20000000;      /* VRAM 512MB */
    DWORD br = 0;
    BOOL ok = DeviceIoControl(gH, IOCTL_INIT_HW, initBuf, sizeof(initBuf), NULL, 0, &br, NULL);
    printf("[%s] INIT_HARDWARE (NBIO_MAP)\n", ok ? "ok" : "FAIL");

    /* ---- Pre-state ---- */
    UINT32 meCntl   = R(0x4A74);
    UINT32 grbm     = R(0x3260);
    UINT32 scratch0 = R(0x32D4);
    UINT32 kiqBase  = R(0xE060);
    printf("  ME_CNTL=0x%08X  GRBM_STATUS=0x%08X  KIQ_BASE_LO=0x%08X\n", meCntl, grbm, kiqBase);
    printf("  SCRATCH before = 0x%08X\n\n", scratch0);

    /* ---- Stage 1: baseline direct MMIO write to SCRATCH ---- */
    W(0x32D4, 0xDEADBEEF);
    UINT32 scratchMMIO = R(0x32D4);
    printf("[Stage 1] Direct MMIO write SCRATCH=0xDEADBEEF -> read 0x%08X %s\n",
           scratchMMIO, scratchMMIO == 0xDEADBEEF ? "(WRITABLE)" : "(RO/DIFF)");

    /* ---- Stage 2: PM4 WRITE_DATA (register) -> SCRATCH, try all control encodings ----
     * Header 0xC0033700 = type3 | count=3 | op=WRITE_DATA(0x37)
     *   payload = [control, addr(32b), data] = 3 DWORDs (count=3)
     * Candidate control bytes (DST_SEL=register, WR_CONFIRM):
     *   0x00300000  AMD spec: DST_SEL bits[23:21]=001 (0x200000) | WR_CONFIRM bit20 (0x100000)
     *   0x00110000  codebase test-gpu-hw-init.c: DST_REG bit16 (0x10000) | WR_CONFIRM bit20 (0x100000)
     *   0x10100000  original (broken) tests: DST_SEL bit28 (0x10000000) | WR_CONFIRM bit20
     * HW masks SCRATCH top nibble to 0x5, so 0xCAFEBABE -> 0x5AFEBABE.
     */
    typedef struct { UINT32 ctrl; const char* name; } CTRL;
    CTRL ctrls[] = {
        {0x00300000, "AMD-spec  DST_SEL[23:21]=reg | WR_CONFIRM"},
        {0x00110000, "codebase  DST_REG bit16        | WR_CONFIRM"},
        {0x10100000, "original  DST_SEL bit28        | WR_CONFIRM"},
    };
    int executed = 0;
    for (int c = 0; c < 3; c++) {
        /* baseline: MMIO-write SCRATCH to a known-different value */
        W(0x32D4, 0xDEADBEEF);
        UINT32 scratchBefore = R(0x32D4);
        UINT32 wd[] = {
            0xC0033700,     /* header */
            ctrls[c].ctrl, /* control candidate */
            0x000032D4,     /* addr: SCRATCH */
            0xCAFEBABE      /* data: marker */
        };
        ok = send_pm4(wd, 4);
        UINT32 scratchAfter = R(0x32D4);
        UINT32 want = 0xCAFEBABE & 0x0FFFFFFF;
        int hit = ((scratchAfter & 0x0FFFFFFF) == want);
        printf("[Stage 2.%d] ctrl=0x%08X (%s)\n", c, ctrls[c].ctrl, ctrls[c].name);
        printf("  SEND_PM4: %s  SCRATCH 0x%08X -> 0x%08X  %s\n",
               ok ? "ok" : "FAIL", scratchBefore, scratchAfter,
               hit ? "*** PM4 EXECUTED! ***" : "(no change)");
        if (hit) executed = 1;
    }
    printf("\n  => %s\n\n", executed
        ? "AT LEAST ONE control encoding executed PM4 on the GPU."
        : "No control encoding executed; problem is the RING PATH, not the packet.");

    /* ---- Stage 3 (only meaningful if Stage 2 executed): configure COMPUTE via PM4 ---- */
    if (executed) {
        printf("[Stage 3] Configure COMPUTE registers via PM4 WRITE_DATA (SEG1 0x120E0+)\n");
        /* DIM_X = 64 at 0x120E4 (SEG1) */
        UINT32 dimx[] = { 0xC0033700, 0x00300000, 0x000120E4, 64 };
        send_pm4(dimx, 4);
        /* DIM_Y = 1 at 0x120E8 (SEG1) */
        UINT32 dimy[] = { 0xC0033700, 0x00300000, 0x000120E8, 1 };
        send_pm4(dimy, 4);
        /* DIM_Z = 1 at 0x120EC (SEG1) */
        UINT32 dimz[] = { 0xC0033700, 0x00300000, 0x000120EC, 1 };
        send_pm4(dimz, 4);
        /* PGM_LO = 0 at 0x12110 (SEG1, no shader yet) */
        UINT32 pgmlo[] = { 0xC0033700, 0x00300000, 0x00012110, 0 };
        send_pm4(pgmlo, 4);
        /* PGM_HI = 0 at 0x12114 (SEG1) */
        UINT32 pgmhi[] = { 0xC0033700, 0x00300000, 0x00012114, 0 };
        send_pm4(pgmhi, 4);

        UINT32 rdDimX = R(0x120E4);
        UINT32 rdDimY = R(0x120E8);
        UINT32 rdDimZ = R(0x120EC);
        printf("  COMPUTE_DIM_X read = 0x%08X (want 64)\n", rdDimX);
        printf("  COMPUTE_DIM_Y read = 0x%08X (want 1)\n", rdDimY);
        printf("  COMPUTE_DIM_Z read = 0x%08X (want 1)\n", rdDimZ);

        /* ---- Stage 4: trigger dispatch (VALID) at DISPATCH_DIRECT 0x120E0 ---- */
        printf("[Stage 4] Trigger DISPATCH_DIRECT (VALID) at 0x120E0\n");
        UINT32 dd[] = { 0xC0033700, 0x00300000, 0x000120E0, 0x80000000 };
        send_pm4(dd, 4);
        /* Poll GRBM_STATUS for compute activity */
        int active = 0;
        for (int i = 0; i < 40; i++) {
            UINT32 gs = R(0x3260);
            if (gs != 0) { active = 1; printf("  GRBM_STATUS[%d]=0x%08X\n", i, gs); break; }
            Sleep(10);
        }
        UINT32 ddAfter = R(0x120E0);
        printf("  DISPATCH_DIRECT after = 0x%08X%s\n", ddAfter,
               (ddAfter & 0x80000000) == 0 ? " (VALID cleared by HW)" : " (VALID still set)");
        printf("  Compute %s\n", active ? "ENGAGED (GRBM busy)" : "idle (no GRBM activity)");
    }

    printf("\n=== Final state ===\n");
    printf("  SCRATCH(0x32D4)=0x%08X  ME_CNTL(0x4A74)=0x%08X  GRBM(0x3260)=0x%08X\n",
           R(0x32D4), R(0x4A74), R(0x3260));
    CloseHandle(gH);
    printf("=== Done ===\n");
    return 0;
}
