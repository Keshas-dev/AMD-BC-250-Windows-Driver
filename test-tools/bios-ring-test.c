/* bios-ring-test.c — Test KIQ_BIOS_RING_SUBMIT: uses BIOS ring, doesn't touch PT_BASE */
#include <windows.h>
#include <stdio.h>

/* IOCTL codes — must match amdbc250_ioctl.h */
#define FILE_DEVICE_AMDBC250    0x8000
#define IOCTL_INDEX             0x270
#define METHOD_BUFFERED         0
#define FILE_ANY_ACCESS         0
#define CTL_CODE_AMDBC250(x, m, a) \
    ((FILE_DEVICE_AMDBC250 << 16) | ((a) << 14) | ((IOCTL_INDEX + (x)) << 2) | (m))

#define IOCTL_AMDBC250_INIT_HARDWARE  CTL_CODE_AMDBC250(0x70, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AMDBC250_KIQ_BIOS_RING_SUBMIT CTL_CODE_AMDBC250(0x88, METHOD_BUFFERED, FILE_ANY_ACCESS)

#pragma pack(push, 1)
typedef struct {
    UINT32 Result;
    UINT32 ScratchBefore;
    UINT32 ScratchAfter;
    UINT32 KiqBaseLo;
    UINT32 KiqBaseHi;
    UINT32 KiqRptrBefore;
    UINT32 KiqRptrAfter;
    UINT32 KiqWptrSet;
    UINT32 MeCntlBefore;
    UINT32 MeCntlAfter;
    UINT32 RingDword0;
    UINT32 RingDword1;
    UINT32 RingDword2;
    UINT32 RingDword3;
} KIQ_BIOS_RING_RESULT;
#pragma pack(pop)

