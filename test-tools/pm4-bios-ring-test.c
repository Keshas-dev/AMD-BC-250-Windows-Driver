#include <windows.h>
#include <stdio.h>

static HANDLE g_h;

static BOOL ReadReg(DWORD offset, DWORD *val) {
    DWORD ra[2] = { offset, 0xDEADBEEF };
    DWORD br = 0;
    BOOL ok = DeviceIoControl(g_h, 0x80000B88, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
    if (ok) *val = ra[1]; else *val = 0xDEADBEEF;
    return ok;
}

static BOOL WriteReg(DWORD offset, DWORD val) {
    DWORD ra[2] = { offset, val };
    DWORD br = 0;
    return DeviceIoControl(g_h, 0x80000B8C, ra, sizeof(ra), NULL, 0, &br, NULL);
}

static DWORD R(DWORD off) { DWORD v=0; ReadReg(off,&v); return v; }

int main(void) {
    printf("=== PM4 BIOS Ring Test v4 ===\n\n");

    g_h = CreateFileW(L"\\\\.\\AMDBC250DreamV43",
        GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (g_h == INVALID_HANDLE_VALUE) { printf("Cannot open driver\n"); return 1; }

    UCHAR ii[32]={0}; DWORD br=0;
    *(UINT64*)ii=0xFE800000ULL;
    *(UINT32*)(ii+8)=0x80000;
    *(UINT32*)(ii+12)=1;
    *(UINT64*)(ii+16)=0xC0000000ULL;
    *(UINT32*)(ii+24)=0x10000000;
    DeviceIoControl(g_h, 0x80000B80, ii, sizeof(ii), NULL, 0, &br, NULL);
    printf("INIT_HARDWARE (NBIO_MAP) done\n\n");

    printf("--- CP Misc ---\n");
    printf("  ME_CNTL      = 0x%08X\n", R(0x4A74));
    printf("  SCRATCH      = 0x%08X\n", R(0x32D4));
    printf("  GRBM_STATUS  = 0x%08X\n", R(0x3260));

    printf("\n--- HQD via broadcast (GRBM_INDEX=0xE0000000, regs at 0x1C00) ---\n");
    WriteReg(0x34D0, 0xE0000000);
    printf("  HQD_ACTIVE     (0x1C00) = 0x%08X\n", R(0x1C00));
    printf("  HQD_PQ_BASE    (0x1C04) = 0x%08X\n", R(0x1C04));
    printf("  HQD_PQ_BASE_HI (0x1C08) = 0x%08X\n", R(0x1C08));
    printf("  HQD_PQ_CNTL    (0x1C0C) = 0x%08X\n", R(0x1C0C));
    printf("  HQD_PERSISTENT (0x1C10) = 0x%08X\n", R(0x1C10));
    printf("  HQD_VMID       (0x1C14) = 0x%08X\n", R(0x1C14));
    printf("  HQD_PQ_RPTR    (0x1C18) = 0x%08X\n", R(0x1C18));
    printf("  HQD_PQ_WPTR_LO (0x1C1C) = 0x%08X\n", R(0x1C1C));

    printf("\n--- HQD via direct MMIO (0xDAC0+) ---\n");
    printf("  HQD_ACTIVE   (0xDAC0) = 0x%08X\n", R(0xDAC0));
    printf("  HQD_VMID     (0xDAC4) = 0x%08X\n", R(0xDAC4));
    printf("  HQD_PQ_BASE  (0xDAC8) = 0x%08X\n", R(0xDAC8));
    printf("  HQD_PQ_BASE_HI(0xDACC)= 0x%08X\n", R(0xDACC));
    printf("  HQD_PQ_RPTR  (0xDAD0) = 0x%08X\n", R(0xDAD0));
    printf("  HQD_PQ_WPTR  (0xDAD4) = 0x%08X\n", R(0xDAD4));
    printf("  HQD_DOORBELL (0xDAD8) = 0x%08X\n", R(0xDAD8));
    printf("  HQD_CONTEXT  (0xDADC) = 0x%08X\n", R(0xDADC));
    printf("  HQD_PQ_CNTL  (0xDAE0) = 0x%08X\n", R(0xDAE0));
    printf("  HQD_IB_RPTR  (0xDAE4) = 0x%08X\n", R(0xDAE4));
    printf("  HQD_IB_WPTR  (0xDAE8) = 0x%08X\n", R(0xDAE8));
    printf("  HQD_IB_BASE  (0xDAEC) = 0x%08X\n", R(0xDAEC));

    printf("\n--- KIQ state ---\n");
    printf("  KIQ_BASE_LO (0xE060) = 0x%08X\n", R(0xE060));
    printf("  KIQ_BASE_HI (0xE064) = 0x%08X\n", R(0xE064));
    printf("  KIQ_CNTL    (0xE068) = 0x%08X\n", R(0xE068));
    printf("  KIQ_RPTR    (0xE06C) = 0x%08X\n", R(0xE06C));
    printf("  KIQ_WPTR    (0xE078) = 0x%08X\n", R(0xE078));

    printf("\n--- GCVM ---\n");
    printf("  L2_CNTL     (0x0B360) = 0x%08X\n", R(0x0B360));
    printf("  CTX0_CNTL   (0x0B460) = 0x%08X\n", R(0x0B460));
    printf("  PT_BASE_LO  (0x0B608) = 0x%08X\n", R(0x0B608));
    printf("  PT_BASE_HI  (0x0B60C) = 0x%08X\n", R(0x0B60C));
    printf("  --- Linux offsets ---\n");
    printf("  CTX0_CNTL   (0x66C0)  = 0x%08X\n", R(0x66C0));
    printf("  CTX0_CNTL2  (0x66C4)  = 0x%08X\n", R(0x66C4));
    printf("  PT_BASE_LO  (0x6C8C)  = 0x%08X\n", R(0x6C8C));
    printf("  PT_BASE_HI  (0x6C90)  = 0x%08X\n", R(0x6C90));
    printf("  --- Wide scan for writable PT_BASE candidates ---\n");
    for (DWORD off = 0x6600; off <= 0x6D00; off += 4) {
        DWORD v = R(off);
        if (v != 0 && v != 0xFFFFFFFF) printf("  [0x%04X] = 0x%08X\n", off, v);
    }

    printf("\n--- GCVM L2 TLB (0x0B300-0x0B3FC) ---\n");
    for (DWORD off=0x0B300; off<=0x0B3FC; off+=4) {
        DWORD v=R(off);
        if (v!=0 && v!=0xFFFFFFFF) printf("  [0x%05X] = 0x%08X\n", off, v);
    }

    printf("\n--- GCVM Context0 (0x0B400-0x0B4FC) non-zero ---\n");
    for (DWORD off=0x0B400; off<=0x0B4FC; off+=4) {
        DWORD v=R(off);
        if (v!=0 && v!=0xFFFFFFFF) printf("  [0x%05X] = 0x%08X\n", off, v);
    }

    printf("\n--- Write test: 0x6C8C and 0x66C0 ---\n");
    printf("  0x6C8C before = 0x%08X\n", R(0x6C8C));
    printf("  0x66C0 before = 0x%08X\n", R(0x66C0));
    WriteReg(0x6C8C, 0x12345678);
    WriteReg(0x66C0, 0x00000001);
    printf("  0x6C8C after  = 0x%08X (expect 0x12345678)\n", R(0x6C8C));
    printf("  0x66C0 after  = 0x%08X (expect 0x00000001)\n", R(0x66C0));
    WriteReg(0x6C8C, 0x00000000);
    WriteReg(0x66C0, 0x00000000);
    printf("  0x6C8C restored= 0x%08X\n", R(0x6C8C));
    printf("  0x66C0 restored= 0x%08X\n", R(0x66C0));
    printf("  SCRATCH before = 0x%08X\n", R(0x32D4));
    struct { UINT32 Result, ScratchBefore, ScratchAfter, MmioMapped, RingAllocated, HqdProgrammed, Pm4Submitted, Padding; } kiqOut;
    memset(&kiqOut, 0, sizeof(kiqOut));
    DWORD br2 = 0;
    BOOL ok = DeviceIoControl(g_h, 0x80000BD0, NULL, 0, &kiqOut, sizeof(kiqOut), &br2, NULL);
    printf("  IOCTL: %s, bytes=%d\n", ok ? "OK" : "FAIL", br2);
    printf("  Result=0x%08X Mmio=%u Ring=%u Hqd=%u Pm4=%u\n",
        kiqOut.Result, kiqOut.MmioMapped, kiqOut.RingAllocated, kiqOut.HqdProgrammed, kiqOut.Pm4Submitted);
    printf("  ScratchBefore=0x%08X ScratchAfter=0x%08X\n", kiqOut.ScratchBefore, kiqOut.ScratchAfter);
    Sleep(100);
    printf("  SCRATCH live   = 0x%08X\n", R(0x32D4));
    if (kiqOut.ScratchAfter != kiqOut.ScratchBefore)
        printf("  >>> PM4 EXECUTED! SCRATCH changed! <<<\n");
    else
        printf("  SCRATCH unchanged\n");

    CloseHandle(g_h);
    printf("\n=== Done ===\n");
    return 0;
}
