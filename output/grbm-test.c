#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define IOCTL_WRITE_REG   0x80000B8C
#define IOCTL_READ_REG    0x80000B88
#define IOCTL_LOAD_CP_FW  0x80000BD6

#define GRBM_GFX_INDEX   0x34D0
#define GRBM_GFX_CNTL    0x2022
#define KIQ_SELECT        0x00010000
#define BROADCAST         0xE0000000

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

    printf("=== Testing GRBM_GFX_CNTL (0x2022) for KIQ ===\n\n");

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

    printf("=== Test 1: GRBM_GFX_INDEX (0x34D0) with ME=1 ===\n");
    write_reg(GRBM_GFX_INDEX, KIQ_SELECT);
    printf("KIQ_BASE_LO via GRBM_GFX_INDEX: 0x%08X\n", read_reg(0xE060));
    printf("KIQ_SIZE via GRBM_GFX_INDEX:   0x%08X\n", read_reg(0xE068));
    write_reg(GRBM_GFX_INDEX, BROADCAST);

    printf("\n=== Test 2: GRBM_GFX_CNTL (0x2022) with ME=1 ===\n");
    write_reg(GRBM_GFX_CNTL, KIQ_SELECT);
    printf("KIQ_BASE_LO via GRBM_GFX_CNTL: 0x%08X\n", read_reg(0xE060));
    printf("KIQ_SIZE via GRBM_GFX_CNTL:   0x%08X\n", read_reg(0xE068));
    
    // Try writing KIQ_SIZE with GRBM_GFX_CNTL selected
    write_reg(0xE068, 0x00000100);
    printf("Wrote 0x100 to KIQ_SIZE, read back: 0x%08X\n", read_reg(0xE068));
    write_reg(GRBM_GFX_CNTL, 0x00000000);

    printf("\n=== Test 3: HQD registers via GRBM_GFX_CNTL KIQ select ===\n");
    write_reg(GRBM_GFX_CNTL, KIQ_SELECT);
    printf("HQD_ACTIVE (0xDAC0):    0x%08X\n", read_reg(0xDAC0));
    printf("HQD_PQ_BASE_LO (0xDAC4): 0x%08X\n", read_reg(0xDAC4));
    printf("HQD_PQ_CONTROL (0xDACC): 0x%08X\n", read_reg(0xDACC));
    
    // Try writing HQD_PQ_CONTROL
    write_reg(0xDACC, 0x00000002);
    printf("Wrote 0x2 to HQD_PQ_CONTROL, read back: 0x%08X\n", read_reg(0xDACC));
    write_reg(GRBM_GFX_CNTL, 0x00000000);

    printf("\n=== Test 4: KIQ with size written via GRBM_GFX_CNTL ===\n");
    write_reg(GRBM_GFX_CNTL, KIQ_SELECT);
    write_reg(0xE060, 0xDEADBEEF);
    write_reg(0xE06C, 0x00000000);
    write_reg(0xE078, 0x00000002);
    write_reg(0xE07C, 0x00000000);
    
    // Try setting KIQ_SIZE
    write_reg(0xE068, 0x00010000);
    printf("KIQ_SIZE after write: 0x%08X\n", read_reg(0xE068));
    
    write_reg(0xE080, 0x00000001);
    printf("KIQ_ACTIVE: 0x%08X\n", read_reg(0xE080));
    
    Sleep(500);
    
    printf("KIQ_RPTR after 500ms: 0x%08X\n", read_reg(0xE06C));
    write_reg(GRBM_GFX_CNTL, 0x00000000);
    write_reg(0xE080, 0x00000000);

    printf("\nSCRATCH after: 0x%08X\n", read_reg(0x32D4));
    printf("\n=== Test Complete ===\n");
    CloseHandle(hDevice);
    return 0;
}
