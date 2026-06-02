#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "amdbc250_ioctl.h"

static HANDLE OpenDevice(void) {
    return CreateFileA("\\\\.\\AMDBC250DreamV43",
        GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
}

static UINT32 MmioRead(HANDLE h, UINT64 pa, UINT32 size, UINT32 off) {
    AMDBC250_IOCTL_MMIO_TEST m = {0};
    m.PhysicalAddress = pa; m.Size = size; m.OffsetRead = off;
    DWORD bytes = 0;
    DeviceIoControl(h, IOCTL_AMDBC250_MMIO_TEST, &m, sizeof(m), &m, sizeof(m), &bytes, NULL);
    return m.MapResult ? m.ValueRead : 0xDEAD0000;
}

static BOOL MmioWrite(HANDLE h, UINT64 pa, UINT32 size, UINT32 off, UINT32 val) {
    AMDBC250_IOCTL_MMIO_TEST m = {0};
    m.PhysicalAddress = pa; m.Size = size;
    m.OffsetWrite = off; m.ValueWrite = val;
    m.OffsetRead = off;
    DWORD bytes = 0;
    DeviceIoControl(h, IOCTL_AMDBC250_MMIO_TEST, &m, sizeof(m), &m, sizeof(m), &bytes, NULL);
    return m.MapResult;
}

static BOOL ReadPciConfig(HANDLE h, UINT32 bus, UINT32 dev, UINT32 func, UCHAR cfg[256]) {
    AMDBC250_IOCTL_READ_PCI_CONFIG r = {0};
    r.Bus = bus; r.Device = dev; r.Function = func;
    DWORD bytes = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_AMDBC250_READ_PCI_CONFIG, &r, sizeof(r), &r, sizeof(r), &bytes, NULL);
    if (ok && r.BytesRead > 0) { memcpy(cfg, r.ConfigData, 256); return TRUE; }
    return FALSE;
}

