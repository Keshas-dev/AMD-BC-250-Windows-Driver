/* dcn-live-counter.c — is the DCN scanout LIVE? Measure frame-counter
 * deltas over time on all OTG pipes, and read the scanout framebuffer
 * address + a few framebuffer pixels to confirm a real display is being
 * driven by BC-250's DCN.
 *
 * Also dumps the PIPE2 (live timing) and PIPE3 (enabled) detail regs so
 * we can identify which OTG actually drives the physical monitor.
 *
 * Uses existing driver IOCTLs (no driver rebuild needed):
 *   INIT_HW   0x80000B80
 *   READ_REG  0x80000B88   {UINT32 Off; UINT32 Val;}
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

#define IOCTL_INIT_HW   0x80000B80
#define IOCTL_READ_REG  0x80000B88
#define AMDBC250_INIT_FLAG_NBIO_MAP 0x00000001

static HANDLE gH;
static UINT32 R(UINT32 off) {
    struct { UINT32 o; UINT32 v; } in = { off, 0 }, out = { 0, 0 };
    DWORD br = 0;
    if (DeviceIoControl(gH, IOCTL_READ_REG, &in, 8, &out, 8, &br, NULL))
        return out.v;
    return 0xFFFFFFFF;
}

static UINT64 counter_delta(UINT32 off, int samples, int delay_ms) {
    /* Return total delta across 'samples' reads with delay_ms between them.
     * Returns 0 if the counter is static, nonzero if it ticks. */
    UINT32 prev = R(off);
    UINT64 total = 0;
    for (int i = 0; i < samples; i++) {
        Sleep(delay_ms);
        UINT32 cur = R(off);
        /* delta is unsigned wrap-tolerant for monotonic counters */
        total += (UINT32)(cur - prev);
        prev = cur;
    }
    return total;
}

int main(void) {
    printf("=== BC-250 DCN Live Scanout Verification ===\n\n");

    gH = CreateFileW(L"\\\\.\\AMDBC250DreamV43",
                      GENERIC_READ | GENERIC_WRITE, 0, NULL,
                      OPEN_EXISTING, 0, NULL);
    if (gH == INVALID_HANDLE_VALUE) {
        printf("FAIL: cannot open GPU driver (err=%lu)\n", GetLastError());
        return 1;
    }

    UCHAR ib[32] = { 0 };
    *(UINT64*)(ib + 0)  = 0xFE800000ULL;
    *(UINT32*)(ib + 8)  = 0x00080000;
    *(UINT32*)(ib + 12) = AMDBC250_INIT_FLAG_NBIO_MAP;
    *(UINT64*)(ib + 16) = 0xC0000000ULL;
    *(UINT32*)(ib + 24) = 0x20000000;
    DWORD br = 0;
    DeviceIoControl(gH, IOCTL_INIT_HW, ib, sizeof(ib), NULL, 0, &br, NULL);
    printf("[ok] INIT_HARDWARE NBIO_MAP\n\n");

    /* 1. Frame-counter liveness on all OTG pipes.
     *    OTGx_CRTC_STATUS (off+0x28) holds VPOS/HPOS scanline counters.
     *    OTGx (off) bit0 = OTG_CNTL enable. */
    printf("--- OTG liveness (frame/scanline counter deltas over ~1.2s) ---\n");
    for (int pipe = 0; pipe < 4; pipe++) {
        UINT32 base = 0x6000 + pipe * 0x100;
        UINT32 ctrl = R(base);
        UINT64 d_ctrl   = counter_delta(base, 3, 200);
        UINT64 d_status = counter_delta(base + 0x28, 3, 200);
        printf("OTG%d [0x%04X] ctrl=0x%08X %s | delta_ctrl=%llu delta_status=%llu %s\n",
               pipe, base, ctrl, (ctrl & 1) ? "ENABLED" : "off",
               d_ctrl, d_status,
               (d_ctrl || d_status) ? "<-- LIVE" : "");
    }

    /* 2. Which pipe has a framebuffer + timing set? */
    printf("\n--- Per-pipe timing + surface ---\n");
    for (int pipe = 0; pipe < 4; pipe++) {
        UINT32 otg  = 0x6000 + pipe * 0x100;
        UINT32 hubp = 0x5080 + pipe * 0x100;
        UINT32 vtotal = R(otg + 0x10);
        UINT32 htotal = R(otg + 0x14);
        UINT32 surf_lo = R(hubp);
        UINT32 surf_hi = R(hubp + 0x04);
        UINT32 pitch   = R(hubp + 0x0C);
        UINT32 dim     = R(hubp + 0x10);
        uint64_t surf  = ((uint64_t)surf_hi << 32) | surf_lo;
        printf("PIPE%d: V=%u H=%u surf=0x%016llX pitch=%u dim=0x%08X %s\n",
               pipe, (vtotal==0xFFFFFFFF?0:vtotal), (htotal==0xFFFFFFFF?0:htotal),
               surf, pitch, dim,
               (vtotal && vtotal!=0xFFFFFFFF) ? "<-- TIMING SET" : "");
    }

    /* 3. Read back a few pixels from each candidate framebuffer to see
     *    if it holds live desktop content (vs zeros). */
    printf("\n--- Framebuffer pixel readback (live desktop?) ---\n");
    for (int pipe = 0; pipe < 4; pipe++) {
        UINT32 hubp = 0x5080 + pipe * 0x100;
        UINT32 surf_lo = R(hubp);
        UINT32 surf_hi = R(hubp + 0x04);
        uint64_t surf = ((uint64_t)surf_hi << 32) | surf_lo;
        if (surf == 0 || surf == 0xFFFFFFFF || (surf >> 32) != 0) {
            /* skip invalid/64-bit-addressed surfaces (BAR5 probe can't reach them) */
            printf("PIPE%d surf=0x%016llX (skip: no 32-bit-low FB)\n", pipe, surf);
            continue;
        }
        UINT32 lo = (UINT32)surf;
        if (lo >= 0x80000) {
            printf("PIPE%d surf_lo=0x%08X outside BAR5 window (0x80000) - cannot read\n", pipe, lo);
            continue;
        }
        UINT32 px0  = R(lo);
        UINT32 px1  = R(lo + 0x1000);
        UINT32 px2  = R(lo + 0x100000);
        printf("PIPE%d surf=0x%08X: px0=0x%08X px1k=0x%08X px1M=0x%08X %s\n",
               pipe, lo, px0, px1, px2,
               (px0 || px1 || px2) ? "<-- content" : "(zeros)");
    }

    CloseHandle(gH);
    printf("\n=== Done ===\n");
    return 0;
}
