#include <windows.h>
#include <stdio.h>

#define IOCTL_READ_REG  0x80000B88
#define IOCTL_INIT_HW   0x80000B80

static unsigned long read_reg(HANDLE h, unsigned int offset)
{
    unsigned int ra[2] = { offset, 0 };
    DWORD retlen;
    DeviceIoControl(h, IOCTL_READ_REG, ra, sizeof(ra), ra, sizeof(ra), &retlen, NULL);
    return ra[1];
}

int main()
{
    HANDLE h = CreateFileA("\\\\.\\AMDBC250DreamV43",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { printf("FAIL\n"); return 1; }

    /* INIT with Flags=1 (NBIO_MAP — safe, no GPU init) */
    {
        unsigned char buf[32] = {0};
        *(unsigned long long*)(buf + 0) = 0xFE800000ULL;
        *(unsigned int*)(buf + 8) = 0x00080000;
        *(unsigned int*)(buf + 12) = 1;  /* NBIO_MAP */
        *(unsigned long long*)(buf + 16) = 0xC0000000ULL;
        *(unsigned int*)(buf + 24) = 0x10000000;
        DWORD retlen;
        DeviceIoControl(h, IOCTL_INIT_HW, buf, sizeof(buf), buf, sizeof(buf), &retlen, NULL);
    }

    printf("=== NBIO Doorbell & CP registers ===\n");
    printf("(Using NBIO_MAP — only reads, no GFX init)\n\n");

    /* NBIO registers at the correct offsets (NBIO, not GC_BASE shifted) */
    unsigned int nbio[] = { 0xC0F0, 0xC0F4, 0xC1F0, 0xC1F4, 0xC0E0, 0xC060, 0xC100, 0xC0C0, 0xC0C4 };
    char* nnames[] = { "DOORBELL_RANGE_LOW (C0F0)", "DOORBELL_RANGE_HIGH (C0F4)",
                       "DOORBELL_RANGE_LOW (C1F0)", "DOORBELL_RANGE_HIGH (C1F4)",
                       "CP_MEC_CNTL", "CP_ME_CNTL", "CP_DEBUG",
                       "CP_SEM_WAIT_TIMER", "CP_SEM_INCOMPLETE_TIMER" };
    for (int i = 0; i < 9; i++) {
        unsigned long val = read_reg(h, nbio[i]);
        printf("  [0x%06X] %-32s = 0x%08X\n", nbio[i], nnames[i], val);
    }

    /* Also check some more CP/SDMA registers */
    printf("\n=== Other CP registers ===\n");
    unsigned int misc[] = { 0xC1C0, 0xC1C4, 0xC1E0, 0xC1E4, 0xC200, 0xD000, 0xD004, 0xD008 };
    char* mnames[] = { "CP_IB1_BASE_LO", "CP_IB1_BASE_HI", "CP_IB2_BASE_LO", "CP_IB2_BASE_HI",
                       "CP_INT_STATUS", "SDMA0_F32", "SDMA0_STATUS", "SDMA0_CNTL" };
    for (int i = 0; i < 8; i++) {
        unsigned long val = read_reg(h, misc[i]);
        printf("  [0x%06X] %-20s = 0x%08X\n", misc[i], mnames[i], val);
    }

    /* Check IB registers at GC_BASE-shifted offsets */
    printf("\n=== IB registers (GC_BASE shifted) ===\n");
    printf("Navi10 -> BC-250: NBIO+0xC1C0 -> 0x1260+0xC1C0=0xD420\n");
    unsigned int ib_regs[] = { 0xD420, 0xD424, 0xD440, 0xD444, 0xD4E0, 0xD4E4, 0xD500, 0xD504 };
    for (int i = 0; i < 8; i++) {
        unsigned long v = read_reg(h, ib_regs[i]);
        if (v != 0) printf("  [0x%06X] = 0x%08X  <-- NON-ZERO!\n", ib_regs[i], v);
    }
    printf("  (all others zero or FF)\n");

    /* BAR5 doorbell region probe */
    printf("\n=== BAR5 THM/doorbell region (0x8000-0x8100) ===\n");
    for (unsigned int off = 0x8000; off <= 0x8100; off += 16) {
        unsigned long v = read_reg(h, off);
        if (v != 0) printf("  [0x%04X] = 0x%08X\n", off, v);
    }

    CloseHandle(h);
    return 0;
}
