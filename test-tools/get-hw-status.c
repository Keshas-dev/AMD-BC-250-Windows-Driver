#include <windows.h>
#include <stdio.h>

#define FILE_DEVICE_AMDBC250    0x8000
#define IOCTL_INDEX             0x270
#define METHOD_BUFFERED         0
#define FILE_ANY_ACCESS         0
#define CTL_CODE_AMDBC250(x, m, a) \
    ((FILE_DEVICE_AMDBC250 << 16) | ((a) << 14) | ((IOCTL_INDEX + (x)) << 2) | (m))

#define IOCTL_AMDBC250_GET_HW_STATUS  CTL_CODE_AMDBC250(0x74, METHOD_BUFFERED, FILE_ANY_ACCESS)

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
    
    AMDBC250_IOCTL_HW_STATUS status = {0};
    DWORD bytes = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_AMDBC250_GET_HW_STATUS, NULL, 0, &status, sizeof(status), &bytes, NULL);
    
    if (ok) {
        printf("HW Status:\n");
        printf("  MMIO Mapped: %s\n", status.MmioMapped ? "YES" : "NO");
        printf("  Rings Initialized: %s\n", status.RingsInitialized ? "YES" : "NO");
        printf("  Fence Initialized: %s\n", status.FenceInitialized ? "YES" : "NO");
        printf("  GfxRing PhysAddr: 0x%016llX\n", status.GfxRingPhysAddr);
        printf("  GfxRing Size: %u bytes\n", status.GfxRingSize);
        printf("  GfxRing Wptr: %u\n", status.GfxRingWptr);
        printf("  GfxRing Rptr: %u\n", status.GfxRingRptr);
        printf("  Fence PhysAddr: 0x%016llX\n", status.FencePhysAddr);
        printf("  Fence Value: 0x%016llX\n", status.FenceValue);
        printf("  Last Submitted Fence: 0x%016llX\n", status.LastSubmittedFence);
    } else {
        printf("GET_HW_STATUS failed (err=%lu)\n", GetLastError());
    }
    
    CloseHandle(h);
    return 0;
}