#include <windows.h>
#include <stdio.h>
#include "amdbc250_ioctl.h"

static FILE *g = NULL;
static HANDLE hDev;

static void Log(const char *fmt, ...) {
    va_list a; va_start(a, fmt);
    vfprintf(stdout, fmt, a); va_end(a); fflush(stdout);
    if (g) { va_start(a, fmt); vfprintf(g, fmt, a); va_end(a); fflush(g); }
}

static UINT32 Rd(UINT64 pa) {
    AMDBC250_IOCTL_MMIO_TEST m = {0};
    m.PhysicalAddress = pa; m.Size = 4; m.OffsetRead = 0;
    DWORD b = 0;
    DeviceIoControl(hDev, IOCTL_AMDBC250_MMIO_TEST, &m, sizeof(m), &m, sizeof(m), &b, NULL);
    return m.MapResult ? m.ValueRead : 0xDEAD0000;
}

static BOOL ReadPci(UINT32 bus, UINT32 dev, UINT32 func, UINT32 off, UINT32 *val) {
    AMDBC250_IOCTL_READ_PCI_CONFIG r = {0};
    r.Bus = bus; r.Device = dev; r.Function = func;
    DWORD b = 0;
    BOOL ok = DeviceIoControl(hDev, IOCTL_AMDBC250_READ_PCI_CONFIG, &r, sizeof(r), &r, sizeof(r), &b, NULL);
    if (ok && r.BytesRead > off + 3) { *val = *(UINT32*)(r.ConfigData + off); return TRUE; }
    return FALSE;
}

