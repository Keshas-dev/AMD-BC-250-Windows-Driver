#include <windows.h>
#include <stdio.h>

/* PSP driver IOCTLs (go through clean MmMapIoSpace, no PCI config writes) */
#define PSP_DEVICE      L"\\\\.\\AmdBcPsp"
#define PSP_CTL(fn)     CTL_CODE(FILE_DEVICE_UNKNOWN, fn, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PSP_INIT_HW   PSP_CTL(0x803)
#define IOCTL_PSP_READ_REG  PSP_CTL(0x800)
#define IOCTL_PSP_WRITE_REG PSP_CTL(0x801)

static HANDLE gPsp = INVALID_HANDLE_VALUE;

static ULONG ReadReg(ULONG off) {
    ULONG in[2] = {off, 0}, out[2] = {0}, br = 0;
    DeviceIoControl(gPsp, IOCTL_PSP_READ_REG, in, 4, out, 8, &br, NULL);
    return out[0];
}

static BOOL WriteReg(ULONG off, ULONG val) {
    ULONG in[2] = {off, val}, out[2] = {0}, br = 0;
    return DeviceIoControl(gPsp, IOCTL_PSP_WRITE_REG, in, 8, out, 8, &br, NULL);
}

int main(void) {
    printf("Step 1: Open PSP driver\n"); fflush(stdout);
    gPsp = CreateFileW(PSP_DEVICE, GENERIC_READ|GENERIC_WRITE,
                       0, NULL, OPEN_EXISTING, 0, NULL);
    if (gPsp == INVALID_HANDLE_VALUE) {
        printf("FAIL: PSP device not found (err=%lu)\n", GetLastError());
        printf("Trying GPU device instead...\n"); fflush(stdout);
        /* Fallback: open GPU device + minimal setup */
        HANDLE gGpu = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
                                  0, NULL, OPEN_EXISTING, 0, NULL);
        if (gGpu == INVALID_HANDLE_VALUE) {
            printf("GPU device also not found\n");
            return 1;
        }
        /* Check HW status */
        UCHAR buf[64] = {0}; DWORD br = 0;
        DeviceIoControl(gGpu, 0x80000B90, NULL, 0, buf, sizeof(buf), &br, NULL);
        printf("GPU HW status: MmioMapped=%lu Rings=%lu Fence=%lu\n",
               *(ULONG*)(buf+0), *(ULONG*)(buf+8), *(ULONG*)(buf+16));
        CloseHandle(gGpu);
        return 1;
    }
    printf("OK\n");

    printf("Step 2: PSP INIT_HW (map GPU BAR5 at 0xFE800000)\n"); fflush(stdout);
    {
        UCHAR req[32] = {0};
        /* PSP_INIT_HW_REQUEST: PhysicalAddress(8) + Size(4) */
        *(ULONGLONG*)(req+0) = 0xFE800000ULL;  /* GPU BAR5 */
        *(ULONG*)(req+8) = 0x80000;
        ULONG out = 0; DWORD br = 0;
        BOOL ok = DeviceIoControl(gPsp, IOCTL_PSP_INIT_HW, req, 12, &out, 4, &br, NULL);
        printf("  INIT_HW returned %d, VA=0x%08lX\n", ok, out);
    }

    printf("Step 3: Read GPU_ID (0x0000)\n"); fflush(stdout);
    printf("  0x0000 = 0x%08X\n", ReadReg(0x0000));

    printf("Step 4: Read ME_CNTL (0x4A74)\n"); fflush(stdout);
    ULONG me = ReadReg(0x4A74);
    printf("  0x4A74 = 0x%08X\n", me);

    printf("Step 5: Read GRBM_STATUS (0x3260)\n"); fflush(stdout);
    printf("  0x3260 = 0x%08X\n", ReadReg(0x3260));

    printf("Step 6: Read SCRATCH (0x32D4)\n"); fflush(stdout);
    printf("  0x32D4 = 0x%08X\n", ReadReg(0x32D4));

    printf("Step 7: Read KIQ_BASE (0xE060)\n"); fflush(stdout);
    printf("  0xE060 = 0x%08X\n", ReadReg(0xE060));

    printf("Step 8: Read KIQ_SIZE (0xE068)\n"); fflush(stdout);
    printf("  0xE068 = 0x%08X\n", ReadReg(0xE068));

    if (me != 0 && me != 0xFFFFFFFF) {
        printf("\nStep 9: Writing 0 to ME_CNTL (0x4A74)\n"); fflush(stdout);
        BOOL wrote = WriteReg(0x4A74, 0);
        printf("  Write returned %d\n", wrote);
        printf("  ME_CNTL after = 0x%08X\n", ReadReg(0x4A74));
    } else {
        printf("\nStep 9: ME_CNTL=0x%08X - appears dead or already unhalted\n", me);
    }

    printf("\nStep 10: GRBM_STATUS = 0x%08X\n", ReadReg(0x3260));
    printf("Step 11: KIQ_RPTR   = 0x%08X\n", ReadReg(0xE06C));
    printf("Step 12: KIQ_WPTR   = 0x%08X\n", ReadReg(0xE078));

    printf("\n=== DONE ===\n");
    CloseHandle(gPsp);
    return 0;
}
