#include <windows.h>
#include <stdio.h>
#include <stdint.h>

#define IOCTL_WRITE_REG   0x80000B8C
#define IOCTL_READ_REG    0x80000B88

static HANDLE hDevice;
uint32_t read_reg(uint32_t offset) {
    uint32_t bytesReturned;
    struct { uint32_t offset; uint32_t value; } inBuf = {offset, 0};
    uint32_t outBuf[2] = {0};
    DeviceIoControl(hDevice, IOCTL_READ_REG, &inBuf, sizeof(inBuf), outBuf, sizeof(outBuf), &bytesReturned, NULL);
    return (bytesReturned >= 4) ? outBuf[1] : 0xFFFFFFFF;
}
void write_reg(uint32_t offset, uint32_t value) {
    uint32_t bytesReturned;
    struct { uint32_t offset; uint32_t value; } inBuf = {offset, value};
    DeviceIoControl(hDevice, IOCTL_WRITE_REG, &inBuf, sizeof(inBuf), NULL, 0, &bytesReturned, NULL);
}
int main() {
    hDevice = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hDevice == INVALID_HANDLE_VALUE) { printf("Open failed: %lu\n", GetLastError()); return 1; }
    
    printf("=== HQD register test with GRBM_GFX_CNTL ===\n\n");
    
    printf("Without GRBM_GFX_CNTL:\n");
    printf("  0xDAC0 (HQD_PQ_BASE_LO) = 0x%08X\n", read_reg(0xDAC0));
    printf("  0xDAC8 (HQD_PQ_CONTROL) = 0x%08X\n", read_reg(0xDAC8));
    printf("  0xDACC (HQD_PQ_RPTR) = 0x%08X\n", read_reg(0xDACC));
    printf("  0xDAD0 (HQD_PQ_WPTR) = 0x%08X\n", read_reg(0xDAD0));
    printf("  0xDAE0 (HQD_ACTIVE) = 0x%08X\n", read_reg(0xDAE0));
    
    printf("\nSetting GRBM_GFX_CNTL (0x2022) = 0x00020100\n");
    write_reg(0x2022, 0x00020100);
    
    printf("\nWith GRBM_GFX_CNTL (ME=1, PIPE=1, QUEUE=0):\n");
    printf("  0xDAC0 (HQD_PQ_BASE_LO) = 0x%08X\n", read_reg(0xDAC0));
    printf("  0xDAC8 (HQD_PQ_CONTROL) = 0x%08X\n", read_reg(0xDAC8));
    printf("  0xDACC (HQD_PQ_RPTR) = 0x%08X\n", read_reg(0xDACC));
    printf("  0xDAD0 (HQD_PQ_WPTR) = 0x%08X\n", read_reg(0xDAD0));
    printf("  0xDAE0 (HQD_ACTIVE) = 0x%08X\n", read_reg(0xDAE0));
    
    printf("\nWriting test values...\n");
    write_reg(0xDAC0, 0x12345000);
    write_reg(0xDAC8, 0x0000000A);
    write_reg(0xDACC, 0x00000000);
    write_reg(0xDAD0, 0x00000000);
    write_reg(0xDADC, 0x00000001);
    write_reg(0xDAE0, 0x00000001);
    
    printf("\nReading back with GRBM_GFX_CNTL:\n");
    printf("  0xDAC0 = 0x%08X\n", read_reg(0xDAC0));
    printf("  0xDAC4 = 0x%08X\n", read_reg(0xDAC4));
    printf("  0xDAC8 = 0x%08X\n", read_reg(0xDAC8));
    printf("  0xDACC = 0x%08X\n", read_reg(0xDACC));
    printf("  0xDAD0 = 0x%08X\n", read_reg(0xDAD0));
    printf("  0xDADC = 0x%08X\n", read_reg(0xDADC));
    printf("  0xDAE0 = 0x%08X\n", read_reg(0xDAE0));
    
    printf("\n--- CLEANUP ---\n");
    write_reg(0x2022, 0xE0000000);
    
    printf("\n=== Done ===\n");
    CloseHandle(hDevice);
    return 0;
}
