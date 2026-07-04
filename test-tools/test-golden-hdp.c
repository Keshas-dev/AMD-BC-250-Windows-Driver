#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>

#define IOCTL_AMDBC250_READ_REG    0x80000B88
#define IOCTL_AMDBC250_WRITE_REG   0x80000B8C
#define IOCTL_AMDBC250_INIT_HARDWARE 0x80000B80

static HANDLE OpenKmd(void) {
    return CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
}

static BOOL ReadReg(HANDLE h, UINT32 off, UINT32 *val) {
    UINT32 ra[2] = {off, 0xDEADBEEF};
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_AMDBC250_READ_REG, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
    if (ok && val) *val = ra[1];
    return ok;
}

static BOOL InitHW(HANDLE h) {
    UCHAR initIn[32] = {0}, initOut[32] = {0};
    DWORD br = 0;
    *(UINT64*)(initIn+0) = 0xFE800000ULL;
    *(UINT32*)(initIn+8) = 0x00080000;
    *(UINT32*)(initIn+12) = 1;
    *(UINT64*)(initIn+16) = 0xC0000000ULL;
    *(UINT32*)(initIn+24) = 0x10000000;
    return DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE, initIn, sizeof(initIn), initOut, sizeof(initOut), &br, NULL);
}

typedef struct { UINT32 Offset; const char *Name; } REG;

