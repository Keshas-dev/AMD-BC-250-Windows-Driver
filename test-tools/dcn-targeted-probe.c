/* dcn-targeted-probe.c — safe targeted DCN register probe for BC-250.
 * Determines the REAL DCN base address by testing the two candidate
 * DCE base addresses from ip_discovery (DMU/0: idx2=0x34C0, idx3=0x9000,
 * byte units, same convention as GC_BASE=0x1260).
 *
 * Register byte offsets derived from Linux dcn_2_0_0_offset.h (DCN2.0):
 *   OTG0_OTG_CONTROL              0x1b41 -> 0x6D04
 *   OTG0_OTG_STATUS               0x1b49 -> 0x6D24
 *   OTG0_OTG_STATUS_FRAME_COUNT   0x1b4c -> 0x6D30
 *   OTG0_OTG_H_TOTAL              0x1b2a -> 0x6CA8
 *   OTG0_OTG_V_TOTAL              0x1b2f -> 0x6CBC
 *   OTG3_OTG_CONTROL              0x1cc1 -> 0x7304
 *   OTG3_OTG_STATUS               0x1cc9 -> 0x7324
 *   OTG3_OTG_STATUS_FRAME_COUNT   0x1ccc -> 0x7330
 *   OTG3_OTG_H_TOTAL              0x1caa -> 0x72A8
 *   OTG3_OTG_V_TOTAL              0x1caf -> 0x72BC
 *   HUBPREQ0_DCSURF_SURFACE_PITCH           0x0607 -> 0x181C
 *   HUBPREQ0_DCSURF_PRIMARY_SURFACE_ADDRESS  0x060a -> 0x1828
 *   HUBPREQ0_DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH 0x060b -> 0x182C
 *   HUBPREQ0_HUBPREQ_MEM_PWR_CTRL           0x0661 -> 0x1984
 *   DMCUB_REGION0_OFFSET          0x3238 -> 0xC8E0
 *
 * Real BAR5 addr = DCE_base + byte_offset (both candidate bases tested).
 * This is READ-ONLY (no writes) — safe to run.
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

static UINT32 frame_delta(UINT32 off, int samples, int delay_ms) {
    /* total delta of a monotonically increasing counter; 0 == static */
    UINT32 prev = R(off);
    UINT32 total = 0;
    for (int i = 0; i < samples; i++) {
        Sleep(delay_ms);
        UINT32 cur = R(off);
        total += (UINT32)(cur - prev);
        prev = cur;
    }
    return total;
}

static void probe_base(const char* name, UINT32 base) {
    /* OTG0 */
    UINT32 otg0_ctrl   = R(base + 0x6D04);
    UINT32 otg0_status = R(base + 0x6D24);
    UINT32 otg0_htot   = R(base + 0x6CA8);
    UINT32 otg0_vtot   = R(base + 0x6CBC);
    UINT32 otg0_fc     = R(base + 0x6D30);
    /* OTG3 */
    UINT32 otg3_ctrl   = R(base + 0x7304);
    UINT32 otg3_status = R(base + 0x7324);
    UINT32 otg3_htot   = R(base + 0x72A8);
    UINT32 otg3_vtot   = R(base + 0x72BC);
    UINT32 otg3_fc     = R(base + 0x7330);
    /* HUBPREQ0 */
    UINT32 hub_pitch   = R(base + 0x181C);
    UINT32 hub_sa      = R(base + 0x1828);
    UINT32 hub_sahi    = R(base + 0x182C);
    UINT32 hub_pwr     = R(base + 0x1984);
    /* DMCUB */
    UINT32 dmcub_off   = R(base + 0xC8E0);

    printf("[%s base 0x%04X]\n", name, base);
    printf("  OTG0: ctrl=0x%08X st=0x%08X H=%u V=%u fc=0x%08X\n",
           otg0_ctrl, otg0_status,
           (otg0_htot==0xFFFFFFFF?0:otg0_htot), (otg0_vtot==0xFFFFFFFF?0:otg0_vtot), otg0_fc);
    printf("  OTG3: ctrl=0x%08X st=0x%08X H=%u V=%u fc=0x%08X\n",
           otg3_ctrl, otg3_status,
           (otg3_htot==0xFFFFFFFF?0:otg3_htot), (otg3_vtot==0xFFFFFFFF?0:otg3_vtot), otg3_fc);
    printf("  HUBP: pitch=0x%08X surf=0x%08X%08X pwr=0x%08X\n",
           hub_pitch, hub_sahi, hub_sa, hub_pwr);
    printf("  DMCUB: region0_off=0x%08X\n", dmcub_off);

    /* Liveness: which OTG frame counter actually ticks? */
    UINT32 fc0 = frame_delta(base + 0x6D30, 3, 200);
    UINT32 fc3 = frame_delta(base + 0x7330, 3, 200);
    printf("  liveness(delta~0.6s): OTG0_fc=0x%X OTG3_fc=0x%X %s%s\n\n",
           fc0, fc3,
           fc0 ? "<-- OTG0 LIVE" : "", fc3 ? "<-- OTG3 LIVE" : "");
}

