#include <windows.h>
#include <stdio.h>

#define PSP_IOCTL_INIT_HW       CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define PSP_IOCTL_READ_REG      CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define PSP_IOCTL_WRITE_REG     CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define PSP_IOCTL_KIQ_SUBMIT    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x818, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _PSP_KIQ_SUBMIT_REQUEST {
    ULONG CommandCount;
    ULONG Reserved[3];
    ULONG Commands[64];
} PSP_KIQ_SUBMIT_REQUEST;

static BOOL PspReadReg(HANDLE h, unsigned offset, unsigned *val) {
    unsigned ra[2] = {offset, 0};
    unsigned resp[2] = {0};
    DWORD br = 0;
    return DeviceIoControl(h, PSP_IOCTL_READ_REG, ra, sizeof(ra), resp, sizeof(resp), &br, NULL) && val ? (*val = resp[0], TRUE) : FALSE;
}
static BOOL PspWriteReg(HANDLE h, unsigned offset, unsigned val) {
    unsigned ra[2] = {offset, val};
    DWORD br = 0;
    return DeviceIoControl(h, PSP_IOCTL_WRITE_REG, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
}

int main() {
    printf("=== PSP KIQ Long Wait Test ===\n");
    HANDLE hPsp = CreateFileW(L"\\\\.\\AmdBcPsp", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hPsp == INVALID_HANDLE_VALUE) { printf("Cannot open PSP (err=%lu)\n", GetLastError()); return 1; }
    
    struct { unsigned __int64 PA; unsigned size; } req = {0xFE800000ULL, 0x00080000};
    DWORD br = 0;
    DeviceIoControl(hPsp, PSP_IOCTL_INIT_HW, &req, sizeof(req), NULL, 0, &br, NULL);
    
    unsigned val;
    PspReadReg(hPsp, 0x32D4, &val);
    printf("SCRATCH before: 0x%08X\n", val);
    
    PspWriteReg(hPsp, 0x4A74, 0x00000000);
    Sleep(100);
    PspReadReg(hPsp, 0x4A74, &val);
    printf("ME_CNTL: 0x%08X\n", val);
    
    PSP_KIQ_SUBMIT_REQUEST sub = {0};
    sub.CommandCount = 5;
    sub.Commands[0] = 0xC0370003;
    sub.Commands[1] = 0x10100000;
    sub.Commands[2] = 0x000032D4;
    sub.Commands[3] = 0x00000000;
    sub.Commands[4] = 0xCAFEBABE;
    
    printf("Submitting PM4...\n");
    DeviceIoControl(hPsp, PSP_IOCTL_KIQ_SUBMIT, &sub, sizeof(sub), NULL, 0, &br, NULL);
    
    printf("Waiting (up to 5 seconds)...\n");
    for (int i = 0; i <= 10; i++) {
        Sleep(500);
        unsigned rptr, wptr, scratch;
        PspReadReg(hPsp, 0xE06C, &rptr);
        PspReadReg(hPsp, 0xE078, &wptr);
        PspReadReg(hPsp, 0x32D4, &scratch);
        printf("  +%ds: RPTR=0x%08X WPTR=0x%08X SCRATCH=0x%08X\n", i*500/1000, rptr, wptr, scratch);
        if (scratch == 0xCAFEBABE) {
            printf("*** SUCCESS! ***\n");
            break;
        }
    }
    
    CloseHandle(hPsp);
    return 0;
}
