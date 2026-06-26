#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define IOCTL_WRITE_REG   0x80000B8C
#define IOCTL_READ_REG    0x80000B88
#define IOCTL_LOAD_CP_FW  0x80000BD6

#define GRBM_GFX_CNTL    0x2022
#define KIQ_SELECT        0x00010000
#define BROADCAST         0x00000000

static HANDLE hDevice = INVALID_HANDLE_VALUE;

uint32_t read_reg(uint32_t offset) {
    uint32_t bytesReturned;
    struct { uint32_t offset; uint32_t value; } inBuf = {offset, 0};
    uint32_t outBuf[2] = {0};

    BOOL result = DeviceIoControl(hDevice, IOCTL_READ_REG, &inBuf, sizeof(inBuf),
        outBuf, sizeof(outBuf), &bytesReturned, NULL);

    if (!result || bytesReturned < 4) {
        printf("  READ FAILED (err=%lu)\n", GetLastError());
        return 0xFFFFFFFF;
    }
    return outBuf[1];
}

void write_reg(uint32_t offset, uint32_t value) {
    uint32_t bytesReturned;
    struct { uint32_t offset; uint32_t value; } inBuf = {offset, value};

    DeviceIoControl(hDevice, IOCTL_WRITE_REG, &inBuf, sizeof(inBuf), NULL, 0, &bytesReturned, NULL);
}

void load_cp_fw(const uint8_t* fw, uint32_t size, uint32_t type) {
    uint32_t bytesReturned;
    uint8_t* inBuf = (uint8_t*)malloc(size + 8);
    if (!inBuf) return;

    *(uint32_t*)inBuf = type;
    *(uint32_t*)(inBuf + 4) = 0;
    memcpy(inBuf + 8, fw, size);

    DeviceIoControl(hDevice, IOCTL_LOAD_CP_FW, inBuf, size + 8, NULL, 0, &bytesReturned, NULL);
    free(inBuf);
}

int main(int argc, char* argv[]) {
    const char* device = "\\\\.\\AMDBC250DreamV43";
    const char* mec_fw = "C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\firmware\\cyan_skillfish2_mec.bin";

    hDevice = CreateFileA(device, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("Failed to open driver: %lu\n", GetLastError());
        return 1;
    }

    printf("=== HQD KIQ Setup via GRBM_GFX_CNTL (0x2022) ===\n\n");

    uint32_t scratchBefore = read_reg(0x32D4);
    printf("SCRATCH before: 0x%08X\n", scratchBefore);

    printf("Loading MEC firmware...\n");
    FILE* f = fopen(mec_fw, "rb");
    fseek(f, 0, SEEK_END);
    long fwSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* fwData = (uint8_t*)malloc(fwSize);
    fread(fwData, 1, fwSize, f);
    fclose(f);
    load_cp_fw(fwData, (uint32_t)fwSize, 4);
    free(fwData);
    printf("MEC firmware loaded.\n\n");

    // Use GRBM_GFX_CNTL for KIQ select (Linux method)
    #define SELECT_KIQ  write_reg(GRBM_GFX_CNTL, KIQ_SELECT)
    #define SELECT_NONE write_reg(GRBM_GFX_CNTL, BROADCAST)

    printf("=== HQD Register Setup (via GRBM_GFX_CNTL KIQ select) ===\n");
    
    SELECT_KIQ;
    
    // HQD_PQ_BASE_LO - ring buffer base address (lower 32 bits)
    write_reg(0xDAC4, 0xDEADBEEF);
    printf("HQD_PQ_BASE_LO (0xDAC4): wrote 0xDEADBEEF, read: 0x%08X\n", read_reg(0xDAC4));
    
    // HQD_PQ_BASE_HI - ring buffer base address (upper 32 bits)
    write_reg(0xDAC8, 0x00000000);
    printf("HQD_PQ_BASE_HI (0xDAC8): wrote 0x0, read: 0x%08X\n", read_reg(0xDAC8));
    
    // HQD_PQ_CONTROL - ring control
    // bit 0 = queue enable, bit 1 = set it to 1 for... something
    uint32_t pq_ctrl = read_reg(0xDACC);
    printf("HQD_PQ_CONTROL (0xDACC): read: 0x%08X\n", pq_ctrl);
    
    // Try enabling queue (set bit 0)
    write_reg(0xDACC, pq_ctrl | 0x00000001);
    printf("HQD_PQ_CONTROL after enable: 0x%08X\n", read_reg(0xDACC));
    
    // HQD_PQ_RPTR - read pointer
    write_reg(0xDAD0, 0x00000000);
    printf("HQD_PQ_RPTR (0xDAD0): wrote 0x0, read: 0x%08X\n", read_reg(0xDAD0));
    
    // HQD_PQ_WPTR - write pointer  
    write_reg(0xDAD4, 0x00000002);
    printf("HQD_PQ_WPTR (0xDAD4): wrote 0x2, read: 0x%08X\n", read_reg(0xDAD4));
    
    // HQD_ACTIVE - queue active
    write_reg(0xDAC0, 0x00000001);
    printf("HQD_ACTIVE (0xDAC0): wrote 0x1, read: 0x%08X\n", read_reg(0xDAC0));
    
    SELECT_NONE;
    
    printf("\nWaiting 1 second...\n");
    Sleep(1000);
    
    printf("\n=== Check Results ===\n");
    SELECT_KIQ;
    printf("HQD_PQ_RPTR after 1s: 0x%08X\n", read_reg(0xDAD0));
    printf("HQD_ACTIVE: 0x%08X\n", read_reg(0xDAC0));
    
    // Also check KIQ_RPTR
    printf("KIQ_RPTR (0xE06C): 0x%08X\n", read_reg(0xE06C));
    
    SELECT_NONE;
    
    printf("\nSCRATCH after: 0x%08X\n", read_reg(0x32D4));
    
    // Cleanup: deactivate
    SELECT_KIQ;
    write_reg(0xDAC0, 0x00000000);
    write_reg(0xDAD0, 0x00000000);
    SELECT_NONE;
    
    printf("\n=== Test Complete ===\n");
    CloseHandle(hDevice);
    return 0;
}
