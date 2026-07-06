#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE g_hDev = INVALID_HANDLE_VALUE;

static BOOL WriteReg(uint32_t off, uint32_t val) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD ret = 0;
    r.RegisterOffset = off; r.Value = val;
    return DeviceIoControl(g_hDev, IOCTL_AMDBC250_WRITE_REG, &r, sizeof(r), &r, sizeof(r), &ret, NULL);
}
static uint32_t ReadReg(uint32_t off) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD ret = 0;
    r.RegisterOffset = off; r.Value = 0;
    if (DeviceIoControl(g_hDev, IOCTL_AMDBC250_READ_REG, &r, sizeof(r), &r, sizeof(r), &ret, NULL)) return r.Value;
    return 0xFFFFFFFF;
}

int main() {
    g_hDev = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (g_hDev == INVALID_HANDLE_VALUE) { printf("FAIL: CreateFile gle=%lu\n", GetLastError()); return 1; }
    printf("CreateFile OK\n");

    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD ret = 0;
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL; ih.MmioSize = 0x80000;
    ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
    BOOL ok = DeviceIoControl(g_hDev, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &ret, NULL);
    printf("INIT_HW: ok=%d gle=%lu\n\n", ok, GetLastError());

    printf("CP_ME_CNTL  (0x4A74): 0x%08X\n", ReadReg(0x4A74));
    printf("CP_MEC_CNTL (0x4B14): 0x%08X\n", ReadReg(0x4B14));

    ULONG meCntl = ReadReg(0x4A74);
    if (meCntl & 0x10000000) printf("  -> ME is HALTED (bit 28 set)\n");
    else                     printf("  -> ME is UNHALTED\n");

    ULONG mecCntl = ReadReg(0x4B14);
    if (mecCntl & 0x00000001) printf("  -> MEC is HALTED (bit 0 set)\n");
    else                      printf("  -> MEC is UNHALTED\n");

    printf("\nUnhalting BOTH engines...\n");
    WriteReg(0x4A74, 0);
    WriteReg(0x4B14, 0);
    printf("CP_ME_CNTL  -> 0x%08X\n", ReadReg(0x4A74));
    printf("CP_MEC_CNTL -> 0x%08X\n", ReadReg(0x4B14));

    printf("\n=== KIQ Register Write-Probe ===\n");
    ULONG v;
    v = ReadReg(0xE060); printf("KIQ_BASE_LO (0xE060): 0x%08X", v);
    WriteReg(0xE060, 0x12345678); printf(" -> 0x%08X", ReadReg(0xE060));
    WriteReg(0xE060, 0);          printf(" -> 0x%08X", ReadReg(0xE060));
    printf("\n");

    v = ReadReg(0xE064); printf("KIQ_BASE_HI (0xE064): 0x%08X", v);
    WriteReg(0xE064, 0x87654321); printf(" -> 0x%08X", ReadReg(0xE064));
    printf("\n");

    v = ReadReg(0xE068); printf("KIQ_CNTL    (0xE068): 0x%08X", v);
    WriteReg(0xE068, 0x0000103F); printf(" -> 0x%08X", ReadReg(0xE068));
    printf(" [size=63 blksize=1]");
    printf("\n");

    v = ReadReg(0xE078); printf("KIQ_WPTR    (0xE078): 0x%08X", v);
    WriteReg(0xE078, 0x00000005); printf(" -> 0x%08X", ReadReg(0xE078));
    WriteReg(0xE078, 0);          printf(" -> 0x%08X", ReadReg(0xE078));
    printf("\n");

    v = ReadReg(0xE06C); printf("KIQ_RPTR    (0xE06C): 0x%08X", v);
    WriteReg(0xE06C, 0x00000010); printf(" -> 0x%08X", ReadReg(0xE06C));
    printf("\n");

    v = ReadReg(0xE07C); printf("KIQ_VMID    (0xE07C): 0x%08X", v);
    WriteReg(0xE07C, 0x00000000); printf(" -> 0x%08X", ReadReg(0xE07C));
    printf("\n");

    v = ReadReg(0xE080); printf("KIQ_ACTIVE  (0xE080): 0x%08X", v);
    WriteReg(0xE080, 0x00000001); printf(" -> 0x%08X", ReadReg(0xE080));
    printf("\n");

    printf("\n=== GFX Ring0 Write-Probe ===\n");
    v = ReadReg(0xDA60); printf("GFX_BASE_LO (0xDA60): 0x%08X", v);
    WriteReg(0xDA60, 0x5678ABCD); printf(" -> 0x%08X", ReadReg(0xDA60));
    printf("\n");

    v = ReadReg(0xDA68); printf("GFX_CNTL    (0xDA68): 0x%08X", v);
    WriteReg(0xDA68, 0x8000103F); printf(" -> 0x%08X", ReadReg(0xDA68));
    printf(" [rptr_wr_en=1 bufsz=63]");
    printf("\n");

    v = ReadReg(0xDA78); printf("GFX_WPTR    (0xDA78): 0x%08X", v);
    WriteReg(0xDA78, 0x00000020); printf(" -> 0x%08X", ReadReg(0xDA78));
    printf("\n");

    v = ReadReg(0xDA80); printf("GFX_DOORBL  (0xDA80): 0x%08X", v);
    WriteReg(0xDA80, 0x00000001); printf(" -> 0x%08X", ReadReg(0xDA80));
    printf("\n");

    /* Restore all modified registers to safe values */
    WriteReg(0xE068, 0);  /* KIQ_CNTL ← 0 (disable ring processing first) */
    WriteReg(0xE060, 0);  /* KIQ_BASE_LO ← 0 */
    WriteReg(0xE064, 0);  /* KIQ_BASE_HI ← 0 */
    WriteReg(0xE078, 0);  /* KIQ_WPTR ← 0 */
    WriteReg(0xE06C, 0);  /* KIQ_RPTR ← 0 */
    WriteReg(0xE080, 0);  /* KIQ_ACTIVE ← 0 */
    WriteReg(0xDA60, 0);  /* GFX_BASE_LO ← 0 */
    WriteReg(0xDA78, 0);  /* GFX_WPTR ← 0 */
    WriteReg(0xDA80, 0);  /* GFX_DOORBELL ← 0 */

    printf("\n=== Final KIQ State ===\n");
    printf("KIQ_BASE_LO: 0x%08X\n", ReadReg(0xE060));
    printf("KIQ_CNTL:    0x%08X\n", ReadReg(0xE068));
    printf("KIQ_WPTR:    0x%08X\n", ReadReg(0xE078));
    printf("KIQ_RPTR:    0x%08X\n", ReadReg(0xE06C));

    printf("\n=== Final GFX Ring0 State ===\n");
    printf("GFX_BASE_LO: 0x%08X\n", ReadReg(0xDA60));
    printf("GFX_CNTL:    0x%08X\n", ReadReg(0xDA68));
    printf("GFX_RPTR:    0x%08X\n", ReadReg(0xDA6C));
    printf("GFX_WPTR:    0x%08X\n", ReadReg(0xDA78));

    printf("\n=== Scratch ===\n");
    printf("SCRATCH_REG0: 0x%08X\n", ReadReg(0x32D4));

    CloseHandle(g_hDev);
    printf("\nDONE\n");
    return 0;
}
