/* kiq-unhalt.c — Just clear ME_HALT, let BIOS ring commands execute! */
#include <windows.h>
#include <stdio.h>

static BOOL ReadReg(HANDLE h, unsigned offset, unsigned *val) {
    unsigned ra[2] = {offset, 0xDEADBEEF};
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, 0x80000B88, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
    if (ok) *val = ra[1]; else *val = 0xDEADBEEF;
    return ok;
}

static BOOL WriteReg(HANDLE h, unsigned offset, unsigned val) {
    unsigned ra[2] = {offset, val};
    DWORD br = 0;
    return DeviceIoControl(h, 0x80000B8C, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
}

int main(void) {
    printf("=== KIQ Unhalt Test ===\n");
    printf("BIOS already set KIQ_BASE=0x7E522000, WPTR=8. Just clear ME_HALT!\n\n");

    HANDLE h = CreateFileW(L"\\\\.\\AMDBC250DreamV43",
        GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("Cannot open device error=%lu\n", GetLastError());
        return 1;
    }

    /* INIT_HARDWARE */
    UCHAR initIn[32] = {0}; DWORD br = 0;
    *(unsigned __int64*)(initIn + 0) = 0xFE800000ULL;
    *(unsigned*)(initIn + 8) = 0x00080000;
    *(unsigned*)(initIn + 12) = 1;
    *(unsigned __int64*)(initIn + 16) = 0xC0000000ULL;
    *(unsigned*)(initIn + 24) = 0x10000000;
    DeviceIoControl(h, 0x80000B80, initIn, sizeof(initIn), NULL, 0, &br, NULL);

    unsigned val;

    /* Read BIOS state */
    printf("--- BIOS State ---\n");
    ReadReg(h, 0x32D4, &val); printf("SCRATCH     = 0x%08X\n", val);
    ReadReg(h, 0x4A74, &val); printf("ME_CNTL     = 0x%08X", val);
    printf(val & (1<<28) ? " [ME_HALT]" : "");
    printf(val & (1<<30) ? " [PFP_HALT]" : "");
    printf("\n");
    ReadReg(h, 0xE060, &val); printf("KIQ_BASE_LO = 0x%08X\n", val);
    ReadReg(h, 0xE064, &val); printf("KIQ_BASE_HI = 0x%08X\n", val);
    ReadReg(h, 0xE06C, &val); printf("KIQ_RPTR    = 0x%08X\n", val);
    ReadReg(h, 0xE078, &val); printf("KIQ_WPTR    = 0x%08X\n", val);

    /* Step 1: Read ME_CNTL */
    ReadReg(h, 0x4A74, &val);
    unsigned meOriginal = val;
    printf("\n--- Step 1: ME_CNTL = 0x%08X ---\n", val);

    /* Step 2: Clear ME_HALT (bit 28) and PFP_HALT (bit 30) */
    unsigned meNew = meOriginal & ~((1 << 28) | (1 << 30));
    printf("Clearing ME_HALT + PFP_HALT: 0x%08X -> 0x%08X\n", meOriginal, meNew);
    WriteReg(h, 0x4A74, meNew);

    /* Verify */
    ReadReg(h, 0x4A74, &val);
    printf("ME_CNTL after write: 0x%08X", val);
    printf(val & (1<<28) ? " [ME_HALT]" : "");
    printf(val & (1<<30) ? " [PFP_HALT]" : "");
    printf("\n");

    /* Step 3: Wait for GPU to process the 8 DWORDs */
    printf("\n--- Step 3: Waiting 100ms for GPU to process ring ---\n");
    Sleep(100);

    /* Step 4: Check results */
    printf("--- Step 4: Results ---\n");
    ReadReg(h, 0x32D4, &val); printf("SCRATCH     = 0x%08X\n", val);
    ReadReg(h, 0xE06C, &val); printf("KIQ_RPTR    = 0x%08X\n", val);
    ReadReg(h, 0xE078, &val); printf("KIQ_WPTR    = 0x%08X\n", val);
    ReadReg(h, 0x4A74, &val); printf("ME_CNTL     = 0x%08X\n", val);

    /* Analysis */
    printf("\n--- Analysis ---\n");
    ReadReg(h, 0xE06C, &val);
    if (val > 0) {
        printf("*** KIQ_RPTR moved from 0 to %u! GPU CP is processing commands! ***\n", val);
    } else {
        printf("KIQ_RPTR still 0 - GPU CP not processing\n");
    }

    ReadReg(h, 0x32D4, &val);
    if (val != 0x4D585042) {
        printf("*** SCRATCH changed from 0x4D585042 to 0x%08X! ***\n", val);
    } else {
        printf("SCRATCH unchanged\n");
    }

    /* Cleanup: restore ME halt */
    WriteReg(h, 0x4A74, meOriginal);
    printf("\nRestored ME_CNTL to 0x%08X\n", meOriginal);

    CloseHandle(h);
    printf("\n=== Done ===\n");
    return 0;
}
