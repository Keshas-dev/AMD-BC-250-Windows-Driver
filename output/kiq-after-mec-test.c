#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define IOCTL_WRITE_REG   0x80000B8C
#define IOCTL_READ_REG    0x80000B88
#define IOCTL_LOAD_CP_FW  0x80000BD6

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
    return outBuf[1];  /* Value is at index 1, not 0! */
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

    printf("=== KIQ After MEC Firmware Load Test ===\n\n");

    // Read SCRATCH before
    uint32_t scratchBefore = read_reg(0x32D4);
    printf("SCRATCH before: 0x%08X\n", scratchBefore);

    // Load MEC firmware
    printf("Loading MEC firmware...\n");
    FILE* f = fopen(mec_fw, "rb");
    if (!f) {
        printf("Failed to open firmware file\n");
        CloseHandle(hDevice);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long fwSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* fwData = (uint8_t*)malloc(fwSize);
    fread(fwData, 1, fwSize, f);
    fclose(f);

    load_cp_fw(fwData, (uint32_t)fwSize, 4);
    free(fwData);
    printf("MEC firmware loaded.\n");

    printf("\n=== KIQ Setup Workaround ===\n");

    // Select ME=1
    write_reg(0x34D0, 0xE0000000 | (1 << 16));
    printf("GRBM_GFX_INDEX: selected ME=1\n");

    // KIQ_BASE_LO
    write_reg(0xE060, 0xDEADBEEF);
    uint32_t kiqBaseLo = read_reg(0xE060);
    printf("KIQ_BASE_LO wrote 0xDEADBEEF, read: 0x%08X\n", kiqBaseLo);

    // KIQ_BASE_HI
    write_reg(0xE064, 0x00000000);
    uint32_t kiqBaseHi = read_reg(0xE064);
    printf("KIQ_BASE_HI wrote 0x00000000, read: 0x%08X\n", kiqBaseHi);

    // KIQ_SIZE - read-only
    uint32_t kiqSize = read_reg(0xE068);
    printf("KIQ_SIZE (0xE068): 0x%08X (READ-ONLY)\n", kiqSize);

    // KIQ_RPTR
    write_reg(0xE06C, 0x00000000);
    uint32_t kiqRptr = read_reg(0xE06C);
    printf("KIQ_RPTR wrote 0x0, read: 0x%08X\n", kiqRptr);

    // KIQ_WPTR
    write_reg(0xE078, 0x00000002);
    uint32_t kiqWptr = read_reg(0xE078);
    printf("KIQ_WPTR wrote 0x2, read: 0x%08X\n", kiqWptr);

    // KIQ_DOORBELL
    write_reg(0xE074, 0x00000001);
    uint32_t kiqDoor = read_reg(0xE074);
    printf("KIQ_DOORBELL wrote 0x1, read: 0x%08X\n", kiqDoor);

    // KIQ_VMID
    write_reg(0xE07C, 0x00000000);
    uint32_t kiqVmid = read_reg(0xE07C);
    printf("KIQ_VMID wrote 0x0, read: 0x%08X\n", kiqVmid);

    // KIQ_ACTIVE
    write_reg(0xE080, 0x00000001);
    uint32_t kiqActive = read_reg(0xE080);
    printf("KIQ_ACTIVE wrote 0x1, read: 0x%08X\n", kiqActive);

    printf("\nWaiting 500ms for CP to process ring...\n");
    Sleep(500);

    // Check RPTR again
    uint32_t rptrAfter = read_reg(0xE06C);
    printf("KIQ_RPTR after 500ms: 0x%08X\n", rptrAfter);

    // Check GRBM_STATUS
    uint32_t grbmStatus = read_reg(0x2010);
    printf("GRBM_STATUS (0x2010): 0x%08X\n", grbmStatus);

    // SCRATCH after
    uint32_t scratchAfter = read_reg(0x32D4);
    printf("SCRATCH after: 0x%08X\n", scratchAfter);

    printf("\n=== Analysis ===\n");
    if (rptrAfter == 0) {
        printf("KIQ_RPTR stayed 0 - CP did NOT process ring\n");
        printf("Root cause: KIQ_SIZE=0 (0x%08X) tells CP ring has 0 bytes\n", kiqSize);
        printf("Solution: Need to bypass KIQ_SIZE check in MEC firmware\n");
    } else {
        printf("KIQ_RPTR advanced to 0x%08X - ring processed!\n", rptrAfter);
    }

    // Restore GRBM_GFX_INDEX
    write_reg(0x34D0, 0xE0000000);

    // Deactivate KIQ
    write_reg(0xE080, 0x00000000);

    printf("\n=== Test Complete ===\n");
    CloseHandle(hDevice);
    return 0;
}
