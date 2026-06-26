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
    PVOID ringVa = VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!ringVa) {
        printf("Failed to allocate ring buffer\n");
        CloseHandle(hPsp);
        return 1;
    }
    RtlZeroMemory(ringVa, 0x1000);
    printf("Ring buffer VA=%p\n", ringVa);

    /* Step 5: Write PM4 commands to ring buffer (submitted via IOCTL) */
    printf("\n--- Writing PM4 Commands ---\n");
    PSP_KIQ_SUBMIT_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    // PM4 IT_WRITE_DATA to SCRATCH (0x32D4) = 0xCAFEBABE
    req.CommandCount = 5;     // 5 DWORDs: header + control + addr_lo + addr_hi + data
    req.Commands[0] = 0xC0370003;   // PM4: IT_WRITE_DATA (count=3)
    req.Commands[1] = 0x10100000;   // CONTROL: DST_SEL=register, WR_CONFIRM
    req.Commands[2] = 0x000032D4;   // ADDR_LO = SCRATCH register byte offset
    req.Commands[3] = 0x00000000;   // ADDR_HI
    req.Commands[4] = 0xCAFEBABE;   // DATA
    printf("Wrote PM4 commands to IOCTL request\n");

    /* Step 6: Submit via PSP KIQ */
    printf("\n--- Submitting via PSP KIQ ---\n");
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
    VirtualFree(ringVa, 0, MEM_RELEASE);
    CloseHandle(hPsp);
    printf("\n=== Done ===\n");
    return 0;
}