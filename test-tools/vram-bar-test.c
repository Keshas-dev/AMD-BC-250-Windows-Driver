#define INITGUID
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

static FILE *g = NULL;
static void Log(const char *fmt, ...) {
    va_list a; va_start(a, fmt);
    vfprintf(stdout, fmt, a); va_end(a); fflush(stdout);
    if (g) { va_start(a, fmt); vfprintf(g, fmt, a); va_end(a); fflush(g); }
}

static HANDLE OpenKmd(void) {
    return CreateFileW(L"\\\\.\\AMDBC250DreamV43",
        GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
}

static BOOL MmioTest(HANDLE h, UINT64 physAddr, UINT32 size, UINT32 offsetWrite, UINT32 valueWrite,
                    UINT32 offsetRead, UINT32 *valueRead, UINT32 *mapResult) {
    UCHAR inBuf[sizeof(UINT64) + 6*sizeof(UINT32)] = {0};
    UCHAR outBuf[sizeof(UINT64) + 6*sizeof(UINT32)] = {0};
    
    *(UINT64*)(inBuf + 0)  = physAddr;  // PhysicalAddress
    *(UINT32*)(inBuf + 8)  = size;      // Size
    *(UINT32*)(inBuf + 12) = offsetRead; // OffsetRead
    *(UINT32*)(inBuf + 16) = 0;         // ValueRead (out)
    *(UINT32*)(inBuf + 20) = offsetWrite; // OffsetWrite
    *(UINT32*)(inBuf + 24) = valueWrite;  // ValueWrite
    *(UINT32*)(inBuf + 28) = 0;         // ValueWrittenBack (out)
    *(UINT32*)(inBuf + 32) = 0;         // MapResult (out)
    *(UINT32*)(inBuf + 36) = 0;         // Padding
    
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, 0x80000B80, inBuf, sizeof(inBuf), outBuf, sizeof(outBuf), &br, NULL);
    
    if (ok) {
        *mapResult = *(UINT32*)(outBuf + 32);
        *valueRead = *(UINT32*)(outBuf + 16);
    }
    
    return ok;
}

int main(void) {
    g = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\vram-bar-test.log", "w");
    if (!g) { printf("Cannot open log\n"); return 1; }
    Log("=== VRAM BAR Direct Mapping Test ===\n\n"); fflush(g);

    HANDLE h = OpenKmd();
    if (h == INVALID_HANDLE_VALUE) {
        Log("KMD NOT FOUND\n"); fclose(g); return 1;
    }
    
    UINT32 valueRead, mapResult;
    UINT64 vramBase = 0xC0000000ULL;  // VRAM BAR physical address from nbio-unlock test
    UINT32 testSize = 0x1000;         // 4KB test region
    UINT32 testOffset = 0x0;          // Start at beginning
    UINT32 testValue = 0xDEADBEEF;
    
    Log("Testing VRAM BAR mapping at PA=0x%llX, size=0x%X\n", vramBase, testSize); fflush(g);
    
    BOOL ok = MmioTest(h, vramBase, testSize, testOffset, testValue, testOffset, &valueRead, &mapResult);
    Log("MmIoTest result: %s\n", ok ? "OK" : "FAIL");
    if (ok) {
        Log("  MapResult: %u (1=success, 0=failed)\n", mapResult);
        if (mapResult) {
            Log("  Wrote 0x%08X to offset 0x%X\n", testValue, testOffset);
            Log("  Read back: 0x%08X\n", valueRead);
            if (valueRead == testValue) {
                Log("  *** VRAM BAR READ/WRITE SUCCESS! ***\n");
            } else {
                Log("  *** VRAM BAR READ/WRITE FAILED (value mismatch) ***\n");
            }
        } else {
            Log("  *** MMIO mapping failed - VRAM BAR may be inaccessible ***\n");
        }
    }
    fflush(g);
    
    // Also test a few more offsets to see if we can write/read different values
    if (ok && mapResult) {
        Log("\n--- Testing multiple offsets ---\n"); fflush(g);
        for (int i = 0; i < 5; i++) {
            UINT32 offset = i * 0x10;
            UINT32 writeVal = 0xA0000000 + i;
            UINT32 readVal = 0;
            
            ok = MmioTest(h, vramBase, testSize, offset, writeVal, offset, &readVal, &mapResult);
            Log("  Offset 0x%X: wrote 0x%08X, read 0x%08X, mapResult=%u\n", 
                offset, writeVal, readVal, mapResult);
            if (ok && mapResult && readVal == writeVal) {
                Log("    SUCCESS\n");
            } else {
                Log("    FAILED\n");
            }
            fflush(g);
        }
    }
    
    CloseHandle(h);
    Log("\n=== VRAM BAR Test Complete ===\n"); fflush(g);
    if (g) fclose(g);
    printf("Done. Check output\\vram-bar-test.log\n");
    return 0;
}