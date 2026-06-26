#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

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
    
    printf("=== PSP-style KIQ setup (no ACTIVE, no HQD) ===\n\n");
    
    uint8_t* fw; long fwSize;
    FILE* f = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\firmware\\cyan_skillfish2_mec.bin", "rb");
    fseek(f, 0, SEEK_END); fwSize = ftell(f); fseek(f, 0, SEEK_SET);
    fw = (uint8_t*)malloc(fwSize); fread(fw, 1, fwSize, f); fclose(f);
    load_cp_fw(fw, (uint32_t)fwSize, 4);
    free(fw);
    printf("MEC firmware loaded.\n\n");
    
    // Allocate ring buffer (contiguous physical memory below 4GB)
    // Use same approach as PSP driver
    PHYSICAL_ADDRESS lowAddr = {0};
    PHYSICAL_ADDRESS highAddr; highAddr.QuadPart = 0xFFFFFFFFULL;
    PHYSICAL_ADDRESS boundaryAddr = {0};
    
    // Try different sizes
    uint32_t ringSizes[] = {0x1000, 0x2000, 0x4000, 0x10000, 0x40000};
    
    for (int s = 0; s < 5; s++) {
        uint32_t ringSize = ringSizes[s];
        PVOID ringVa = MmAllocateContiguousMemorySpecifyCache(ringSize, lowAddr, highAddr, boundaryAddr, MmNonCached);
        if (!ringVa) {
            printf("Failed to allocate %u bytes for ring\n", ringSize);
            continue;
        }
        
        PHYSICAL_ADDRESS ringPa = MmGetPhysicalAddress(ringVa);
        printf("\n--- Ring: VA=%p PA=0x%llX size=%u ---\n", ringVa, ringPa.QuadPart, ringSize);
        
        // Zero the ring
        memset(ringVa, 0, ringSize);
        
        // Write a NOP PM4 packet at offset 0
        // PM4 NOP: 0x80000000 (header with count=0)
        *(volatile uint32_t*)ringVa = 0x80000000;
        
        // PSP-style KIQ setup
        write_reg(0x34D0, 0x00010000);  // ME=1
        
        write_reg(0xE060, (uint32_t)(ringPa.QuadPart & 0xFFFFFFFF));  // BASE_LO
        write_reg(0xE064, (uint32_t)(ringPa.QuadPart >> 32));         // BASE_HI
        write_reg(0xE06C, 0x00000000);  // RPTR = 0
        write_reg(0xE078, 0x00000001);  // WPTR = 1 (one NOP packet)
        
        printf("KIQ_BASE_LO: 0x%08X\n", read_reg(0xE060));
        printf("KIQ_RPTR: 0x%08X\n", read_reg(0xE06C));
        printf("KIQ_WPTR: 0x%08X\n", read_reg(0xE078));
        
        // Don't write KIQ_ACTIVE!
        // Don't write KIQ_DOORBELL!
        // Don't write KIQ_VMID!
        // Don't write KIQ_CNTL!
        
        // Unhalt ME_CNTL (PSP does this)
        write_reg(0x4A74, 0x00000000);  // ME_CNTL = 0 (unhalt all)
        printf("ME_CNTL: 0x%08X\n", read_reg(0x4A74));
        
        printf("Waiting 1 second...\n");
        Sleep(1000);
        
        printf("KIQ_RPTR after 1s: 0x%08X\n", read_reg(0xE06C));
        printf("GRBM_STATUS: 0x%08X\n", read_reg(0x2010));
        
        // Check if ring was consumed
        uint32_t* ringPtr = (uint32_t*)ringVa;
        printf("Ring[0]: 0x%08X (should be 0 if consumed)\n", ringPtr[0]);
        
        // Cleanup
        write_reg(0x4A74, 0x70000000);  // Halt all
        write_reg(0x34D0, 0xE0000000);  // Broadcast
        MmFreeContiguousMemory(ringVa);
        
        if (read_reg(0xE06C) != 0) {
            printf("\n*** SUCCESS! KIQ_RPTR advanced with ring size %u ***\n", ringSize);
            break;
        }
    }
    
    printf("\nSCRATCH: 0x%08X\n", read_reg(0x32D4));
    printf("\n=== Done ===\n");
    CloseHandle(hDevice);
    return 0;
}
