#include <windows.h>
#include <stdio.h>
#include <string.h>

#define IOCTL_AMDBC250_INIT_HARDWARE  0x80000B80
#define IOCTL_AMDBC250_REG_DUMP       0x80000BD8
#define IOCTL_AMDBC250_KIQ_NOP_TEST   0x80000BDC

typedef struct {
    UINT32 Result;
    UINT32 GpuId;
    UINT32 GrbmStatus;
    UINT32 GrbmStatusSe0;
    UINT32 GrbmStatusSe1;
    UINT32 CcShaderArrayConfig;
    UINT32 Scratch;
    UINT32 SpiWgpMask;
    UINT32 GrbmGfxIndex;
    UINT32 MeCntl;
    UINT32 PfpCntl;
    UINT32 CeCntl;
    UINT32 KiqBaseLo;
    UINT32 KiqBaseHi;
    UINT32 KiqCntl;
    UINT32 KiqRptr;
    UINT32 KiqWptr;
    UINT32 HqdActiveKiq;
    UINT32 HqdPqBaseKiq;
    UINT32 HqdPqBaseHiKiq;
    UINT32 HqdPqRptrKiq;
    UINT32 HqdPqWptrLoKiq;
    UINT32 HqdVmidKiq;
    UINT32 HqdActiveCmp;
    UINT32 HqdPqBaseCmp;
    UINT32 HqdPqBaseHiCmp;
    UINT32 HqdPqRptrCmp;
    UINT32 HqdPqWptrCmp;
    UINT32 HqdVmidCmp;
    UINT32 HqdAqCntlCmp;
    UINT32 GcvmL2Cntl;
    UINT32 GcvmContext0Cntl;
    UINT32 GcvmPtBaseLo;
    UINT32 GcvmPtBaseHi;
    UINT32 Ctx0[20];
    UINT32 RlcCntl;
    UINT32 Sdma0Cntl;
    UINT32 CpRb0BaseProbe[4];
    UINT32 CpRb1BaseProbe[4];
} REG_DUMP_OUT;

typedef struct {
    UINT32 Result;
    UINT32 ScratchBefore;
    UINT32 ScratchAfter;
    UINT32 KiqRptrBefore;
    UINT32 KiqRptrAfter;
    UINT32 KiqWptrSet;
    UINT32 RingPaLo;
    UINT32 RingPaHi;
    UINT32 MeCntlBefore;
    UINT32 MeCntlAfter;
    UINT32 GcvmContext0CntlBefore;
    UINT32 GcvmContext0CntlAfter;
} KIQ_NOP_OUT;

static const char *DevicePath = "\\\\.\\AMDBC250DreamV43";

