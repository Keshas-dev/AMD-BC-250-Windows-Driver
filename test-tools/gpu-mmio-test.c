#define INITGUID
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static FILE *g = NULL;
static BOOL g_UnsafeWrites = FALSE;
static BOOL g_DeepReads = FALSE;
static void Log(const char *fmt, ...) {
    va_list a; va_start(a, fmt);
    vfprintf(stdout, fmt, a); va_end(a); fflush(stdout);
    if (g) { va_start(a, fmt); vfprintf(g, fmt, a); va_end(a); fflush(g); }
}

static HANDLE OpenKmd(void) {
    return CreateFileW(L"\\\\.\\AMDBC250DreamV43",
        GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
}

static BOOL ReadReg(HANDLE h, UINT32 offset, UINT32 *val) {
    UINT32 ra[2] = {offset, 0xDEADBEEF};
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, 0x80000B88, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
    if (ok && val) *val = ra[1];
    return ok;
}

static BOOL WriteReg(HANDLE h, UINT32 offset, UINT32 val) {
    UINT32 ra[2] = {offset, val};
    DWORD br = 0;
    return DeviceIoControl(h, 0x80000B8C, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
}

static BOOL InitHardwareNBIO(HANDLE h) {
    UCHAR initIn[32] = {0}, initOut[32] = {0};
    DWORD br = 0;
    *(UINT64*)(initIn + 0)  = 0xFE800000ULL;
    *(UINT32*)(initIn + 8)  = 0x00080000;
    *(UINT32*)(initIn + 12) = 1;
    *(UINT64*)(initIn + 16) = 0xC0000000ULL;
    *(UINT32*)(initIn + 24) = 0x10000000;
    return DeviceIoControl(h, 0x80000B80, initIn, sizeof(initIn), initOut, sizeof(initOut), &br, NULL);
}

static const char *RegName(UINT32 off) {
    switch (off) {
    case 0x0000: return "GPU_ID";
    case 0x0040: return "GPURevision";
    case 0x0044: return "GPURevision2";
    case 0x0D00: return "CLK_CNTL";
    case 0x2000: return "GRBM_STATUS_SE0";
    case 0x2004: return "GRBM_STATUS(old)";
    case 0x2074: return "SCRATCH_REG(old)";
    case 0x2100: return "CP_STATUS";
    case 0x2104: return "CP_STALLED";
    case 0x2108: return "CP_RB0_BASE";
    case 0x210C: return "CP_RB0_CNTL";
    case 0x2110: return "CP_RB0_RPTR";
    case 0x2114: return "CP_RB0_WPTR";
    case 0x3000: return "GC_UNKNOWN_3000";
    case 0x3004: return "GC_UNKNOWN_3004";
    case 0x3008: return "GC_UNKNOWN_3008";
    case 0x300C: return "GC_UNKNOWN_300C";
    case 0x3200: return "GRBM_BREAD_CRUMB";
    case 0x3204: return "GRBM_BREAD_CRUMB2";
    case 0x3260: return "GRBM_STATUS(GC_BASE)";
    case 0x3264: return "CC_UCONFIG(GC_BASE)";
    case 0x3268: return "GC_CONFIG";
    case 0x326C: return "GRBM_CNTL(GC_BASE)";
    case 0x32D4: return "SCRATCH_REG(GC_BASE)";
    case 0x34FC: return "SPI_WGP_CNTL";
    case 0x5000: return "MMHUB_VMHUB0_BASE";
    case 0x50C4: return "MMHUB_VM_PERM";
    case 0x50D0: return "MMHUB_VM_CONFIG";
    case 0x8000: return "THM_TMON0";
    case 0xA000: return "RSMU";
    case 0xC000: return "NBIO_BIF_RB_BASE";
    case 0xC0D8: return "NBIO_CTRL";
    case 0xC174: return "NBIO_MASK";
    case 0xC1A4: return "NBIO_ENABLE";
    case 0xC1E4: return "NBIO_CONFIG";
    case 0x1A214: return "DF_MC_BASE";
    default: return NULL;
    }
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (_stricmp(argv[i], "--unsafe-writes") == 0) {
            g_UnsafeWrites = TRUE;
        } else if (_stricmp(argv[i], "--deep-unsafe-reads") == 0) {
            g_DeepReads = TRUE;
        }
    }

    g = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\gpu-mmio-test.log", "w");
    if (!g) { printf("Cannot open log\n"); return 1; }
    Log("=== GPU MMIO Test (READ-ONLY by default) ===\n\n");
    if (!g_UnsafeWrites && !g_DeepReads) {
        Log("Mode: safe read-only. Pass --unsafe-writes to enable writes, or --deep-unsafe-reads for RSMU/MMHUB/DF ranges.\n\n");
    } else if (g_UnsafeWrites) {
        Log("Mode: UNSAFE write/readback tests enabled.\n\n");
    } else {
        Log("Mode: deep unsafe read ranges enabled.\n\n");
    }

    HANDLE h = OpenKmd();
    if (h == INVALID_HANDLE_VALUE) {
        Log("KMD NOT FOUND\n"); fclose(g); return 1;
    }
    Log("Driver opened\n");

    BOOL ok = InitHardwareNBIO(h);
    Log("INIT_HARDWARE NBIO_MAP: %s\n\n", ok ? "OK" : "FAILED");
    if (!ok) { fclose(g); return 1; }

    UINT32 v;

    /* ===== PHASE 1: Core GPU registers (GC_BASE-shifted) ===== */
    Log("=== PHASE 1: GC_BASE-shifted registers (0x3200+) ===\n");
    {
        UINT32 regs[] = {
            0x3200, 0x3204, 0x3208, 0x320C,
            0x3210, 0x3214, 0x3218, 0x321C,
            0x3220, 0x3224, 0x3228, 0x322C,
            0x3230, 0x3234, 0x3238, 0x323C,
            0x3240, 0x3244, 0x3248, 0x324C,
            0x3250, 0x3254, 0x3258, 0x325C,
            0x3260, 0x3264, 0x3268, 0x326C,
            0x3270, 0x3274, 0x3278, 0x327C,
            0x3280, 0x3284, 0x3288, 0x328C,
            0x3290, 0x3294, 0x3298, 0x329C,
            0x32A0, 0x32A4, 0x32A8, 0x32AC,
            0x32B0, 0x32B4, 0x32B8, 0x32BC,
            0x32C0, 0x32C4, 0x32C8, 0x32CC,
            0x32D0, 0x32D4, 0x32D8, 0x32DC,
            0x32E0, 0x32E4, 0x32E8, 0x32EC,
            0x32F0, 0x32F4, 0x32F8, 0x32FC,
        };
        for (int i = 0; i < sizeof(regs)/sizeof(regs[0]); i++) {
            ReadReg(h, regs[i], &v);
            const char *nm = RegName(regs[i]);
            if (nm) Log("  [0x%04X] %-20s = 0x%08X\n", regs[i], nm, v);
            else     Log("  [0x%04X] %-20s = 0x%08X\n", regs[i], "?", v);
        }
    }

    /* ===== PHASE 2: SPI/WGP/THM range (0x3400+) ===== */
    Log("\n=== PHASE 2: SPI/WGP/THM (0x3400-0x8100) ===\n");
    if (!g_DeepReads) {
        Log("  SKIPPED: THM/MMHUB/DF read ranges are disabled by default.\n");
    } else {
        Log("  WARNING: deep read range enabled.\n");
        UINT32 regs[] = {
            0x3400, 0x3404, 0x3408, 0x340C,
            0x34F0, 0x34F4, 0x34F8, 0x34FC,
            0x3500, 0x3504, 0x3508, 0x350C,
            0x5000, 0x5004, 0x5008, 0x500C,
            0x50C4, 0x50D0,
            0x8000, 0x8004, 0x8008, 0x800C,
            0x8100, 0x8104, 0x8108, 0x810C,
        };
        for (int i = 0; i < sizeof(regs)/sizeof(regs[0]); i++) {
            ReadReg(h, regs[i], &v);
            if (v != 0xFFFFFFFF && v != 0x00000000)
                Log("  [0x%04X] = 0x%08X\n", regs[i], v);
        }
    }

    /* ===== PHASE 3: NBIO registers (0xC000+) ===== */
    Log("\n=== PHASE 3: NBIO registers (0xC000+) ===\n");
    {
        UINT32 regs[] = {
            0xC000, 0xC004, 0xC008, 0xC00C,
            0xC0D8, 0xC100, 0xC104, 0xC108, 0xC10C,
            0xC140, 0xC144, 0xC148, 0xC14C,
            0xC174, 0xC1A4, 0xC1E4,
            0xC200, 0xC204, 0xC208, 0xC20C,
        };
        for (int i = 0; i < sizeof(regs)/sizeof(regs[0]); i++) {
            ReadReg(h, regs[i], &v);
            const char *nm = RegName(regs[i]);
            if (nm) Log("  [0x%04X] %-12s = 0x%08X\n", regs[i], nm, v);
            else     Log("  [0x%04X] %-12s = 0x%08X\n", regs[i], "?", v);
        }
    }

    /* ===== PHASE 4: RSMU/DF range (0xA000+) ===== */
    Log("\n=== PHASE 4: RSMU/DF (0xA000-0x1B000) ===\n");
    if (!g_DeepReads) {
        Log("  SKIPPED: RSMU/DF read ranges are disabled by default.\n");
    } else {
        Log("  WARNING: RSMU/DF deep read range enabled.\n");
        UINT32 regs[] = {
            0xA000, 0xA004, 0xA008, 0xA00C,
            0xA200, 0xA204, 0xA208, 0xA20C,
            0x1A000, 0x1A004, 0x1A214, 0x1A218,
        };
        for (int i = 0; i < sizeof(regs)/sizeof(regs[0]); i++) {
            ReadReg(h, regs[i], &v);
            if (v != 0xFFFFFFFF && v != 0x00000000)
                Log("  [0x%05X] = 0x%08X\n", regs[i], v);
        }
    }

    /* ===== PHASE 5: Optional SCRATCH write/readback test ===== */
    Log("\n=== PHASE 5: SCRATCH write/readback ===\n");
    if (!g_UnsafeWrites) {
        Log("  SKIPPED: write tests are disabled by default.\n");
    } else {
        UINT32 scratchBefore = 0, scratchAfter = 0;
        UINT32 testVals[] = { 0x11111111, 0x00000000, 0x55555555 };
        if (!ReadReg(h, 0x32D4, &scratchBefore)) {
            Log("  SCRATCH read failed before write test\n");
        } else {
            for (int i = 0; i < sizeof(testVals)/sizeof(testVals[0]); i++) {
                if (!WriteReg(h, 0x32D4, testVals[i])) {
                    Log("  SCRATCH[0x32D4]: write=0x%08X IOCTL failed\n", testVals[i]);
                    continue;
                }
                if (!ReadReg(h, 0x32D4, &scratchAfter)) {
                    Log("  SCRATCH[0x32D4]: write=0x%08X read IOCTL failed\n", testVals[i]);
                    continue;
                }
                Log("  SCRATCH[0x32D4]: write=0x%08X read=0x%08X %s\n",
                    testVals[i], scratchAfter, (scratchAfter == testVals[i]) ? "OK" : "MISMATCH");
            }
            if (!WriteReg(h, 0x32D4, scratchBefore)) {
                Log("  SCRATCH restore failed: could not write 0x%08X\n", scratchBefore);
            } else if (!ReadReg(h, 0x32D4, &scratchAfter)) {
                Log("  SCRATCH restore readback failed\n");
            } else {
                Log("  SCRATCH restored to 0x%08X %s\n",
                    scratchAfter, (scratchAfter == scratchBefore) ? "OK" : "MISMATCH");
            }
        }
    }

    /* ===== PHASE 6: Optional GRBM_CNTL unlock attempt ===== */
    Log("\n=== PHASE 6: GRBM_CNTL unlock (0x326C) ===\n");
    if (!g_UnsafeWrites) {
        Log("  SKIPPED: GRBM_CNTL writes are disabled by default.\n");
    } else {
        UINT32 grbmCntlBefore = 0, grbmCntlAfter = 0;
        if (!ReadReg(h, 0x326C, &grbmCntlBefore)) {
            Log("  GRBM_CNTL read failed before write test\n");
        } else {
            Log("  GRBM_CNTL before: 0x%08X\n", grbmCntlBefore);
            if (!WriteReg(h, 0x326C, 0x00000000)) {
                Log("  GRBM_CNTL write 0 IOCTL failed\n");
            } else if (!ReadReg(h, 0x326C, &grbmCntlAfter)) {
                Log("  GRBM_CNTL read IOCTL failed after write 0\n");
            } else {
                Log("  GRBM_CNTL after write 0: 0x%08X\n", grbmCntlAfter);
            }

            if (!WriteReg(h, 0x326C, grbmCntlBefore)) {
                Log("  GRBM_CNTL restore failed: could not write 0x%08X\n", grbmCntlBefore);
            } else if (!ReadReg(h, 0x326C, &grbmCntlAfter)) {
                Log("  GRBM_CNTL restore readback IOCTL failed\n");
            } else {
                Log("  GRBM_CNTL restored to 0x%08X %s\n",
                    grbmCntlAfter, (grbmCntlAfter == grbmCntlBefore) ? "OK" : "MISMATCH");
            }
            if (!ReadReg(h, 0x3260, &v)) {
                Log("  GRBM_STATUS read IOCTL failed\n");
            } else {
                Log("  GRBM_STATUS after: 0x%08X\n", v);
            }
        }
    }

    CloseHandle(h);

    Log("\n=== Safe GPU MMIO Test Complete ===\n");
    fclose(g);
    printf("Done. Check output\\gpu-mmio-test.log\n");
    return 0;
}
