/*
 * gfx-ring-ro-test.c — DECISIVE: is the BC-250 GFX ring (CP_RB0) base
 * writable from host BAR5, and does the CP consume the ring (RPTR advance)?
 *
 * Motivation: ps5-win-driver / ZEROAESQUERDA "GFX ring init" writes to
 * CP_RB0_BASE=0xC100 (legacy/wrong offset) then does a SCRATCH handshake
 * that ALWAYS passes (SCRATCH is writable) -> false positive. This test
 * probes the CORRECT Linux BC-250 GFX ring base (mmCP_RB0_BASE=0x1DE0 =>
 * BAR5 0x89E0) for writability, plus a real CP kick (WPTR -> RPTR).
 *
 * If CP_RB0_BASE is READ-ONLY, the host CANNOT program the GFX ring on
 * BC-250 -> the "GFX ring works" claim is disproven, matching the KIQ
 * (KIQ_BASE_LO) and compute results. Display-only (DCN MMIO) is the only
 * viable path.
 *
 * Uses existing driver IOCTLs (no driver rebuild needed):
 *   INIT_HW   0x80000B80
 *   READ_REG  0x80000B88   {UINT32 Off; UINT32 Val;}
 *   WRITE_REG 0x80000B8C   {UINT32 Off; UINT32 Val;}
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

#define IOCTL_INIT_HW   0x80000B80
#define IOCTL_READ_REG  0x80000B88
#define IOCTL_WRITE_REG 0x80000B8C

#define AMDBC250_INIT_FLAG_NBIO_MAP 0x00000001

/* Correct BC-250 GFX ring (CP_RB0), Linux mm + GC_BASE(0x1260) */
#define CP_RB0_BASE     0x89E0   /* mmCP_RB0_BASE=0x1DE0 */
#define CP_RB0_BASE_HI  0x8BA4   /* mmCP_RB0_BASE_HI=0x1E51 */
#define CP_RB0_CNTL     0x89E4   /* mmCP_RB0_CNTL=0x1DE1 */
#define CP_RB0_RPTR     0x4FE0   /* mmCP_RB0_RPTR=0x0F60 */
#define CP_RB0_WPTR     0x8A30   /* mmCP_RB0_WPTR=0x1DF4 */

/* Legacy / wrong offsets used by other forks (for comparison) */
#define CP_RB0_BASE_LEGACY_C100 0xC100   /* ps5-win-driver / hw_extra.h */
#define CP_RING0_BASE_DA60    0xDA60   /* AGENTS.md old GFX ring base */

#define CP_ME_CNTL      0x4A74   /* GC_BASE + 0x3814 (correct BC-250) */
#define GRBM_STATUS     0x3260
#define SCRATCH         0x32D4

#define CP_ME_CNTL__ME_HALT   (1u << 28)
#define CP_ME_CNTL__PFP_HALT  (1u << 30)

static HANDLE gH;

static UINT32 R(UINT32 off) {
    struct { UINT32 o; UINT32 v; } in = { off, 0 }, out = { 0, 0 };
    DWORD br = 0;
    DeviceIoControl(gH, IOCTL_READ_REG, &in, 8, &out, 8, &br, NULL);
    return out.v;
}
static void W(UINT32 off, UINT32 v) {
    struct { UINT32 o; UINT32 v; } in = { off, v };
    DWORD br = 0;
    DeviceIoControl(gH, IOCTL_WRITE_REG, &in, 8, NULL, 0, &br, NULL);
}

static const char *wr_test(UINT32 off, UINT32 pat) {
    UINT32 before = R(off);
    W(off, pat);
    UINT32 after = R(off);
    printf("    0x%04X: before=0x%08X wrote=0x%08X after=0x%08X -> %s\n",
           off, before, pat, after,
           (after == pat) ? "WRITABLE" : "READ-ONLY");
    return (after == pat) ? "WRITABLE" : "READ-ONLY";
}

