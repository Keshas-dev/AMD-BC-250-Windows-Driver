#define INITGUID
#include <windows.h>
#include <stdio.h>

static BOOL ReadReg(HANDLE h, UINT32 offset, UINT32 *val) {
    UINT32 ra[2] = {offset, 0};
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, 0x80000B88, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
    if (ok && val) *val = ra[1];
    return ok;
}

static BOOL InitHardwareNBIO(HANDLE h) {
    UCHAR initIn[32] = {0}, initOut[32] = {0};
    DWORD br = 0;
    *(UINT64*)(initIn + 0)  = 0xFE800000ULL;
    *(UINT32*)(initIn + 8)  = 0x00080000;
    *(UINT32*)(initIn + 12) = 1;
    return DeviceIoControl(h, 0x80000B80, initIn, sizeof(initIn), initOut, sizeof(initOut), &br, NULL);
}

int main(void) {
    HANDLE h = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { printf("KMD NOT FOUND\n"); return 1; }
    InitHardwareNBIO(h);
    
    UINT32 regs[] = {
        0x3AD8, 0x3AE0, 0x3AE4, 0x3AFC, 0x3B00, 0x3B04, 0x3B08, 0x3B0C,
        0xE060, 0xE064, 0xE068, 0xE06C, 0xE070, 0xE074, 0xE078, 0xE07C,
        0x2F00, 0x2F04, 0x2F08, 0x2F0C, 0x2F10, 0x2F14, 0x2F18, 0x2F1C,
        0x2F20, 0x2F24, 0x2F28, 0x2F2C
    };
    const char* names[] = {
        "HQD_CP_RB0_CMD", "HQD_CP_RB0_CNTL", "HQD_CP_RB0_STATUS", "HQD_CP_RB0_FUNC",
        "HQD_CP_RB0_BASE", "HQD_CP_RB0_SIZE", "HQD_CP_RB0_RPTR", "HQD_CP_RB0_WPTR",
        "KIQ_BASE_LO", "KIQ_BASE_HI", "KIQ_RB_CNTL", "KIQ_RB_STATUS", "KIQ_DOORBELL", "KIQ_WPTR", "KIQ_RBDC_PTR", "KIQ_CP_RB0_WPTR_HI",
        "CP_ME_CNTL", "CP_ME_STATUS", "CP_ME_HALT", "CP_ME_HARTPEND", "CP_MEC_CNTL", "CP_MEC_STATUS", "CP_MEC_HALT", "CP_MEC_HARTPEND",
        "CP_MEC_CNTL", "CP_MEC_STATUS", "CP_MEC_HALT", "CP_MEC_HARTPEND"
    };
    
    printf("=== HQD Registers ===\n");
    for (int i = 0; i < sizeof(regs)/sizeof(regs[0]); i++) {
        UINT32 v;
        if (ReadReg(h, regs[i], &v)) {
            printf("  [0x%04X] %-24s = 0x%08X\n", regs[i], names[i], v);
        } else {
            printf("  [0x%04X] %-24s = READ FAIL\n", regs[i], names[i]);
        }
    }
    CloseHandle(h);
    return 0;
}