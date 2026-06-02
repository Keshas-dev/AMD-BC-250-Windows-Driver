#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "amdbc250_ioctl.h"

static HANDLE OpenDevice(void) {
    return CreateFileA("\\\\.\\AMDBC250DreamV43",
        GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
}

static BOOL ReadPciConfig(HANDLE h, UINT32 bus, UINT32 dev, UINT32 func, UCHAR cfg[256]) {
    AMDBC250_IOCTL_READ_PCI_CONFIG r = {0};
    r.Bus = bus; r.Device = dev; r.Function = func;
    DWORD bytes = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_AMDBC250_READ_PCI_CONFIG, &r, sizeof(r), &r, sizeof(r), &bytes, NULL);
    if (ok && r.BytesRead > 0) { memcpy(cfg, r.ConfigData, 256); return TRUE; }
    return FALSE;
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

int main(void) {
    printf("AMD BC-250 Deep Probe\n");
    printf("=====================\n\n");

    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) { printf("ERROR: Cannot open device\n"); return 1; }

    UCHAR cfg[256];

    /* BAR addresses */
    ReadPciConfig(h, 1, 0, 0, cfg);
    UINT32 bar0 = *(UINT32*)(cfg + 0x10) & 0xFFFFFFF0;
    UINT32 bar5 = *(UINT32*)(cfg + 0x24) & 0xFFFFFFF0;
    printf("  BAR0 (VRAM): 0x%08X\n", bar0);
    printf("  BAR5 (GPU):  0x%08X\n\n", bar5);

    /* ================================================ */
    /* Part 1: VRAM offset 0 mystery — 0xFF121212      */
    /* ================================================ */
    printf("--- Part 1: VRAM+0x0000 Mystery (0xFF121212) ---\n\n");

    /* Read current value */
    printf("  VRAM+0x0000 current: 0x%08X\n", MmioRead(h, bar0, 0x10000, 0));

    /* Try writing different values and read back */
    UINT32 testVals[] = {0x00000000, 0xFFFFFFFF, 0x12345678, 0xDEADBEEF, 0x00000001};
    for (int i = 0; i < 5; i++) {
        MmioWrite(h, bar0, 0x10000, 0, testVals[i]);
        UINT32 rb = MmioRead(h, bar0, 0x10000, 0);
        printf("  Write 0x%08X -> read 0x%08X\n", testVals[i], rb);
    }

    /* Read nearby offsets to see the picture */
    printf("\n  VRAM dump +0x000 to +0x040:\n");
    for (UINT32 off = 0; off < 0x40; off += 4) {
        UINT32 v = MmioRead(h, bar0, 0x10000, off);
        printf("    +0x%02X: 0x%08X", off, v);
        if (off == 0) printf(" <-- mystery");
        printf("\n");
    }

    /* Test: does VRAM+0x000 behave differently with different map sizes? */
    printf("\n  Different map sizes at VRAM+0x000:\n");
    UINT32 sizes[] = {0x1000, 0x10000, 0x100000, 0x1000000};
    for (int i = 0; i < 4; i++) {
        UINT32 v = MmioRead(h, bar0, sizes[i], 0);
        printf("    map=0x%X -> read 0x%08X\n", sizes[i], v);
    }

    /* ================================================ */
    /* Part 2: BAR5 full 256-byte register dump        */
    /* ================================================ */
    printf("\n--- Part 2: BAR5 Full 256-byte Dump ---\n\n");

    printf("  BAR5+0x00 to BAR5+0xFF:\n");
    for (UINT32 off = 0; off < 0x100; off += 4) {
        UINT32 v = MmioRead(h, bar5, 0x100, off);
        if (v != 0 || off < 0x10) {
            printf("    +0x%02X: 0x%08X", off, v);
            /* Known AMD GPU register offsets */
            switch (off) {
            case 0x00: printf(" (GPU_ID/GFX_VERSION)"); break;
            case 0x04: printf(" (GPU_ID2)"); break;
            case 0x08: printf(" (ROM_CNTL)"); break;
            case 0x0C: printf(" (CONFIG)"); break;
            case 0x10: printf(" (CONFIG2)"); break;
            case 0x14: printf(" (HW_REV)"); break;
            case 0x34: printf(" (GPU_ID?)"); break;
            case 0x3C: printf(" (GPU_ID?)"); break;
            case 0x54: printf(" (CHIP_ID?)"); break;
            case 0x80: printf(" (SCRATCH_REG0)"); break;
            case 0x84: printf(" (SCRATCH_REG1)"); break;
            case 0x88: printf(" (SCRATCH_REG2)"); break;
            case 0x8C: printf(" (SCRATCH_REG3)"); break;
            case 0x90: printf (" (SCRATCH_REG4)"); break;
            }
            printf("\n");
        }
    }

    /* ================================================ */
    /* Part 3: BAR5 scratch register write test        */
    /* ================================================ */
    printf("\n--- Part 3: BAR5 Scratch Register Write Test ---\n\n");

    /* AMD GPUs typically have scratch registers at 0x80-0x9C */
    printf("  Testing BAR5 scratch registers (0x80-0x9C):\n");
    for (UINT32 off = 0x80; off <= 0x9C; off += 4) {
        UINT32 orig = MmioRead(h, bar5, 0x100, off);
        /* Try writing a test pattern */
        MmioWrite(h, bar5, 0x100, off, 0xDEADBEEF);
        UINT32 rb = MmioRead(h, bar5, 0x100, off);
        printf("    +0x%02X: orig=0x%08X wrote=0xDEADBEEF read=0x%08X %s\n",
            off, orig, rb, (rb == 0xDEADBEEF) ? "WRITES WORK!" : "write failed");
    }

    /* Also test GPU_ID at offset 0 — is it read-only? */
    printf("\n  GPU_ID at +0x00 read-only test:\n");
    UINT32 gpuId = MmioRead(h, bar5, 0x100, 0);
    MmioWrite(h, bar5, 0x100, 0, 0x12345678);
    UINT32 gpuIdAfter = MmioRead(h, bar5, 0x100, 0);
    printf("    Before: 0x%08X, After write: 0x%08X (unchanged=%s)\n",
        gpuId, gpuIdAfter, (gpuId == gpuIdAfter) ? "yes" : "NO!");

    /* ================================================ */
    /* Part 4: Extended BAR5 scan (beyond 128KB)       */
    /* ================================================ */
    printf("\n--- Part 4: Extended BAR5 Scan ---\n\n");

    printf("  Probing BAR5 in 128KB blocks up to 1MB:\n");
    for (UINT64 blockOff = 0x20000; blockOff < 0x100000; blockOff += 0x20000) {
        UINT32 v0 = MmioRead(h, bar5, 0x100, (UINT32)blockOff);
        if (v0 != 0 && v0 != 0xFFFFFFFF && v0 != 0xDEAD0000) {
            printf("    +0x%05X: 0x%08X\n", (UINT32)blockOff, v0);
            /* Dump first 16 dwords of this block */
            for (UINT32 off = 0; off < 0x40; off += 4) {
                UINT32 v = MmioRead(h, bar5, 0x100, (UINT32)(blockOff + off));
                if (v != 0) {
                    printf("      +0x%05X: 0x%08X\n", (UINT32)(blockOff + off), v);
                }
            }
        }
    }

    /* ================================================ */
    /* Part 5: VRAM deeper write/read at various sizes */
    /* ================================================ */
    printf("\n--- Part 5: VRAM Write/Read at Various Offsets ---\n\n");

    /* Test offsets 0x100, 0x1000, 0x10000, 0x100000 */
    UINT32 offsets[] = {0x100, 0x1000, 0x4000, 0x10000, 0x100000, 0x1000000};
    for (int i = 0; i < 6; i++) {
        MmioWrite(h, bar0, 0x10000, offsets[i], 0xBEEF0000 | offsets[i]);
        UINT32 rb = MmioRead(h, bar0, 0x10000, offsets[i]);
        printf("  VRAM+0x%07X: wrote 0x%08X read 0x%08X %s\n",
            offsets[i], 0xBEEF0000 | offsets[i], rb,
            (rb == (0xBEEF0000 | offsets[i])) ? "OK" : "FAIL");
    }

    /* ================================================ */
    /* Part 6: SMBus deep dump (0xFED80000)            */
    /* ================================================ */
    printf("\n--- Part 6: SMBus/HT Registers (0xFED80000) ---\n\n");

    UINT64 smbus = 0xFED80000;
    printf("  SMBus+0x000 to +0x0FF:\n");
    for (UINT32 off = 0; off < 0x100; off += 4) {
        UINT32 v = MmioRead(h, smbus, 0x200, off);
        if (v != 0) {
            printf("    +0x%02X: 0x%08X", off, v);
            if (off == 0x00) printf(" (VendorId/DeviceId)");
            if (off == 0x04) printf(" (PCI Command?)");
            if (off == 0x08) printf (".");
            if (off == 0x0C) printf(" (Revision)");
            if (off == 0x10) printf(" (BAR0?)");
            if (off == 0x14) printf(" (BAR1?)");
            printf("\n");
        }
    }

    /* ================================================ */
    /* Part 7: GPU BAR4 (0x0000E000, 4KB)              */
    /* ================================================ */
    printf("\n--- Part 7: GPU BAR4 (0x%08X, 4KB) ---\n\n", 0x0000E000 & 0xFFFFFFF0);

    UINT32 bar4 = *(UINT32*)(cfg + 0x20) & 0xFFFFFFF0;
    printf("  BAR4: 0x%08X (raw=0x%08X)\n", bar4, *(UINT32*)(cfg + 0x20));

    if (bar4 != 0 && bar4 != 0xFFFFFFF0) {
        printf("  BAR4 dump +0x00 to +0x100:\n");
        for (UINT32 off = 0; off < 0x100; off += 4) {
            UINT32 v = MmioRead(h, bar4, 0x100, off);
            if (v != 0 && v != 0xFFFFFFFF && v != 0xDEAD0000) {
                printf("    +0x%02X: 0x%08X\n", off, v);
            }
        }
    }

    /* ================================================ */
    /* Part 8: Host Bridge (B0:D0:F0) MMIO             */
    /* ================================================ */
    printf("\n--- Part 8: Host Bridge MMIO (0xFED80000 area) ---\n\n");

    ReadPciConfig(h, 0, 0, 0, cfg);
    UINT32 hbBar0 = *(UINT32*)(cfg + 0x10);
    UINT32 hbBar1 = *(UINT32*)(cfg + 0x14);
    printf("  Host Bridge BAR0: 0x%08X\n", hbBar0);
    printf("  Host Bridge BAR1: 0x%08X\n", hbBar1);

    /* Check host bridge command register */
    UINT16 cmd = *(UINT16*)(cfg + 0x04);
    printf("  Host Bridge Cmd: 0x%04X (IO=%d Mem=%d BusMaster=%d)\n",
        cmd, (cmd & 1), (cmd & 2) >> 1, (cmd & 4) >> 2);

    /* ================================================ */
    /* Part 9: GPU BAR0 size detection                 */
    /* ================================================ */
    printf("\n--- Part 9: VRAM Size Detection ---\n\n");

    /* Write known pattern at various large offsets to find VRAM limit */
    UINT32 vrtestOffsets[] = {
        0x10000000,  /* 256MB */
        0x20000000,  /* 512MB */
        0x40000000,  /* 1GB */
        0x80000000,  /* 2GB */
        0xC0000000,  /* 3GB */
    };
    for (int i = 0; i < 5; i++) {
        if (vrtestOffsets[i] < 0x100000000) {
            MmioWrite(h, bar0, 0x10000, vrtestOffsets[i], 0xFACE0000 | i);
            UINT32 rb = MmioRead(h, bar0, 0x10000, vrtestOffsets[i]);
            printf("  VRAM+0x%08X: wrote 0x%08X read 0x%08X %s\n",
                vrtestOffsets[i], 0xFACE0000 | i, rb,
                (rb == (0xFACE0000 | i)) ? "OK" : "FAIL");
        }
    }

    printf("\nDone.\n");
    CloseHandle(h);
    return 0;
}
