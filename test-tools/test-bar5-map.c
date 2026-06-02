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

static UINT32 MmioWrite(HANDLE h, UINT64 pa, UINT32 size, UINT32 off, UINT32 val) {
    AMDBC250_IOCTL_MMIO_TEST m = {0};
    m.PhysicalAddress = pa; m.Size = size;
    m.OffsetWrite = off; m.ValueWrite = val;
    m.OffsetRead = off;
    DWORD bytes = 0;
    DeviceIoControl(h, IOCTL_AMDBC250_MMIO_TEST, &m, sizeof(m), &m, sizeof(m), &bytes, NULL);
    return m.MapResult ? m.ValueWrittenBack : 0xDEAD0000;
}

int main(void) {
    printf("AMD BC-250 BAR5 Full Register Map\n");
    printf("==================================\n\n");

    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) { printf("ERROR: Cannot open device\n"); return 1; }

    UINT64 bar5 = 0xFE800000;
    printf("  BAR5: 0x%08X\n\n", (UINT32)bar5);

    /* ================================================ */
    /* Full BAR5 scan — every 4 bytes, 256KB            */
    /* ================================================ */
    printf("--- BAR5 Complete Register Map (256KB) ---\n\n");

    printf("  Non-zero/non-0xFFFFFFFF registers:\n");
    printf("  (offset)   (value)     (read-only/writable)\n");

    for (UINT32 off = 0; off < 0x40000; off += 4) {
        UINT32 v = MmioRead(h, bar5, 0x1000, off);
        if (v == 0 || v == 0xFFFFFFFF || v == 0xDEAD0000) continue;

        /* Test writability */
        const char* access = "?";
        if (off == 0x0000 || off == 0x0034 || off == 0x003C || off == 0x0054) {
            access = "RO";  /* Known read-only from previous tests */
        } else {
            /* Quick writability test */
            UINT32 orig = v;
            /* Write the value back (safe — no change) */
            UINT32 wb = MmioWrite(h, bar5, 0x1000, off, orig);
            if (wb == orig) {
                access = "RW";
            } else {
                /* Try writing DEADBEEF to see if it changes */
                wb = MmioWrite(h, bar5, 0x1000, off, 0xDEADBEEF);
                UINT32 rb = MmioRead(h, bar5, 0x1000, off);
                if (rb != orig) {
                    access = "CHG";
                    /* Restore original */
                    MmioWrite(h, bar5, 0x1000, off, orig);
                } else {
                    access = "RO";
                }
            }
        }

        printf("  +0x%05X: 0x%08X  [%s]\n", off, v, access);
    }

    /* ================================================ */
    /* Deep dump of interesting register blocks         */
    /* ================================================ */
    printf("\n--- Detailed Register Blocks ---\n\n");

    /* Block 1: 0x000-0x040 (GPU identification) */
    printf("  Block 1: GPU Identification (0x000-0x040)\n");
    for (UINT32 off = 0x000; off <= 0x040; off += 4) {
        UINT32 v = MmioRead(h, bar5, 0x100, off);
        printf("    +0x%03X: 0x%08X", off, v);
        switch (off) {
        case 0x00: printf(" (GFX_VERSION / GPU_ID)"); break;
        case 0x04: printf(" (GPU_ID2?)"); break;
        case 0x08: printf(" (ROM_CNTL)"); break;
        case 0x0C: printf(" (CONFIG?)"); break;
        case 0x10: printf(" (CONFIG2?)"); break;
        case 0x14: printf(" (HW_REV?)"); break;
        case 0x20: printf(" (CC_GC_SHADER_DISABLE)"); break;
        case 0x34: printf(" (GPU_ID)"); break;
        case 0x3C: printf(" (GPU_ID)"); break;
        }
        printf("\n");
    }

    /* Block 2: 0x100-0x170 (NBIO/strap region) */
    printf("\n  Block 2: NBIO/Strap (0x100-0x170)\n");
    for (UINT32 off = 0x100; off <= 0x170; off += 4) {
        UINT32 v = MmioRead(h, bar5, 0x200, off);
        if (v != 0) {
            printf("    +0x%03X: 0x%08X", off, v);
            if (off == 0x130) printf(" (NBIO strap?)");
            if (off == 0x138) printf(" (strap?)");
            if (off == 0x14C) printf(" (strap?)");
            if (off == 0x150) printf(" (strap?)");
            if (off == 0x158) printf(" (strap?)");
            if (off == 0x15C) printf(" (strap?)");
            if (off == 0x160) printf(" (strap?)");
            if (off == 0x164) printf(" (strap?)");
            printf("\n");
        }
    }

    /* Block 3: 0x200-0x220 (scratch?) */
    printf("\n  Block 3: Scratch/Status (0x200-0x220)\n");
    for (UINT32 off = 0x200; off <= 0x220; off += 4) {
        UINT32 v = MmioRead(h, bar5, 0x400, off);
        printf("    +0x%03X: 0x%08X\n", off, v);
    }

    /* Block 4: 0x300-0x320 (HDP registers) */
    printf("\n  Block 4: HDP Registers (0x300-0x320)\n");
    for (UINT32 off = 0x3000; off <= 0x3020; off += 4) {
        UINT32 v = MmioRead(h, bar5, 0x10000, off);
        printf("    +0x%04X: 0x%08X", off, v);
        switch (off) {
        case 0x3000: printf(" (HDP_MEM_POWER_LS)"); break;
        case 0x3004: printf(" (HDP_MEM_POWER_CNTL)"); break;
        case 0x3008: printf(" (HDP_MEM_POWER_STATUS)"); break;
        case 0x300C: printf(" (HDP_CNTL2)"); break;
        case 0x3010: printf(" (HDP_CNTL)"); break;
        case 0x3014: printf(" (HDP_DEBUG)"); break;
        case 0x3018: printf(" (HDP_DEBUG2)"); break;
        case 0x301C: printf(" (HDP_SOFT_RESET)"); break;
        }
        printf("\n");
    }

    /* Block 5: 0x400-0x600 (GFX config / clock / power) */
    printf("\n  Block 5: GFX Config (0x400-0x600)\n");
    for (UINT32 off = 0x400; off <= 0x600; off += 4) {
        UINT32 v = MmioRead(h, bar5, 0x2000, off);
        if (v != 0) {
            printf("    +0x%03X: 0x%08X\n", off, v);
        }
    }

    /* Block 6: 0x800-0x810 */
    printf("\n  Block 6: Unknown (0x800-0x810)\n");
    for (UINT32 off = 0x800; off <= 0x810; off += 4) {
        UINT32 v = MmioRead(h, bar5, 0x1000, off);
        printf("    +0x%03X: 0x%08X\n", off, v);
    }

    /* Block 7: 0xF000+ (high region) */
    printf("\n  Block 7: High Region (0xF000-0xF040)\n");
    for (UINT32 off = 0xF000; off <= 0xF040; off += 4) {
        UINT32 v = MmioRead(h, bar5, 0x10000, off);
        if (v != 0) {
            printf("    +0x%04X: 0x%08X\n", off, v);
        }
    }

    /* ================================================ */
    /* Scratch register test — write 0-7 to find comm  */
    /* ================================================ */
    printf("\n--- Scratch Register Search ---\n\n");

    /* AMD GPUs use scratch registers (offset 0x200-0x21C on GFX10) for
       software-hardware communication. Write test values to find them. */
    printf("  Writing test patterns to search for scratch regs:\n");
    for (UINT32 off = 0x0000; off < 0x40000; off += 0x100) {
        /* Write a unique pattern based on offset */
        UINT32 pattern = 0x53435248 | (off >> 4);  /* "SCHR" | offset_id */
        MmioWrite(h, bar5, 0x10000, off, pattern);
        UINT32 rb = MmioRead(h, bar5, 0x10000, off);
        if (rb == pattern) {
            printf("    SCRATCH at BAR5+0x%05X! Pattern matched.\n", off);
        }
    }

    /* ================================================ */
    /* GPU power/clock state dump                       */
    /* ================================================ */
    printf("\n--- GPU Power/Clock State ---\n\n");

    /* Read all power/clock related registers */
    printf("  Power/Clock registers at BAR5+0x400-0x500:\n");
    for (UINT32 off = 0x400; off < 0x500; off += 4) {
        UINT32 v = MmioRead(h, bar5, 0x2000, off);
        if (v != 0) {
            printf("    +0x%03X: 0x%08X\n", off, v);
        }
    }

    printf("\nDone.\n");
    CloseHandle(h);
    return 0;
}
