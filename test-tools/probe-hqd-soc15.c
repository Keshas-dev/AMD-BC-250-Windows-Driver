#include <windows.h>
#include <stdio.h>

#define IOCTL_READ_REG  0x80000B88
#define IOCTL_INIT_HW   0x80000B80

#define REG_GRBM_STATUS   0x00003260  /* GC_BASE+0x2000 */
#define REG_GRBM_GFX_IDX  0x000034D0  /* GC_BASE+0x2270 */
#define REG_SCRATCH       0x000032D4  /* GC_BASE+0x2074 */

/* HQD registers at SOC15 small offsets (GC_BASE + mm from gc_10_1_0_offset.h) */
#define REG_CP_HQD_ACTIVE_SOC15      0x000030B7  /* GC_BASE+0x1E57 */
#define REG_CP_HQD_VMID_SOC15        0x00003065  /* GC_BASE+0x1E05 */
#define REG_CP_HQD_PQ_BASE_SOC15     0x0000306B  /* GC_BASE+0x1E0B */
#define REG_CP_HQD_PQ_CONTROL_SOC15  0x0000307E  /* GC_BASE+0x1E1E */
#define REG_CP_HQD_PQ_WPTR_LO_SOC15  0x000030D0  /* GC_BASE+0x1E70? guessing */
#define REG_CP_HQD_PERSISTENT_SOC15  0x00003069  /* GC_BASE+0x1E09? guessing */
#define REG_CP_HQD_EOP_BASE_SOC15    0x000030C0  /* need real value */

int main(void)
{
    HANDLE hDev = CreateFileA("\\\\.\\AMDBC250DreamV43",
        GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hDev == INVALID_HANDLE_VALUE) {
        printf("FAIL open err=%lu\n", GetLastError());
        return 1;
    }

    unsigned char buf[32] = {0};
    *(unsigned long long*)(buf+0) = 0xFE800000ULL;
    *(unsigned int*)(buf+8) = 0x00080000;
    *(unsigned int*)(buf+12) = 1;
    *(unsigned long long*)(buf+16) = 0xC0000000ULL;
    *(unsigned int*)(buf+24) = 0x10000000;
    DWORD retlen;
    if (!DeviceIoControl(hDev, IOCTL_INIT_HW, buf, sizeof(buf), buf, sizeof(buf), &retlen, NULL)) {
        printf("FAIL INIT_HW err=%lu\n", GetLastError());
        CloseHandle(hDev); return 1;
    }
    printf("INIT_HW OK\n\n");

    unsigned long grbm = 0, scratch = 0, gfx_idx = 0;

    /* Read reference registers */
    { unsigned int ra[2] = { REG_GRBM_STATUS, 0 };
      DeviceIoControl(hDev, IOCTL_READ_REG, ra, sizeof(ra), ra, sizeof(ra), &retlen, NULL);
      grbm = ra[1]; }

    { unsigned int ra[2] = { REG_SCRATCH, 0 };
      DeviceIoControl(hDev, IOCTL_READ_REG, ra, sizeof(ra), ra, sizeof(ra), &retlen, NULL);
      scratch = ra[1]; }

    { unsigned int ra[2] = { REG_GRBM_GFX_IDX, 0 };
      DeviceIoControl(hDev, IOCTL_READ_REG, ra, sizeof(ra), ra, sizeof(ra), &retlen, NULL);
      gfx_idx = ra[1]; }

    printf("Reference:\n");
    printf("  GRBM_STATUS       [0x3260] = 0x%08X\n", grbm);
    printf("  SCRATCH           [0x32D4] = 0x%08X\n", scratch);
    printf("  GRBM_GFX_INDEX    [0x34D0] = 0x%08X\n\n", gfx_idx);

    /* Read SOC15-style offsets */
    static const struct { unsigned long off; const char *name; } soc15_regs[] = {
        { REG_CP_HQD_ACTIVE_SOC15,      "CP_HQD_ACTIVE" },
        { REG_CP_HQD_VMID_SOC15,        "CP_HQD_VMID" },
        { REG_CP_HQD_PQ_BASE_SOC15,     "CP_HQD_PQ_BASE" },
        { REG_CP_HQD_PQ_CONTROL_SOC15,  "CP_HQD_PQ_CONTROL" },
        { REG_CP_HQD_PERSISTENT_SOC15,  "CP_HQD_PERSISTENT_STATE" },
        { REG_CP_HQD_EOP_BASE_SOC15,    "CP_HQD_EOP_BASE_ADDR" },
    };

    printf("SOC15-style HQD offsets (GC_BASE + mm):\n");
    for (int i = 0; i < sizeof(soc15_regs)/sizeof(soc15_regs[0]); i++) {
        unsigned int ra[2] = { soc15_regs[i].off, 0 };
        DeviceIoControl(hDev, IOCTL_READ_REG, ra, sizeof(ra), ra, sizeof(ra), &retlen, NULL);
        printf("  [0x%05X] %-25s = 0x%08X\n", soc15_regs[i].off, soc15_regs[i].name, ra[1]);
    }

    /* Also try a broad scan of the 0x3060-0x30E0 range */
    printf("\nScan 0x3060-0x30DF (by 4):\n");
    for (int off = 0x3060; off <= 0x30DF; off += 4) {
        unsigned int ra[2] = { (unsigned int)off, 0 };
        DeviceIoControl(hDev, IOCTL_READ_REG, ra, sizeof(ra), ra, sizeof(ra), &retlen, NULL);
        if (ra[1] != 0 && ra[1] != 0xFFFFFFFF)
            printf("  [0x%04X] = 0x%08X\n", off, ra[1]);
    }

    printf("\nScan 0x3070-0x30EF (odd aligned, 1-byte):\n");
    for (int off = 0x3070; off <= 0x30EF; off += 1) {
        unsigned int ra[2] = { (unsigned int)off, 0 };
        DeviceIoControl(hDev, IOCTL_READ_REG, ra, sizeof(ra), ra, sizeof(ra), &retlen, NULL);
        if (ra[1] != 0 && ra[1] != 0xFFFFFFFF)
            printf("  [0x%04X] = 0x%08X\n", off, ra[1]);
    }

    CloseHandle(hDev);
    printf("\nDone\n");
    return 0;
}
