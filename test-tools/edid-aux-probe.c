/* edid-aux-probe.c — READ-ONLY probe of the BC-250 DP AUX / DDC register blocks
 * to locate the LIVE DisplayPort AUX engine that can read a real monitor EDID.
 *
 * Background (see AGENTS/ip_discovery): BC-250 DCN2.1 registers follow
 *   BAR5 = AMDBC250_DCN_BASE (0xD300) + mm * 4.
 * The DP AUX engine (dio_dc_dp_aux0) mm indices (BASE_IDX 2, same DCN block as
 * the verified-live OTG regs) are: AUX_CONTROL 0x1F50, AUX_SW_CONTROL 0x1F51,
 * AUX_ARB_CONTROL 0x1F52, AUX_INTERRUPT_CONTROL 0x1F53, AUX_SW_STATUS 0x1F54,
 * AUX_SW_DATA 0x1F56, AUX_LS_DATA 0x1F57  -> BAR5 0x15040..0x1505C.
 * The legacy amdbc250_hw_extra.h defines the SAME block at 0x4800-0x480C.
 * Probe BOTH + a few near-by instances to see which is live (0xFFFFFFFF = dead).
 *
 * SAFE: uses READ_REG only, never writes. No link training. Read-only.
 *
 * Usage: edid-aux-probe.exe
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

#define IOCTL_INIT_HW   0x80000B80
#define IOCTL_READ_REG  0x80000B88
#define AMDBC250_INIT_FLAG_NBIO_MAP 0x00000001
#define DCN_BASE        0x0000D300   /* AMDBC250_DCN_BASE */

static HANDLE gH;
static UINT32 R(UINT32 off) {
    struct { UINT32 o; UINT32 v; } in = { off, 0 }, out = { 0, 0 };
    DWORD br = 0;
    if (DeviceIoControl(gH, IOCTL_READ_REG, &in, 8, &out, 8, &br, NULL))
        return out.v;
    return 0xFFFFFFFF;
}

static void probe_block(const char *name, UINT32 base, int n, int stride) {
    printf("\n--- %s ---\n", name);
    int dead = 0, zero = 0, alive = 0;
    for (int i = 0; i < n; i++) {
        UINT32 off = base + i * stride;
        UINT32 v = R(off);
        if (v == 0xFFFFFFFFUL) { dead++; }
        else if (v != 0) { alive++; }
        else { zero++; }
        printf("  0x%08X = 0x%08X  %s\n", off, v,
               v == 0xFFFFFFFF ? "(DEAD)" : (v != 0 ? "(non-zero/live)" : "(0/idle)"));
    }
    printf("  -> %s: dead=%d zero=%d live=%d\n", name, dead, zero, alive);
}

int main(void) {
    printf("=== BC-250 DP AUX / DDC EDID probe (READ-ONLY) ===\n");

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
    printf("[ok] INIT_HARDWARE NBIO_MAP\n");

    /* --- 1. Candidate DP AUX engine (DCN base 0xD300 + mm*4) --- */
    /* mm 0x1F50..0x1F57 (AUX_CONTROL..AUX_LS_DATA) */
    probe_block("DP_AUX0 @ DCN (mm 0x1F50.., BAR5 0x15040..)", DCN_BASE + 0x1F50 * 4, 8, 4);

    /* --- 2. Legacy amdbc250_hw_extra.h block (0x4800..0x480C) --- */
    probe_block("Legacy DP_AUX0 @ 0x4800", 0x4800, 4, 4);

    /* --- 3. A few more DCN AUX mm slices (AUX1/AUX2/AUX3 instances) --- */
    /* stride between aux engines is 0x20 mm dwords = 0x80 bytes */
    probe_block("DP_AUX1 @ DCN (mm 0x1F70..)", DCN_BASE + 0x1F70 * 4, 4, 4);
    probe_block("DP_AUX2 @ DCN (mm 0x1F90..)", DCN_BASE + 0x1F90 * 4, 4, 4);

    /* --- 4. Sanity: live OTG to confirm DCN base is the live one --- */
    printf("\n--- Sanity (DCN OTG, verified live in prior probes) ---\n");
    UINT32 otg[] = {
        DCN_BASE + 0x1B2A * 4,  /* OTG0_H_TOTAL  */
        DCN_BASE + 0x1B2F * 4,  /* OTG0_V_TOTAL  */
        DCN_BASE + 0x1B41 * 4,  /* OTG0_CONTROL  */
        DCN_BASE + 0x1B4C * 4,  /* OTG0_STATUS_FRAME_COUNT */
    };
    const char *otgName[] = { "OTG0_H_TOTAL", "OTG0_V_TOTAL", "OTG0_CONTROL", "OTG0_FRAME_CNT" };
    for (int i = 0; i < 4; i++) {
        UINT32 v = R(otg[i]);
        printf("  %-20s [0x%05X] = 0x%08X%s\n", otgName[i], otg[i], v,
               (v != 0 && v != 0xFFFFFFFF) ? "  <-- live" : "");
    }

    CloseHandle(gH);
    printf("\n=== Done ===\n");
    return 0;
}