int main(void) {
    printf("AMD BC-250 BAR5 READ-ONLY Map (SAFE — NO WRITES TO GPU REGS)\n");
    printf("=============================================================\n\n");
    printf("  *** CRITICAL: WRITES TO BAR5 CAUSE HARD FREEZE ***\n");
    printf("  *** This tool only READS — do NOT add write tests ***\n\n");

    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) { printf("ERROR: Cannot open device\n"); return 1; }

    UINT64 bar5 = 0xFE800000;
    printf("  BAR5: 0x%08X\n\n", (UINT32)bar5);

    UCHAR cfg[256];
    ReadPciConfig(h, 1, 0, 0, cfg);

    /* ================================================ */
    /* Part 1: Full BAR5 scan — read-only               */
    /* ================================================ */
    printf("--- Part 1: BAR5 Complete Read-Only Map (256KB) ---\n\n");

    printf("  Non-zero/non-0xFFFFFFFF registers:\n");
    for (UINT32 off = 0; off < 0x40000; off += 4) {
        UINT32 v = MmioRead(h, bar5, 0x1000, off);
        if (v == 0 || v == 0xFFFFFFFF || v == 0xDEAD0000) continue;

        const char* region = "???";
        if (off < 0x100) region = "ID";
        else if (off < 0x200) region = "NBIO";
        else if (off < 0x300) region = "SCR";
        else if (off < 0x400) region = "HDP";
        else if (off < 0x800) region = "GFX";
        else if (off < 0xC00) region = "GC";
        else if (off < 0x1000) region = "??1";
        else if (off < 0x2000) region = "MMHUB";
        else if (off < 0x3000) region = "OSSSYS";
        else if (off < 0x4000) region = "DCN?";
        else if (off < 0x8000) region = "UVD?";
        else if (off < 0xC000) region = "VCN?";
        else if (off < 0x10000) region = "??2";
        else if (off < 0x20000) region = "HIGH1";
        else if (off < 0x30000) region = "HIGH2";
        else region = "HIGH3";

        printf("    +0x%05X: 0x%08X  [%s]\n", off, v, region);
    }

    /* ================================================ */
    /* Part 2: Known register identification            */
    /* ================================================ */
    printf("\n--- Part 2: Known Register Identification ---\n\n");

    struct { UINT32 off; const char* name; } known[] = {
        {0x0000, "GFX_VERSION / GPU_ID"},
        {0x0004, "GPU_ID2 / scratch?"},
        {0x0008, "ROM_CNTL"},
        {0x000C, "CONFIG"},
        {0x0010, "CONFIG2"},
        {0x0014, "HW_REV"},
        {0x0034, "GPU_ID (shadow)"},
        {0x003C, "GPU_ID (shadow)"},
        {0x0054, "CHIP_ID"},
        {0x130,  "NBIO_STRAP_0"},
        {0x138,  "NBIO_STRAP_1"},
        {0x13C,  "NBIO_STRAP_2"},
        {0x144,  "NBIO_STRAP_3"},
        {0x14C,  "NBIO_STRAP_4"},
        {0x150,  "NBIO_STRAP_5"},
        {0x158,  "NBIO_STRAP_6"},
        {0x15C,  "NBIO_STRAP_7"},
        {0x160,  "NBIO_STRAP_8"},
        {0x164,  "NBIO_STRAP_9"},
        {0x200,  "SCRATCH_REG0"},
        {0x204,  "SCRATCH_REG1"},
        {0x208,  "SCRATCH_REG2"},
        {0x20C,  "SCRATCH_REG3"},
        {0x3000, "HDP_MEM_POWER_LS"},
        {0x3004, "HDP_MEM_POWER_CNTL"},
        {0x3008, "HDP_MEM_POWER_STATUS"},
        {0x300C, "HDP_CNTL2"},
        {0x3010, "HDP_CNTL"},
        {0x3014, "HDP_DEBUG"},
        {0x3018, "HDP_DEBUG2"},
        {0x301C, "HDP_SOFT_RESET"},
        {0x400,  "GFX_CONFIG_0"},
        {0x420,  "GFX_CONFIG_1"},
        {0x42C,  "GFX_CONFIG_2"},
        {0x43C,  "GFX_CONFIG_3"},
        {0x440,  "GFX_CONFIG_4"},
        {0x458,  "GFX_CLK_CNTL"},
        {0x45C,  "GFX_CLK_STATUS"},
        {0x474,  "GFX_PWR_CNTL_0"},
        {0x478,  "GFX_PWR_CNTL_1"},
        {0x490,  "GFX_VOLTAGE"},
        {0x4C0,  "GFX_POWER_STATUS"},
        {0x4C4,  "GFX_RESET_CNTL"},
        {0x4CC,  "GFX_FEATURES"},
        {0x4D4,  "GFX_CNTL"},
        {0x4D8,  "GFX_CNTL2"},
        {0x4DC,  "GFX_CNTL3"},
        {0x4EC,  "GFX_CNTL4"},
        {0x508,  "GFX_CLK_DIV"},
        {0x510,  "GFX_FB_SIZE"},
        {0x518,  "GFX_CLK_DIV2"},
        {0x800,  "GC_CONFIG_0"},
        {0xF000, "HIGH_REGION_0"},
    };

    for (int i = 0; i < sizeof(known)/sizeof(known[0]); i++) {
        UINT32 v = MmioRead(h, bar5, 0x40000, known[i].off);
        if (v != 0xDEAD0000) {
            printf("    +0x%04X: 0x%08X  (%s)\n", known[i].off, v, known[i].name);
        }
    }

    /* ================================================ */
    /* Part 3: VRAM read-only exploration               */
    /* ================================================ */
    printf("\n--- Part 3: VRAM Exploration (READ-ONLY) ---\n\n");

    UINT64 bar0 = 0xC0000000;
    printf("  VRAM+0x0000: 0x%08X (hardware reg, read-only)\n", MmioRead(h, bar0, 0x100, 0));

    /* Dump VRAM header region */
    printf("  VRAM header (+0x000 to +0x100):\n");
    for (UINT32 off = 0; off < 0x100; off += 4) {
        UINT32 v = MmioRead(h, bar0, 0x1000, off);
        if (v != 0) {
            printf("    +0x%03X: 0x%08X\n", off, v);
        }
    }

    /* Search for GPU command structures in VRAM */
    printf("\n  VRAM firmware/signature search:\n");
    /* Look for common GPU firmware signatures */
    for (UINT32 off = 0x100; off < 0x10000; off += 0x1000) {
        UINT32 v0 = MmioRead(h, bar0, 0x10000, off);
        UINT32 v4 = MmioRead(h, bar0, 0x10000, off + 4);
        /* Look for "MCAT", "SMC", "PM4", "UVD" signatures */
        if ((v0 & 0xFF) == 'M' && ((v0 >> 8) & 0xFF) == 'C') {
            printf("    VRAM+0x%06X: 0x%08X 0x%08X (MC signature?)\n", off, v0, v4);
        }
        if (v0 == 0x534D4300 || v0 == 0x534D4301) {  /* SMC */
            printf("    VRAM+0x%06X: 0x%08X (SMC firmware header)\n", off, v0);
        }
    }

    /* ================================================ */
    /* Part 4: PCI config dump (full 256 bytes)         */
    /* ================================================ */
    printf("\n--- Part 4: PCI Config Dump ---\n\n");

    ReadPciConfig(h, 1, 0, 0, cfg);
    printf("  GPU B1:D0:F0:\n");
    for (UINT32 off = 0; off < 0x100; off += 4) {
        UINT32 v = *(UINT32*)(cfg + off);
        if (v != 0 && v != 0xFFFFFFFF) {
            printf("    +0x%02X: 0x%08X", off, v);
            switch (off) {
            case 0x00: printf(" (VenID:DevID)"); break;
            case 0x04: printf(" (Cmd:Status)"); break;
            case 0x08: printf(" (Rev:Class)"); break;
            case 0x0C: printf(" (BIST:Header:Latency:CacheLine)"); break;
            case 0x10: printf(" (BAR0 lo)"); break;
            case 0x14: printf(" (BAR0 hi)"); break;
            case 0x18: printf(" (BAR2 lo)"); break;
            case 0x1C: printf(" (BAR2 hi)"); break;
            case 0x20: printf(" (BAR4)"); break;
            case 0x24: printf(" (BAR5)"); break;
            case 0x28: printf(" (CardBus)"); break;
            case 0x2C: printf(" (SubVen:SubDev)"); break;
            case 0x30: printf(" (ExpRom BAR)"); break;
            case 0x34: printf(" (CapPtr)"); break;
            case 0x3C: printf(" (IntLine:IntPin:MinGnt:MaxLat)"); break;
            }
            printf("\n");
        }
    }

    /* Follow capability list */
    printf("\n  PCI Capability List:\n");
    UINT8 capPtr = cfg[0x34];
    int capCount = 0;
    while (capPtr != 0 && capPtr < 0xFC && capCount < 20) {
        UINT8 capId = cfg[capPtr];
        UINT8 capNext = cfg[capPtr + 1];
        printf("    Cap at 0x%02X: ID=0x%02X", capPtr, capId);
        switch (capId) {
        case 0x01: printf(" (Power Mgmt)"); break;
        case 0x02: printf(" (AGP)"); break;
        case 0x03: printf(" (VPD)"); break;
        case 0x05: printf(" (MSI)"); break;
        case 0x09: printf(" (PCIe)"); break;
        case 0x0D: printf(" (Slot Power)"); break;
        case 0x10: printf(" (AER)"); break;
        case 0x11: printf(" (ACS)"); break;
        case 0x12: printf(" (AF)"); break;
        case 0x13: printf(" (FPB)"); break;
        }
        printf("\n");
        capPtr = capNext;
        capCount++;
    }

    printf("\nDone.\n");
    CloseHandle(h);
    return 0;
}
