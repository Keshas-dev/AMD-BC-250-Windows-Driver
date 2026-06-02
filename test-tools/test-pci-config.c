#define INITGUID
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

#define IOCTL_SMN_ACCESS 0x80000BC4

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
    g_log = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\pci-config-test.log", "w");
    if (!g_log) { printf("Cannot open log\n"); return 1; }

    Log("=== PCI CONFIG SPACE TEST ===\n\n");

    HANDLE h = OpenMyDriver();
    if (h == INVALID_HANDLE_VALUE) {
        Log("Driver not found!\n");
        fclose(g_log);
        return 1;
    }

    Log("Driver opened successfully\n\n");

    DWORD bytesReturned = 0;
    BOOL ok;

    // Test 1: SMN access to PSP registers
    Log("1. Testing SMN access to PSP registers...\n");
    
    UINT32 pspRegs[] = {
        0x00000088,  // C2PMSG_35 (bootloader command)
        0x0000008C,  // C2PMSG_36 (firmware address)
        0x00000100,  // C2PMSG_64 (ring creation)
        0x0000010C,  // C2PMSG_67 (ring write pointer)
        0x00000114,  // C2PMSG_69 (ring address low)
        0x00000118,  // C2PMSG_70 (ring address high)
        0x0000011C,  // C2PMSG_71 (ring size)
        0x00000144,  // C2PMSG_81 (SOS alive)
    };
    
    for (int i = 0; i < 8; i++) {
        UINT32 smnIn[6] = {0};
        UINT32 smnOut[6] = {0};
        smnIn[0] = pspRegs[i];  // SMN address
        smnIn[1] = 0;           // Data
        smnIn[2] = 0;           // IsWrite = 0 (read)
        smnIn[3] = 0;           // IndexPort (use default)
        smnIn[4] = 0;           // DataPort (use default)
        smnIn[5] = 0;           // Result
        
        ok = DeviceIoControl(h, IOCTL_SMN_ACCESS,
            smnIn, sizeof(smnIn), smnOut, sizeof(smnOut), &bytesReturned, NULL);
        
        if (ok) {
            Log("   SMN[0x%08X] = 0x%08X (result=%u)\n", 
                pspRegs[i], smnOut[1], smnOut[5]);
            
            if (smnOut[1] != 0x00000000 && smnOut[1] != 0xFFFFFFFF) {
                Log("   -> PSP register accessible!\n");
            }
        } else {
            Log("   SMN[0x%08X] failed (err=%u)\n", pspRegs[i], GetLastError());
        }
    }

    // Test 2: Try different SMN port addresses
    Log("\n2. Testing different SMN port addresses...\n");
    
    UINT32 smnPorts[][2] = {
        {0x3B10528, 0x3B10564},  // Default
        {0x3B1052C, 0x3B10568},  // Alternative 1
        {0x3B10530, 0x3B1056C},  // Alternative 2
        {0x3B10534, 0x3B10570},  // Alternative 3
    };
    
    for (int i = 0; i < 4; i++) {
        UINT32 smnIn[6] = {0};
        UINT32 smnOut[6] = {0};
        smnIn[0] = 0x00000088;  // C2PMSG_35
        smnIn[1] = 0;
        smnIn[2] = 0;
        smnIn[3] = smnPorts[i][0];  // Custom index port
        smnIn[4] = smnPorts[i][1];  // Custom data port
        smnIn[5] = 0;
        
        ok = DeviceIoControl(h, IOCTL_SMN_ACCESS,
            smnIn, sizeof(smnIn), smnOut, sizeof(smnOut), &bytesReturned, NULL);
        
        if (ok) {
            Log("   SMN ports 0x%08X/0x%08X: value=0x%08X\n",
                smnPorts[i][0], smnPorts[i][1], smnOut[1]);
        } else {
            Log("   SMN ports 0x%08X/0x%08X failed\n",
                smnPorts[i][0], smnPorts[i][1]);
        }
    }

    // Test 3: Try to read GPU registers through SMN
    Log("\n3. Testing GPU register access through SMN...\n");
    
    UINT32 gpuRegs[] = {
        0x00000000,  // GPU_ID
        0x00000004,  // GPU_STATUS
        0x00000008,  // GPU_CONTROL
        0x00000010,  // GPU_SCRATCH
    };
    
    for (int i = 0; i < 4; i++) {
        UINT32 smnIn[6] = {0};
        UINT32 smnOut[6] = {0};
        smnIn[0] = gpuRegs[i];
        smnIn[1] = 0;
        smnIn[2] = 0;
        smnIn[3] = 0;
        smnIn[4] = 0;
        smnIn[5] = 0;
        
        ok = DeviceIoControl(h, IOCTL_SMN_ACCESS,
            smnIn, sizeof(smnIn), smnOut, sizeof(smnOut), &bytesReturned, NULL);
        
        if (ok) {
            Log("   GPU[0x%08X] = 0x%08X\n", gpuRegs[i], smnOut[1]);
        } else {
            Log("   GPU[0x%08X] failed\n", gpuRegs[i]);
        }
    }

    CloseHandle(h);

    Log("\n=== PCI CONFIG TEST COMPLETE ===\n");
    fclose(g_log);

    printf("Done. Check output\\pci-config-test.log\n");
    return 0;
}