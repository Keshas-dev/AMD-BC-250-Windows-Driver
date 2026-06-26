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
    
    printf("=== KIQ_WPTR boundary test ===\n\n");
    
    uint8_t* fw; long fwSize;
    FILE* f = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\firmware\\cyan_skillfish2_mec.bin", "rb");
    fseek(f, 0, SEEK_END); fwSize = ftell(f); fseek(f, 0, SEEK_SET);
    fw = (uint8_t*)malloc(fwSize); fread(fw, 1, fwSize, f); fclose(f);
    
    write_reg(0x34D0, 0x00010000);
    load_cp_fw(fw, (uint32_t)fwSize, 4);
    free(fw);
    
    printf("Testing WPTR values around 0x400-0x800 boundary:\n");
    uint32_t testValues[] = {0x000003FF, 0x00000400, 0x00000401, 0x00000402,
                             0x000007FE, 0x000007FF, 0x00000800, 0x00000801,
                             0x00000FFF, 0x00001000, 0x00001001};
    
    for (int i = 0; i < 11; i++) {
        write_reg(0xE078, testValues[i]);
        uint32_t readback = read_reg(0xE078);
        printf("  Wrote 0x%08X, read: 0x%08X%s\n", 
               testValues[i], readback, (readback != testValues[i]) ? " <-- MISMATCH" : "");
    }
    
    printf("\nSCRATCH: 0x%08X\n", read_reg(0x32D4));
    
    printf("\n--- CLEANUP ---\n");
    write_reg(0xE080, 0x00000000);
    write_reg(0x34D0, 0xE0000000);
    
    printf("\n=== Done ===\n");
    CloseHandle(hDevice);
    return 0;
}
