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
    
    printf("=== HQD KIQ setup (Linux method, size in PQ_CONTROL) ===\n\n");
    
    uint8_t* fw; long fwSize;
    FILE* f = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\firmware\\cyan_skillfish2_mec.bin", "rb");
    fseek(f, 0, SEEK_END); fwSize = ftell(f); fseek(f, 0, SEEK_SET);
    fw = (uint8_t*)malloc(fwSize); fread(fw, 1, fwSize, f); fclose(f);
    load_cp_fw(fw, (uint32_t)fwSize, 4);
    free(fw);
    printf("MEC firmware loaded.\n\n");
    
    // Use GRBM_GFX_CNTL (Linux uses this, not GRBM_GFX_INDEX)
    // KIQ select: ME=2, PIPE=1, QUEUE=0
    uint32_t kiq_sel = (2 << 16) | (1 << 8) | 0;
    
    printf("=== HQD Setup via GRBM_GFX_CNTL ===\n");
    write_reg(0x2022, kiq_sel);
    
    // PQ_BASE_LO - ring buffer base (must be 16-byte aligned, so low 4 bits = 0)
    write_reg(0xDAC4, 0xDEADBEE0);
    printf("HQD_PQ_BASE_LO: 0x%08X\n", read_reg(0xDAC4));
    
    // PQ_BASE_HI
    write_reg(0xDAC8, 0x00000000);
    printf("HQD_PQ_BASE_HI: 0x%08X\n", read_reg(0xDAC8));
    
    // PQ_CONTROL with size
    // Size encoding: bits[23:16] = log2(ring_size_dwords) - 1
    // For 256KB ring: 256*1024/4 = 65536 dwords, log2=16, encoded=15 (0x0F)
    // For 512KB ring: 131072 dwords, log2=17, encoded=16 (0x10)
    uint32_t pq_ctrl = (0x10 << 16) | 0x3;  // 512KB ring, enable, bit1
    write_reg(0xDACC, pq_ctrl);
    printf("HQD_PQ_CONTROL: wrote 0x%08X, read: 0x%08X\n", pq_ctrl, read_reg(0xDACC));
    
    // PQ_RPTR
    write_reg(0xDAD0, 0x00000000);
    printf("HQD_PQ_RPTR: 0x%08X\n", read_reg(0xDAD0));
    
    // PQ_WPTR
    write_reg(0xDAD4, 0x00000002);
    printf("HQD_PQ_WPTR: 0x%08X\n", read_reg(0xDAD4));
    
    // VMID
    write_reg(0xDAD8, 0x00000000);
    printf("HQD_PQ_VMID: 0x%08X\n", read_reg(0xDAD8));
    
    // ACTIVE
    write_reg(0xDAC0, 0x00000001);
    printf("HQD_ACTIVE: wrote 1, read: 0x%08X\n", read_reg(0xDAC0));
    
    // Keep GRBM_GFX_CNTL set (don't deselect!)
    printf("\nKeeping GRBM_GFX_CNTL=0x%08X active\n", kiq_sel);
    
    Sleep(1000);
    
    printf("\n=== After 1 second ===\n");
    printf("HQD_ACTIVE: 0x%08X\n", read_reg(0xDAC0));
    printf("HQD_PQ_RPTR: 0x%08X\n", read_reg(0xDAD0));
    printf("HQD_PQ_WPTR: 0x%08X\n", read_reg(0xDAD4));
    printf("KIQ_RPTR (0xE06C): 0x%08X\n", read_reg(0xE06C));
    printf("GRBM_STATUS (0x2010): 0x%08X\n", read_reg(0x2010));
    
    // Try writing NOP PM4 packet to ring and advance WPTR
    printf("\n=== Try submitting NOP PM4 ===\n");
    // PM4 NOP header: 0x80000000 | (length-1)
    // NOP is 1 DWORD: 0x80000000
    write_reg(0xDAD4, 0x00000001);  // WPTR = 1
    printf("WPTR after NOP: 0x%08X\n", read_reg(0xDAD4));
    
    Sleep(500);
    
    printf("RPTR after NOP: 0x%08X\n", read_reg(0xDAD0));
    
    // Cleanup
    write_reg(0xDAC0, 0x00000000);
    write_reg(0x2022, 0x00000000);
    
    printf("\nSCRATCH: 0x%08X\n", read_reg(0x32D4));
    printf("\n=== Done ===\n");
    CloseHandle(hDevice);
    return 0;
}
