#include <windows.h>
#include <stdio.h>

#define IOCTL_AMDBC250_INIT_HARDWARE  0x80000B80
#define IOCTL_AMDBC250_GPU_IB_TEST    0x80000BE4
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
} GPU_KIQ_TEST_OUT;

int main() {
    printf("=== GPU IB Test (bypasses KIQ/HQD) ===\n");

    HANDLE h = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("Cannot open GPU driver (err=%lu)\n", GetLastError());
        return 1;
    }
    printf("GPU driver opened\n");

    DWORD br = 0;

    printf("\n--- INIT_HARDWARE (NBIO_MAP) ---\n");
    UCHAR initIn[32] = {0};
    *(unsigned __int64*)(initIn + 0) = 0xFE800000ULL;
    *(unsigned*)(initIn + 8) = 0x00080000;
    *(unsigned*)(initIn + 12) = AMDBC250_INIT_FLAG_NBIO_MAP;
    *(unsigned __int64*)(initIn + 16) = 0xC0000000ULL;
    *(unsigned*)(initIn + 24) = 0x10000000;
    BOOL ok = DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE, initIn, sizeof(initIn), NULL, 0, &br, NULL);
    printf("INIT_HARDWARE: %s (err=%lu)\n", ok ? "OK" : "FAILED", GetLastError());

    printf("\n--- GPU IB Test (0x80000BE4) ---\n");
    GPU_KIQ_TEST_OUT ibOut = {0};
    UCHAR empty[4] = {0};
    ok = DeviceIoControl(h, IOCTL_AMDBC250_GPU_IB_TEST,
        empty, sizeof(empty), &ibOut, sizeof(ibOut), &br, NULL);
    if (ok) {
        printf("MmioMapped=%u RingAlloc=%u HqdProg=%u Pm4Sub=%u UseIB=%u\n",
            ibOut.MmioMapped, ibOut.RingAllocated, ibOut.HqdProgrammed, ibOut.Pm4Submitted, ibOut.UseIB);
        printf("SCRATCH before: 0x%08X\n", ibOut.ScratchBefore);
        printf("SCRATCH after:  0x%08X\n", ibOut.ScratchAfter);
        printf("Result: 0x%08X\n", ibOut.Result);
        if (ibOut.ScratchAfter == 0xCAFEBABE)
            printf("\n*** PM4 EXECUTED via IB! SCRATCH = 0xCAFEBABE ***\n");
        else if (ibOut.ScratchAfter != ibOut.ScratchBefore)
            printf("\n*** SCRATCH CHANGED! 0x%08X -> 0x%08X ***\n",
                ibOut.ScratchBefore, ibOut.ScratchAfter);
        else
            printf("\nSCRATCH unchanged - IB did not execute PM4\n");
    } else {
        printf("IB test FAILED (err=%lu)\n", GetLastError());
    }

    CloseHandle(h);
    printf("\n=== Done ===\n");
    return 0;
}
