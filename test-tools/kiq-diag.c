/* kiq-diag.c — Diagnostic: read all GCVM/KIQ regs after GPU_KIQ_TEST */
#include <windows.h>
#include <stdio.h>

static BOOL ReadReg(HANDLE h, unsigned offset, unsigned *val) {
    unsigned ra[2] = {offset, 0xDEADBEEF};
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, 0x80000B88, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
    if (ok) *val = ra[1]; else *val = 0xDEADBEEF;
    return ok;
}

static void DumpRegs(HANDLE h) {
    unsigned val;
    printf("\n--- Register Dump ---\n");
    /* BAR5 sanity */
    ReadReg(h, 0x3260, &val); printf("GRBM_STATUS     = 0x%08X\n", val);
    ReadReg(h, 0x32D4, &val); printf("SCRATCH         = 0x%08X\n", val);
    ReadReg(h, 0x4A74, &val); printf("ME_CNTL         = 0x%08X", val);
    if (val & (1<<28)) printf(" [ME_HALT]");
    if (val & (1<<30)) printf(" [PFP_HALT]");
    printf("\n");
    /* KIQ engine */
    ReadReg(h, 0xE060, &val); printf("KIQ_BASE_LO     = 0x%08X\n", val);
    ReadReg(h, 0xE064, &val); printf("KIQ_BASE_HI     = 0x%08X\n", val);
    ReadReg(h, 0xE06C, &val); printf("KIQ_RPTR        = 0x%08X\n", val);
    ReadReg(h, 0xE078, &val); printf("KIQ_WPTR        = 0x%08X\n", val);
    /* HQD engine */
    ReadReg(h, 0x34D0, &val); printf("GRBM_INDEX      = 0x%08X\n", val);
    ReadReg(h, 0xDAC0, &val); printf("HQD_ACTIVE      = 0x%08X\n", val);
    ReadReg(h, 0xDAD8, &val); printf("HQD_PQ_BASE_LO  = 0x%08X\n", val);
    ReadReg(h, 0xDADC, &val); printf("HQD_PQ_BASE_HI  = 0x%08X\n", val);
    ReadReg(h, 0xDAFC, &val); printf("HQD_PQ_CONTROL  = 0x%08X\n", val);
    ReadReg(h, 0xDAC4, &val); printf("HQD_VMID        = 0x%08X\n", val);
    ReadReg(h, 0xDAC8, &val); printf("HQD_PERSISTENT  = 0x%08X\n", val);
    /* GCVM OLD offsets */
    ReadReg(h, 0x0B460, &val); printf("CTX_CNTL(OLD)   = 0x%08X\n", val);
    ReadReg(h, 0x0B360, &val); printf("L2_CNTL(OLD)    = 0x%08X\n", val);
    ReadReg(h, 0x0B608, &val); printf("PT_BASE_LO(OLD) = 0x%08X\n", val);
    ReadReg(h, 0x0B60C, &val); printf("PT_BASE_HI(OLD) = 0x%08X\n", val);
    /* GCVM Linux offsets */
    ReadReg(h, 0x6AE0, &val); printf("CTX_CNTL(LNX)   = 0x%08X\n", val);
    ReadReg(h, 0x6C8C, &val); printf("PT_BASE_LO(LNX) = 0x%08X\n", val);
    ReadReg(h, 0x6C90, &val); printf("PT_BASE_HI(LNX) = 0x%08X\n", val);
    ReadReg(h, 0x69E0, &val); printf("L2_CNTL(LNX)    = 0x%08X\n", val);
    /* Wide scan Context0 area (0x0B400-0x0B4D0) */
    printf("\n--- Context0 area (OLD 0x0B400-0x0B4D0) ---\n");
    for (unsigned off = 0x0B400; off <= 0x0B4D0; off += 4) {
        ReadReg(h, off, &val);
        if (val != 0 && val != 0xFFFFFFFF) {
            printf("  [0x%05X] = 0x%08X\n", off, val);
        }
    }
}

int main(void) {
    printf("=== KIQ Diagnostic ===\n");

    HANDLE h = CreateFileW(L"\\\\.\\AMDBC250DreamV43",
        GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("Cannot open device error=%lu\n", GetLastError());
        return 1;
    }

    /* INIT_HARDWARE */
    UCHAR initIn[32] = {0}; DWORD br = 0;
    *(unsigned __int64*)(initIn + 0) = 0xFE800000ULL;
    *(unsigned*)(initIn + 8) = 0x00080000;
    *(unsigned*)(initIn + 12) = 1;
    *(unsigned __int64*)(initIn + 16) = 0xC0000000ULL;
    *(unsigned*)(initIn + 24) = 0x10000000;
    DeviceIoControl(h, 0x80000B80, initIn, sizeof(initIn), NULL, 0, &br, NULL);

    printf("=== BEFORE GPU_KIQ_TEST ===");
    DumpRegs(h);

    /* Run GPU_KIQ_TEST */
    printf("\n--- Running GPU_KIQ_TEST ---\n");
    UCHAR empty[4] = {0};
    UCHAR out[32] = {0};
    DeviceIoControl(h, 0x80000BD0, empty, sizeof(empty), out, sizeof(out), &br, NULL);
    unsigned *o = (unsigned*)out;
    printf("Result=%u ScratchBefore=0x%08X ScratchAfter=0x%08X\n", o[0], o[1], o[2]);
    printf("Mmio=%u Ring=%u Hqd=%u Pm4=%u\n", o[3], o[4], o[5], o[6]);

    printf("\n=== AFTER GPU_KIQ_TEST ===");
    DumpRegs(h);

    CloseHandle(h);
    printf("\n=== Done ===\n");
    return 0;
}