int main(void) {
    printf("=== BC-250 Targeted DCN Probe (find real DCN base) ===\n\n");

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

    /* Sanity: DCE_VERSION (mm 0x005e, BASE_IDX=1). BASE_IDX1 base in
     * DWORD units (0xC0) -> byte = 0xC0*4 = 0x300. Byte = 0x300 + 0x178 = 0x478.
     * OTG0_PIXEL_RATE_CNTL (mm 0x80, BASE_IDX=1): 0x300 + 0x200 = 0x500. */
    printf("--- DCE sanity (low, non-freeze) ---\n");
    UINT32 dce_ver = R(0x300 + 0x5e * 4);   /* 0x478 */
    UINT32 otg0_prc = R(0x300 + 0x80 * 4);  /* 0x500 */
    UINT32 otg3_prc = R(0x300 + 0x8c * 4);  /* 0x530 */
    printf("  DCE_VERSION  [0x478] = 0x%08X\n", dce_ver);
    printf("  OTG0_PIXEL_RATE_CNTL [0x500] = 0x%08X\n", otg0_prc);
    printf("  OTG3_PIXEL_RATE_CNTL [0x530] = 0x%08X\n", otg3_prc);
    if (dce_ver != 0 && dce_ver != 0xFFFFFFFF)
        printf("  DCE_VERSION non-zero => BASE_IDX=1 base 0xC0(dword)=0x300 is real\n");
    printf("\n");

    /* Candidate DCN bases: ip_discovery DMU/0 bases are in DWORD units,
     * multiply by 4 for BAR5 bytes (0x34C0 dword -> 0xD300 byte,
     * 0x9000 dword -> 0x24000 byte). 0xD300 showed live DCHUBBUB/VTG. */
    probe_base("DCN base 0xD300", 0xD300);
    probe_base("DCN base 0x24000", 0x24000);

    /* Reference: existing hw.h-style map (OTG base 0x6000) for comparison */
    printf("--- Reference (hw.h-style map, OTG base 0x6000) ---\n");
    UINT32 r3_ctrl = R(0x6300);
    UINT32 r3_fc   = R(0x6300 + 0x1C); /* STATUS_FRAME_COUNT guess */
    UINT32 d3      = frame_delta(0x6300 + 0x1C, 3, 200);
    printf("  OTG3@0x6300: ctrl=0x%08X fc=0x%08X delta=0x%X %s\n\n",
           r3_ctrl, r3_fc, d3, d3 ? "<-- LIVE" : "(static)");

    /* DCHUBBUB/VTG liveness — these MUST be live if DCN is scanning out.
     * mm DCHUBBUB_CTRL_STATUS=0x0534, VTG0_CONTROL=0x0528, BASE_IDX=2.
     * Probe several candidate DCE bases to find which one has live DCN. */
    printf("--- DCHUBBUB/VTG liveness across candidate bases ---\n");
    UINT32 bases[] = { 0xD300, 0x24000, 0x34C0, 0x9000, 0x6000, 0x5000 };
    for (int i = 0; i < sizeof(bases)/sizeof(bases[0]); i++) {
        UINT32 b = bases[i];
        UINT32 dchub_status = R(b + 0x534 * 4);
        UINT32 dchub_clk    = R(b + 0x52F * 4);
        UINT32 vtg0         = R(b + 0x528 * 4);
        UINT32 vtg1         = R(b + 0x529 * 4);
        printf("  base 0x%04X: DCHUBBUB_CTRL_STATUS=0x%08X CLK_CNTL=0x%08X VTG0=0x%08X VTG1=0x%08X%s\n",
               b, dchub_status, dchub_clk, vtg0, vtg1,
               (dchub_status != 0 && dchub_status != 0xFFFFFFFF) ? " <-- possible live DCN" : "");
    }
    printf("\n");

    /* Long OTG3 frame-counter measurement at the old hw.h address (0x631C)
     * and at the Linux-corrected candidate addresses, to catch slow ticks. */
    printf("--- Long frame-counter measurement (~1.2s) ---\n");
    UINT32 fc_candidates[] = {
        0x631C,            /* hw.h-style OTG3 STATUS_FRAME_COUNT guess */
        0xD300 + 0x6D30,   /* OTG0 fc, DCN base 0xD300 */
        0xD300 + 0x7330,   /* OTG3 fc, DCN base 0xD300 */
        0x24000 + 0x6D30,  /* OTG0 fc, DCN base 0x24000 */
        0x24000 + 0x7330,  /* OTG3 fc, DCN base 0x24000 */
    };
    for (int i = 0; i < sizeof(fc_candidates)/sizeof(fc_candidates[0]); i++) {
        UINT32 a = fc_candidates[i];
        UINT32 first = R(a);
        UINT32 delta = frame_delta(a, 4, 300);
        printf("  fc[0x%04X] = 0x%08X delta=0x%X %s\n", a, first, delta,
               delta ? "<-- LIVE" : "(static)");
    }
    printf("\n");

    CloseHandle(gH);
    printf("=== Done ===\n");
    return 0;
}
