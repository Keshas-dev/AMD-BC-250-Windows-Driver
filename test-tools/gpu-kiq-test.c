#include <windows.h>
#include <stdio.h>

#define IOCTL_AMDBC250_INIT_HARDWARE  0x80000B80
#define IOCTL_AMDBC250_GPU_KIQ_TEST   0x80000BD0
#define AMDBC250_INIT_FLAG_NBIO_MAP   0x00000001

typedef struct {
    UINT32 Result;
    UINT32 ScratchBefore;
    UINT32 ScratchAfter;
    UINT32 MmioMapped;
    UINT32 RingAllocated;
    UINT32 HqdProgrammed;
    UINT32 Pm4Submitted;
    UINT32 UseIB;
    UINT32 HqdRptr;
    UINT32 KiQ_RP;
    UINT32 GrbmStat;
    UINT32 FwLoaded;
    UINT32 MecCntlBefore;
    UINT32 MecUnhalted;
    UINT32 ScratchAfter2;
    UINT32 HqdRptr2;
    UINT32 KiQ_RP2;
    UINT32 GrbmStat2;
    UINT32 FbLocationBase;
    UINT32 FbOffset;
    UINT64 RingGpuVa;
    UINT32 HqdPqWptrRb;
    UINT32 DoorKicked;
    UINT32 DbLoRb;
    UINT32 DbHiRb;
    UINT32 DbCtlRb;
} GPU_KIQ_TEST_OUT;

int main(int argc, char *argv[])
{
    printf("=== GPU KIQ Test ===\n");

    HANDLE h = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("Cannot open GPU driver (err=%lu)\n", GetLastError());
        return 1;
    }
    printf("GPU driver opened\n");

    DWORD br = 0;

    /* Step 1: INIT_HARDWARE with NBIO_MAP (just map MMIO, skip full GPU init) */
    printf("\n--- INIT_HARDWARE (NBIO_MAP) ---\n");
    UCHAR initIn[32] = {0};
    *(unsigned __int64*)(initIn + 0) = 0xFE800000ULL;   /* MmioPhysicalBase = BAR5 */
    *(unsigned*)(initIn + 8) = 0x00080000;               /* MmioSize = 512KB */
    *(unsigned*)(initIn + 12) = AMDBC250_INIT_FLAG_NBIO_MAP;  /* Flags = NBIO_MAP */
    *(unsigned __int64*)(initIn + 16) = 0xC0000000ULL;  /* FbPhysicalBase */
    *(unsigned*)(initIn + 24) = 0x10000000;              /* FbSize = 256MB */
    BOOL ok = DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE, initIn, sizeof(initIn), NULL, 0, &br, NULL);
    printf("INIT_HARDWARE: %s (err=%lu)\n", ok ? "OK" : "FAILED", GetLastError());

    /* Step 2: GPU KIQ Test (allocates ring, programs HQD, submits PM4) */
    printf("\n--- GPU KIQ Test (0x80000BD0) ---\n");
    GPU_KIQ_TEST_OUT kiqOut = {0};
    UCHAR empty[4] = {0};
    ok = DeviceIoControl(h, IOCTL_AMDBC250_GPU_KIQ_TEST,
        empty, sizeof(empty), &kiqOut, sizeof(kiqOut), &br, NULL);
    if (ok) {
        printf("MmioMapped=%u RingAlloc=%u HqdProg=%u Pm4Sub=%u\n",
            kiqOut.MmioMapped, kiqOut.RingAllocated, kiqOut.HqdProgrammed, kiqOut.Pm4Submitted);
        printf("SCRATCH before: 0x%08X\n", kiqOut.ScratchBefore);
        printf("SCRATCH after:  0x%08X\n", kiqOut.ScratchAfter);
        printf("Result:         0x%08X\n", kiqOut.Result);
        printf("HQD_PQ_RPTR:   0x%08X  (HW-consumed; 7=fetched+done, 0=never fetched)\n", kiqOut.HqdRptr);
        printf("KIQ_RPTR:      0x%08X\n", kiqOut.KiQ_RP);
        printf("GRBM_STATUS:    0x%08X\n", kiqOut.GrbmStat);
    printf("FW_LOADED:      %u  (1=ME/PFP/CE/MEC microcode uploaded)\n", kiqOut.FwLoaded);
        printf("CP_MEC_CNTL(before): 0x%08X  MEC_UNHALTED=%u\n", kiqOut.MecCntlBefore, kiqOut.MecUnhalted);
        printf("RingGpuVa:     0x%016llX  (GPU VA programmed into PQ_BASE; must be in VRAM window)\n", (unsigned long long)kiqOut.RingGpuVa);
        printf("FB_LOC_BASE:    0x%08X  (VRAM GPU-VA base, dmesg=0xF4000000)\n", kiqOut.FbLocationBase);
        printf("HQD_PQ_WPTR_RB: 0x%08X  (CP WPTR readback; 7=kick fetched ring)\n", kiqOut.HqdPqWptrRb);
        printf("DOORBELL_KICK:  %u  (1=wrote 64-bit WPTR to PCI BAR2)\n", kiqOut.DoorKicked);
        printf("DB_RANGE_LO:   0x%08X  DB_RANGE_HI: 0x%08X  DB_CTL: 0x%08X\n",
            kiqOut.DbLoRb, kiqOut.DbHiRb, kiqOut.DbCtlRb);
        if (kiqOut.ScratchAfter != 0x5AFEBABE) {
            printf("RETRY KICK:  HQD_PQ_RPTR=0x%08X KIQ_RPTR=0x%08X GRBM=0x%08X SCRATCH=0x%08X\n",
                kiqOut.HqdRptr2, kiqOut.KiQ_RP2, kiqOut.GrbmStat2, kiqOut.ScratchAfter2);
        }
        if (kiqOut.ScratchAfter == 0x5AFEBABE)
            printf("\n*** PM4 EXECUTED! SCRATCH = 0x5AFEBABE ***\n");
        else if (kiqOut.ScratchAfter != kiqOut.ScratchBefore)
            printf("\n*** SCRATCH CHANGED! 0x%08X -> 0x%08X ***\n",
                kiqOut.ScratchBefore, kiqOut.ScratchAfter);
        else
            printf("\nSCRATCH unchanged - PM4 did not execute\n");
    } else {
        printf("KIQ test FAILED (err=%lu)\n", GetLastError());
    }

    CloseHandle(h);
    return 0;
}
