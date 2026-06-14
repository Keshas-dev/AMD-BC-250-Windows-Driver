#include <windows.h>
#include <stdio.h>

#define IOCTL_READ_REG  0x80000B88
#define IOCTL_WRITE_REG 0x80000B8C
#define IOCTL_INIT_HW   0x80000B80

#define R_GFX_BASE_LO  0x0000DA60
#define R_GFX_CNTL     0x0000DA68
#define R_GFX_RPTR     0x0000DA6C
#define R_GFX_RPTR_ADDR_LO 0x0000DA70
#define R_GFX_RPTR_ADDR_HI 0x0000DA74
#define R_GFX_WPTR     0x0000DA78
#define R_GFX_DOORBELL 0x0000DA80
#define R_SCRATCH      0x000032D4
#define R_GRBM_STATUS  0x00003260
#define R_GRBM_GFX_IDX 0x000034D0
#define R_COMPUTE0_CNTL 0x0000DB68
#define R_COMPUTE0_WPTR 0x0000DB78

static HANDLE hDev;
static unsigned long rd(unsigned int off) {
    unsigned int ra[2] = { off, 0 }; DWORD retlen;
    DeviceIoControl(hDev, IOCTL_READ_REG, ra, sizeof(ra), ra, sizeof(ra), &retlen, NULL);
    return ra[1];
}
static void wr(unsigned int off, unsigned long val) {
    unsigned int wa[2] = { off, val }; DWORD retlen;
    DeviceIoControl(hDev, IOCTL_WRITE_REG, wa, sizeof(wa), wa, sizeof(wa), &retlen, NULL);
}

int main(void)
{
    hDev = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hDev == INVALID_HANDLE_VALUE) { printf("FAIL open\n"); return 1; }

    unsigned char buf[32] = {0};
    *(unsigned long long*)(buf+0) = 0xFE800000ULL;
    *(unsigned int*)(buf+8) = 0x00080000;
    *(unsigned int*)(buf+12) = 1;
    DWORD retlen;
    DeviceIoControl(hDev, IOCTL_INIT_HW, buf, sizeof(buf), buf, sizeof(buf), &retlen, NULL);
    printf("INIT_HW OK\n");

    printf("\n=== Initial State ===\n");
    printf("GFX_CNTL=0x%08X RPTR=0x%08X WPTR=0x%08X DOORBELL=0x%08X\n",
        rd(R_GFX_CNTL), rd(R_GFX_RPTR), rd(R_GFX_WPTR), rd(R_GFX_DOORBELL));
    printf("SCRATCH=0x%08X GRBM_STATUS=0x%08X\n", rd(R_SCRATCH), rd(R_GRBM_STATUS));
    printf("COMPUTE0_CNTL=0x%08X WPTR=0x%08X\n", rd(R_COMPUTE0_CNTL), rd(R_COMPUTE0_WPTR));

    /* Step 1: Test doorbell = same as current WPTR */
    printf("\n=== Test 1: Write DOORBELL = WPTR (no change) ===\n");
    unsigned long wptr = rd(R_GFX_WPTR);
    wr(R_GFX_DOORBELL, wptr);
    printf("DOORBELL=0x%08X → WPTR=0x%08X RPTR=0x%08X SCRATCH=0x%08X\n",
        wptr, rd(R_GFX_WPTR), rd(R_GFX_RPTR), rd(R_SCRATCH));
    Sleep(10);
    printf("After 10ms: RPTR=0x%08X SCRATCH=0x%08X\n", rd(R_GFX_RPTR), rd(R_SCRATCH));

    /* Step 2: Enable RPTR writeback in CNTL */
    printf("\n=== Test 2: Enable CNTL + doorbell ===\n");
    printf("SCRATCH before: 0x%08X\n", rd(R_SCRATCH));
    wr(R_GFX_CNTL, 0x00400001);
    printf("CNTL=0x00400001, readback=0x%08X\n", rd(R_GFX_CNTL));
    wr(R_GFX_DOORBELL, wptr);
    Sleep(10);
    printf("After doorbell: SCRATCH=0x%08X RPTR=0x%08X\n", rd(R_SCRATCH), rd(R_GFX_RPTR));

    /* Step 3: Try doorbell with WPTR+8 (potentially unsafe) */
    printf("\n=== Test 3: DOORBELL = WPTR + 8 ===\n");
    unsigned long rptr_before = rd(R_GFX_RPTR);
    unsigned long wptr_cur = rd(R_GFX_WPTR);
    printf("BEFORE: RPTR=0x%08X WPTR=0x%08X SCRATCH=0x%08X\n",
        rptr_before, wptr_cur, rd(R_SCRATCH));
    wr(R_GFX_DOORBELL, wptr_cur + 8);
    printf("DOORBELL=0x%08X written\n", wptr_cur + 8);
    Sleep(100);
    printf("AFTER 100ms: RPTR=0x%08X WPTR=0x%08X SCRATCH=0x%08X\n",
        rd(R_GFX_RPTR), rd(R_GFX_WPTR), rd(R_SCRATCH));
    printf("GRBM_STATUS=0x%08X (RPTR changed? %s)\n", rd(R_GRBM_STATUS),
        (rd(R_GFX_RPTR) != rptr_before) ? "YES" : "NO");

    /* Restore WPTR */
    wr(R_GFX_WPTR, wptr);
    wr(R_GFX_CNTL, 1);
    printf("\nRestored WPTR=0x%08X CNTL=1\n", wptr);

    CloseHandle(hDev);
    printf("\nDone\n");
    return 0;
}
