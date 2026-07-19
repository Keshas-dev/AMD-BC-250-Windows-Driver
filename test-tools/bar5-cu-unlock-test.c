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

#define SPI_PG_ENABLE_STATIC_WGP_MASK 0x5C3C
#define CC_GC_SHADER_ARRAY_CONFIG    0x9C1C
#define RLC_PG_ALWAYS_ON_WGP_MASK    0x3D64

#define GRBM_GFX_INDEX_BROADCAST 0xE0000000

/* Candidate GRBM_GFX_INDEX addresses (different GC_BASE assumptions) */
static const uint32_t GFX_INDEX_CANDIDATES[] = { 0x34D0, 0x3460, 0x9A60, 0xA800 };
static const char* GFX_INDEX_NAMES[] = { "0x34D0 (old probe)", "0x3460 (GC1260+mm)", "0x9A60 (GC1260+mm*4)", "0xA800 (GC2000+mm*4)" };

static uint32_t FindRealGrbmGfxIndex(void) {
    printf("\n=== Probing GRBM_GFX_INDEX candidates ===\n");
    for (int i = 0; i < 4; i++) {
        uint32_t addr = GFX_INDEX_CANDIDATES[i];
        uint32_t before = ReadReg(addr);
        WriteReg(addr, GRBM_GFX_INDEX_BROADCAST);
        uint32_t after = ReadReg(addr);
        WriteReg(addr, before); /* restore */
        int ok = (after == GRBM_GFX_INDEX_BROADCAST);
        printf("  %-28s before=0x%08X wrote=0x%08X read=0x%08X %s\n",
               GFX_INDEX_NAMES[i], before, GRBM_GFX_INDEX_BROADCAST, after,
               ok ? "*** MATCH (real GRBM_GFX_INDEX) ***" : (after == before ? "[RO]" : "[W1C?]"));
        if (ok) return addr;
    }
    printf("  No candidate matched 0xE0000000. GRBM_GFX_INDEX not found at these addresses.\n");
    return 0;
}

static void UnlockCU(uint32_t gfxIndexAddr) {
    printf("\n=== BC-250 40CU Unlock (GRBM_GFX_INDEX=0x%04X) ===\n", gfxIndexAddr);

    /* broadcast all SE/SH/PIPE/QUEUE/INSTANCE */
    WriteReg(gfxIndexAddr, GRBM_GFX_INDEX_BROADCAST);

    uint32_t spi_before = ReadReg(SPI_PG_ENABLE_STATIC_WGP_MASK);
    printf("SPI_PG_ENABLE_STATIC_WGP_MASK (0x%04X): before=0x%08X\n", SPI_PG_ENABLE_STATIC_WGP_MASK, spi_before);
    WriteReg(SPI_PG_ENABLE_STATIC_WGP_MASK, 0x1f);
    uint32_t spi_after = ReadReg(SPI_PG_ENABLE_STATIC_WGP_MASK);
    printf("  wrote 0x1F -> readback 0x%08X %s\n", spi_after,
           (spi_after == 0x1f) ? "[UNLOCKED!]" : "[LOCKED]");

    uint32_t cc_before = ReadReg(CC_GC_SHADER_ARRAY_CONFIG);
    printf("CC_GC_SHADER_ARRAY_CONFIG (0x%04X): before=0x%08X\n", CC_GC_SHADER_ARRAY_CONFIG, cc_before);
    WriteReg(CC_GC_SHADER_ARRAY_CONFIG, 0);
    uint32_t cc_after = ReadReg(CC_GC_SHADER_ARRAY_CONFIG);
    printf("  wrote 0x0 -> readback 0x%08X %s\n", cc_after,
           (cc_after == 0) ? "[CLEARED!]" : "[PARTIAL]");

    uint32_t rlc_before = ReadReg(RLC_PG_ALWAYS_ON_WGP_MASK);
    printf("RLC_PG_ALWAYS_ON_WGP_MASK (0x%04X): before=0x%08X\n", RLC_PG_ALWAYS_ON_WGP_MASK, rlc_before);
    WriteReg(RLC_PG_ALWAYS_ON_WGP_MASK, 0x1f);
    uint32_t rlc_after = ReadReg(RLC_PG_ALWAYS_ON_WGP_MASK);
    printf("  wrote 0x1F -> readback 0x%08X %s\n", rlc_after,
           (rlc_after == 0x1f) ? "[POWERED!]" : "[LOCKED]");

    printf("\n=== RESULT ===\n");
    if (spi_after == 0x1f && (cc_after == 0 || cc_before == 0) && rlc_after == 0x1f) {
        printf("*** 40CU UNLOCK SUCCEEDED — WGPs are NOT fused! ***\n");
    } else {
        printf("*** Unlock failed. SPI/RLC registers are SOS-locked (not writable from host). ***\n");
    }
}

int main(void) {
    g_hDev = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (g_hDev == INVALID_HANDLE_VALUE) {
        printf("FAIL: cannot open GPU device (err=%lu)\n", GetLastError());
        return 1;
    }

    printf("=== INIT_HARDWARE (BAR5 + NBIO_MAP) ===\n");
    {
        AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br = 0;
        memset(&ih, 0, sizeof(ih));
        ih.MmioPhysicalBase = 0xFE800000ULL;
        ih.MmioSize = 0x80000;
        ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
        if (!DeviceIoControl(g_hDev, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), NULL, 0, &br, NULL)) {
            printf("FAIL: INIT_HARDWARE (err=%lu)\n", GetLastError());
            return 1;
        }
        printf("OK\n");
    }

    uint32_t gfxIndex = FindRealGrbmGfxIndex();
    if (gfxIndex != 0) {
        UnlockCU(gfxIndex);
    } else {
        printf("\n=== Cannot find GRBM_GFX_INDEX — trying SPI/CC/RLC WITHOUT broadcast ===\n");
        /* Last resort: try writing SPI/CC/RLC directly (maybe broadcast not needed) */
        uint32_t spi_before = ReadReg(SPI_PG_ENABLE_STATIC_WGP_MASK);
        WriteReg(SPI_PG_ENABLE_STATIC_WGP_MASK, 0x1f);
        uint32_t spi_after = ReadReg(SPI_PG_ENABLE_STATIC_WGP_MASK);
        printf("SPI_PG_ENABLE_STATIC_WGP_MASK: before=0x%08X wrote=0x1F read=0x%08X %s\n",
               spi_before, spi_after, (spi_after == 0x1f) ? "[UNLOCKED!]" : "[LOCKED]");
    }

    printf("\n=== DONE ===\n");
    CloseHandle(g_hDev);
    return 0;
}
