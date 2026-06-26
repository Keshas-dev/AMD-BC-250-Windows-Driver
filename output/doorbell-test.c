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
    
    printf("=== KIQ_DOORBELL test ===\n\n");
    
    uint8_t* fw; long fwSize;
    FILE* f = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\firmware\\cyan_skillfish2_mec.bin", "rb");
    fseek(f, 0, SEEK_END); fwSize = ftell(f); fseek(f, 0, SEEK_SET);
    fw = (uint8_t*)malloc(fwSize); fread(fw, 1, fwSize, f); fclose(f);
    
    write_reg(0x34D0, 0x00010000);
    load_cp_fw(fw, (uint32_t)fwSize, 4);
    free(fw);
    
    uint32_t pa_low = 0x12345000;
    uint32_t pa_high = 0;
    
    printf("Setting up KIQ ring:\n");
    write_reg(0xE060, pa_low);
    printf("  KIQ_BASE_LO = 0x%08X\n", pa_low);
    write_reg(0xE06C, 0x00000000);
    printf("  KIQ_RPTR = 0x%08X\n", 0x00000000);
    write_reg(0xE07C, 0x00000001);
    printf("  KIQ_VMID = 0x%08X\n", 0x00000001);
    
    printf("\nTesting KIQ_DOORBELL:\n");
    uint32_t doorbell_vals[] = {0x00000000, 0x00000001, 0x00000002, 0x00000004, 
                                 0x00000008, 0x00000010, 0x00000020, 0x00000040,
                                 0x00000080, 0x00000100, 0x00000200, 0x00000400,
                                 0x00000800, 0x00001000, 0x00002000, 0x00004000,
                                 0x00008000, 0x00010000, 0x00020000, 0x00040000,
                                 0x00080000, 0x00100000, 0x00200000, 0x00400000,
                                 0x00800000, 0x01000000, 0x02000000, 0x04000000,
                                 0x08000000, 0x10000000, 0x20000000, 0x40000000,
                                 0x80000000, 0xFFFFFFFF};
    
    for (int i = 0; i < 34; i++) {
        write_reg(0xE074, doorbell_vals[i]);
        uint32_t wptr = read_reg(0xE078);
        uint32_t rptr = read_reg(0xE06C);
        uint32_t active = read_reg(0xE080);
        printf("  DOORBELL=0x%08X -> WPTR=0x%08X RPTR=0x%08X ACTIVE=0x%08X\n", 
               doorbell_vals[i], wptr, rptr, active);
    }
    
    printf("\nSCRATCH: 0x%08X\n", read_reg(0x32D4));
    
    printf("\n--- CLEANUP ---\n");
    write_reg(0xE080, 0x00000000);
    write_reg(0x34D0, 0xE0000000);
    
    printf("\n=== Done ===\n");
    CloseHandle(hDevice);
    return 0;
}