int main(int argc, char *argv[]) {
    int doRegDump = 1, doNopTest = 1;
    if (argc > 1) {
        if (strcmp(argv[1], "dump") == 0) doNopTest = 0;
        if (strcmp(argv[1], "nop") == 0) doRegDump = 0;
    }

    printf("=== GPU Register Dump + KIQ NOP Test ===\n");

    HANDLE h = CreateFileA(DevicePath, GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("Cannot open GPU driver (err=%lu)\n", GetLastError());
        return 1;
    }
    printf("GPU driver opened\n");

    DWORD br = 0;

    /* INIT_HARDWARE to ensure BAR5 is mapped */
    printf("\n--- INIT_HARDWARE ---\n");
    UCHAR initIn[32] = {0};
    UCHAR initOut[32] = {0};
    *(UINT64*)(initIn + 0)  = 0xFE800000ULL;
    *(UINT32*)(initIn + 8)  = 0x00080000;
    *(UINT32*)(initIn + 12) = 2;
    *(UINT64*)(initIn + 16) = 0xC0000000ULL;
    *(UINT32*)(initIn + 24) = 0x10000000;
    BOOL ok = DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE,
        initIn, sizeof(initIn), initOut, sizeof(initOut), &br, NULL);
    printf("INIT_HARDWARE: %s\n", ok ? "OK" : "FAIL");

    /* Step 1: Register dump */
    if (doRegDump) {
        printf("\n=== REGISTER DUMP ===\n");
        REG_DUMP_OUT dump = {0};
        ok = DeviceIoControl(h, IOCTL_AMDBC250_REG_DUMP,
            NULL, 0, &dump, sizeof(dump), &br, NULL);
        if (!ok) {
            printf("REG_DUMP failed (err=%lu)\n", GetLastError());
        } else {
            printf("[GC]\n");
            printf("  GPU_ID                      = 0x%08X\n", dump.GpuId);
            printf("  GRBM_STATUS                 = 0x%08X\n", dump.GrbmStatus);
            printf("  GRBM_STATUS_SE0             = 0x%08X\n", dump.GrbmStatusSe0);
            printf("  GRBM_STATUS_SE1             = 0x%08X\n", dump.GrbmStatusSe1);
            printf("  CC_GC_SHADER_ARRAY_CONFIG   = 0x%08X\n", dump.CcShaderArrayConfig);
            printf("  SCRATCH                     = 0x%08X\n", dump.Scratch);
            printf("  SPI_WGP_MASK                = 0x%08X\n", dump.SpiWgpMask);
            printf("  GRBM_GFX_INDEX              = 0x%08X\n", dump.GrbmGfxIndex);

            printf("[CP]\n");
            printf("  ME_CNTL (raw 0x4A74)        = 0x%08X\n", dump.MeCntl);
            printf("  PFP_CNTL (raw 0x4A78)       = 0x%08X\n", dump.PfpCntl);
            printf("  CE_CNTL (raw 0x4A7C)        = 0x%08X\n", dump.CeCntl);

            printf("[KIQ ring]\n");
            printf("  KIQ_BASE_LO (0xE060)        = 0x%08X\n", dump.KiqBaseLo);
            printf("  KIQ_BASE_HI (0xE064)        = 0x%08X\n", dump.KiqBaseHi);
            printf("  KIQ_CNTL (0xE068)           = 0x%08X\n", dump.KiqCntl);
            printf("  KIQ_RPTR (0xE06C)           = 0x%08X\n", dump.KiqRptr);
            printf("  KIQ_WPTR (0xE078)           = 0x%08X\n", dump.KiqWptr);

            printf("[HQD KIQ queue]\n");
            printf("  HQD_ACTIVE (0xDAC0)         = 0x%08X\n", dump.HqdActiveKiq);
            printf("  HQD_VMID (0xDAC4)           = 0x%08X\n", dump.HqdVmidKiq);
            printf("  HQD_PQ_BASE (0xDAD8)        = 0x%08X\n", dump.HqdPqBaseKiq);
            printf("  HQD_PQ_BASE_HI (0xDADC)     = 0x%08X\n", dump.HqdPqBaseHiKiq);
            printf("  HQD_PQ_RPTR (0xDAE0)        = 0x%08X\n", dump.HqdPqRptrKiq);
            printf("  HQD_PQ_WPTR_LO (0xDB90)     = 0x%08X\n", dump.HqdPqWptrLoKiq);

            printf("[HQD compute/GFX ring]\n");
            printf("  HQD_AQ_CNTL (0xDBC0)        = 0x%08X\n", dump.HqdAqCntlCmp);
            printf("  HQD_PQ_BASE (0xDBC8)        = 0x%08X\n", dump.HqdPqBaseCmp);
            printf("  HQD_PQ_BASE_HI (0xDBCC)     = 0x%08X\n", dump.HqdPqBaseHiCmp);
            printf("  HQD_PQ_RPTR (0xDBD0)        = 0x%08X\n", dump.HqdPqRptrCmp);
            printf("  HQD_PQ_WPTR (0xDBD4)        = 0x%08X\n", dump.HqdPqWptrCmp);
            printf("  HQD_VMID (0xDCF0)           = 0x%08X\n", dump.HqdVmidCmp);
            printf("  HQD_ACTIVE (0xDCF4)         = 0x%08X\n", dump.HqdActiveCmp);

            printf("[GCVM]\n");
            printf("  GCVM_L2_CNTL (0x0B360)      = 0x%08X\n", dump.GcvmL2Cntl);
            printf("  GCVM_CONTEXT0_CNTL (0x0B460)= 0x%08X\n", dump.GcvmContext0Cntl);
            printf("  PT_BASE_LO (0x0B608)        = 0x%08X\n", dump.GcvmPtBaseLo);
            printf("  PT_BASE_HI (0x0B60C)        = 0x%08X\n", dump.GcvmPtBaseHi);

            printf("[BIOS Context0 TLB]\n");
            {
                int i;
                for (i = 0; i < 20; i++) {
                    printf("  0x0B4%03X                     = 0x%08X\n", 0x08 + i * 4, dump.Ctx0[i]);
                }
            }

            printf("[CP ring probe 0xDA60]\n");
            printf("  0xDA60                       = 0x%08X\n", dump.CpRb0BaseProbe[0]);
            printf("  0xDA64                       = 0x%08X\n", dump.CpRb0BaseProbe[1]);
            printf("  0xDA68                       = 0x%08X\n", dump.CpRb0BaseProbe[2]);
            printf("  0xDA6C                       = 0x%08X\n", dump.CpRb0BaseProbe[3]);
            printf("  0xDA70                       = 0x%08X\n", dump.CpRb1BaseProbe[0]);
            printf("  0xDA74                       = 0x%08X\n", dump.CpRb1BaseProbe[1]);
            printf("  0xDA78                       = 0x%08X\n", dump.CpRb1BaseProbe[2]);
            printf("  0xDA7C                       = 0x%08X\n", dump.CpRb1BaseProbe[3]);

            printf("[Other]\n");
            printf("  RLC_CNTL                     = 0x%08X\n", dump.RlcCntl);
            printf("  SDMA0_CNTL                   = 0x%08X\n", dump.Sdma0Cntl);
        }
    }

    /* Step 2: KIQ NOP test */
    if (doNopTest) {
        printf("\n=== KIQ NOP TEST ===\n");
        printf("Writing PM4 NOP + WRITE_REG(SCRATCH=0xCAFEBABE) via KIQ ring\n");
        KIQ_NOP_OUT kt = {0};
        ok = DeviceIoControl(h, IOCTL_AMDBC250_KIQ_NOP_TEST,
            NULL, 0, &kt, sizeof(kt), &br, NULL);
        if (!ok) {
            printf("KIQ_NOP_TEST failed (err=%lu)\n", GetLastError());
        } else {
            printf("  Result                      = %u", kt.Result);
            if (kt.Result == 2) printf(" (SUCCESS! PM4 executed!)\n");
            else if (kt.Result == 1) printf(" (RPTR advanced, but PM4 didn't fully execute)\n");
            else if (kt.Result == 0) printf(" (no progress — GCVM flat mapping may not exist)\n");
            else printf(" (error)\n");
            printf("  SCRATCH before              = 0x%08X\n", kt.ScratchBefore);
            printf("  SCRATCH after               = 0x%08X\n", kt.ScratchAfter);
            printf("  KIQ_RPTR before             = 0x%08X\n", kt.KiqRptrBefore);
            printf("  KIQ_RPTR after              = 0x%08X\n", kt.KiqRptrAfter);
            printf("  KIQ_WPTR set                = 0x%08X\n", kt.KiqWptrSet);
            printf("  Ring PA                     = 0x%08X%08X\n", kt.RingPaHi, kt.RingPaLo);
            printf("  ME_CNTL before              = 0x%08X\n", kt.MeCntlBefore);
            printf("  ME_CNTL after               = 0x%08X\n", kt.MeCntlAfter);
            printf("  GCVM_CTX0_CNTL before       = 0x%08X\n", kt.GcvmContext0CntlBefore);
            printf("  GCVM_CTX0_CNTL after        = 0x%08X\n", kt.GcvmContext0CntlAfter);
        }
    }

    CloseHandle(h);
    printf("\nDone.\n");
    return 0;
}
