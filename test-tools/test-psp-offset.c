#define INITGUID
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

#define IOCTL_TEST_REGISTER 0x80000C08

static FILE *g_log = NULL;

void Log(const char *fmt, ...) {
    va_list a; va_start(a, fmt);
    vfprintf(stdout, fmt, a); va_end(a);
    if (g_log) { va_start(a, fmt); vfprintf(g_log, fmt, a); va_end(a); fflush(g_log); }
}

HANDLE OpenMyDriver() {
    return CreateFileW(L"\\\\.\\AMDBC250DreamV43",
        GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
}

int main() {
    g_log = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\psp-offset-test.log", "w");
    if (!g_log) { printf("Cannot open log\n"); return 1; }

    Log("=== PSP OFFSET TEST ===\n\n");

    HANDLE h = OpenMyDriver();
    if (h == INVALID_HANDLE_VALUE) {
        Log("Driver not found!\n");
        fclose(g_log);
        return 1;
    }

    Log("Driver opened successfully\n\n");

    DWORD bytesReturned = 0;
    BOOL ok;

    // PSP MMIO registers with offset 0x10000
    Log("1. Testing PSP MMIO registers with offset 0x10000...\n");
    
    UINT32 pspRegs[] = {
        0x10088,  // C2PMSG_35 (bootloader command) = 0x10000 + 0x0088
        0x1008C,  // C2PMSG_36 (firmware address) = 0x10000 + 0x008C
        0x10100,  // C2PMSG_64 (ring creation) = 0x10000 + 0x0100
        0x1010C,  // C2PMSG_67 (ring write pointer) = 0x10000 + 0x010C
        0x10114,  // C2PMSG_69 (ring address low) = 0x10000 + 0x0114
        0x10118,  // C2PMSG_70 (ring address high) = 0x10000 + 0x0118
        0x1011C,  // C2PMSG_71 (ring size) = 0x10000 + 0x011C
        0x10144,  // C2PMSG_81 (SOS alive) = 0x10000 + 0x0144
    };
    
    for (int i = 0; i < 8; i++) {
        UINT32 regIn[2] = {0};
        UINT32 regOut[5] = {0};
        regIn[0] = pspRegs[i];  // Address with PSP offset
        regIn[1] = 0;           // Write value (0 for read)
        
        ok = DeviceIoControl(h, IOCTL_TEST_REGISTER,
            regIn, sizeof(regIn), regOut, sizeof(regOut), &bytesReturned, NULL);
        
        if (ok) {
            Log("   PSP[0x%08X] = 0x%08X (before), 0x%08X (after)\n", 
                pspRegs[i], regOut[0], regOut[1]);
            
            if (regOut[0] != 0x00000000 && regOut[0] != 0xFFFFFFFF) {
                Log("   -> PSP register ACCESSIBLE!\n");
            } else if (regOut[0] == 0xFFFFFFFF) {
                Log("   -> PSP register BLOCKED (0xFFFFFFFF)\n");
            } else {
                Log("   -> PSP register returns 0x00000000\n");
            }
        } else {
            Log("   PSP[0x%08X] failed (err=%u)\n", pspRegs[i], GetLastError());
        }
    }

    // Test 2: Read GPU registers
    Log("\n2. Testing GPU register access...\n");
    
    UINT32 gpuRegs[] = {
        0x00000000,  // GPU_ID
        0x00000004,  // GPU_STATUS
        0x00000008,  // GPU_CONTROL
        0x00000010,  // GPU_SCRATCH
    };
    
    for (int i = 0; i < 4; i++) {
        UINT32 regIn[2] = {0};
        UINT32 regOut[5] = {0};
        regIn[0] = gpuRegs[i];
        regIn[1] = 0;
        
        ok = DeviceIoControl(h, IOCTL_TEST_REGISTER,
            regIn, sizeof(regIn), regOut, sizeof(regOut), &bytesReturned, NULL);
        
        if (ok) {
            Log("   GPU[0x%08X] = 0x%08X\n", gpuRegs[i], regOut[0]);
        } else {
            Log("   GPU[0x%08X] failed\n", gpuRegs[i]);
        }
    }

    CloseHandle(h);

    Log("\n=== PSP OFFSET TEST COMPLETE ===\n");
    fclose(g_log);

    printf("Done. Check output\\psp-offset-test.log\n");
    return 0;
}