#include <windows.h>
#include <stdio.h>

#define IOCTL_READ_REG   0x80000B88
#define IOCTL_INIT_HW    0x80000B80

static HANDLE g_hDev = INVALID_HANDLE_VALUE;

static int open_dev(void) {
    g_hDev = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_hDev == INVALID_HANDLE_VALUE) return 0;
    return 1;
}

static int init_hw(void) {
    unsigned char buf[32] = {0};
    *(unsigned long long*)(buf+0) = 0xFE800000ULL;
    *(unsigned int*)(buf+8) = 0x00080000;
    *(unsigned int*)(buf+12) = 1;
    *(unsigned long long*)(buf+16) = 0xC0000000ULL;
    *(unsigned int*)(buf+24) = 0x10000000;
    DWORD retlen;
    return DeviceIoControl(g_hDev, IOCTL_INIT_HW, buf, sizeof(buf), buf, sizeof(buf), &retlen, NULL);
}

static unsigned long rd(unsigned int off) {
    unsigned int ra[2] = { off, 0 }; DWORD retlen;
    DeviceIoControl(g_hDev, IOCTL_READ_REG, ra, sizeof(ra), ra, sizeof(ra), &retlen, NULL);
    return ra[1];
}

static void rreg(unsigned int off, const char *name) {
    printf("  [0x%05X] %-35s = 0x%08X\n", off, name, rd(off));
}

int main() {
    if (!open_dev()) { printf("FAIL open\n"); return 1; }
    if (!init_hw())  { printf("FAIL init_hw\n"); return 1; }
    printf("INIT_HW OK\n\n");

    printf("=== GFX Ring ===\n");
    rreg(0xDA60, "CP_GFX_RING0_BASE_LO");
    rreg(0xDA64, "CP_GFX_RING0_BASE_HI");
    rreg(0xDA68, "CP_GFX_RING0_CNTL");
    rreg(0xDA6C, "CP_GFX_RING0_RPTR");
    rreg(0xDA70, "CP_GFX_RING0_RPTR_ADDR_LO");
    rreg(0xDA78, "CP_GFX_RING0_WPTR");

    printf("\n=== KIQ Direct Regs ===\n");
    rreg(0xE060, "KIQ_BASE_LO");
    rreg(0xE064, "KIQ_BASE_HI");
    rreg(0xE068, "KIQ_CNTL");
    rreg(0xE06C, "KIQ_RPTR");
    rreg(0xE078, "KIQ_WPTR");

    printf("\n=== HQD (our offsets from hw.h) ===\n");
    rreg(0xDAC0, "CP_HQD_ACTIVE");
    rreg(0xDAC4, "CP_HQD_VMID");
    rreg(0xDAC8, "CP_HQD_PERSISTENT_STATE");
    rreg(0xDAD8, "CP_HQD_PQ_BASE");
    rreg(0xDADC, "CP_HQD_PQ_BASE_HI");
    rreg(0xDAE0, "CP_HQD_PQ_RPTR");
    rreg(0xDAFC, "CP_HQD_PQ_CONTROL");
    rreg(0xDAE4, "CP_HQD_PQ_RPTR_REPORT_ADDR");
    rreg(0xDAE8, "CP_HQD_PQ_RPTR_REPORT_ADDR_HI");
    rreg(0xDAEC, "CP_HQD_PQ_WPTR_POLL_ADDR");
    rreg(0xDAF0, "CP_HQD_PQ_WPTR_POLL_ADDR_HI");
    rreg(0xDB90, "CP_HQD_PQ_WPTR_LO");
    rreg(0xDB94, "CP_HQD_PQ_WPTR_HI");
    rreg(0xDB4C, "CP_HQD_EOP_BASE_ADDR");
    rreg(0xDB54, "CP_HQD_EOP_CONTROL");
    rreg(0xDAF4, "CP_HQD_PQ_DOORBELL_CONTROL");
    rreg(0xDAB8, "CP_MQD_BASE_ADDR");
    rreg(0xDB18, "CP_HQD_DEQUEUE_REQUEST");
    rreg(0xDB6C, "CP_PQ_WPTR_POLL_CNTL");

    printf("\n=== GRBM / RLC ===\n");
    rreg(0x34D0, "GRBM_GFX_INDEX (cand A)");
    rreg(0xC200, "GRBM_GFX_INDEX (cand B)");
    rreg(0xECAA, "RLC_CP_SCHEDULERS (default)");

    /* Sienna_Cichlid variant — maybe correct for BC-250 (GC 10.3.6) */
    /* SEG1(0xA000) + 0x4CA1 = 0xECA1 in DWORD units */
    unsigned int rlc_sc_offsets[] = {
        0xECAA,  /* default 0x4CAA * 4 ... wait, our hw.h formula */
        0xECA1,  /* 0xA000 + 0x4CA1 = 0xECA1 (DWORD) */
        0x3A284, /* (0xA000+0x4CA1)*4 = 0x3A284 (byte) */
        0,       /* sentinel */
    };
    for (int i = 0; rlc_sc_offsets[i]; i++) {
        unsigned long v = rd(rlc_sc_offsets[i]);
        printf("  [0x%05X] RLC_CP_SCHEDULERS candidate %d = 0x%08X\n",
            rlc_sc_offsets[i], i+1, v);
    }

    printf("\n=== CP/GRBM Status ===\n");
    rreg(0x3260, "GRBM_STATUS");
    rreg(0x32D4, "SCRATCH_REG0");

    /* Try to read from known Navi10 CP_SEQ range to find pattern */
    printf("\n=== Probe CP register map (read-only, safe) ===\n");
    for (unsigned int off = 0xC800; off <= 0xCA00; off += 64) {
        unsigned long v = rd(off);
        if (v != 0 && v != 0xFFFFFFFF)
            printf("  [0x%04X] = 0x%08X (non-zero)\n", off, v);
    }

    CloseHandle(g_hDev);
    return 0;
}
