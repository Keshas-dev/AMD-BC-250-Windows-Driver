/*++
  dump-fresh-boot.c â€” Dump ALL critical GPU registers on fresh boot state
  
  Purpose: Understand what BIOS configured before we touch anything.
  Run AFTER reboot, BEFORE any GPU_KIQ_TEST or INIT_HARDWARE.
  
  Reads via BAR5 proxy IOCTLs (0x900/0x901).
--*/

#include <windows.h>
#include <stdio.h>

#define IOCTL_AMDBC250_BAR5_READ_PROXY   0x900
#define IOCTL_AMDBC250_BAR5_WRITE_PROXY  0x901
#define IOCTL_AMDBC250_INIT_HARDWARE     0x80000B80

static HANDLE g_Dev = INVALID_HANDLE_VALUE;

static ULONG Bar5Read(ULONG offset) {
    ULONG inOffset = offset;
    ULONG outValue = 0;
    ULONG bytes = 0;
    DeviceIoControl(g_Dev, IOCTL_AMDBC250_BAR5_READ_PROXY,
                    &inOffset, sizeof(inOffset), &outValue, sizeof(outValue), &bytes, NULL);
    return outValue;
}

static void Bar5Write(ULONG offset, ULONG value) {
    ULONG params[2] = { offset, value };
    ULONG bytes = 0;
    DeviceIoControl(g_Dev, IOCTL_AMDBC250_BAR5_WRITE_PROXY,
                    params, sizeof(params), NULL, 0, &bytes, NULL);
}

#define GC 0x1260
#define R(name, off) printf("  %-40s = 0x%08X\n", name, Bar5Read(off))

