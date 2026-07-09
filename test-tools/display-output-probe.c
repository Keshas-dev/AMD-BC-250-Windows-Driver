/*
 * display-output-probe.c — DECISIVE: does the BC-250 have a usable
 * DisplayPort output? The DCN display ENGINE (OTG/CRTC) is known
 * alive (Pipe3 OTG counter 0x270D), but the actual DP PHY /
 * transmitter (DIO block) is what gets fused/unwired on mining
 * cards. If DIO/PHY reads DEAD (0xFFFFFFFF) or 0, there is no
 * physical output to drive -> display-only is pointless.
 *
 * Prints RAW values (distinguishing 0 vs 0xFFFFFFFF/DEAD) so we
 * can tell "PHY absent" from "PHY idle".
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

static HANDLE gH;
static UINT32 R(UINT32 off) {
    struct { UINT32 o; UINT32 v; } in = { off, 0 }, out = { 0, 0 };
    DWORD br = 0;
    DeviceIoControl(gH, IOCTL_READ_REG, &in, 8, &out, 8, &br, NULL);
    return out.v;
}

static const char *state(UINT32 v) {
    if (v == 0xFFFFFFFF) return "DEAD";
    if (v == 0) return "0";
    return "alive";
}

int main(void) {
    printf("=== BC-250 DisplayPort Output Presence (RAW) ===\n\n");

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

    /* --- DIO / PHY (DisplayPort transmitter) --- */
    printf("--- DIO/PHY (0x5C00+, DP transmitter) ---\n");
    printf("   (DEAD=0xFFFFFFFF -> PHY absent/fused; alive/0 -> present)\n");
    int dead = 0, alive = 0, zero = 0;
    for (UINT32 phy = 0; phy < 4; phy++) {
        UINT32 base = 0x5C00 + phy * 0x80;
        UINT32 v = R(base);
        printf("   DIO%d [0x%04X] = 0x%08X (%s)\n", phy, base, v, state(v));
        if (v == 0xFFFFFFFF) dead++;
        else if (v == 0) zero++;
        else alive++;
    }
    printf("   -> DIO: dead=%d zero=%d alive=%d\n\n", dead, zero, alive);

    /* --- HPD (Hot Plug Detect) --- */
    printf("--- HPD (0x5400+, monitor-connect sense) ---\n");
    for (UINT32 phy = 0; phy < 4; phy++) {
        UINT32 base = 0x5400 + phy * 0x100;
        UINT32 v = R(base);
        UINT32 v1 = R(base + 0x10);
        printf("   HPD%d [0x%04X]=0x%08X [0x%04X]=0x%08X (%s)\n",
               phy, base, v, base + 0x10, v1, state(v));
    }
    printf("   (bit31 of HPDx = CONNECTED sense; 0x80840000 looks like stale default)\n\n");

    /* --- LINK / Encoder candidates (DCN) --- */
    printf("--- LINK/Encoder candidates ---\n");
    UINT32 link[] = { 0x5900, 0x5980, 0x5A00, 0x5A80, 0x5B00, 0x5B80, 0x5E00, 0x5E80 };
    for (int i = 0; i < sizeof(link) / sizeof(link[0]); i++) {
        UINT32 v = R(link[i]);
        printf("   [0x%04X] = 0x%08X (%s)\n", link[i], v, state(v));
    }
    printf("\n");

    /* --- Re-confirm DCN engine alive --- */
    printf("--- DCN engine liveness ---\n");
    UINT32 otg3 = R(0x6300);
    printf("   OTG3 [0x6300] = 0x%08X %s (live counter if nonzero/递增)\n",
           otg3, otg3 ? "(ACTIVE)" : "(0)");

    printf("\n=== VERDICT ===\n");
    if (dead >= 3) {
        printf("DIO/PHY registers are DEAD (0xFFFFFFFF) -> DisplayPort transmitter\n"
               "is ABSENT / FUSED OFF on this BC-250 mining card.\n"
               "DCN logic exists but cannot reach a physical connector.\n"
               "=> display-only would produce NO output. NOT worth implementing.\n");
    } else if (alive >= 1) {
        printf("DIO/PHY registers are alive -> DP transmitter present.\n"
               "Display output MAY be possible (still needs link training + a plugged monitor).\n");
    } else {
        printf("DIO/PHY registers read 0 (idle but present?) -> inconclusive.\n"
               "PHY may need link training to wake; re-probe after enabling CRTC.\n");
    }

    CloseHandle(gH);
    return 0;
}
