/* wgp-deep-test.c - try SPI_PG write at different init stages + GRBM reset */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE h;
static BOOL W32(uint32_t o, uint32_t v) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=v; return DeviceIoControl(h,IOCTL_AMDBC250_WRITE_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL); }
static uint32_t R32(uint32_t o) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=0; if(DeviceIoControl(h,IOCTL_AMDBC250_READ_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL)) return r.Value; return 0xFFFFFFFF; }

#define SPI_PG  0x5C3C
#define CC_ARRAY 0x9C1C
#define RLC_PG  0x3D64
#define GRBM_IDX 0x34D0

static int tryWriteSpi(uint32_t value) {
    W32(SPI_PG, value);
    uint32_t rb = R32(SPI_PG);
    return (rb == value) ? 1 : 0;
}

static void showState(const char *tag) {
    printf("  [%s] SPI_PG=0x%08X CC=0x%08X RLC=0x%08X GRBM=0x%08X\n", tag, R32(SPI_PG), R32(CC_ARRAY), R32(RLC_PG), R32(0x2000));
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    h = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { printf("FAIL: open err=%lu\n", GetLastError()); return 1; }
    printf("=== WGP Deep Test ===\n");

    AMDBC250_IOCTL_INIT_HARDWARE ih;
    DWORD br = 0;
    memset(&ih, 0, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL;
    ih.MmioSize = 0x80000;
    ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
    if (!DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &br, NULL)) {
        printf("INIT fail\n"); return 1;
    }
    printf("INIT OK\n");
    showState("after init");

    /* Stage 1: direct write */
    printf("\n=== Stage 1: Direct SPI_PG write ===\n");
    int ok = tryWriteSpi(0x1F);
    printf("  SPI_PG <- 0x1F: %s (readback 0x%08X)\n", ok ? "STUCK!" : "LOCKED", R32(SPI_PG));

    /* Stage 2: try GRBM soft reset then write */
    printf("\n=== Stage 2: GRBM soft reset then write ===\n");
    printf("  GRBM_SOFT_RST (0x3278) = 0x%08X\n", R32(0x3278));
    W32(0x3278, 0x00000001);  /* assert soft reset */
    Sleep(10);
    W32(0x3278, 0x00000000);  /* deassert */
    Sleep(10);
    printf("  After reset: GRBM_SOFT_RST = 0x%08X\n", R32(0x3278));
    ok = tryWriteSpi(0x1F);
    printf("  SPI_PG <- 0x1F: %s (readback 0x%08X)\n", ok ? "STUCK!" : "LOCKED", R32(SPI_PG));

    /* Stage 3: try per-bank GRBM_GFX_INDEX with different layouts */
    printf("\n=== Stage 3: Per-bank GRBM_GFX_INDEX ===\n");
    /* Linux gfx10 GRBM_GFX_INDEX layout:
     *   bits 7:0   = INSTANCE_INDEX
     *   bits 15:8  = SH_INDEX
     *   bits 23:16 = SE_INDEX
     *   bit 24     = INSTANCE_BROADCAST_WRITES
     *   bit 26     = SH_BROADCAST_WRITES
     *   bit 28     = SE_BROADCAST_WRITES
     * Broadcast = 0x15000000 (bit24+bit26+bit28)
     */
    uint32_t bcast = 0x15000000;
    printf("  Testing broadcast = 0x%08X\n", bcast);
    W32(GRBM_IDX, bcast);
    uint32_t rb = R32(GRBM_IDX);
    printf("  GRBM_GFX_INDEX write 0x%08X readback 0x%08X %s\n", bcast, rb, (rb==bcast)?"OK":"MISMATCH");

    /* Try broadcast write */
    W32(GRBM_IDX, bcast);
    ok = tryWriteSpi(0x1F);
    printf("  SPI_PG <- 0x1F (broadcast): %s (readback 0x%08X)\n", ok ? "STUCK!" : "LOCKED", R32(SPI_PG));

    /* Per-bank: SE0/SH0 */
    W32(GRBM_IDX, 0x00000000);
    ok = tryWriteSpi(0x1F);
    printf("  SPI_PG <- 0x1F (SE0/SH0): %s (readback 0x%08X)\n", ok ? "STUCK!" : "LOCKED", R32(SPI_PG));

    /* Per-bank: SE0/SH1 */
    W32(GRBM_IDX, 0x00000100);  /* SH_INDEX=1 at bits 15:8 */
    ok = tryWriteSpi(0x1F);
    printf("  SPI_PG <- 0x1F (SE0/SH1): %s (readback 0x%08X)\n", ok ? "STUCK!" : "LOCKED", R32(SPI_PG));

    /* Per-bank: SE1/SH0 */
    W32(GRBM_IDX, 0x00010000);  /* SE_INDEX=1 at bits 23:16 */
    ok = tryWriteSpi(0x1F);
    printf("  SPI_PG <- 0x1F (SE1/SH0): %s (readback 0x%08X)\n", ok ? "STUCK!" : "LOCKED", R32(SPI_PG));

    /* Per-bank: SE1/SH1 */
    W32(GRBM_IDX, 0x00010100);
    ok = tryWriteSpi(0x1F);
    printf("  SPI_PG <- 0x1F (SE1/SH1): %s (readback 0x%08X)\n", ok ? "STUCK!" : "LOCKED", R32(SPI_PG));

    /* Stage 4: Try different SPI_PG values */
    printf("\n=== Stage 4: Different SPI_PG values ===\n");
    W32(GRBM_IDX, bcast);
    uint32_t vals[] = {0x01, 0x07, 0x0F, 0x1F, 0xFF, 0xFFFFFFFF};
    for (int i = 0; i < 6; i++) {
        W32(SPI_PG, vals[i]);
        uint32_t r = R32(SPI_PG);
        printf("  SPI_PG <- 0x%08X readback 0x%08X %s\n", vals[i], r, (r==vals[i])?"STUCK!":"locked");
    }

    printf("\n=== DONE ===\n");
    CloseHandle(h);
    return 0;
}