int main(void) {
    g_Dev = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
                        0, NULL, OPEN_EXISTING, 0, NULL);
    if (g_Dev == INVALID_HANDLE_VALUE) {
        printf("Cannot open device (error %lu)\n", GetLastError());
        return 1;
    }

    printf("=== INITIALIZING BAR5 ===\n");
    {
        /* AMDBC250_IOCTL_INIT_HARDWARE: MmioPhysicalBase(8) + MmioSize(4) + Flags(4) = 16 bytes min */
        UCHAR initBuf[32] = {0};
        ULONGLONG mmioPA = 0xFE800000ULL;  /* BAR5 physical address */
        ULONG mmioSize = 0x80000;           /* 512KB */
        ULONG flags = 0x02;                 /* AMDBC250_INIT_FLAG_NBIO_MAP */
        ULONG bytes = 0;
        *(ULONGLONG*)&initBuf[0] = mmioPA;
        *(ULONG*)&initBuf[8] = mmioSize;
        *(ULONG*)&initBuf[12] = flags;
        BOOL ok = DeviceIoControl(g_Dev, IOCTL_AMDBC250_INIT_HARDWARE,
                                  initBuf, 16, NULL, 0, &bytes, NULL);
        printf("INIT_HARDWARE sent: PA=0x%llX Size=0x%X Flags=0x%X result=%s\n\n",
               mmioPA, mmioSize, flags, ok ? "OK" : "FAIL");
    }

    printf("=== FRESH BOOT REGISTER DUMP ===\n\n");

    /* ---- GC: GRBM, CC, SCRATCH ---- */
    printf("[GC: GRBM / CC / SCRATCH]\n");
    R("GPU_ID (0x3840)",         GC + 0x3840);
    R("GRBM_STATUS (0x3260)",    GC + 0x3260);
    R("GRBM_STATUS_SE0 (0x3268)", GC + 0x3268);
    R("GRBM_STATUS_SE1 (0x326C)", GC + 0x326C);
    R("CC_GC_SHADER_ARRAY_CONFIG (0x3264)", GC + 0x3264);
    R("SCRATCH (0x32D4)",        GC + 0x32D4);
    R("SPI_WGP_MASK (0x34FC)",   GC + 0x34FC);
    R("GRBM_GFX_INDEX (0x33C4)", GC + 0x33C4);
    printf("\n");

    /* ---- CP: ME, ring, fence (Navi10 offsets + GC_BASE) ---- */
    printf("[CP: Command Processor]\n");
    R("ME_CNTL (0x1E00)",           GC + 0x1E00);
    R("PFP_CNTL (0x1E04)",          GC + 0x1E04);
    R("CE_CNTL (0x1E08)",           GC + 0x1E08);
    R("CP_RB0_BASE (0x1A60)",       GC + 0x1A60);
    R("CP_RB0_CNTL (0x1A64)",       GC + 0x1A64);
    R("CP_RB0_RPTR (0x1A68)",       GC + 0x1A68);
    R("CP_RB0_WPTR (0x1A6C)",       GC + 0x1A6C);
    R("CP_RB1_BASE (0x1A70)",       GC + 0x1A70);
    R("CP_RB1_CNTL (0x1A74)",       GC + 0x1A74);
    R("CP_RB1_RPTR (0x1A78)",       GC + 0x1A78);
    R("CP_RB1_WPTR (0x1A7C)",       GC + 0x1A7C);
    printf("\n");

    /* ---- KIQ ---- */
    printf("[CP: KIQ Ring]\n");
    R("KIQ_BASE (0x1E60)",          GC + 0x1E60);
    R("KIQ_CNTL (0x1E68)",          GC + 0x1E68);
    R("KIQ_RPTR (0x1E6C)",          GC + 0x1E6C);
    R("KIQ_WPTR (0x1E70)",          GC + 0x1E70);
    printf("\n");

    /* ---- GCVM (BAR5 offsets, known from previous sessions) ---- */
    printf("[GCVM: Virtual Memory]\n");
    R("GCVM_L2_CNTL (0x0B360)",          0x0B360);
    R("GCVM_CONTEXT0_CNTL (0x0B460)",    0x0B460);
    R("GCVM_CONTEXT0_PT_BASE_LO (0x0B608)", 0x0B608);
    R("GCVM_CONTEXT0_PT_BASE_HI (0x0B60C)", 0x0B60C);
    printf("\n");

    /* ---- GCVM: Context0, L2 TLB, PT_BASE ---- */
    printf("[GCVM: Virtual Memory]\n");
    R("GCVM_CONTEXT0_CNTL (0x0B460)", 0x0B460);
    R("GCVM_CONTEXT0_PT_BASE_LO (0x0B608)", 0x0B608);
    R("GCVM_CONTEXT0_PT_BASE_HI (0x0B60C)", 0x0B60C);
    R("GCVM_L2_CNTL (0x0B360)",   0x0B360);
    R("L2 TLB tag (0x0B31C)",     0x0B31C);
    R("L2 TLB data1 (0x0B320)",   0x0B320);
    R("L2 TLB data2 (0x0B324)",   0x0B324);
    R("L2 TLB data3 (0x0B328)",   0x0B328);
    printf("\n");

    /* ---- Context0 registers (key offsets) ---- */
    printf("[GCVM: Context0 BIOS State]\n");
    R("CTX0_0x0B404", 0x0B404);
    R("CTX0_0x0B408", 0x0B408);
    R("CTX0_0x0B40C", 0x0B40C);
    R("CTX0_0x0B41C", 0x0B41C);
    R("CTX0_0x0B420", 0x0B420);
    R("CTX0_0x0B424", 0x0B424);
    R("CTX0_0x0B428", 0x0B428);
    R("CTX0_0x0B42C", 0x0B42C);
    R("CTX0_0x0B430", 0x0B430);
    R("CTX0_0x0B434", 0x0B434);
    R("CTX0_0x0B438", 0x0B438);
    R("CTX0_0x0B43C", 0x0B43C);
    R("CTX0_0x0B440", 0x0B440);
    R("CTX0_0x0B444", 0x0B444);
    R("CTX0_0x0B448", 0x0B448);
    R("CTX0_0x0B44C", 0x0B44C);
    R("CTX0_0x0B450", 0x0B450);
    R("CTX0_0x0B454", 0x0B454);
    R("CTX0_0x0B458", 0x0B458);
    R("CTX0_0x0B45C", 0x0B45C);
    R("CTX0_0x0B460", 0x0B460);
    R("CTX0_0x0B4C0", 0x0B4C0);
    R("CTX0_0x0B4C4", 0x0B4C4);
    R("CTX0_0x0B4C8", 0x0B4C8);
    R("CTX0_0x0B4CC", 0x0B4CC);
    R("CTX0_0x0B4D0", 0x0B4D0);
    R("CTX0_0x0B4D4", 0x0B4D4);
    printf("\n");

    /* ---- HQD: Queue registers ---- */
    printf("[HQD: Queue Registers]\n");
    R("HQD_AQ_CNTL (0xDBC0)",    GC + 0xDBC0);
    R("HQD_PQ_BASE (0xDBC8)",    GC + 0xDBC8);
    R("HQD_PQ_BASE_HI (0xDBCC)", GC + 0xDBCC);
    R("HQD_PQ_RPTR (0xDBD0)",    GC + 0xDBD0);
    R("HQD_PQ_WPTR (0xDBD4)",    GC + 0xDBD4);
    R("HQD_VMID (0xDCF0)",       GC + 0xDCF0);
    R("HQD_ACTIVE (0xDCF4)",     GC + 0xDCF4);
    printf("\n");

    /* ---- HDP (Host Data Path) ---- */
    printf("[HDP: Host Data Path]\n");
    R("MC_VM_FB_LOC_TOP (0x0524)", 0x0524);
    R("MC_VM_FB_LOC_BOTTOM (0x0528)", 0x0528);
    R("MC_VM_AGP_BASE (0x051C)", 0x051C);
    printf("\n");

    /* ---- PSP ---- */
    printf("[PSP: Mailbox]\n");
    R("C2PMSG_0 (0x16098)", 0x16098);
    R("C2PMSG_81 (0x16094)", 0x16094);
    R("C2PMSG_64 (0x16040)", 0x16040);
    printf("\n");

    /* ---- SDMA ---- */
    printf("[SDMA]\n");
    R("SDMA0_CNTL (0x10040)", 0x10040);
    R("SDMA0_RB_CNTL (0x10044)", 0x10044);
    R("SDMA0_RB_BASE (0x10048)", 0x10048);
    R("SDMA0_RB_RPTR (0x1004C)", 0x1004C);
    R("SDMA0_RB_WPTR (0x10050)", 0x10050);
    printf("\n");

    /* ---- MMHUB ---- */
    printf("[MMHUB]\n");
    R("MMHUB_FB_LOCATION (0x1A00C)", 0x1A00C);
    R("MMHUB_APT_CNTL (0x1A018)", 0x1A018);
    printf("\n");

    /* ---- THM / SMUIO / Clocks ---- */
    printf("[THM / Thermal]\n");
    R("THM_TCON (0x16500)", 0x16500);
    printf("\n");

    printf("=== END DUMP ===\n");

    CloseHandle(g_Dev);
    return 0;
}
