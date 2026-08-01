#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>

#define AMDBC250_DEVICE_PATH L"\\\\.\\AMDBC250DreamV43"
#define IOCTL_AMDBC250_READ_REG   ((ULONG)0x80000B88)
#define IOCTL_AMDBC250_WRITE_REG  ((ULONG)0x80000B8C)
#define IOCTL_AMDBC250_INIT_HARDWARE ((ULONG)0x80000B80)
#define IOCTL_AMDBC250_PSP_REG_ACCESS ((ULONG)0x80000BD0)

typedef struct { ULONG Offset, Value, Status; } REG_IOCTL;
typedef struct { ULONG64 MmioPhysicalBase; ULONG MmioSize, Flags; ULONG64 FbPhysicalBase; ULONG FbSize; } INIT_HW;
typedef struct { ULONG RegOffset; ULONG RegValue; ULONG Mask; UCHAR Write; UCHAR Reserved[3]; ULONG Status; } PSP_REG_ACCESS;

static HANDLE g_h;
static int OpenGpu(void) {
    g_h = CreateFileW(AMDBC250_DEVICE_PATH, GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    return g_h != INVALID_HANDLE_VALUE ? 0 : -1;
}
static ULONG ReadReg(ULONG offset) {
    REG_IOCTL req = {offset,0,0}; DWORD ret;
    DeviceIoControl(g_h, IOCTL_AMDBC250_READ_REG, &req,sizeof(req), &req,sizeof(req), &ret, NULL);
    return req.Value;
}
static int WriteReg(ULONG offset, ULONG value) {
    REG_IOCTL req = {offset,value,0}; DWORD ret;
    return DeviceIoControl(g_h, IOCTL_AMDBC250_WRITE_REG, &req,sizeof(req), &req,sizeof(req), &ret, NULL) ? 0 : -1;
}
static int PspRegAccess(ULONG offset, ULONG value, UCHAR write) {
    PSP_REG_ACCESS req = {offset, value, 0, write, {0}, 0}; DWORD ret;
    return DeviceIoControl(g_h, IOCTL_AMDBC250_PSP_REG_ACCESS, &req,sizeof(req), &req,sizeof(req), &ret, NULL) ? req.Status : -1;
}
static int InitHw(void) {
    INIT_HW ih = {0}; ih.MmioPhysicalBase = 0xFE800000ULL; ih.MmioSize = 0x80000; ih.Flags = 1; DWORD ret;
    return DeviceIoControl(g_h, IOCTL_AMDBC250_INIT_HARDWARE, &ih,sizeof(ih), &ih,sizeof(ih), &ret, NULL) ? 0 : -1;
}

int main(void) {
    if (OpenGpu() != 0) { printf("Cannot open GPU\n"); return 1; }
    if (InitHw() != 0) { printf("InitHw FAILED err=%lu\n", GetLastError()); return 1; }
    printf("=== RLC via PSP REG_ACCESS ===\n\n");

    /* Step 1: Read RLC_CNTL via PSP */
    printf("--- Reading via PSP REG_ACCESS ---\n");
    int st = PspRegAccess(0x14260, 0, 0);
    printf("RLC_CNTL via PSP: Status=%d", st);
    if (st == 0) { /* read: value is in req.RegValue - but we can't get it back from this API */
        printf(" (read successful)\n");
    } else printf("\n");

    /* Step 2: Try writing RLC_CNTL via PSP */
    printf("\n--- Writing RLC_CNTL=1 via PSP REG_ACCESS ---\n");
    st = PspRegAccess(0x14260, 1, 1);
    printf("PSP REG_ACCESS write: Status=%d\n", st);

    /* Step 3: Read back via BAR5 */
    printf("\n--- Readback via BAR5 ---\n");
    printf("RLC_CNTL   (0x14260)= 0x%08X\n", ReadReg(0x14260));
    printf("GRBM_STATUS(0x3260) = 0x%08X\n", ReadReg(0x3260));

    /* Step 4: Try ME_CNTL to check if engine comes alive */
    printf("\n--- Final State ---\n");
    printf("ME_CNTL    (0x4A74) = 0x%08X\n", ReadReg(0x4A74));
    printf("CP_RB0_BASE_LO(0x89E0)= 0x%08X\n", ReadReg(0x89E0));
    printf("CP_RB0_CNTL   (0x89E4)= 0x%08X\n", ReadReg(0x89E4));
    printf("CP_RB0_WPTR   (0x8A30)= 0x%08X\n", ReadReg(0x8A30));
    printf("SCRATCH    (0x32D4) = 0x%08X\n", ReadReg(0x32D4));
    printf("C2PMSG_81  (0x10614)= 0x%08X\n", ReadReg(0x10614));

    CloseHandle(g_h);
    return 0;
}
