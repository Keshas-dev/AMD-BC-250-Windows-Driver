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

static UINT32 MmioReadSafe(HANDLE h, UINT64 pa, UINT32 mapSize, UINT32 off) {
    if (off + 4 > mapSize) return 0xDEAD0000;
    return MmioRead(h, pa, mapSize, off);
}

static BOOL MmioWriteSafe(HANDLE h, UINT64 pa, UINT32 mapSize, UINT32 off, UINT32 val) {
    if (off + 4 > mapSize) return FALSE;
    return MmioWrite(h, pa, mapSize, off, val);
}

int main(void) {
    printf("AMD BC-250 VRAM Boundary + GPU Register Deep Probe\n");
    printf("===================================================\n\n");

    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) { printf("ERROR: Cannot open device\n"); return 1; }

    UCHAR cfg[256];
    ReadPciConfig(h, 1, 0, 0, cfg);
    UINT32 bar0 = *(UINT32*)(cfg + 0x10) & 0xFFFFFFF0;
    UINT32 bar5 = *(UINT32*)(cfg + 0x24) & 0xFFFFFFF0;
    printf("  BAR0 (VRAM): 0x%08X\n", bar0);
    printf("  BAR5 (GPU):  0x%08X\n\n", bar5);

    /* ================================================ */
    /* Part 1: VRAM boundary detection                  */
    /* ================================================ */
    printf("--- Part 1: VRAM Boundary Detection ---\n\n");

    /* Test with 1MB mapping, scan offset boundaries */
    printf("  Testing write+read with 1MB mapping:\n");
    UINT32 mapSize = 0x100000;  /* 1MB */
    UINT32 testOffsets[] = {
        0x0000, 0x0004, 0x100, 0x1000, 0x4000, 0x8000, 0xC000,
        0x10000, 0x20000, 0x40000, 0x80000, 0xC0000, 0xF0000, 0xFFFFC
    };
    for (int i = 0; i < sizeof(testOffsets)/sizeof(testOffsets[0]); i++) {
        UINT32 off = testOffsets[i];
        if (off + 4 > mapSize) continue;
        UINT32 testVal = 0xABCD0000 | (off & 0xFFFF);
        MmioWriteSafe(h, bar0, mapSize, off, testVal);
        UINT32 rb = MmioReadSafe(h, bar0, mapSize, off);
        printf("    VRAM+0x%07X: wrote 0x%08X read 0x%08X %s\n",
            off, testVal, rb,
            (rb == testVal) ? "OK" :
            (rb == 0xFF121212) ? "READ-ONLY REG" :
            (rb == 0xDEAD0000) ? "MAP-FAIL" : "MISMATCH");
    }

    /* ================================================ */
    /* Part 2: VRAM+0x0000 deep analysis               */
    /* ================================================ */
    printf("\n--- Part 2: VRAM+0x0000 Deep Analysis ---\n\n");

    /* Read with different mapping sizes to confirm */
    UINT32 mapSizes[] = {0x1000, 0x4000, 0x10000, 0x100000};
    printf("  VRAM+0x0000 with different map sizes:\n");
    for (int i = 0; i < 4; i++) {
        UINT32 v = MmioRead(h, bar0, mapSizes[i], 0);
        printf("    map=0x%06X: 0x%08X\n", mapSizes[i], v);
    }

    /* Dump 64 bytes at VRAM+0x000 */
    printf("\n  VRAM dump +0x000 to +0x040 (fresh read):\n");
    for (UINT32 off = 0; off < 0x40; off += 4) {
        UINT32 v = MmioRead(h, bar0, 0x100, off);
        printf("    +0x%02X: 0x%08X", off, v);
        if (off == 0) printf(" (read-only hardware reg?)");
        printf("\n");
    }

    /* ================================================ */
    /* Part 3: BAR5 GPU register write test (all region) */
    /* ================================================ */
    printf("\n--- Part 3: BAR5 GPU Register Write Test ---\n\n");

    /* Test writing to various BAR5 offsets */
    printf("  Testing write+read across BAR5 (64KB map):\n");
    UINT32 bar5Tests[] = {
        0x0000, 0x0004, 0x0008, 0x000C,
        0x0080, 0x0084, 0x0088, 0x008C,
        0x0100, 0x0200, 0x0400, 0x0800,
        0x1000, 0x2000, 0x3000, 0x3004, 0x3008, 0x3010,
        0x4000, 0x8000, 0xC000,
        0xF000, 0xFFFC
    };
    for (int i = 0; i < sizeof(bar5Tests)/sizeof(bar5Tests[0]); i++) {
        UINT32 off = bar5Tests[i];
        UINT32 orig = MmioRead(h, bar5, 0x10000, off);
        MmioWrite(h, bar5, 0x10000, off, 0xDEADBEEF);
        UINT32 rb = MmioRead(h, bar5, 0x10000, off);
        printf("    BAR5+0x%04X: orig=0x%08X wrote=DEADBEEF read=0x%08X %s\n",
            off, orig, rb,
            (rb == 0xDEADBEEF) ? "WRITE OK!" :
            (rb == orig) ? "read-only" : "changed");
    }

    /* ================================================ */
    /* Part 4: Search for GPU regs in VRAM space       */
    /* ================================================ */
    printf("\n--- Part 4: Search GPU regs in VRAM High Area ---\n\n");

    /* Some AMD GPUs expose registers in the VRAM space above the framebuffer */
    printf("  Scanning VRAM+0x100000 to VRAM+0xF00000 (16MB steps, 1MB map):\n");
    for (UINT32 base = 0x100000; base < 0xF00000; base += 0x100000) {
        UINT32 v0 = MmioRead(h, bar0, 0x100000, base);
        UINT32 v4 = MmioRead(h, bar0, 0x100000, base + 4);
        if (v0 != 0 && v0 != 0xDEAD0000) {
            printf("    VRAM+0x%07X: %08X %08X\n", base, v0, v4);
        }
    }

    /* ================================================ */
    /* Part 5: NBIO indirect access                    */
    /* ================================================ */
    printf("\n--- Part 5: NBIO MMIO Indirect Access ---\n\n");

    /* Ariel Root Complex has indirect MMIO for config access */
    /* Try standard AMD NBIO indirect MMIO pattern */
    UINT64 nbioBase = 0xFED80000; /* HT/SMBus space */

    /* AMD NBIO: register at base+0x00 = DeviceId */
    printf("  NBIO (0xFED80000) device ID: 0x%08X\n", MmioRead(h, nbioBase, 0x100, 0));
    printf("  NBIO (0xFED80000) +0x04: 0x%08X\n", MmioRead(h, nbioBase, 0x100, 4));

    /* Check if PSP has registers accessible via its BAR5 */
    ReadPciConfig(h, 1, 0, 2, cfg);  /* PSP is B1:D0:F2 */
    UINT32 pspBar5 = *(UINT32*)(cfg + 0x24) & 0xFFFFFFF0;
    printf("\n  PSP BAR5: 0x%08X\n", pspBar5);
    printf("  PSP BAR5 full 256-byte dump:\n");
    for (UINT32 off = 0; off < 0x100; off += 4) {
        UINT32 v = MmioRead(h, pspBar5, 0x100, off);
        if (v != 0 && v != 0xFFFFFFFF && v != 0xDEAD0000) {
            printf("    +0x%02X: 0x%08X\n", off, v);
        }
    }

    /* PSP MMIO via memory-mapped registers above PSP BAR5 */
    printf("  PSP extended scan (BAR5+0x100 to BAR5+0x1000):\n");
    for (UINT32 off = 0x100; off < 0x1000; off += 4) {
        UINT32 v = MmioRead(h, pspBar5, 0x2000, off);
        if (v != 0 && v != 0xFFFFFFFF && v != 0xDEAD0000) {
            printf("    +0x%03X: 0x%08X\n", off, v);
        }
    }

    /* ================================================ */
    /* Part 6: Try alternate GPU register bases        */
    /* ================================================ */
    printf("\n--- Part 6: Alternate GPU Register Bases ---\n\n");

    /* In some AMD APUs, GPU regs are mapped via host bridge or NBIO */
    /* Try reading from addresses just above BAR5 */
    printf("  Probing around BAR5 (0xFE7FF000 - 0xFE801000):\n");
    for (UINT32 addr = 0xFE7FF000; addr <= 0xFE801000; addr += 4) {
        UINT32 v = MmioRead(h, addr, 0x100, 0);
        if (v != 0 && v != 0xFFFFFFFF && v != 0xDEAD0000) {
            printf("    0x%08X: 0x%08X\n", addr, v);
        }
    }

    /* Try GPU at common AMD APU register base 0xD0000000 with small map */
    printf("\n  GPU BAR2 (0xD0000000) with small map:\n");
    for (UINT32 off = 0; off < 0x100; off += 4) {
        UINT32 v = MmioRead(h, 0xD0000000, 0x100, off);
        if (v != 0 && v != 0xFFFFFFFF && v != 0xDEAD0000) {
            printf("    +0x%02X: 0x%08X\n", off, v);
        }
    }

    /* ================================================ */
    /* Part 7: PCI Bridge secondary bus MMIO            */
    /* ================================================ */
    printf("\n--- Part 7: PCI Bridge Config ---\n\n");

    ReadPciConfig(h, 0, 0, 1, cfg);  /* Host bridge child PCI bridge B0:D8:F1 */
    printf("  Bridge B0:D8:F1:\n");
    printf("    Cmd: 0x%04X\n", *(UINT16*)(cfg + 0x04));
    printf("    SecBus: %u\n", cfg[0x19]);
    printf("    SubBus: %u\n", cfg[0x1A]);
    printf("    MemBase: 0x%04X MemLimit: 0x%04X\n",
        *(UINT16*)(cfg + 0x20), *(UINT16*)(cfg + 0x22));
    printf("    PrefMemBase: 0x%04X PrefMemLimit: 0x%04X\n",
        *(UINT16*)(cfg + 0x24), *(UINT16*)(cfg + 0x26));
    printf("    PrefMemBaseHi: 0x%08X PrefMemLimitHi: 0x%08X\n",
        *(UINT32*)(cfg + 0x28), *(UINT32*)(cfg + 0x2C));
    printf("    BridgeCtrl: 0x%04X\n", *(UINT16*)(cfg + 0x3E));

    /* Read GPU PCI config from secondary bus */
    ReadPciConfig(h, 1, 0, 0, cfg);
    printf("\n  GPU B1:D0:F0:\n");
    printf("    Cmd: 0x%04X (IO=%d Mem=%d BM=%d)\n",
        *(UINT16*)(cfg + 0x04),
        (*(UINT16*)(cfg + 0x04) & 1),
        (*(UINT16*)(cfg + 0x04) & 2) >> 1,
        (*(UINT16*)(cfg + 0x04) & 4) >> 2);
    printf("    Status: 0x%04X\n", *(UINT16*)(cfg + 0x06));
    printf("    BAR0: 0x%08X (size hint: %s)\n", *(UINT32*)(cfg + 0x10),
        (*(UINT32*)(cfg + 0x10) & 8) ? "64-bit" : "32-bit");
    printf("    BAR2: 0x%08X (size hint: %s)\n", *(UINT32*)(cfg + 0x18),
        (*(UINT32*)(cfg + 0x18) & 8) ? "64-bit" : "32-bit");
    printf("    BAR4: 0x%08X\n", *(UINT32*)(cfg + 0x20));
    printf("    BAR5: 0x%08X\n", *(UINT32*)(cfg + 0x24));
    printf("    IntLine: %d IntPin: %c\n", cfg[0x3C], cfg[0x3D] ? 'A' + cfg[0x3D] - 1 : 'N');

    printf("\nDone.\n");
    CloseHandle(h);
    return 0;
}
