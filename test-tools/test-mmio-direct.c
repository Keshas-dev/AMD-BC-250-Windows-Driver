#define INITGUID
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

#define IOCTL_READ_MMIO 0x80000C04
#define IOCTL_WRITE_MMIO 0x80000C08

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
    g_log = fopen("C:\\AMD-BC-250\AMD-BC-250-Windows-Driver-main\output\\mmio-test.log", "w");
    if (!g_log) { printf("Cannot open log\n"); return 1; }

    Log("=== MMIO ACCESS TEST ===\n\n");

    HANDLE h = OpenMyDriver();
    if (h == INVALID_HANDLE_VALUE) {
        Log("Driver not found!\n");
        fclose(g_log);
        return 1;
    }

    Log("Driver opened successfully\n\n");

    DWORD bytesReturned = 0;
    BOOL ok;

    // Test 1: Read PSP MMIO registers
    Log("1. Testing PSP MMIO register access...\n");
    
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
        UINT32 mmioIn[2] = {0};
        UINT32 mmioOut[5] = {0};
        mmioIn[0] = pspRegs[i];  // Address
        mmioIn[1] = 0;           // Value (for write)
        
        ok = DeviceIoControl(h, IOCTL_READ_MMIO,
            mmioIn, sizeof(mmioIn), mmioOut, sizeof(mmioOut), &bytesReturned, NULL);
        
        if (ok) {
            Log("   MMIO[0x%08X] = 0x%08X\n", pspRegs[i], mmioOut[0]);
            
            if (mmioOut[0] != 0x00000000 && mmioOut[0] != 0xFFFFFFFF) {
                Log("   -> PSP register accessible!\n");
            }
        } else {
            Log("   MMIO[0x%08X] failed (err=%u)\n", pspRegs[i], GetLastError());
        }
    }

    // Test 2: Read GPU registers
    Log("\n2. Testing GPU register access...\n");
    
    UINT32 gpuRegs[] = {
        0x00000000,  // GPU_ID
        0x00000004,  // GPU_STATUS
        0x00000008,  // GPU_CONTROL
        0x00000010,  // GPU_SCRATCH
        0x00000100,  // PSP offset
        0x00000200,  // Another PSP offset
    };
    
    for (int i = 0; i < 6; i++) {
        UINT32 mmioIn[2] = {0};
        UINT32 mmioOut[5] = {0};
        mmioIn[0] = gpuRegs[i];
        mmioIn[1] = 0;
        
        ok = DeviceIoControl(h, IOCTL_READ_MMIO,
            mmioIn, sizeof(mmioIn), mmioOut, sizeof(mmioOut), &bytesReturned, NULL);
        
        if (ok) {
            Log("   GPU[0x%08X] = 0x%08X\n", gpuRegs[i], mmioOut[0]);
        } else {
            Log("   GPU[0x%08X] failed\n", gpuRegs[i]);
        }
    }

    // Test 3: Try to write to PSP registers
    Log("\n3. Testing PSP register write...\n");
    
    UINT32 writeRegs[] = {
        0x00000088,  // C2PMSG_35
        0x00000100,  // C2PMSG_64
    };
    
    for (int i = 0; i < 2; i++) {
        UINT32 mmioIn[2] = {0};
        UINT32 mmioOut[5] = {0};
        mmioIn[0] = writeRegs[i];
        mmioIn[1] = 0x12345678;  // Test value
        
        ok = DeviceIoControl(h, IOCTL_WRITE_MMIO,
            mmioIn, sizeof(mmioIn), mmioOut, sizeof(mmioOut), &bytesReturned, NULL);
        
        if (ok) {
            Log("   Write to MMIO[0x%08X] = 0x%08X: success=%u\n", 
                writeRegs[i], mmioIn[1], mmioOut[0]);
        } else {
            Log("   Write to MMIO[0x%08X] failed\n", writeRegs[i]);
        }
    }

    CloseHandle(h);

    Log("\n=== MMIO TEST COMPLETE ===\n");
    fclose(g_log);

    printf("Done. Check output\\mmio-test.log\n");
    return 0;
}