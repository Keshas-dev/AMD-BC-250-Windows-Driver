/* psp-kIQ-correct-test.c — Test GPU PM4 execution via PSP KIQ using correct IOCTL */
/* Uses the actual PSP_IOCTL_KIQ_SUBMIT (0x00222060) with proper request structure */

#include <windows.h>
#include <stdio.h>

#define PSP_DEVICE_NAME         L"\\\\.\\AmdBcPsp"
#define PSP_IOCTL_READ_REG      CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define PSP_IOCTL_WRITE_REG     CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define PSP_IOCTL_INIT_HW       CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define PSP_IOCTL_KIQ_SUBMIT    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x818, METHOD_BUFFERED, FILE_ANY_ACCESS) /* KIQ ring submit */

typedef struct {
    ULONG64 RingBufferPA;
    ULONG   RingBufferSize;
    ULONG   WriteOffset;
    ULONG   CommandCount;
    ULONG   CommandBuffer[16];  // Fixed size for simplicity
} PSP_KIQ_SUBMIT_REQUEST, *PPSP_KIQ_SUBMIT_REQUEST;

static BOOL PspReadReg(HANDLE h, unsigned offset, unsigned *val) {
    unsigned ra[2] = {offset, 0};
    unsigned resp[2] = {0, 0};
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, PSP_IOCTL_READ_REG, ra, sizeof(ra), resp, sizeof(resp), &br, NULL);
    if (ok && val) *val = resp[0];
    return ok;
}

static BOOL PspWriteReg(HANDLE h, unsigned offset, unsigned val) {
    unsigned ra[2] = {offset, val};
    DWORD br = 0;
    return DeviceIoControl(h, PSP_IOCTL_WRITE_REG, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
}

int main(void) {
    printf("=== PSP KIQ Submit Test (Correct IOCTL) ===\n\n");

    /* Step 1: Open PSP device */
    HANDLE hPsp = CreateFileW(PSP_DEVICE_NAME, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hPsp == INVALID_HANDLE_VALUE) {
        printf("Cannot open PSP device error=%lu\n", GetLastError());
        return 1;
    }
    printf("PSP device opened (handle=%p)\n", hPsp);

    /* Step 2: Init PSP HW (map GPU BAR5) */
    {
        struct { unsigned __int64 PA; unsigned size; } req;
        req.PA = 0xFE800000ULL;
        req.size = 0x00080000;
        DWORD br = 0;
        BOOL ok = DeviceIoControl(hPsp, PSP_IOCTL_INIT_HW, &req, sizeof(req), NULL, 0, &br, NULL);
        printf("PSP INIT_HW (GPU BAR5): ok=%d error=%lu\n", ok, GetLastError());
    }

    /* Step 3: Read initial state */
    printf("\n--- Initial State ---\n");
    unsigned val;
    PspReadReg(hPsp, 0x32D4, &val); printf("  SCRATCH (0x32D4)        = 0x%08X\n", val);
    PspReadReg(hPsp, 0x4A74, &val); printf("  ME_CNTL (0x4A74)        = 0x%08X\n", val);

    /* Step 4: Allocate ring buffer in system memory */
    printf("\n--- Allocating Ring Buffer ---\n");
    PVOID ringVa = NULL;
    PHYSICAL_ADDRESS ringPa = {0};
    {
        PHYSICAL_ADDRESS low = {0}, high = {0};
        high.QuadPart = 0xFFFFFFFFULL;  // Allow allocation anywhere
        ringVa = MmAllocateContiguousMemory(0x1000, low, high);  // 4KB ring
        if (!ringVa) {
            printf("Failed to allocate ring buffer\n");
            CloseHandle(hPsp);
            return 1;
        }
        RtlZeroMemory(ringVa, 0x1000);
        ringPa = MmGetPhysicalAddress(ringVa);
        printf("Ring buffer VA=%p PA=0x%llX\n", ringVa, ringPa.QuadPart);
    }

    /* Step 5: Write PM4 commands to ring buffer */
    printf("\n--- Writing PM4 Commands ---\n");
    volatile PULONG ring = (volatile PULONG)ringVa;
    // PM4 IT_WRITE_DATA to SCRATCH (0x32D4) = 0xCAFEBABE
    // Header: TYPE=3(11), COUNT=3, OPCODE=0x37 -> 0xC0033700
    ring[0] = 0xC0033700;   // PM4: IT_WRITE_DATA (count=3)
    ring[1] = 0x00000000;   // CONTROL: default (no confirm, dest_sel=GFX)
    ring[2] = 0x000032D4;   // SCRATCH register offset in DWORDs (0x32D4 / 4)
    ring[3] = 0xCAFEBABE;   // value to write
    ring[4] = 0xC0001000;   // PM4: NOP (count=0) - padding
    printf("Wrote PM4 commands to ring buffer\n");

    /* Step 6: Submit via PSP KIQ */
    printf("\n--- Submitting via PSP KIQ ---\n");
    PSP_KIQ_SUBMIT_REQUEST req;
    req.RingBufferPA = ringPa.QuadPart;
    req.RingBufferSize = 0x1000;  // 4KB
    req.WriteOffset = 0;          // Start at beginning
    req.CommandCount = 5;         // 5 DWORDs written
    
    DWORD br = 0;
    BOOL ok = DeviceIoControl(hPsp, PSP_IOCTL_KIQ_SUBMIT, &req, sizeof(req), NULL, 0, &br, NULL);
    printf("PSP_IOCTL_KIQ_SUBMIT: ok=%d error=%lu\n", ok, GetLastError());

    /* Step 7: Check results */
    printf("\n--- Results ---\n");
    PspReadReg(hPsp, 0x32D4, &val); printf("  SCRATCH (0x32D4)        = 0x%08X\n", val);
    if (val == 0xCAFEBABE) {
        printf("  *** SUCCESS: GPU executed PM4 via PSP KIQ! ***\n");
    } else {
        printf("  GPU did not execute PM4 (SCRATCH unchanged)\n");
    }

    /* Step 8: Cleanup */
    MmFreeContiguousMemory(ringVa);
    CloseHandle(hPsp);
    printf("\n=== Done ===\n");
    return 0;
}