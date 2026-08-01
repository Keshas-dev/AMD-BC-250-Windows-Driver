#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>

#define AMDBC250_DEVICE_PATH L"\\\\.\\AMDBC250DreamV43"
#define IOCTL_AMDBC250_READ_REG   ((ULONG)0x80000B88)
#define IOCTL_AMDBC250_WRITE_REG  ((ULONG)0x80000B8C)
#define IOCTL_AMDBC250_INIT_HARDWARE ((ULONG)0x80000B80)

typedef struct { ULONG Offset, Value, Status; } REG_IOCTL;
typedef struct { ULONG64 MmioPhysicalBase; ULONG MmioSize, Flags; ULONG64 FbPhysicalBase; ULONG FbSize; } INIT_HW;

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
static int InitHw(void) {
    INIT_HW ih = {0}; ih.MmioPhysicalBase = 0xFE800000ULL; ih.MmioSize = 0x80000; ih.Flags = 1; DWORD ret;
    return DeviceIoControl(g_h, IOCTL_AMDBC250_INIT_HARDWARE, &ih,sizeof(ih), &ih,sizeof(ih), &ret, NULL) ? 0 : -1;
}

int main(void) {
    if (OpenGpu() != 0) { printf("Cannot open GPU\n"); return 1; }
    if (InitHw() != 0) { printf("InitHw FAILED err=%lu\n", GetLastError()); return 1; }
    printf("=== RLC Full Init Sequence ===\n\n");

    /* Step 1: Check current state */
    printf("--- Initial State ---\n");
    printf("ME_CNTL    (0x4A74) = 0x%08X\n", ReadReg(0x4A74));
    printf("RLC_CNTL   (0x14260)= 0x%08X\n", ReadReg(0x14260));
    printf("GRBM_STATUS(0x3260) = 0x%08X\n", ReadReg(0x3260));

    /* Step 2: RLC_STOP */
    printf("\n--- RLC_STOP ---\n");
    ULONG rlc = ReadReg(0x14260);
    printf("RLC_CNTL before: 0x%08X\n", rlc);
    WriteReg(0x14260, rlc & ~1);
    ULONG rlcAfter = ReadReg(0x14260);
    printf("RLC_CNTL after stop: 0x%08X\n", rlcAfter);
    if (rlcAfter == rlc) printf("  WARNING: RLC_CNTL may be read-only!\n");

    /* Step 3: Unhalt ME */
    printf("\n--- ME Unhalt ---\n");
    ULONG me = ReadReg(0x4A74);
    printf("ME_CNTL before: 0x%08X\n", me);
    WriteReg(0x4A74, 0);
    ULONG meAfter = ReadReg(0x4A74);
    printf("ME_CNTL after unhalt: 0x%08X\n", meAfter);

    /* Step 4: RLC_START */
    printf("\n--- RLC_START ---\n");
    WriteReg(0x14260, (ReadReg(0x14260) & ~1) | 1);
    ULONG rlcStarted = ReadReg(0x14260);
    printf("RLC_CNTL after start: 0x%08X\n", rlcStarted);

    /* Step 5: Check results */
    printf("\n--- Results ---\n");
    printf("ME_CNTL    = 0x%08X\n", ReadReg(0x4A74));
    printf("RLC_CNTL   = 0x%08X\n", ReadReg(0x14260));
    printf("GRBM_STATUS= 0x%08X\n", ReadReg(0x3260));
    printf("SCRATCH    = 0x%08X\n", ReadReg(0x32D4));
    printf("CP_RB0_BASE_LO = 0x%08X\n", ReadReg(0x89E0));
    printf("CP_RB0_CNTL    = 0x%08X\n", ReadReg(0x89E4));
    printf("CP_RB0_WPTR    = 0x%08X\n", ReadReg(0x8A30));
    printf("CP_RB0_RPTR    = 0x%08X\n", ReadReg(0x8A34));
    printf("C2PMSG_81  = 0x%08X\n", ReadReg(0x10614));

    CloseHandle(g_h);
    return 0;
}
