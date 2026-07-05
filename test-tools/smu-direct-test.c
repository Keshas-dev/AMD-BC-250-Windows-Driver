#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE g_hGpu = INVALID_HANDLE_VALUE;
static FILE *g_log = NULL;

static void Log(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    if (g_log) vfprintf(g_log, fmt, args);
    va_end(args);
}

static int SmnRead(uint32_t addr, uint32_t *val) {
    AMDBC250_IOCTL_SMN_ACCESS s = {0};
    DWORD returned = 0;
    s.SmnAddress = addr;
    s.IsWrite = 0;
    if (!DeviceIoControl(g_hGpu, IOCTL_AMDBC250_SMN_ACCESS,
        &s, sizeof(s), &s, sizeof(s), &returned, NULL)) {
        Log("  SMN_READ 0x%08X err=%u\n", addr, GetLastError());
        return 0;
    }
    if (val) *val = s.SmnData;
    return s.Result;
}

static int SmnWrite(uint32_t addr, uint32_t val) {
    AMDBC250_IOCTL_SMN_ACCESS s = {0};
    DWORD returned = 0;
    s.SmnAddress = addr;
    s.SmnData = val;
    s.IsWrite = 1;
    if (!DeviceIoControl(g_hGpu, IOCTL_AMDBC250_SMN_ACCESS,
        &s, sizeof(s), &s, sizeof(s), &returned, NULL)) {
        Log("  SMN_WRITE 0x%08X err=%u\n", addr, GetLastError());
        return 0;
    }
    return s.Result;
}

int main() {
    g_log = fopen("smu-direct-test.log", "w");
    
    g_hGpu = CreateFileA("\\\\.\\AMDBC250DreamV43",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, 0, NULL);
    
    if (g_hGpu == INVALID_HANDLE_VALUE) {
        printf("FAILED to open GPU driver (err=%u)\n", GetLastError());
        return 1;
    }
    Log("GPU driver opened OK\n\n");
    
    // 1. Read SMU NBIF registers via SMN
    Log("=== SMU NBIF Registers (SMN 0x0154xxxx) ===\n");
    uint32_t smuRegs[] = {
        0x0154001C,  // SMU NBIF control
        0x0154002C,  // SMU NBIF/IOMMU page table config
        0x015400F0,  // SMU command
        0x015400F4,  // SMU argument
        0x015400F8,  // SMU trigger
    };
    for (int i = 0; i < 5; i++) {
        uint32_t val = 0;
        if (SmnRead(smuRegs[i], &val)) {
            Log("  SMN[0x%08X] = 0x%08X\n", smuRegs[i], val);
        } else {
            Log("  SMN[0x%08X] read failed\n", smuRegs[i]);
        }
    }
    
    // 2. Try SMU wake command via direct SMN
    Log("\n=== SMU Direct Command Test ===\n");
    
    Log("\nTrying SMU GetSmuInfo (cmd=0x01) via SMN...\n");
    SmnWrite(0x015400F0, 0x01);
    SmnWrite(0x015400F4, 0x00);
    SmnWrite(0x015400F8, 0x01);
    Sleep(100);
    
    {
        uint32_t cmd = 0, arg = 0, trig = 0;
        SmnRead(0x015400F0, &cmd);
        SmnRead(0x015400F4, &arg);
        SmnRead(0x015400F8, &trig);
        Log("  After GetSmuInfo: cmd=0x%08X arg=0x%08X trig=0x%08X\n", cmd, arg, trig);
    }
    
    Log("\nTrying SMU PowerUpGfx (cmd=0x0B) via SMN...\n");
    SmnWrite(0x015400F0, 0x0B);
    SmnWrite(0x015400F4, 0x01);
    SmnWrite(0x015400F8, 0x01);
    Sleep(100);
    
    {
        uint32_t cmd = 0, arg = 0, trig = 0;
        SmnRead(0x015400F0, &cmd);
        SmnRead(0x015400F4, &arg);
        SmnRead(0x015400F8, &trig);
        Log("  After PowerUpGfx: cmd=0x%08X arg=0x%08X trig=0x%08X\n", cmd, arg, trig);
    }
    
    // 3. Try reading some other SMN regions to verify SMN access works
    Log("\n=== Verify SMN access with known registers ===\n");
    uint32_t testRegs[] = {
        0x00000000,  // GPU_ID via SMN
        0x00000088,  // C2PMSG_35 via SMN
        0x00000090,  // C2PMSG_36 via SMN
    };
    for (int i = 0; i < 3; i++) {
        uint32_t val = 0;
        if (SmnRead(testRegs[i], &val)) {
            Log("  SMN[0x%08X] = 0x%08X\n", testRegs[i], val);
        } else {
            Log("  SMN[0x%08X] read failed\n", testRegs[i]);
        }
    }
    
    CloseHandle(g_hGpu);
    
    Log("\n=== DONE ===\n");
    if (g_log) fclose(g_log);
    return 0;
}
