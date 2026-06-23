#include <windows.h>
#include <stdio.h>

#define FILE_DEVICE_AMDBC250    0x8000
#define IOCTL_INDEX             0x270
#define METHOD_BUFFERED         0
#define FILE_ANY_ACCESS         0
#define CTL_CODE_AMDBC250(x, m, a) \
    ((FILE_DEVICE_AMDBC250 << 16) | ((a) << 14) | ((IOCTL_INDEX + (x)) << 2) | (m))

#define IOCTL_AMDBC250_INIT_HARDWARE  CTL_CODE_AMDBC250(0x70, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AMDBC250_READ_REG       CTL_CODE_AMDBC250(0x72, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AMDBC250_GET_HW_STATUS  CTL_CODE_AMDBC250(0x74, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct {
    UINT32 Offset;
    UINT32 Value;
} REG_IO;

typedef struct {
    UINT32 MmioMapped;
    UINT32 RingsInitialized;
    UINT32 FenceInitialized;
    UINT64 GfxRingPhysAddr;
    UINT32 GfxRingSize;
    UINT32 GfxRingWptr;
    UINT32 GfxRingRptr;
    UINT64 FencePhysAddr;
    UINT64 FenceValue;
    UINT64 LastSubmittedFence;
} AMDBC250_IOCTL_HW_STATUS;

int main() {
    HANDLE h = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("Cannot open GPU driver (err=%lu)\n", GetLastError());
        return 1;
    }
    
    printf("GPU driver opened\n");
    
    // Call INIT_HARDWARE WITHOUT NBIO_MAP flag (should do full init)
    printf("\n--- Calling INIT_HARDWARE (NO NBIO_MAP) ---\n");
    UCHAR initIn[32] = {0};
    UCHAR initOut[32] = {0};
    *(UINT64*)(initIn + 0)  = 0xFE800000ULL;  // BAR5 physical
    *(UINT32*)(initIn + 8)  = 0x00080000;     // BAR5 size
    *(UINT32*)(initIn + 12) = 0;              // Flags=0: NO NBIO_MAP (FULL INIT)
    *(UINT64*)(initIn + 16) = 0xC0000000ULL;  // BAR0 physical
    *(UINT32*)(initIn + 24) = 0x10000000;     // BAR0 size
    
    DWORD bytes = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE,
        initIn, sizeof(initIn), initOut, sizeof(initOut), &bytes, NULL);
    printf("INIT_HARDWARE: %s\n", ok ? "OK" : "FAILED");
    
    // Read some registers to see state
    printf("\n--- Reading registers ---\n");
    REG_IO regIo;
    DWORD br;
    
    // Read KIQ_BASE
    regIo.Offset = 0xE060;  // KIQ_BASE_LO
    DeviceIoControl(h, IOCTL_AMDBC250_READ_REG, &regIo, sizeof(regIo), &regIo, sizeof(regIo), &br, NULL);
    UINT32 kiqBaseLo = regIo.Value;
    
    regIo.Offset = 0xE064;  // KIQ_BASE_HI
    DeviceIoControl(h, IOCTL_AMDBC250_READ_REG, &regIo, sizeof(regIo), &regIo, sizeof(regIo), &br, NULL);
    UINT32 kiqBaseHi = regIo.Value;
    
    printf("KIQ_BASE = 0x%08X%08X\n", kiqBaseHi, kiqBaseLo);
    
    // Read a few other registers
    regIo.Offset = 0x3260;  // GRBM_STATUS
    DeviceIoControl(h, IOCTL_AMDBC250_READ_REG, &regIo, sizeof(regIo), &regIo, sizeof(regIo), &br, NULL);
    printf("GRBM_STATUS = 0x%08X\n", regIo.Value);
    
    regIo.Offset = 0x3264;  // CC_UCONFIG
    DeviceIoControl(h, IOCTL_AMDBC250_READ_REG, &regIo, sizeof(regIo), &regIo, sizeof(regIo), &br, NULL);
    printf("CC_UCONFIG = 0x%08X\n", regIo.Value);
    
    regIo.Offset = 0x32D4;  // SCRATCH
    DeviceIoControl(h, IOCTL_AMDBC250_READ_REG, &regIo, sizeof(regIo), &regIo, sizeof(regIo), &br, NULL);
    printf("SCRATCH = 0x%08X\n", regIo.Value);
    
    // Get HW status
    printf("\n--- Getting HW status ---\n");
    AMDBC250_IOCTL_HW_STATUS hwStatus = {0};
    ok = DeviceIoControl(h, IOCTL_AMDBC250_GET_HW_STATUS, NULL, 0, &hwStatus, sizeof(hwStatus), &bytes, NULL);
    if (ok) {
        printf("HW Status:\n");
        printf("  Rings Initialized: %s\n", hwStatus.RingsInitialized ? "YES" : "NO");
        printf("  GfxRing PhysAddr: 0x%016llX\n", hwStatus.GfxRingPhysAddr);
        printf("  GfxRing Size: %u bytes\n", hwStatus.GfxRingSize);
        printf("  GfxRing Wptr: %u\n", hwStatus.GfxRingWptr);
        printf("  GfxRing Rptr: %u\n", hwStatus.GfxRingRptr);
    } else {
        printf("GET_HW_STATUS failed (err=%lu)\n", GetLastError());
    }
    
    CloseHandle(h);
    return 0;
}