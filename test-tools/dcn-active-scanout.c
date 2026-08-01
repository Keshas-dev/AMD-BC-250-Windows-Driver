/* dcn-active-scanout.c — find the ACTIVE DCN scanout on BC-250.
 * Probe all 4 OTG pipes for live timing (V_TOTAL/H_TOTAL non-zero) and
 * the associated HUBP surface (PRIMARY_SURFACE_ADDRESS, PITCH, DIMENSIONS).
 * Also read OTG3 timing in detail since dcn-result.txt showed OTG3 live.
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
#define AMDBC250_INIT_FLAG_NBIO_MAP 0x00000001

static HANDLE gH;
static UINT32 R(UINT32 off) {
    struct { UINT32 o; UINT32 v; } in = { off, 0 }, out = { 0, 0 };
    DWORD br = 0;
    if (DeviceIoControl(gH, IOCTL_READ_REG, &in, 8, &out, 8, &br, NULL))
        return out.v;
    return 0xFFFFFFFF;
}

int main(void) {
    printf("=== BC-250 Active DCN Scanout Probe ===\n\n");

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

    /* For each pipe: OTG base = 0x6000 + pipe*0x100, HUBP surf = 0x5080 + pipe*0x100 */
    printf("--- Per-pipe OTG + HUBP scanout state ---\n");
    for (int pipe = 0; pipe < 4; pipe++) {
        UINT32 otg    = 0x6000 + pipe * 0x100;
        UINT32 hubp   = 0x5080 + pipe * 0x100;
        UINT32 otg_ctrl = R(otg);
        UINT32 v_total  = R(otg + 0x10);
        UINT32 h_total  = R(otg + 0x14);
        UINT32 v_blank  = R(otg + 0x18);
        UINT32 h_blank  = R(otg + 0x1C);
        UINT32 v_sync   = R(otg + 0x20);
        UINT32 h_sync   = R(otg + 0x24);
        UINT32 crtc_st  = R(otg + 0x28);
        UINT32 surf_lo  = R(hubp);
        UINT32 surf_hi  = R(hubp + 0x04);
        UINT32 pitch    = R(hubp + 0x0C);
        UINT32 dim      = R(hubp + 0x10);
        UINT32 tiling   = R(hubp + 0x18);
        uint64_t surf   = ((uint64_t)surf_hi << 32) | surf_lo;

        int timing_live = (v_total != 0 && v_total != 0xFFFFFFFF) ||
                          (h_total != 0 && h_total != 0xFFFFFFFF);
        printf("PIPE %d: ctrl=0x%08X V=%u H=%u | vb=0x%08X hb=0x%08X vs=0x%08X hs=0x%08X | crtc=0x%08X\n",
               pipe, otg_ctrl,
               (v_total==0xFFFFFFFF?0:v_total), (h_total==0xFFFFFFFF?0:h_total),
               v_blank, h_blank, v_sync, h_sync, crtc_st);
        printf("        surf=0x%016llX pitch=0x%08X dim=0x%08X tiling=0x%08X %s\n",
               surf, pitch, dim, tiling,
               timing_live ? "<-- LIVE TIMING" : (otg_ctrl & 1 ? "<-- CTRL ENABLED" : ""));
    }

    printf("\n--- OTG3 detail (was live counter 0x270D) ---\n");
    for (int pipe = 3; pipe >= 0; pipe--) {
        UINT32 otg = 0x6000 + pipe * 0x100;
        UINT32 v = R(otg);
        if (v != 0 && v != 0xFFFFFFFF) {
            printf("OTG%d [0x%04X] = 0x%08X (ACTIVE)\n", pipe, otg, v);
        }
    }

    printf("\n--- Pipe scanout address candidates (VRAM FB) ---\n");
    for (int pipe = 0; pipe < 4; pipe++) {
        UINT32 hubp = 0x5080 + pipe * 0x100;
        UINT32 surf_lo = R(hubp);
        UINT32 surf_hi = R(hubp + 0x04);
        uint64_t surf = ((uint64_t)surf_hi << 32) | surf_lo;
        if (surf != 0 && surf != 0xFFFFFFFF00000000ULL && surf != 0xFFFFFFFF) {
            printf("PIPE%d surface = 0x%016llX\n", pipe, surf);
        }
    }

    printf("\n--- DMCUB liveness (command processor) ---\n");
    UINT32 dmcub[] = { 0x7000, 0x7004, 0x7010, 0x7014, 0x7018, 0x701C, 0x7020 };
    for (int i = 0; i < sizeof(dmcub)/sizeof(dmcub[0]); i++) {
        UINT32 v = R(dmcub[i]);
        printf("  DMCUB[0x%04X] = 0x%08X%s\n", dmcub[i], v, v ? " <-- non-zero" : "");
    }

    CloseHandle(gH);
    printf("\n=== Done ===\n");
    return 0;
}