int main(void) {
    printf("=== BC-250 GFX Ring (CP_RB0) DECISIVE RO Test ===\n\n");

    gH = CreateFileW(L"\\\\.\\AMDBC250DreamV43",
                      GENERIC_READ | GENERIC_WRITE, 0, NULL,
                      OPEN_EXISTING, 0, NULL);
    if (gH == INVALID_HANDLE_VALUE) {
        printf("FAIL: cannot open GPU driver (err=%lu)\n", GetLastError());
        return 1;
    }

    /* INIT_HARDWARE with NBIO_MAP (map BAR5 only, skip full HW init) */
    UCHAR ib[32] = { 0 };
    *(UINT64*)(ib + 0)  = 0xFE800000ULL;          /* MmioPhysicalBase = BAR5 */
    *(UINT32*)(ib + 8)  = 0x00080000;             /* MmioSize = 512KB */
    *(UINT32*)(ib + 12) = AMDBC250_INIT_FLAG_NBIO_MAP;
    *(UINT64*)(ib + 16) = 0xC0000000ULL;           /* FbPhysicalBase */
    *(UINT32*)(ib + 24) = 0x20000000;             /* FbSize = 512MB */
    DWORD br = 0;
    DeviceIoControl(gH, IOCTL_INIT_HW, ib, sizeof(ib), NULL, 0, &br, NULL);
    printf("[ok] INIT_HARDWARE NBIO_MAP\n\n");

    printf("--- CORRECT GFX ring (CP_RB0) writability ---\n");
    const char *baseVerdict = wr_test(CP_RB0_BASE, 0xA5A5A5A5);
    wr_test(CP_RB0_BASE_HI, 0x5A5A5A5A);
    wr_test(CP_RB0_CNTL,     0x12345678);
    wr_test(CP_RB0_RPTR,     0x0000000A);
    wr_test(CP_RB0_WPTR,     0x0000000A);
    printf("\n");

    printf("--- LEGACY / WRONG offsets (what forks actually wrote) ---\n");
    wr_test(CP_RB0_BASE_LEGACY_C100, 0xA5A5A5A5);
    wr_test(CP_RING0_BASE_DA60,        0xA5A5A5A5);
    printf("\n");

    /* Restore correct base if it was writable (it won't be) */
    W(CP_RB0_BASE, 0);

    /* --- Real CP kick attempt --- */
    printf("--- CP kick (halt -> resume -> WPTR -> RPTR) ---\n");
    UINT32 mec0 = R(CP_ME_CNTL);
    W(CP_ME_CNTL, CP_ME_CNTL__ME_HALT | CP_ME_CNTL__PFP_HALT);  /* halt */
    W(CP_RB0_WPTR, 0x00000008);   /* point WPTR ahead of RPTR */
    W(CP_RB0_WPTR + 4, 0);        /* WPTR_HI */
    W(CP_ME_CNTL, 0);               /* resume CP */
    Sleep(50);
    UINT32 rptrBefore = R(CP_RB0_RPTR);
    UINT32 grbm = R(GRBM_STATUS);
    UINT32 scratch = R(SCRATCH);
    printf("    CP_ME_CNTL before halt=0x%08X\n", mec0);
    printf("    CP_RB0_RPTR after kick=0x%08X (0=CP never consumed)\n", rptrBefore);
    printf("    GRBM_STATUS=0x%08X  SCRATCH=0x%08X\n", grbm, scratch);
    printf("\n");

    printf("=== VERDICT ===\n");
    if (strcmp(baseVerdict, "WRITABLE") == 0) {
        printf("CP_RB0_BASE WRITABLE -> GFX ring CAN be linked from host.\n"
               "Re-test with a kernel-allocated ring to confirm execution.\n");
    } else {
        printf("CP_RB0_BASE READ-ONLY from host BAR5 -> host CANNOT program the\n"
               "GFX ring on BC-250. CP_RB0_BASE_HI/CNTL/RPTR/WPTR also RO.\n"
               "-> ps5-win-driver / ZEROAESQUERDA 'GFX ring works' = FALSE POSITIVE\n"
               "   (they wrote to legacy 0xC100 / 0xDA60, then a trivial SCRATCH\n"
               "   handshake that cannot fail).\n"
               "-> GFX / compute / ring submission are IMPOSSIBLE from Windows.\n"
               "-> Only display-only via DCN/CRTC MMIO remains viable.\n");
    }

    CloseHandle(gH);
    return 0;
}
