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

    printf("=== RLC Safe Test (after reboot) ===\n");

    /* 1. Check SMU features via Queue 0 (known-safe SMN path) */
    printf("\n--- SMU Features (via SMN Queue 0) ---\n");
    WriteReg(0x38, 0x03B10A68); MemoryBarrier();
    ULONG q0ctrl = ReadReg(0x3C);
    WriteReg(0x38, 0x03B10024); MemoryBarrier();
    ULONG fwFlags = ReadReg(0x3C);
    printf("SMU Q0 ctrl=0x%08X FW_FLAGS=0x%08X\n", q0ctrl, fwFlags);

    /* 2. Try Queue 2 DisableSmuFeatures */
    printf("\n--- Queue 2: DisableSmuFeatures ---\n");
    /* Wait for ready */
    int ok = 0;
    for (int i = 0; i < 100; i++) {
        WriteReg(0x38, 0x03B10564); MemoryBarrier();
        ULONG rsp = ReadReg(0x3C);
        if (rsp == 1) { ok = 1; break; }
        Sleep(10);
    }
    if (!ok) { printf("Q2 not ready\n"); CloseHandle(g_h); return 1; }
    /* Ack */
    WriteReg(0x38, 0x03B10564); MemoryBarrier();
    WriteReg(0x3C, 0); MemoryBarrier();
    /* Write arg = disable GFXOFF(2)+CG(3) */
    WriteReg(0x38, 0x03B10998); MemoryBarrier();
    WriteReg(0x3C, 0x0C); MemoryBarrier();
    /* Write cmd = 0x06 (DisableSmuFeatures) */
    WriteReg(0x38, 0x03B10528); MemoryBarrier();
    WriteReg(0x3C, 0x06); MemoryBarrier();
    /* Poll */
    ULONG rsp = 0;
    for (int i = 0; i < 200; i++) {
        Sleep(10);
        WriteReg(0x38, 0x03B10564); MemoryBarrier();
        rsp = ReadReg(0x3C);
        if (rsp != 0) break;
    }
    printf("Q2 disable GFXOFF+CG: rsp=0x%X\n", rsp);

    /* 3. Read known-safe registers */
    printf("\n--- Known-safe register reads ---\n");
    printf("GRBM_STATUS    (0x3260) = 0x%08X\n", ReadReg(0x3260));
    printf("GRBM_GFX_INDEX (0x34D0) = 0x%08X\n", ReadReg(0x34D0));
    printf("ME_CNTL        (0x4A74) = 0x%08X\n", ReadReg(0x4A74));
    printf("SCRATCH        (0x32D4) = 0x%08X\n", ReadReg(0x32D4));
    printf("C2PMSG_81      (0x10614)= 0x%08X\n", ReadReg(0x10614));
    printf("DISPATCH_INIT  (0x80E0) = 0x%08X\n", ReadReg(0x80E0));
    printf("PGM_LO         (0x8110) = 0x%08X\n", ReadReg(0x8110));

    /* Test only the specific RLC_CNTL address (computed from Linux mm offset) */
    printf("\n--- RLC_CNTL probe (Linux offset 0x4C00*4+GC_BASE) ---\n");
    ULONG rlcAddr = 0x1260 + 0x4C00*4; /* = 0x14260 */
    printf("RLC_CNTL at 0x%05X = attempting...\n", rlcAddr);
    ULONG rlcV = ReadReg(rlcAddr);
    printf("RLC_CNTL at 0x%05X = 0x%08X\n", rlcAddr, rlcV);
    
    /* If we got here without crash, try RLC_STOP + RLC_START */
    if (rlcV != 0xFFFFFFFF) {
        printf("\n--- Attempting RLC_STOP -> RLC_START ---\n");
        /* RLC_STOP: clear bit 0 (RLC_ENABLE_F32) */
        WriteReg(rlcAddr, rlcV & ~1);
        Sleep(100);
        /* RLC_START: set bit 0 */
        WriteReg(rlcAddr, (ReadReg(rlcAddr) & ~1) | 1);
        Sleep(100);
        printf("After RLC start: GRBM_STATUS=0x%08X\n", ReadReg(0x3260));
    }

    CloseHandle(g_h);
    return 0;
}
