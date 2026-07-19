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

/* Linux gc_10_1_0_offset.h, BAR5 = GC_BASE(0x1260) + mm*4 */
#define RB0_BASE      0x89E0   /* mmCP_RB0_BASE=0x1DE0 */
#define RB0_BASE_HI   0x8BA4   /* mmCP_RB0_BASE_HI=0x1E51 */
#define RB0_CNTL      0x89E4   /* mmCP_RB0_CNTL=0x1DE1 */
#define RB0_WPTR      0x8A30   /* mmCP_RB0_WPTR=0x1DF4 */
#define RB0_WPTR_HI   0x8A34   /* mmCP_RB0_WPTR_HI=0x1DF5 */
#define RB0_RPTR      0x4FE0   /* mmCP_RB0_RPTR=0x0F60 */
#define RB0_RPTR_ADDR 0x89EC   /* mmCP_RB0_RPTR_ADDR=0x1DE3 */
#define ME_CNTL       0x4FB8   /* mmCP_ME_CNTL=0x0F56 */
#define GRBM_GFX_IDX  0x9A60   /* mmGRBM_GFX_INDEX=0x2200 */

/* Old (wrong) aliases used by previous tests */
#define OLD_RING0_BASE 0xDA60
#define OLD_ME_CNTL    0x4A74
#define OLD_GRBMIX     0x34D0

static void TestWritable(const char* name, uint32_t addr, uint32_t val) {
    uint32_t before = ReadReg(addr);
    WriteReg(addr, val);
    uint32_t after = ReadReg(addr);
    WriteReg(addr, before);
    const char* verdict = (after == val) ? "WRITABLE!" :
                          (after == before) ? "RO" : "PARTIAL/W1C";
    printf("  %-18s 0x%04X: 0x%08X -> 0x%08X -> 0x%08X  [%s]\n",
           name, addr, before, val, after, verdict);
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

    printf("\n=== OLD (wrong) aliases vs LINUX-CORRECTED ===\n");
    printf("--- WRONG aliases (should be RO/dead) ---\n");
    TestWritable("OLD_RB0_BASE",  OLD_RING0_BASE, 0x12345678);
    TestWritable("OLD_ME_CNTL",   OLD_ME_CNTL,    0x00000000);
    TestWritable("OLD_GRBMIX",    OLD_GRBMIX,     0xE0000000);

    printf("\n--- LINUX-CORRECTED (GC_BASE + mm*4) ---\n");
    TestWritable("RB0_BASE",    RB0_BASE,    0x12345678);
    TestWritable("RB0_BASE_HI", RB0_BASE_HI, 0x00000000);
    TestWritable("RB0_CNTL",    RB0_CNTL,    0x00400001);
    TestWritable("RB0_WPTR",    RB0_WPTR,    0x00000008);
    TestWritable("RB0_WPTR_HI", RB0_WPTR_HI, 0x00000000);
    TestWritable("RB0_RPTR",    RB0_RPTR,    0x00000000);
    TestWritable("ME_CNTL",     ME_CNTL,     0x00000000);
    TestWritable("GRBM_GFX_IDX",GRBM_GFX_IDX,0xE0000000);

    printf("\n=== CP_ME_CNTL state (corrected 0x%04X) ===\n", ME_CNTL);
    uint32_t mec = ReadReg(ME_CNTL);
    printf("ME_CNTL = 0x%08X\n", mec);
    if (mec != 0 && mec != 0xFFFFFFFF) {
        printf("ME halted -> writing 0 to unhalt...\n");
        WriteReg(ME_CNTL, 0);
        printf("ME_CNTL after = 0x%08X\n", ReadReg(ME_CNTL));
    } else {
        printf("ME already unhalted / unreadable\n");
    }

    printf("\n=== WPTR kick test (corrected RB0_WPTR=0x%04X) ===\n", RB0_WPTR);
    uint32_t w0 = ReadReg(RB0_WPTR);
    uint32_t r0 = ReadReg(RB0_RPTR);
    printf("WPTR before=0x%08X  RPTR before=0x%08X\n", w0, r0);
    WriteReg(RB0_WPTR, w0 + 8);
    uint32_t w1 = ReadReg(RB0_WPTR);
    uint32_t r1 = ReadReg(RB0_RPTR);
    printf("WPTR after =0x%08X  RPTR after =0x%08X\n", w1, r1);
    printf("WPTR wrote? %s | RPTR advanced? %s\n",
           (w1 == w0 + 8) ? "YES" : "NO",
           (r1 != r0) ? "YES" : "NO (no ring buffer / ME not processing)");

    printf("\n=== DONE ===\n");
    CloseHandle(g_hDev);
    return 0;
}
