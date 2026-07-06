#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE g_hDev = INVALID_HANDLE_VALUE;

static uint32_t ReadReg(uint32_t off) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD ret = 0;
    r.RegisterOffset = off; r.Value = 0;
    if (DeviceIoControl(g_hDev, IOCTL_AMDBC250_READ_REG, &r, sizeof(r), &r, sizeof(r), &ret, NULL)) return r.Value;
    return 0xFFFFFFFF;
}
static BOOL WriteReg(uint32_t off, uint32_t val) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD ret = 0;
    r.RegisterOffset = off; r.Value = val;
    return DeviceIoControl(g_hDev, IOCTL_AMDBC250_WRITE_REG, &r, sizeof(r), &r, sizeof(r), &ret, NULL);
}
static void ProbeWrite(const char* name, uint32_t off, uint32_t testVal) {
    uint32_t b = ReadReg(off);
    WriteReg(off, testVal);
    uint32_t a = ReadReg(off);
    WriteReg(off, b);
    printf("  %-40s (0x%04X): 0x%08X -> 0x%08X -> 0x%08X %s\n", name, off, b, testVal, a, (a==testVal?"WRITABLE":(a==b?"RO":"PARTIAL")));
}
static void ReadOnly(const char* name, uint32_t off) {
    printf("  %-40s (0x%04X): 0x%08X\n", name, off, ReadReg(off));
}

int main() {
    g_hDev = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (g_hDev == INVALID_HANDLE_VALUE) { printf("FAIL: CreateFile gle=%lu\n", GetLastError()); return 1; }

    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD ret = 0;
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL; ih.MmioSize = 0x80000;
    ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
    DeviceIoControl(g_hDev, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &ret, NULL);
    printf("INIT_HW OK\n\n");

    /* === Step 1: All HQD registers with ME=1 selected === */
    printf("=== HQD registers with ME=1 (GRBM_GFX_INDEX=0x00010000) ===\n\n");
    WriteReg(0x34D0, 0x00010000);

    printf("-- Queue config --\n");
    ProbeWrite("CP_HQD_PQ_CONTROL", 0x90F0, 0x0000003F);
    ProbeWrite("CP_HQD_PQ_BASE_LO", 0x9124, 0x00001000);
    ProbeWrite("CP_HQD_PQ_BASE_HI", 0x9128, 0x00000000);
    ProbeWrite("CP_HQD_PQ_WPTR_LO", 0x91DC, 0x00000005);
    ProbeWrite("CP_HQD_PQ_WPTR_HI", 0x91E0, 0x00000000);
    printf("\n-- Queue state --\n");
    ProbeWrite("CP_MQD_BASE_ADDR", 0x9104, 0x80000000);
    ProbeWrite("CP_MQD_BASE_ADDR_HI", 0x9108, 0x00000000);
    ProbeWrite("CP_HQD_ACTIVE", 0x910C, 0x00000001);
    ProbeWrite("CP_HQD_VMID", 0x9134, 0x00000000);
    ProbeWrite("CP_HQD_PERSISTENT_STATE", 0x9130, 0x0000E001);
    printf("\n-- EOP --\n");
    ProbeWrite("CP_HQD_EOP_BASE_ADDR", 0x90EC, 0x80000000);
    ProbeWrite("CP_HQD_EOP_BASE_ADDR_HI", 0x90F4, 0x00000000);
    ProbeWrite("CP_HQD_EOP_CONTROL", 0x90F8, 0x08000000);

    /* Restore GRBM_GFX_INDEX */
    WriteReg(0x34D0, 0xE0000000);

    /* === Step 2: Compare with broadcast === */
    printf("\n\n=== HQD registers with BROADCAST (GRBM_GFX_INDEX=0xE0000000) ===\n\n");
    WriteReg(0x34D0, 0xE0000000);

    printf("-- Queue config --\n");
    ProbeWrite("CP_HQD_PQ_CONTROL", 0x90F0, 0x0000003F);
    ProbeWrite("CP_HQD_PQ_BASE_LO", 0x9124, 0x00001000);
    printf("\n-- Queue state --\n");
    ProbeWrite("CP_HQD_ACTIVE", 0x910C, 0x00000001);

    /* === Step 3: KIQ registers with ME=1 === */
    printf("\n\n=== KIQ registers with ME=1 ===\n\n");
    WriteReg(0x34D0, 0x00010000);

    ProbeWrite("KIQ_BASE_LO", 0xE060, 0x12345678);
    ProbeWrite("KIQ_CNTL", 0xE068, 0x8000103F);
    ProbeWrite("KIQ_WPTR", 0xE078, 0x00000005);
    ProbeWrite("KIQ_RPTR", 0xE06C, 0x00000000);
    ProbeWrite("KIQ_ACTIVE", 0xE080, 0x00000001);

    WriteReg(0x34D0, 0xE0000000);

    /* === Step 4: Engine status === */
    printf("\n\n=== Engine Status ===\n");
    ReadOnly("GRBM_STATUS", 0x3260);
    ReadOnly("CP_ME_CNTL", 0x4A74);
    ReadOnly("CP_MEC_CNTL", 0x4B14);
    ReadOnly("RLC_CNTL", 0x4B20);
    ReadOnly("RLC_CP_SCHEDULERS", 0xECA8);
    ReadOnly("GRBM_GFX_INDEX", 0x34D0);

    CloseHandle(g_hDev);
    printf("\nDONE\n");
    return 0;
}