int main(void) {
    g = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\probe-v5.log", "w");
    Log("=== PHASE 5: NBIO + MMIO Deep Probe ===\n\n");

    hDev = CreateFileA("\\\\.\\AMDBC250DreamV43",
        GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hDev == INVALID_HANDLE_VALUE) {
        Log("FATAL: driver open failed %lu\n", GetLastError());
        fclose(g); return 1;
    }
    Log("Driver opened\n\n");

    /* ============================================== */
    /* S1: Test NBIO 0x15FE7000 (from NBIO+0x50)     */
    /* ============================================== */
    Log("=== S1: NBIO MMIO base 0x15FE7000 ===\n");
    for (UINT64 off = 0; off < 0x1000; off += 4) {
        UINT32 v = Rd(0x15FE7000 + off);
        if (v != 0 && v != 0xDEAD0000 && v != 0xFFFFFFFF) {
            Log("  +0x%04X: 0x%08X\n", (UINT32)off, v);
        }
    }
    Log("S1 DONE\n\n");

    /* ============================================== */
    /* S2: NBIO+0x400-0x700 detailed                  */
    /* ============================================== */
    Log("=== S2: NBIO 0xFEB00000+0x400-0x700 detailed ===\n");
    for (UINT64 off = 0x400; off < 0x780; off += 4) {
        UINT32 v = Rd(0xFEB00000 + off);
        if (v != 0 && v != 0xDEAD0000) {
            Log("  +0x%04X: 0x%08X\n", (UINT32)off, v);
        }
    }
    Log("S2 DONE\n\n");

    /* ============================================== */
    /* S3: NBIO+0x700-0xFFF                           */
    /* ============================================== */
    Log("=== S3: NBIO 0xFEB00000+0x700-0xFFF ===\n");
    for (UINT64 off = 0x700; off < 0x1000; off += 4) {
        UINT32 v = Rd(0xFEB00000 + off);
        if (v != 0 && v != 0xDEAD0000) {
            Log("  +0x%04X: 0x%08X\n", (UINT32)off, v);
        }
    }
    Log("S3 DONE\n\n");

    /* ============================================== */
    /* S4: Test addresses found in NBIO+0x50 region   */
    /*   +0x50 = 0x15FE7000 (tested in S1)           */
    /*   +0x504 = 0xE0010000 (I/O?)                  */
    /*   +0x630 = 0x2EFFD000 (DMA?)                  */
    /*   +0x638 = 0x15F320E0 (?)                     */
    /*   +0x640 = DMA ring 2                          */
    /*   +0x650 = 0x2EFFD200                          */
    /*   +0x658 = 0x15FC5FB0                          */
    /* ============================================== */
    Log("=== S4: Test NBIO-referenced addresses ===\n");
    
    UINT64 test_addrs[] = {
        0x2EFFD000,  /* DMA ring 0 */
        0x2EFFD200,  /* DMA ring 1 */
        0x2EFFD400,  /* DMA ring 2 */
        0x2EFFD600,  /* DMA ring 3 */
        0x2EFFD800,  /* DMA ring 4 */
        0x2EFFDA00,  /* DMA ring 5 */
        0x2EFFDC00,  /* DMA ring 6 */
        0x2EFFDE00,  /* DMA ring 7 */
        0x15F320E0,  /* NBIO+0x638 */
        0x15FC5FB0,  /* NBIO+0x658 */
        0x15FC8591,  /* NBIO+0x678 */
        0x15FCCCB1,  /* NBIO+0x698 */
        0x15FCF111,  /* NBIO+0x6B8 */
        0x15FD0C70,  /* NBIO+0x6D8 */
        0x15FD2170,  /* NBIO+0x6F8 */
        0x15FD4370,  /* NBIO+0x718 */
    };
    const char *test_names[] = {
        "DMA ring 0", "DMA ring 1", "DMA ring 2", "DMA ring 3",
        "DMA ring 4", "DMA ring 5", "DMA ring 6", "DMA ring 7",
        "NBIO+0x638", "NBIO+0x658", "NBIO+0x678", "NBIO+0x698",
        "NBIO+0x6B8", "NBIO+0x6D8", "NBIO+0x6F8", "NBIO+0x718",
    };

    for (int i = 0; i < 16; i++) {
        UINT32 v0 = Rd(test_addrs[i]);
        UINT32 v4 = Rd(test_addrs[i] + 4);
        UINT32 v8 = Rd(test_addrs[i] + 8);
        UINT32 vc = Rd(test_addrs[i] + 0xC);
        Log("  0x%08X: %08X %08X %08X %08X  (%s)\n",
            (UINT32)test_addrs[i], v0, v4, v8, vc, test_names[i]);
    }
    Log("S4 DONE\n\n");

    /* ============================================== */
    /* S5: USB controller MMIO (NBIO+0x514 = 0x20425355) */
    /* ============================================== */
    Log("=== S5: USB controller regions ===\n");
    /* "USB" = 0x20425355, these are USB MMIO bases */
    UINT64 usb_addrs[] = { 0x20425355 };  /* This is the ASCII, not an address! */
    /* Actually the value 0x20425355 at +0x514 is an identifier, not an address.
       Let me scan around those NBIO slots more carefully */
    
    /* NBIO +0x500 has PCIe slot config. Scan more of those */
    for (UINT64 off = 0x500; off < 0x600; off += 4) {
        UINT32 v = Rd(0xFEB00000 + off);
        if (v != 0 && v != 0xDEAD0000) {
            Log("  NBIO+0x%03X: 0x%08X\n", (UINT32)off, v);
        }
    }
    Log("S5 DONE\n\n");

    /* ============================================== */
    /* S6: Scan around 0x15FE7000 ± 0x10000           */
    /* ============================================== */
    Log("=== S6: 0x15FD0000-0x15FF0000 page scan ===\n");
    for (UINT64 pa = 0x15FD0000; pa < 0x15FF0000; pa += 0x1000) {
        UINT32 v = Rd(pa);
        if (v != 0 && v != 0xDEAD0000 && v != 0xFFFFFFFF) {
            Log("  PA 0x%08X: 0x%08X\n", (UINT32)pa, v);
        }
    }
    Log("S6 DONE\n\n");

    /* ============================================== */
    /* S7: Scan GPU BAR5 +0x1000-0x1FFF               */
    /* ============================================== */
    Log("=== S7: GPU BAR5 +0x1000-0x1FFF ===\n");
    for (UINT64 off = 0x1000; off < 0x2000; off += 4) {
        UINT32 v = Rd(0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000 && v != 0xFFFFFFFF) {
            Log("  +0x%04X: 0x%08X\n", (UINT32)off, v);
        }
    }
    Log("S7 DONE\n\n");

    /* ============================================== */
    /* S8: GPU BAR5 +0x2000-0x2FFF                    */
    /* ============================================== */
    Log("=== S8: GPU BAR5 +0x2000-0x2FFF ===\n");
    for (UINT64 off = 0x2000; off < 0x3000; off += 4) {
        UINT32 v = Rd(0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000 && v != 0xFFFFFFFF) {
            Log("  +0x%04X: 0x%08X\n", (UINT32)off, v);
        }
    }
    Log("S8 DONE\n\n");

    /* ============================================== */
    /* S9: GPU BAR5 +0x3000-0x3FFF                    */
    /* ============================================== */
    Log("=== S9: GPU BAR5 +0x3000-0x3FFF ===\n");
    for (UINT64 off = 0x3000; off < 0x4000; off += 4) {
        UINT32 v = Rd(0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000 && v != 0xFFFFFFFF) {
            Log("  +0x%04X: 0x%08X\n", (UINT32)off, v);
        }
    }
    Log("S9 DONE\n\n");

    /* ============================================== */
    /* S10: GPU BAR5 +0x4000-0x5FFF                   */
    /* ============================================== */
    Log("=== S10: GPU BAR5 +0x4000-0x5FFF ===\n");
    for (UINT64 off = 0x4000; off < 0x6000; off += 4) {
        UINT32 v = Rd(0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000 && v != 0xFFFFFFFF) {
            Log("  +0x%04X: 0x%08X\n", (UINT32)off, v);
        }
    }
    Log("S10 DONE\n\n");

    /* ============================================== */
    /* S11: GPU BAR5 +0x6000-0x7FFF                   */
    /* ============================================== */
    Log("=== S11: GPU BAR5 +0x6000-0x7FFF ===\n");
    for (UINT64 off = 0x6000; off < 0x8000; off += 4) {
        UINT32 v = Rd(0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000 && v != 0xFFFFFFFF) {
            Log("  +0x%04X: 0x%08X\n", (UINT32)off, v);
        }
    }
    Log("S11 DONE\n\n");

    /* ============================================== */
    /* S12: GPU BAR5 +0x8000-0x9FFF                   */
    /* ============================================== */
    Log("=== S12: GPU BAR5 +0x8000-0x9FFF ===\n");
    for (UINT64 off = 0x8000; off < 0xA000; off += 4) {
        UINT32 v = Rd(0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000 && v != 0xFFFFFFFF) {
            Log("  +0x%04X: 0x%08X\n", (UINT32)off, v);
        }
    }
    Log("S12 DONE\n\n");

    /* ============================================== */
    /* S13: GPU BAR5 +0xA000-0xBFFF                   */
    /* ============================================== */
    Log("=== S13: GPU BAR5 +0xA000-0xBFFF ===\n");
    for (UINT64 off = 0xA000; off < 0xC000; off += 4) {
        UINT32 v = Rd(0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000 && v != 0xFFFFFFFF) {
            Log("  +0x%04X: 0x%08X\n", (UINT32)off, v);
        }
    }
    Log("S13 DONE\n\n");

    /* ============================================== */
    /* S14: GPU BAR5 +0xC000-0xDFFF                   */
    /* ============================================== */
    Log("=== S14: GPU BAR5 +0xC000-0xDFFF ===\n");
    for (UINT64 off = 0xC000; off < 0xE000; off += 4) {
        UINT32 v = Rd(0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000 && v != 0xFFFFFFFF) {
            Log("  +0x%04X: 0x%08X\n", (UINT32)off, v);
        }
    }
    Log("S14 DONE\n\n");

    /* ============================================== */
    /* S15: GPU BAR5 +0xE000-0xFFFF                   */
    /* ============================================== */
    Log("=== S15: GPU BAR5 +0xE000-0xFFFF ===\n");
    for (UINT64 off = 0xE000; off < 0x10000; off += 4) {
        UINT32 v = Rd(0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000 && v != 0xFFFFFFFF) {
            Log("  +0x%04X: 0x%08X\n", (UINT32)off, v);
        }
    }
    Log("S15 DONE\n\n");

    /* ============================================== */
    /* S16: VRAM header area 0xC0000000               */
    /* ============================================== */
    Log("=== S16: VRAM 0xC0000000 header ===\n");
    for (UINT64 off = 0; off < 0x100; off += 4) {
        UINT32 v = Rd(0xC0000000 + off);
        Log("  +0x%04X: 0x%08X\n", (UINT32)off, v);
    }
    Log("S16 DONE\n\n");

    /* ============================================== */
    /* S17: GPU BAR5 +0x10000-0x1FFFF                 */
    /* ============================================== */
    Log("=== S17: GPU BAR5 +0x10000-0x1FFFF (4KB pages) ===\n");
    for (UINT64 off = 0x10000; off < 0x20000; off += 0x1000) {
        UINT32 v = Rd(0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000 && v != 0xFFFFFFFF) {
            Log("  +0x%05X: 0x%08X\n", (UINT32)off, v);
            /* Read first 64 DWORDs of this page if non-zero */
            for (UINT64 s = 4; s < 0x100; s += 4) {
                UINT32 vs = Rd(0xFE800000 + off + s);
                if (vs != 0 && vs != 0xFFFFFFFF) {
                    Log("    +0x%05X: 0x%08X\n", (UINT32)(off + s), vs);
                }
            }
        }
    }
    Log("S17 DONE\n\n");

    /* ============================================== */
    /* S18: GPU BAR5 +0x20000-0x3FFFF                 */
    /* ============================================== */
    Log("=== S18: GPU BAR5 +0x20000-0x3FFFF (16KB pages) ===\n");
    for (UINT64 off = 0x20000; off < 0x40000; off += 0x1000) {
        UINT32 v = Rd(0xFE800000 + off);
        if (v != 0 && v != 0xDEAD0000 && v != 0xFFFFFFFF) {
            Log("  +0x%05X: 0x%08X\n", (UINT32)off, v);
            for (UINT64 s = 4; s < 0x100; s += 4) {
                UINT32 vs = Rd(0xFE800000 + off + s);
                if (vs != 0 && vs != 0xFFFFFFFF) {
                    Log("    +0x%05X: 0x%08X\n", (UINT32)(off + s), vs);
                }
            }
        }
    }
    Log("S18 DONE\n\n");

    /* ============================================== */
    /* S19: PSP BAR5 0xFE884000                       */
    /* ============================================== */
    Log("=== S19: PSP BAR5 0xFE884000 ===\n");
    for (UINT64 off = 0; off < 0x1000; off += 4) {
        UINT32 v = Rd(0xFE884000 + off);
        if (v != 0 && v != 0xDEAD0000 && v != 0xFFFFFFFF) {
            Log("  +0x%04X: 0x%08X\n", (UINT32)off, v);
        }
    }
    Log("S19 DONE\n\n");

    CloseHandle(hDev);
    Log("=== ALL DONE ===\n");
    fclose(g);
    printf("Done. Check probe-v5.log\n");
    return 0;
}
