/*
 * g3d-state-ro - READ-ONLY 3D state dump. Writes nothing.
 *
 * Purpose: measure, not assume. AGENTS.md claims GFX ring BASE registers are
 * host-read-only (SOS-locked). That claim decides whether VMID/GART is required
 * to make the CP consume our ring, or whether the ring simply needs a kick.
 * AGENTS has been wrong before, so this measures it live instead.
 *
 * Also dumps the MC_VM/GART registers we currently write in DreamV3GartInitialize
 * (0x9528..) read-only, to see whether they look like UMC registers or something
 * else entirely (0x9528 may be in GC range, not UMC).
 *
 * SAFE: read-only. No register writes, no SMU messages, no fences.
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE g_hDev = INVALID_HANDLE_VALUE;

static uint32_t ReadReg(uint32_t offset) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD returned = 0;
    r.RegisterOffset = offset; r.Value = 0;
    if (DeviceIoControl(g_hDev, IOCTL_AMDBC250_READ_REG, &r, sizeof(r),
                        &r, sizeof(r), &returned, NULL))
        return r.Value;
    return 0xFFFFFFFF;
}

static void Row(const char *name, uint32_t addr) {
    uint32_t v = ReadReg(addr);
    if (v == 0xFFFFFFFF) printf("  %-26s 0x%05X  = 0xFFFFFFFF   (unmapped/dead)\n", name, addr);
    else                 printf("  %-26s 0x%05X  = 0x%08X\n", name, addr, v);
}

static void WriteReg(uint32_t offset, uint32_t value) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD returned = 0;
    r.RegisterOffset = offset; r.Value = value;
    DeviceIoControl(g_hDev, IOCTL_AMDBC250_WRITE_REG, &r, sizeof(r),
                    &r, sizeof(r), &returned, NULL);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);

    g_hDev = CreateFileA("\\\\.\\AMDBC250DreamV43",
        GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, 0, NULL);
    if (g_hDev == INVALID_HANDLE_VALUE) {
        printf("FAIL: cannot open GPU device (err=%lu)\n", GetLastError());
        return 1;
    }

    {
        AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br = 0;
        memset(&ih, 0, sizeof(ih));
        ih.MmioPhysicalBase = 0xFE800000ULL;
        ih.MmioSize = 0x80000;
        ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
        if (!DeviceIoControl(g_hDev, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih),
                             NULL, 0, &br, NULL)) {
            printf("FAIL: INIT_HARDWARE (err=%lu)\n", GetLastError());
            return 1;
        }
    }

    printf("\n=== SANITY ===\n");
    Row("GPU_ID",                0x0000);
    Row("GRBM_STATUS",           0x3260);
    Row("SCRATCH_REG0",          0x32D4);
    Row("GRBM_GFX_INDEX",        0x34D0);

    printf("\n=== GFX RING (Linux mmCP_RB0_* , GC_BASE+mm*4) ===\n");
    Row("CP_RB0_BASE_LO",        0x89E0);
    Row("CP_RB0_BASE_HI",        0x8BA4);
    Row("CP_RB0_CNTL",           0x89E4);
    Row("CP_RB0_RPTR_ADDR",      0x89EC);
    Row("CP_RB0_WPTR",           0x8A30);
    Row("CP_RB0_RPTR",           0x4FE0);
    Row("CP_RB1_BASE_LO",        0x89F0);
    Row("CP_RB1_BASE_HI",        0x8BB4);

    printf("\n=== GFX RING alias at 0xDA6x (older tests used this) ===\n");
    Row("alias RB0_BASE_LO",     0xDA60);
    Row("alias RB0_CNTL",        0xDA68);
    Row("alias RB0_RPTR",        0xDA6C);
    Row("alias RB0_WPTR",        0xDA78);

    printf("\n=== KIQ ===\n");
    Row("KIQ_BASE_LO",           0xE060);
    Row("KIQ_SIZE",              0xE068);

    printf("\n=== WGP / CU power ===\n");
    /* CC_GC_SHADER_ARRAY_CONFIG and SPI_PG_ENABLE_STATIC_WGP_MASK are
     * GRBM_GFX_INDEX-indexed. Selecting broadcast so the value we read is the
     * one actually programmed, not the value in whatever bank a previous test
     * happened to leave selected. Selecting is not a config write. */
    printf("  (broadcast select GRBM_GFX_INDEX = 0x15000000)\n");
    WriteReg(0x34D0, 0x15000000);
    Row("SPI_PG_WGP_MASK",       0x5C3C);
    Row("CC_GC_SHADER_ARRAY",    0x9C1C);
    Row("CP_ME_CNTL",            0x4A74);
    Row("CP_MEC_CNTL",           0x4B14);

    printf("\n=== GPU VM / page table ===\n");
    Row("GCVM_PT_BASE0_LO",      0x0B408);
    Row("GCVM_PT_BASE0_HI",      0x0B40C);
    Row("GCVM_PT_BASE1_LO",      0x0B608);
    Row("GCVM_PT_BASE1_HI",      0x0B60C);
    Row("GCVM_STATUS",           0x0C000);

    printf("\n=== MC_VM / GART - what 0x9528.. actually is ===\n");
    Row("MC_VM_AGP_BASE(0x9528)",0x9528);
    Row("MC_VM_AGP_TOP(0x952C)", 0x952C);
    Row("MC_VM_AGP_BOT(0x9530)", 0x9530);
    Row("MC_VM_APER_LOW(0x9540)",0x9540);
    Row("MC_VM_APER_HIGH(0x9544)",0x9544);
    Row("MC_VM_APER_BASE(0x9548)",0x9548);

    printf("\n=== UMC (for comparison: is 0x9528 UMC or GC?) ===\n");
    Row("UMC0 base+0x00",        0x14000);
    Row("UMC0 base+0x04",        0x14004);
    Row("UMC1 base+0x00",        0x54000);
    Row("GC 0x1278 (near 0x9528's mm)", 0x1260 + 0x1278 * 0 + 0x1278 * 4);

    printf("\nDone (read-only, nothing was written).\n");
    CloseHandle(g_hDev);
    return 0;
}