int main(void) {
    printf("=== BIOS Ring Submit Test ===\n");
    printf("Uses BIOS ring (0x7E522000), does NOT touch PT_BASE\n\n");

    HANDLE hDevice = CreateFileA(
        "\\\\.\\AMDBC250DreamV43",
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL);

    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("ERROR: Cannot open GPU driver (err=%lu)\n", GetLastError());
        printf("Make driver is loaded and test signing is on.\n");
        return 1;
    }
    printf("GPU driver opened\n");

    /* Step 1: INIT_HARDWARE */
    DWORD bytesReturned = 0;
    BOOL ok;
    printf("\n--- INIT_HARDWARE (NBIO_MAP) ---\n");
    {
        UCHAR initIn[32] = {0};
        UCHAR initOut[32] = {0};
        *(UINT64*)(initIn + 0)  = 0xFE800000ULL;  /* BAR5 physical */
        *(UINT32*)(initIn + 8)  = 0x00080000;      /* BAR5 size (512KB) */
        *(UINT32*)(initIn + 12) = 1;               /* Flags=1: NBIO_MAP */
        *(UINT64*)(initIn + 16) = 0xC0000000ULL;   /* BAR0 physical */
        *(UINT32*)(initIn + 24) = 0x10000000;      /* BAR0 size (256MB) */
        ok = DeviceIoControl(hDevice, 0x80000B80, initIn, sizeof(initIn),
                                  initOut, sizeof(initOut), &bytesReturned, NULL);
        printf("INIT_HARDWARE: %s\n", ok ? "OK" : "FAILED");
    }

    /* Step 2: KIQ_BIOS_RING_SUBMIT - input: actual KIQ ring PA */
    printf("\n--- KIQ_BIOS_RING_SUBMIT ---\n");
    KIQ_BIOS_RING_RESULT result = {0};
    ULONG kiqRingPa = 0x7E508000;  /* ACTUAL BIOS KIQ_BASE (not 0x7E522000) */
    {
        UCHAR ioctlIn[8] = {0};
        *(UINT32*)ioctlIn = kiqRingPa;     /* KiqBaseLo */
        *(UINT32*)(ioctlIn + 4) = 0;       /* KiqBaseHi */
        ok = DeviceIoControl(hDevice, IOCTL_AMDBC250_KIQ_BIOS_RING_SUBMIT,
                             ioctlIn, sizeof(ioctlIn),
                             &result, sizeof(result), &bytesReturned, NULL);
    }

    if (!ok) {
        printf("ERROR: DeviceIoControl failed (err=%lu)\n", GetLastError());
        CloseHandle(hDevice);
        return 1;
    }

    printf("Result code:        %lu ", result.Result);
    switch (result.Result) {
        case 0: printf("(FAIL — no change)\n"); break;
        case 1: printf("(RPTR changed — partial success)\n"); break;
        case 2: printf("(SUCCESS — SCRATCH=0xCAFEBABE!)\n"); break;
        default: printf("(unknown)\n"); break;
    }

    printf("KIQ_BASE:           0x%08X%08X", result.KiqBaseHi, result.KiqBaseLo);
    if (result.KiqBaseLo == kiqRingPa)
        printf(" (= TEST RING)\n");
    else if (result.KiqBaseLo == 0x7E522000)
        printf(" (= BIOS ring)\n");
    else if (result.KiqBaseLo == 0 && result.KiqBaseHi == 0)
        printf(" (= ZERO — HW not readable?)\n");
    else
        printf("\n");

    printf("SCRATCH before:     0x%08X", result.ScratchBefore);
    if (result.ScratchBefore == 0x4D585042) printf(" (MXPB OK)\n");
    else if (result.ScratchBefore == 0xCAFEBABE) printf(" (already set!)\n");
    else printf("\n");

    printf("SCRATCH after:      0x%08X", result.ScratchAfter);
    if (result.ScratchAfter == 0xCAFEBABE) printf(" *** WRITE_DATA EXECUTED! ***\n");
    else if (result.ScratchAfter == 0xDEADBEEF) printf(" *** NOP PATTERN WRITTEN BACK! ***\n");
    else if (result.ScratchAfter == result.ScratchBefore) printf(" (unchanged)\n");
    else printf(" (changed to 0x%08X)\n", result.ScratchAfter);

    printf("KIQ_RPTR before:    %lu\n", result.KiqRptrBefore);
    printf("KIQ_RPTR after:     %lu", result.KiqRptrAfter);
    if (result.KiqRptrAfter == 4)
        printf(" *** GPU PROCESSED NOP PACKETS! ***\n");
    else if (result.KiqRptrAfter != result.KiqRptrBefore)
        printf(" (GPU advanced RPTR!)\n");
    else printf("\n");

    printf("WPTR set:           %lu\n", result.KiqWptrSet);

    printf("ME_CNTL before:     0x%08X", result.MeCntlBefore);
    if (result.MeCntlBefore & (1 << 28)) printf(" (ME halted)");
    if (result.MeCntlBefore & (1 << 30)) printf(" (PFP halted)");
    printf("\n");

    printf("ME_CNTL after:      0x%08X", result.MeCntlAfter);
    if (result.MeCntlAfter & (1 << 28)) printf(" (ME halted)");
    if (result.MeCntlAfter & (1 << 30)) printf(" (PFP halted)");
    printf("\n");

    printf("Ring[0..3]:         0x%08X 0x%08X 0x%08X 0x%08X\n",
           result.RingDword0, result.RingDword1, result.RingDword2, result.RingDword3);

    CloseHandle(hDevice);

    printf("\n");
    if (result.KiqRptrAfter == 4)
        printf("*** GPU PROCESSED NOP PACKETS - RPTR = 4! ***\n");
    else if (result.ScratchAfter == 0xCAFEBABE)
        printf("*** PM4 EXECUTEd SUCCESSFULLY! SCRATCH = 0xCAFEBABE ***\n");
    else if (result.KiqRptrAfter != result.KiqRptrBefore)
        printf("Partial: RPTR moved but SCRATCH not written. Check PM4 format.\n");
    else
        printf("FAIL: GPU did not process ring. Check KdPrint logs with DbgView.\n");

    return (result.Result >= 1) ? 0 : 1;
}
