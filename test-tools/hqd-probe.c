#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define IOCTL_READ_REG   0x80000B88
#define IOCTL_WRITE_REG  0x80000B8C
#define IOCTL_INIT_HW    0x80000B80

static HANDLE g_hDev = INVALID_HANDLE_VALUE;

static int open_dev(void) {
    g_hDev = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    return (g_hDev != INVALID_HANDLE_VALUE);
}
static int init_hw(void) {
    unsigned char buf[32] = {0};
    *(unsigned long long*)(buf+0) = 0xFE800000ULL;
    *(unsigned int*)(buf+8) = 0x00080000;
    *(unsigned int*)(buf+12) = 1;
    *(unsigned long long*)(buf+16) = 0xC0000000ULL;
    *(unsigned int*)(buf+24) = 0x10000000;
    DWORD retlen;
    return DeviceIoControl(g_hDev, IOCTL_INIT_HW, buf, sizeof(buf), buf, sizeof(buf), &retlen, NULL);
}

static unsigned long rd(unsigned int off) {
    unsigned int ra[2] = { off, 0 }; DWORD retlen;
    DeviceIoControl(g_hDev, IOCTL_READ_REG, ra, sizeof(ra), ra, sizeof(ra), &retlen, NULL);
    return ra[1];
}
static void wr(unsigned int off, unsigned long val) {
    unsigned int wa[2] = { off, val }; DWORD retlen;
    DeviceIoControl(g_hDev, IOCTL_WRITE_REG, wa, sizeof(wa), wa, sizeof(wa), &retlen, NULL);
}

static void test_wr(unsigned int off, unsigned long val, const char *name) {
    unsigned long before = rd(off);
    wr(off, val);
    unsigned long after = rd(off);
    printf("  [0x%05X] %-30s: was=0x%08X, wrote=0x%08X, now=0x%08X %s\n",
        off, name, before, val, after,
        (before != val && after == val) ? "OK" : (before == after ? "STUCK" : "PARTIAL"));
}

int main() {
    if (!open_dev()) { printf("FAIL open\n"); return 1; }
    if (!init_hw()) { printf("FAIL init_hw\n"); return 1; }
    printf("INIT_HW OK\n");

    /* Test: are 0x34D0 and 0xC200 aliased? */
    printf("\n=== GRBM_GFX_INDEX Aliasing Test ===\n");
    unsigned long v34_before = rd(0x34D0);
    unsigned long vC2_before = rd(0xC200);
    wr(0x34D0, 0xDEAD0000);   /* write to 0x34D0 */
    unsigned long v34_after = rd(0x34D0);
    unsigned long vC2_after = rd(0xC200);
    wr(0xC200, 0x12340000);   /* write to 0xC200 */
    unsigned long v34_final = rd(0x34D0);
    unsigned long vC2_final = rd(0xC200);
    wr(0x34D0, v34_before);   /* restore 0x34D0 */
    wr(0xC200, vC2_before);   /* restore 0xC200 */

    printf("  0x34D0: before=0x%08X, after 0xDEAD=0x%08X, after 0xC200=0x%08X\n", v34_before, v34_after, v34_final);
    printf("  0xC200: before=0x%08X, after 0xDEAD=0x%08X, after 0x1234=0x%08X\n", vC2_before, vC2_after, vC2_final);
    if (v34_final == 0x12340000 && vC2_final == 0x12340000)
        printf("  *** ALIASED: writes to one affect the other ***\n");
    else if (v34_final == 0xDEAD0000 && vC2_final == 0x12340000)
        printf("  SEPARATE registers (each holds its own value)\n");
    else
        printf("  UNCLEAR aliasing relationship\n");

    /* Test: HQD register writeability in BROADCAST mode */
    printf("\n=== HQD Register Writeability (BROADCAST=0x80000000) ===\n");
    wr(0x34D0, 0x80000000); /* set broadcast */
    unsigned long grbm = rd(0x34D0);
    printf("  GRBM_GFX_INDEX = 0x%08X (broadcast %s)\n", grbm, grbm == 0x80000000 ? "OK" : "DIFF");

    /* Test each HQD register with a non-zero value */
    test_wr(0xDAC0, 0x01, "CP_HQD_ACTIVE=1");
    test_wr(0xDAC4, 0x01, "CP_HQD_VMID=1");
    test_wr(0xDAC8, 0xE001, "CP_HQD_PERSISTENT_STATE");
    test_wr(0xDAD8, 0xDEAD0000, "CP_HQD_PQ_BASE=0xDEAD");
    test_wr(0xDAFC, 0x08, "CP_HQD_PQ_CONTROL=8");
    test_wr(0xDB90, 0x12345678, "CP_HQD_PQ_WPTR_LO");
    test_wr(0xDB94, 0x00000000, "CP_HQD_PQ_WPTR_HI=0");
    test_wr(0xDB4C, 0x87654321, "CP_HQD_EOP_BASE_ADDR");
    test_wr(0xDB54, 0x08000000, "CP_HQD_EOP_CONTROL");
    test_wr(0xDAF4, 0x00, "CP_HQD_DOORBELL_CTRL");
    test_wr(0xDAE0, 0xDEAD, "CP_HQD_PQ_RPTR");
    test_wr(0xDB18, 0x01, "CP_HQD_DEQUEUE_REQ");
    test_wr(0xDB6C, 0x00, "CP_PQ_WPTR_POLL_CNTL");

    /* Restore known-save registers */
    wr(0xDAC0, 0);
    wr(0xDAD8, 0);
    wr(0xDAFC, 0);
    wr(0xDB90, 0);
    wr(0xDB4C, 0);
    wr(0xDB54, 0);

    /* Try selecting ME=0 instead of ME=1 */
    printf("\n=== Test with ME=0 selection ===\n");
    wr(0x34D0, 0x00000000); /* ME=0, PIPE=0 */
    grbm = rd(0x34D0);
    printf("  GRBM_GFX_INDEX = 0x%08X\n", grbm);
    test_wr(0xDAC0, 0x01, "CP_HQD_ACTIVE=1 (ME=0)");
    wr(0xDAC0, 0); /* restore */
    wr(0x34D0, 0x80000000); /* back to broadcast */

    CloseHandle(g_hDev);
    return 0;
}
