#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "amdbc250_ioctl.h"
#define MIN(a,b) ((a)<(b)?(a):(b))

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

static BOOL WritePciDword(HANDLE h, UINT32 bus, UINT32 dev, UINT32 func, UINT32 off, UINT32 val) {
    AMDBC250_IOCTL_WRITE_PCI_CONFIG w = {0};
    w.Bus = bus; w.Device = dev; w.Function = func; w.Offset = off; w.Value = val;
    DWORD bytes = 0;
    return DeviceIoControl(h, IOCTL_AMDBC250_WRITE_PCI_CONFIG, &w, sizeof(w), &w, sizeof(w), &bytes, NULL);
}

static UINT32 MmioRead(HANDLE h, UINT64 pa, UINT32 size, UINT32 off) {
    AMDBC250_IOCTL_MMIO_TEST m = {0};
    m.PhysicalAddress = pa; m.Size = size; m.OffsetRead = off;
    DWORD bytes = 0;
    DeviceIoControl(h, IOCTL_AMDBC250_MMIO_TEST, &m, sizeof(m), &m, sizeof(m), &bytes, NULL);
    return m.MapResult ? m.ValueRead : 0xDEAD0000;
}

static BOOL MmioWriteRead(HANDLE h, UINT64 pa, UINT32 size, UINT32 off, UINT32 wval, UINT32 *rval) {
    AMDBC250_IOCTL_MMIO_TEST m = {0};
    m.PhysicalAddress = pa; m.Size = size;
    m.OffsetWrite = off; m.ValueWrite = wval;
    m.OffsetRead = off;
    DWORD bytes = 0;
    DeviceIoControl(h, IOCTL_AMDBC250_MMIO_TEST, &m, sizeof(m), &m, sizeof(m), &bytes, NULL);
    if (rval) *rval = m.ValueWrittenBack;
    return m.MapResult;
}

static const char* IdentifyHdpReg(UINT32 off) {
    switch (off) {
    case 0x00000: return "GPU_ID";
    case 0x00004: return "STATUS";
    case 0x00008: return "ROM_CNTL";
    case 0x0000C: return "CONFIG";
    case 0x00010: return "CONFIG2";
    case 0x0504: return "HDP_MISC_CNTL";
    case 0x0508: return "HDP_MEM_POWER_LS";
    case 0x050C: return "HDP_MEM_POWER_CNTL";
    case 0x0510: return "HDP_MEM_POWER_STATUS";
    case 0x0518: return "HDP_DEBUG";
    case 0x051C: return "HDP_SOFT_RESET";
    case 0x0520: return "HDP_CNTL";
    case 0x0528: return "HDP_XDP_RING_CNTL";
    case 0x052C: return "HDP_XDP_CHICKEN";
    case 0x1000: return "HDP_APT_CNTL";
    case 0x1004: return "HDP_MMIO_DISABLE";
    case 0x1008: return "HDP_NONSURF_BASE";
    case 0x100C: return "HDP_NONSURF_BASE_HIGH";
    case 0x1010: return "HDP_NONSURF_PITCH";
    case 0x1014: return "HDP_NONSURF_INFO";
    case 0x1018: return "HDP_NONSURF_FLAGS";
    default: return NULL;
    }
}