int main(int argc, char *argv[]) {
    BOOL writes = FALSE;
    for (int i = 1; i < argc; i++) if (strcmp(argv[i], "--writes") == 0) writes = TRUE;

    printf("=== Safe GPU Register Test (%s) ===\n", writes ? "READ+WRITE" : "READ-ONLY");
    HANDLE h = OpenKmd();
    if (h == INVALID_HANDLE_VALUE) { printf("Cannot open driver (err=%lu)\n", GetLastError()); return 1; }
    printf("Driver opened\n");
    if (!InitHW(h)) { printf("INIT_HARDWARE failed\n"); CloseHandle(h); return 1; }
    printf("INIT_HARDWARE OK\n\n");

    UINT32 v;

    /* PHASE 1: SCRATCH test (only register known to be writable) */
    printf("--- PHASE 1: SCRATCH (0x32D4) ---\n");
    ReadReg(h, 0x32D4, &v);
    printf("  SCRATCH = 0x%08X\n", v);

    if (writes) {
        printf("  Writing 0xDEADBEEF...\n");
        UINT32 ra[2]; DWORD br;
        ra[0] = 0x32D4; ra[1] = 0xDEADBEEF;
        DeviceIoControl(h, IOCTL_AMDBC250_WRITE_REG, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
        ReadReg(h, 0x32D4, &v);
        printf("  SCRATCH after = 0x%08X (expect 0x5EADBEEF if mask bit31)\n", v);
    }

    /* PHASE 2: GC_BASE registers (0x3200-0x32FF) */
    printf("\n--- PHASE 2: GC_BASE registers (0x3200-0x32FF) ---\n");
    REG gc[] = {
        {0x3200, "GRBM_BREAD_CRUMB"},
        {0x3204, "GRBM_BREAD_CRUMB2"},
        {0x3244, "MISC_3244"},
        {0x3260, "GRBM_STATUS"},
        {0x3264, "CC_GC_SHADER_ARRAY"},
        {0x3268, "GC_CONFIG"},
        {0x326C, "GRBM_CNTL"},
        {0x32B0, "MISC_32B0"},
        {0x32B4, "MISC_32B4"},
        {0x32CC, "MISC_32CC"},
        {0x32D4, "SCRATCH"},
        {0x32D8, "MISC_32D8"},
        {0x32E4, "MISC_32E4"},
        {0x32E8, "MISC_32E8"},
        {0x32EC, "MISC_32EC"},
    };
    for (int i = 0; i < sizeof(gc)/sizeof(gc[0]); i++) {
        ReadReg(h, gc[i].Offset, &v);
        printf("  [0x%04X] %-24s = 0x%08X\n", gc[i].Offset, gc[i].Name, v);
    }

    /* PHASE 3: HQD registers */
    printf("\n--- PHASE 3: HQD/KIQ registers ---\n");
    REG hqd[] = {
        {0xDAD8, "HQD_PQ_BASE"},
        {0xDADC, "HQD_PQ_BASE_HI"},
        {0xDAE0, "HQD_PQ_RPTR"},
        {0xDAE4, "HQD_PQ_RPTR_REPORT_ADDR"},
        {0xDAE8, "HQD_PQ_RPTR_REPORT_HI"},
        {0xDAEC, "HQD_PQ_WPTR_POLL_ADDR"},
        {0xDAF0, "HQD_PQ_WPTR_POLL_HI"},
        {0xDAF4, "HQD_PQ_DOORBELL_CTRL"},
        {0xDAFC, "HQD_PQ_CONTROL"},
        {0xDB00, "HQD_PQ_WPTR_POLL_CNTL"},
        {0xDB90, "HQD_PQ_WPTR_LO"},
        {0xDB94, "HQD_PQ_WPTR_HI"},
        {0xE060, "KIQ_BASE_LO"},
        {0xE064, "KIQ_BASE_HI"},
        {0xE068, "KIQ_CNTL"},
        {0xE06C, "KIQ_RPTR"},
        {0xE070, "KIQ_HPQ0_RPTR"},
        {0xE074, "KIQ_HPQ1_RPTR"},
        {0xE078, "KIQ_WPTR"},
    };
    for (int i = 0; i < sizeof(hqd)/sizeof(hqd[0]); i++) {
        ReadReg(h, hqd[i].Offset, &v);
        printf("  [0x%04X] %-30s = 0x%08X\n", hqd[i].Offset, hqd[i].Name, v);
    }

    /* PHASE 4: HDP registers */
    printf("\n--- PHASE 4: HDP registers ---\n");
    REG hdp[] = {
        {0x0000, "HDP_FB_OFFSET"},
        {0x0004, "HDP_FB_BASE"},
        {0x0008, "HDP_FB_TOP"},
        {0x12A0, "HDP_MEM_COHERENCY_FLUSH"},
        {0x12B0, "HDP_DEBUG0"},
        {0x12C0, "HDP_NONSURFACE_INFO"},
        {0x12C4, "HDP_NONSURFACE_SIZE"},
        {0x12C8, "HDP_NONSURFACE_BASE"},
        {0x12CC, "HDP_NONSURFACE_BASE_HI"},
    };
    for (int i = 0; i < sizeof(hdp)/sizeof(hdp[0]); i++) {
        ReadReg(h, hdp[i].Offset, &v);
        printf("  [0x%04X] %-30s = 0x%08X\n", hdp[i].Offset, hdp[i].Name, v);
    }

    /* PHASE 5: Golden register current values */
    printf("\n--- PHASE 5: Golden register values ---\n");
    REG gold[] = {
        {0x33C4, "GRBM_GFX_INDEX"},
        {0x3440, "CPG_PSP_DEBUG"},
        {0x3444, "CPC_PSP_DEBUG"},
        {0x3494, "SPI_CONFIG_CNTL"},
        {0x3498, "SPI_CONFIG_CNTL_1"},
        {0x34B8, "GE_FAST_CLKS"},
        {0x3500, "UTCL1_CTRL"},
        {0x3504, "CGTT_CPF_CLK_CTRL"},
        {0x350C, "CGTT_SPI_CLK_CTRL"},
        {0x3628, "CB_HW_CONTROL_3"},
        {0x362C, "CB_HW_CONTROL_4"},
        {0x3648, "CH_DRAM_BURST_CTRL"},
        {0x364C, "CH_PIPE_STEER"},
        {0x3650, "CH_VC5_ENABLE"},
        {0x3680, "GCR_GENERAL_CNTL"},
        {0x3798, "PA_SC_ENHANCE"},
        {0x379C, "PA_SC_BINNER_CNTL_0"},
        {0x37C4, "PA_SC_RIGHT_VERTICES"},
        {0x37FC, "TA_CNTL_AUX"},
        {0x3814, "SQ_LDS_CLK_CTRL"},
        {0x39B8, "CP_SD_CNTL"},
        {0x3BA0, "SQ_ARB_CONFIG"},
    };
    for (int i = 0; i < sizeof(gold)/sizeof(gold[0]); i++) {
        ReadReg(h, gold[i].Offset, &v);
        printf("  [0x%04X] %-24s = 0x%08X\n", gold[i].Offset, gold[i].Name, v);
    }

    /* PHASE 6: SPI/WGP registers */
    printf("\n--- PHASE 6: SPI/WGP registers ---\n");
    REG spi[] = {
        {0x34FC, "SPI_WGP_CNTL"},
        {0x34F0, "SPI_GDS_MID_ENABLE"},
    };
    for (int i = 0; i < sizeof(spi)/sizeof(spi[0]); i++) {
        ReadReg(h, spi[i].Offset, &v);
        printf("  [0x%04X] %-24s = 0x%08X\n", spi[i].Offset, spi[i].Name, v);
    }

    CloseHandle(h);
    printf("\n=== Done ===\n");
    return 0;
}
