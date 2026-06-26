#include <windows.h>
#include <stdio.h>
#include <stdint.h>

#define IOCTL_WRITE_REG   0x80000B8C
#define IOCTL_READ_REG    0x80000B88
#define IOCTL_LOAD_CP_FW  0x80000BD6

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
void load_cp_fw(const uint8_t* fw, uint32_t size, uint32_t type) {
    uint32_t bytesReturned;
    uint8_t* inBuf = (uint8_t*)malloc(size + 8);
    *(uint32_t*)inBuf = type;
    *(uint32_t*)(inBuf + 4) = 0;
    memcpy(inBuf + 8, fw, size);
    DeviceIoControl(hDevice, IOCTL_LOAD_CP_FW, inBuf, size + 8, NULL, 0, &bytesReturned, NULL);
    free(inBuf);
}
int main() {
    hDevice = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hDevice == INVALID_HANDLE_VALUE) { printf("Open failed: %lu\n", GetLastError()); return 1; }
    
    printf("=== HQD KIQ test with CORRECT offsets ===\n\n");
    
    uint8_t* fw; long fwSize;
    FILE* f = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\firmware\\cyan_skillfish2_mec2.bin", "rb");
    fseek(f, 0, SEEK_END); fwSize = ftell(f); fseek(f, 0, SEEK_SET);
    fw = (uint8_t*)malloc(fwSize); fread(fw, 1, fwSize, f); fclose(f);
    
    write_reg(0x34D0, 0x00010000);
    load_cp_fw(fw, (uint32_t)fwSize, 4);
    free(fw);
    
    printf("After MEC2 load, SCRATCH = 0x%08X\n", read_reg(0x32D4));
    
    // Correct HQD offsets from driver code (kmd.c:4765-4782)
    uint32_t pa_low = 0x12345000;
    
    printf("\nWriting HQD registers (correct offsets):\n");
    write_reg(0xDAD8, pa_low);   // HQD_PQ_BASE_LO
    printf("  HQD_PQ_BASE_LO (0xDAD8) = 0x%08X\n", pa_low);
    write_reg(0xDADC, 0x00000000); // HQD_PQ_BASE_HI
    printf("  HQD_PQ_BASE_HI (0xDADC) = 0x%08X\n", 0);
    write_reg(0xDAFC, 0x0000000A); // HQD_PQ_CONTROL
    printf("  HQD_PQ_CONTROL (0xDAFC) = 0x%08X\n", 0x0000000A);
    write_reg(0xDAE0, 0x00000000); // HQD_PQ_RPTR
    printf("  HQD_PQ_RPTR (0xDAE0) = 0x%08X\n", 0);
    write_reg(0xDB90, 0x00000000); // HQD_PQ_WPTR_LO
    printf("  HQD_PQ_WPTR_LO (0xDB90) = 0x%08X\n", 0);
    write_reg(0xDAC4, 0x00000001); // HQD_VMID
    printf("  HQD_VMID (0xDAC4) = 0x%08X\n", 1);
    write_reg(0xDAC0, 0x00000001); // HQD_ACTIVE
    printf("  HQD_ACTIVE (0xDAC0) = 0x%08X\n", 1);
    
    printf("\nReading back HQD registers:\n");
    printf("  HQD_ACTIVE (0xDAC0) = 0x%08X\n", read_reg(0xDAC0));
    printf("  HQD_VMID (0xDAC4) = 0x%08X\n", read_reg(0xDAC4));
    printf("  HQD_PQ_BASE_LO (0xDAD8) = 0x%08X\n", read_reg(0xDAD8));
    printf("  HQD_PQ_CONTROL (0xDAFC) = 0x%08X\n", read_reg(0xDAFC));
    printf("  HQD_PQ_RPTR (0xDAE0) = 0x%08X\n", read_reg(0xDAE0));
    printf("  HQD_PQ_WPTR_LO (0xDB90) = 0x%08X\n", read_reg(0xDB90));
    
    printf("\nWriting WPTR=1...\n");
    write_reg(0xDB90, 0x00000001);
    printf("  HQD_PQ_WPTR_LO = 0x%08X\n", read_reg(0xDB90));
    printf("  HQD_PQ_RPTR = 0x%08X\n", read_reg(0xDAE0));
    
    printf("\nSCRATCH = 0x%08X\n", read_reg(0x32D4));
    
    printf("\n--- CLEANUP ---\n");
    write_reg(0xDAC0, 0x00000000);
    write_reg(0x34D0, 0xE0000000);
    
    printf("\n=== Done ===\n");
    CloseHandle(hDevice);
    return 0;
}
