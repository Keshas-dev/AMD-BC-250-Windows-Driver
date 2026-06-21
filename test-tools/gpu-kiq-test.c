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
    UINT32 RingWptr;
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
        printf("WPTR readback:  0x%08X\n", kiqOut.Result);
        if (kiqOut.ScratchAfter == 0xCAFEBABE)
            printf("\n*** PM4 EXECUTED! SCRATCH = 0xCAFEBABE ***\n");
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