int main(void) {
    printf("AMD BC-250 Shared Memory + GPU Register Probe\n");
    printf("================================================\n\n");

    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) { printf("ERROR: Cannot open device\n"); return 1; }

    UCHAR cfg[256];

    /* ================================================ */
    /* Phase 1: Shared memory write/read test           */
    /* ================================================ */
    printf("--- Phase 1: Shared Memory (GDDR6) Test ---\n\n");

    ReadPciConfig(h, 1, 0, 0, cfg);
    UINT32 bar0 = *(UINT32*)(cfg + 0x10) & 0xFFFFFFF0;
    printf("  VRAM base: 0x%08X (shared CPU/GPU memory)\n", bar0);

    /* Test write/read at various VRAM offsets */
    printf("  Testing VRAM write+read...\n");
    UINT32 testVals[] = {0x12345678, 0xDEADBEEF, 0xCAFEBABE, 0x00000000, 0xFFFFFFFF};
    for (int i = 0; i < 5; i++) {
        UINT32 rb;
        BOOL ok = MmioWriteRead(h, bar0, 0x10000, i * 4, testVals[i], &rb);
        printf("    [%04X] wrote 0x%08X -> read 0x%08X %s\n",
            i * 4, testVals[i], rb, (rb == testVals[i]) ? "OK" : "MISMATCH");
    }

    /* Write a known pattern and verify */
    printf("\n  Writing test pattern (64 bytes)...\n");
    for (UINT32 off = 0; off < 64; off += 4) {
        MmioWriteRead(h, bar0, 0x10000, off, 0xAA000000 | off, NULL);
    }
    printf("  Reading back...\n");
    BOOL allMatch = TRUE;
    for (UINT32 off = 0; off < 64; off += 4) {
        UINT32 expected = 0xAA000000 | off;
        UINT32 actual = MmioRead(h, bar0, 0x10000, off);
        if (actual != expected) {
            printf("    [%04X] expected 0x%08X got 0x%08X MISMATCH\n", off, expected, actual);
            allMatch = FALSE;
        }
    }
    printf("  VRAM write/read: %s\n\n", allMatch ? "ALL MATCH!" : "SOME MISMATCHES");

    /* ================================================ */
    /* Phase 2: GPU register access via BAR5            */
    /* ================================================ */
    printf("--- Phase 2: GPU Registers via BAR5 (0xFE800000) ---\n\n");

    ReadPciConfig(h, 1, 0, 0, cfg);
    UINT32 bar5 = *(UINT32*)(cfg + 0x24) & 0xFFFFFFF0;
    printf("  BAR5 base: 0x%08X\n", bar5);

    /* First: probe known offset to confirm BAR5 responds */
    printf("  Probing BAR5+0x0000 (expect GPU_ID): 0x%08X\n", MmioRead(h, bar5, 0x100, 0));
    printf("  Probing BAR5+0x2000 (SRBM region):  0x%08X\n", MmioRead(h, bar5, 0x100, 0x2000));
    printf("  Probing BAR5+0x3000 (HDP region):   0x%08X\n", MmioRead(h, bar5, 0x100, 0x3000));
    printf("  Probing BAR5+0x4000 (GFX region):   0x%08X\n", MmioRead(h, bar5, 0x100, 0x4000));
    printf("  Probing BAR5+0x8000 (DCN region):   0x%08X\n", MmioRead(h, bar5, 0x100, 0x8000));
    printf("  Probing BAR5+0xC000 (MMHUB region): 0x%08X\n", MmioRead(h, bar5, 0x100, 0xC000));
    printf("  Probing BAR5+0x10000 (ROM region):  0x%08X\n", MmioRead(h, bar5, 0x100, 0x10000));

    /* Scan BAR5 in 64KB chunks to find active regions */
    printf("\n  BAR5 region scan (128KB, 64KB pages):\n");
    UINT32 lastNonZero = 0;
    int consecutiveDead = 0;
    for (UINT32 off = 0; off < 0x20000; off += 0x10000) {
        UINT32 v0 = MmioRead(h, bar5, 0x100, off);
        if (v0 == 0xDEAD0000 || v0 == 0xFFFFFFFF) {
            consecutiveDead++;
            if (consecutiveDead > 2) {
                printf("    +0x%05X-0x%05X: 0xFFFFFFFF (skip)\n", off, off + 0xFFFF);
                continue;
            }
        } else {
            consecutiveDead = 0;
        }
        UINT32 v4 = MmioRead(h, bar5, 0x100, off + 4);
        UINT32 v8 = MmioRead(h, bar5, 0x100, off + 8);
        if (v0 != 0 || v4 != 0 || v8 != 0) {
            if (v0 != 0xFFFFFFFF && v0 != 0xDEAD0000) {
                printf("    +0x%05X: %08X %08X %08X\n", off, v0, v4, v8);
                lastNonZero = off;
            }
        }
    }

    /* Deep scan of active regions only */
    printf("\n  Deep scan active regions:\n");
    for (UINT32 region = 0; region <= lastNonZero; region += 0x10000) {
        UINT32 regionEnd = region + 0x10000;
        int hasContent = 0;

        /* Quick check if region has any content */
        for (UINT32 off = region; off < regionEnd; off += 0x1000) {
            UINT32 v = MmioRead(h, bar5, 0x100, off);
            if (v != 0 && v != 0xFFFFFFFF && v != 0xDEAD0000) {
                hasContent = 1;
                break;
            }
        }

        if (!hasContent) continue;

        /* Deep scan the active region */
        for (UINT32 off = region; off < regionEnd; off += 4) {
            UINT32 v = MmioRead(h, bar5, 0x100, off);
            if (v != 0 && v != 0xFFFFFFFF && v != 0xDEAD0000) {
                const char* name = IdentifyHdpReg(off);
                printf("    +0x%05X: 0x%08X", off, v);
                if (name) printf(" (%s)", name);
                printf("\n");
            }
        }
    }

    /* ================================================ */
    /* Phase 3: Targeted SoC MMIO probe (safe only)    */
    /* ================================================ */
    printf("\n--- Phase 3: Targeted SoC MMIO probe ---\n\n");
    printf("  (Only probing known AMD SoC addresses — no broad scan)\n\n");

    /* Known AMD SoC MMIO regions — only probe specific offsets */
    struct { UINT64 base; const char* name; UINT32 offsets[8]; } probes[] = {
        { 0xFED80000, "SMBus/HT", {0, 4, 8, 0xC, 0x10, 0x14, 0x18, 0x1C} },
        { 0xFEDC0000, "IOMMU", {0, 4, 8, 0xC, 0x10, 0x14, 0x18, 0x1C} },
        { 0xFEE00000, "MSI/APIC", {0, 4, 8, 0xC, 0x10, 0x14, 0x18, 0x1C} },
        { 0xFEF00000, "PSB/MP5", {0, 4, 8, 0xC, 0x10, 0x14, 0x18, 0x1C} },
        { 0xFEF80000, "PSP", {0, 4, 8, 0xC, 0x10, 0x14, 0x18, 0x1C} },
    };

    for (int p = 0; p < sizeof(probes)/sizeof(probes[0]); p++) {
        printf("  %s (0x%08X):\n", probes[p].name, (UINT32)probes[p].base);
        for (int o = 0; o < 8; o++) {
            UINT32 v = MmioRead(h, probes[p].base, 0x100, probes[p].offsets[o]);
            if (v != 0 && v != 0xFFFFFFFF && v != 0xDEAD0000) {
                printf("    +%04X: 0x%08X\n", probes[p].offsets[o], v);
            }
        }
    }

    /* ================================================ */
    /* Phase 4: Write VRAM and see if GPU picks it up  */
    /* ================================================ */
    printf("\n--- Phase 4: VRAM handshaking test ---\n\n");

    /* Write a known value at VRAM offset 0 */
    MmioWriteRead(h, bar0, 0x1000, 0, 0x11223344, NULL);
    printf("  Wrote 0x11223344 at VRAM+0x0000\n");

    /* Read it back from different sizes */
    UINT32 r1 = MmioRead(h, bar0, 0x1000, 0);
    UINT32 r2 = MmioRead(h, bar0, 0x100000, 0);
    printf("  Read back (4KB map):  0x%08X\n", r1);
    printf("  Read back (1MB map):  0x%08X\n", r2);

    /* Write at higher offset */
    MmioWriteRead(h, bar0, 0x1000, 0x10000, 0xAABBCCDD, NULL);
    printf("  Wrote 0xAABBCCDD at VRAM+0x10000\n");
    UINT32 r3 = MmioRead(h, bar0, 0x1000, 0x10000);
    printf("  Read back: 0x%08X\n", r3);

    /* ================================================ */
    /* Phase 5: PSP BAR probe                          */
    /* ================================================ */
    printf("\n--- Phase 5: PSP BAR probe ---\n\n");

    ReadPciConfig(h, 1, 0, 2, cfg);
    UINT32 pspBar2 = *(UINT32*)(cfg + 0x10) & 0xFFFFFFF0;
    UINT32 pspBar5 = *(UINT32*)(cfg + 0x24) & 0xFFFFFFF0;
    printf("  PSP BAR2: 0x%08X\n", pspBar2);
    printf("  PSP BAR5: 0x%08X\n", pspBar5);

    /* Probe PSP BAR2 offsets */
    printf("  PSP BAR2 registers:\n");
    for (UINT32 off = 0; off < 0x100; off += 4) {
        UINT32 v = MmioRead(h, pspBar2, 0x100, off);
        if (v != 0 && v != 0xFFFFFFFF && v != 0xDEAD0000) {
            printf("    +0x%02X: 0x%08X\n", off, v);
        }
    }

    /* Probe PSP BAR5 offsets */
    printf("  PSP BAR5 registers:\n");
    for (UINT32 off = 0; off < 0x100; off += 4) {
        UINT32 v = MmioRead(h, pspBar5, 0x100, off);
        if (v != 0 && v != 0xFFFFFFFF && v != 0xDEAD0000) {
            printf("    +0x%02X: 0x%08X\n", off, v);
        }
    }

    printf("\nDone.\n");
    CloseHandle(h);
    return 0;
}
