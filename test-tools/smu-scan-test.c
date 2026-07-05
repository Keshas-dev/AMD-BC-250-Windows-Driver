#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE g_hDev = INVALID_HANDLE_VALUE;

static uint32_t ReadReg(uint32_t offset) {
    AMDBC250_IOCTL_REG_ACCESS r = {0};
    DWORD returned = 0;
    r.RegisterOffset = offset;
    if (DeviceIoControl(g_hDev, IOCTL_AMDBC250_READ_REG,
        &r, sizeof(r), &r, sizeof(r), &returned, NULL))
        return r.Value;
    return 0xFFFFFFFF;
}

int main() {
    g_hDev = CreateFileA("\\\\.\\AMDBC250DreamV43",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, 0, NULL);
    
    if (g_hDev == INVALID_HANDLE_VALUE) {
        printf("FAILED: GPU driver not found (err=%u)\n", GetLastError());
        return 1;
    }
    printf("GPU driver opened\n\n");
    
    printf("=== SMU/PSP Register Scan (0x16000-0x17500) ===\n");
    printf("Offset     Value\n");
    printf("--------   --------\n");
    
    int found = 0;
    for (uint32_t off = 0x16000; off < 0x17500; off += 4) {
        uint32_t v = ReadReg(off);
        if (v != 0 && v != 0xFFFFFFFF) {
            printf("0x%05X   0x%08X\n", off, v);
            found++;
        }
    }
    if (found == 0) {
        printf("(all zero or dead in 0x16000-0x17500)\n");
    }
    
    printf("\n=== Known SMU/PSP Registers ===\n");
    uint32_t keyRegs[] = {
        0x1056C, // C2PMSG_35
        0x10570, // C2PMSG_36
        0x105E0, // C2PMSG_64
        0x10614, // C2PMSG_81
        0x10660, // C2PMSG_101
        0x16600, // THM
        0x16604,
        0x16800, // SMUIO
        0x16804,
        0x16A00, // SMUIO alternative
        0x16A04,
        0x16A08, // SMU C2PMSG_66
        0x16A48, // SMU C2PMSG_82
        0x16A68, // SMU C2PMSG_90
        0x16C00, // CLK
        0x16C04,
        0x17000, // Core init range
        0x17400, // FUSE
        0x17404,
    };
    const char* labels[] = {
        "C2PMSG_35","C2PMSG_36","C2PMSG_64","C2PMSG_81","C2PMSG_101",
        "THM+0","THM+4","SMUIO+0","SMUIO+4","SMUIO_A+0","SMUIO_A+4",
        "SMU_C2P_66","SMU_C2P_82","SMU_C2P_90",
        "CLK+0","CLK+4","CORE_INIT","FUSE+0","FUSE+4"
    };
    for (int i = 0; i < 19; i++) {
        uint32_t v = ReadReg(keyRegs[i]);
        printf("  0x%05X %-12s = 0x%08X", keyRegs[i], labels[i], v);
        if (v == 0xFFFFFFFF) printf(" (DEAD)");
        else if (v == 0) printf(" (zero)");
        printf("\n");
    }
    
    // Also probe some GPU BAR5 base registers
    printf("\n=== GPU Base Registers ===\n");
    uint32_t gpuRegs[] = {0x0000, 0x3260, 0x3264, 0x32D4, 0x34D0, 0x34FC, 0x3D64};
    const char* gpuLabels[] = {"GPU_ID","GRBM_STATUS","CC_CFG","SCRATCH","GRBM_IDX","SPI_WGP","RLC_PG"};
    for (int i = 0; i < 7; i++) {
        uint32_t v = ReadReg(gpuRegs[i]);
        printf("  0x%04X %-12s = 0x%08X", gpuRegs[i], gpuLabels[i], v);
        if (v == 0xFFFFFFFF) printf(" (DEAD)");
        printf("\n");
    }
    
    CloseHandle(g_hDev);
    printf("\n=== DONE ===\n");
    return 0;
}
