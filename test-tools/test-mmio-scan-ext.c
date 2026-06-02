#include <windows.h>
#include <stdio.h>
#include "amdbc250_ioctl.h"

static HANDLE hDev = INVALID_HANDLE_VALUE;

static BOOL InitHw(UINT64 pa, UINT32 size) {
    AMDBC250_IOCTL_INIT_HARDWARE init = {0};
    init.MmioPhysicalBase = pa;
    init.MmioSize = size;
    DWORD bytes = 0;
    return DeviceIoControl(hDev, 0x80000B80, &init, sizeof(init), NULL, 0, &bytes, NULL);
}

static ULONG ReadReg(DWORD offset) {
    AMDBC250_IOCTL_REG_ACCESS reg = {0};
    reg.RegisterOffset = offset;
    DWORD bytes = 0;
    DeviceIoControl(hDev, 0x80000B88, &reg, sizeof(reg), &reg, sizeof(reg), &bytes, NULL);
    return reg.Value;
}

int main(void) {
    printf("AMD BC-250 Extended MMIO Scanner\n");
    printf("Searches physical memory for GPU registers\n");
    printf("============================================\n\n");

    hDev = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hDev == INVALID_HANDLE_VALUE) {
        printf("ERROR: Cannot open device\n");
        return 1;
    }

    /* Expected GPU_ID for BC-250 (Cyan Skillfish = 0x13FE) */
    /* Also check for common RDNA2 patterns */
    UINT expectedIds[] = {0x13FE, 0x13EF, 0x73FF, 0x73EF, 0x164E, 0x1681};
    int numIds = sizeof(expectedIds)/sizeof(expectedIds[0]);

    /* Scan range: 0xFC000000 to 0xFFFFFFFF (top 64MB of low 4GB space)
       Step: 256KB (0x40000) - covers BAR alignment requirements */
    printf("Scanning 0xFC000000 - 0xFFFFFFFF (step=256KB)...\n\n");

    int found = 0;
    int attempts = 0;
    for (UINT64 pa = 0xFC000000; pa <= 0xFFFF0000 && !found; pa += 0x40000) {
        attempts++;
        UINT32 size = 0x1000; /* 4KB - just enough to read GPU_ID */
        
        BOOL ok = InitHw(pa, size);
        if (!ok) continue;

        ULONG val = ReadReg(0x0000);
        
        /* Clear any non-zero value quickly for reporting */
        if ((val & 0xFFFF) != 0) {
            printf("  0x%08llX: GPU_ID=0x%08X", pa, val);
            
            /* Check if it matches expected GPU IDs */
            for (int i = 0; i < numIds; i++) {
                if ((val & 0xFFFF) == expectedIds[i]) {
                    printf(" *** MATCH: BC-250 (0x%04X) ***", expectedIds[i]);
                    found = 1;
                }
            }
            printf("\n");
        }

        if (attempts % 128 == 0) {
            printf("  ... scanned 0x%llX (%d attempts)\n", pa, attempts);
        }
    }

    if (found) {
        printf("\n*** GPU FOUND! ***\n");
    } else {
        printf("\n--- GPU not found in range 0xFC000000-0xFFFFFFFF ---\n");
        printf("GPU_ID = 0x00000000 at all tested addresses.\n");
        printf("This means either:\n");
        printf("  1. BAR address is elsewhere (try wider range)\n");
        printf("  2. Memory Space Enable bit not set (need PCI config write)\n");
        printf("  3. GPU registers inaccessible without SMU init\n");
    }

    CloseHandle(hDev);
    printf("\nDone. (%d attempts)\n", attempts);
    return found ? 0 : 1;
}
