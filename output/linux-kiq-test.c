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
    
    printf("=== Linux KIQ config: MEC 2, pipe 1, queue 0 ===\n\n");
    
    uint8_t* fw; long fwSize;
    FILE* f = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\firmware\\cyan_skillfish2_mec.bin", "rb");
    fseek(f, 0, SEEK_END); fwSize = ftell(f); fseek(f, 0, SEEK_SET);
    fw = (uint8_t*)malloc(fwSize); fread(fw, 1, fwSize, f); fclose(f);
    load_cp_fw(fw, (uint32_t)fwSize, 4);
    free(fw);
    printf("MEC firmware loaded.\n\n");
    
    // Linux uses: MEC=2, PIPE=1, QUEUE=0
    // GRBM_GFX_INDEX = (2 << 16) | (1 << 8) | 0 = 0x00020100
    uint32_t linux_kiq_select = (2 << 16) | (1 << 8) | 0;
    uint32_t broadcast = 0xE0000000;
    
    printf("=== MEC 2, pipe 1, queue 0 (Linux config) ===\n");
    write_reg(0x34D0, linux_kiq_select);
    
    printf("KIQ_BASE_LO (0xE060): 0x%08X\n", read_reg(0xE060));
    printf("KIQ_BASE_HI (0xE064): 0x%08X\n", read_reg(0xE064));
    printf("KIQ_SIZE   (0xE068): 0x%08X\n", read_reg(0xE068));
    printf("KIQ_RPTR   (0xE06C): 0x%08X\n", read_reg(0xE06C));
    printf("KIQ_WPTR   (0xE078): 0x%08X\n", read_reg(0xE078));
    printf("KIQ_DOORBELL(0xE074):0x%08X\n", read_reg(0xE074));
    printf("KIQ_VMID   (0xE07C): 0x%08X\n", read_reg(0xE07C));
    printf("KIQ_ACTIVE (0xE080): 0x%08X\n", read_reg(0xE080));
    
    // Try KIQ setup with MEC 2
    write_reg(0xE060, 0xDEADBEEF);
    write_reg(0xE06C, 0x00000000);
    write_reg(0xE078, 0x00000002);
    write_reg(0xE07C, 0x00000000);
    
    printf("\nKIQ_SIZE after setup: 0x%08X\n", read_reg(0xE068));
    
    write_reg(0xE080, 0x00000001);
    printf("KIQ_ACTIVE: 0x%08X\n", read_reg(0xE080));
    
    Sleep(500);
    
    printf("KIQ_RPTR after 500ms: 0x%08X\n", read_reg(0xE06C));
    
    write_reg(0xE080, 0x00000000);
    write_reg(0x34D0, broadcast);
    
    printf("\n=== Also try MEC 2, pipe 0, queue 0 ===\n");
    write_reg(0x34D0, (2 << 16) | (0 << 8) | 0);
    printf("KIQ_SIZE (0xE068): 0x%08X\n", read_reg(0xE068));
    write_reg(0x34D0, broadcast);
    
    printf("\n=== Also try MEC 1, pipe 1, queue 0 ===\n");
    write_reg(0x34D0, (1 << 16) | (1 << 8) | 0);
    printf("KIQ_SIZE (0xE068): 0x%08X\n", read_reg(0xE068));
    write_reg(0x34D0, broadcast);
    
    printf("\nSCRATCH: 0x%08X\n", read_reg(0x32D4));
    printf("\n=== Done ===\n");
    CloseHandle(hDevice);
    return 0;
}
