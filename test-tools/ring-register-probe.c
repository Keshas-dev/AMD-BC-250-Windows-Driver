#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE g_hDev = INVALID_HANDLE_VALUE;

static BOOL WriteReg(uint32_t offset, uint32_t value) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD returned = 0;
    r.RegisterOffset = offset; r.Value = value;
    return DeviceIoControl(g_hDev, IOCTL_AMDBC250_WRITE_REG, &r, sizeof(r), &r, sizeof(r), &returned, NULL);
}
static uint32_t ReadReg(uint32_t offset) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD returned = 0;
    r.RegisterOffset = offset; r.Value = 0;
    if (DeviceIoControl(g_hDev, IOCTL_AMDBC250_READ_REG, &r, sizeof(r), &r, sizeof(r), &returned, NULL)) return r.Value;
    return 0xFFFFFFFF;
}

static void ProbeReg(const char* name, uint32_t off, int dangerous) {
    uint32_t v1 = ReadReg(off);
    printf("  %-30s [0x%04X] = 0x%08X", name, off, v1);
    if (!dangerous) {
        uint32_t v2, v3;
        WriteReg(off, 0x12345678);
        v2 = ReadReg(off);
        WriteReg(off, 0x87654321);
        v3 = ReadReg(off);
        uint32_t writable = (v1 ^ v2) & (v2 ^ v3);
        uint32_t sticky = v1 ^ v2;
        if (sticky) {
            printf(" -> 0x%08X", v2);
            if (writable) printf(" [WR: 0x%08X mask]", writable);
            if (!writable) printf(" [W1C?]");
        } else {
            printf(" [RO]");
        }
    } else {
        printf(" [SKIP write - dangerous]");
    }
    printf("\n");
}

int main() {
    g_hDev = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (g_hDev == INVALID_HANDLE_VALUE) { printf("FAIL: CreateFile gle=%lu\n", GetLastError()); return 1; }
    printf("CreateFile OK\n");

    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD ret = 0;
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL;
    ih.MmioSize = 0x80000;
    ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
    BOOL ok = DeviceIoControl(g_hDev, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &ret, NULL);
    printf("INIT_HW: ok=%d gle=%lu\n\n", ok, GetLastError());

    printf("=== CP_GFX_RING0 (0xDA60 range) ===\n");
    ProbeReg("RB0_BASE_LO",  0xDA60, 0);
    ProbeReg("RB0_BASE_HI",  0xDA64, 0);
    ProbeReg("RB0_CNTL",     0xDA68, 0);
    ProbeReg("RB0_RPTR",     0xDA6C, 0);
    ProbeReg("RB0_RPTR_ADDR_LO", 0xDA70, 0);
    ProbeReg("RB0_RPTR_ADDR_HI", 0xDA74, 0);
    ProbeReg("RB0_WPTR",     0xDA78, 0);
    ProbeReg("RB0_WPTR_POLL",0xDA7C, 0);
    ProbeReg("RB0_DOORBELL", 0xDA80, 0);

    printf("\n=== CP_RB0 (0x89E0 range, Linux-corrected) ===\n");
    ProbeReg("RB0_BASE",     0x89E0, 0);
    ProbeReg("RB0_BASE_HI",  0x8BA4, 0);
    ProbeReg("RB0_CNTL",     0x89E4, 0);
    ProbeReg("RB0_RPTR",     0x4FE0, 0);
    ProbeReg("RB0_RPTR_ADDR",0x89EC, 0);
    ProbeReg("RB0_RPTR_ADDR_HI", 0x89F0, 0);
    ProbeReg("RB0_WPTR",     0x8A30, 0);
    ProbeReg("RB0_WPTR_HI",  0x8A34, 0);

    printf("\n=== CP_KIQ (0xE060 range) ===\n");
    ProbeReg("KIQ_BASE_LO",  0xE060, 0);
    ProbeReg("KIQ_BASE_HI",  0xE064, 0);
    ProbeReg("KIQ_CNTL",     0xE068, 0);
    ProbeReg("KIQ_RPTR",     0xE06C, 0);
    ProbeReg("KIQ_PQ_CTL",   0xE070, 0);
    ProbeReg("KIQ_DOORBELL", 0xE074, 0);
    ProbeReg("KIQ_WPTR",     0xE078, 0);
    ProbeReg("KIQ_VMID",     0xE07C, 0);
    ProbeReg("KIQ_ACTIVE",   0xE080, 0);

    printf("\n=== MQD/HQD (0x9104 range) ===\n");
    ProbeReg("CP_MQD_BASE_ADDR",   0x9104, 0);
    ProbeReg("CP_MQD_BASE_ADDR_HI",0x9108, 0);
    ProbeReg("CP_HQD_ACTIVE",      0x910C, 0);
    ProbeReg("CP_HQD_VMID",        0x9110, 0);
    ProbeReg("CP_HQD_PQ_BASE",     0x9124, 0);
    ProbeReg("CP_HQD_PQ_CONTROL",  0x9148, 0);
    ProbeReg("CP_HQD_PQ_WPTR_LO",  0x91DC, 0);

    printf("\n=== COMPUTE (0x80E0 range) - READ ONLY ===\n");
    ProbeReg("DISPATCH_INITIATOR", 0x80E0, 1);
    ProbeReg("PGM_LO",            0x8110, 0);
    ProbeReg("PGM_HI",            0x8114, 0);
    ProbeReg("PGM_RSRC1",         0x8128, 0);
    ProbeReg("PGM_RSRC2",         0x812C, 0);

    printf("\n=== ME/MEC control - READ ONLY ===\n");
    ProbeReg("CP_ME_CNTL",        0x4A74, 1);
    ProbeReg("CP_MEC_CNTL",       0x4B14, 1);
    ProbeReg("RLC_CNTL",          0x4B20, 1);

    printf("\n=== GCVM - READ ONLY ===\n");
    ProbeReg("VM_CONTEXT0_CNTL",  0x6C0C, 0);
    ProbeReg("VM_INV_REQ",        0x6C10, 1);

    printf("\n=== SDMA0 - READ ONLY ===\n");
    ProbeReg("SDMA0_CNTL",        0xE018, 1);
    ProbeReg("SDMA0_RB_CNTL",     0xE01C, 0);

    CloseHandle(g_hDev);
    printf("\nDONE\n");
    return 0;
}
