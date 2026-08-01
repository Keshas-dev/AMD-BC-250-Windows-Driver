/* dcn-live-scan.c — scan the DCN register region for LIVE (changing)
 * registers. A working scanout means frame/scanline counters tick, so a
 * register that CHANGES between two reads ~50ms apart is part of the
 * active display path. This reveals the REAL DCN base and active pipe,
 * which the static per-pipe probe at 0x5000-0x8000 missed.
 *
 * Scans BAR5 range 0x0000-0x9000 (safe, all register reads) looking for
 * registers whose value changes between consecutive reads. Reports the
 * live register list with offsets and example values.
 *
 * Uses existing driver IOCTLs (no driver rebuild needed):
 *   INIT_HW   0x80000B80
 *   READ_REG  0x80000B88   {UINT32 Off; UINT32 Val;}
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

#define IOCTL_INIT_HW   0x80000B80
#define IOCTL_READ_REG  0x80000B88
#define AMDBC250_INIT_FLAG_NBIO_MAP 0x00000001

static HANDLE gH;
static UINT32 R(UINT32 off) {
    struct { UINT32 o; UINT32 v; } in = { off, 0 }, out = { 0, 0 };
    DWORD br = 0;
    if (DeviceIoControl(gH, IOCTL_READ_REG, &in, 8, &out, 8, &br, NULL))
        return out.v;
    return 0xFFFFFFFF;
}

int main(void) {
    printf("=== BC-250 DCN Live-Register Scan ===\n");
    printf("(scans for registers that CHANGE between reads ~40ms apart)\n\n");

    gH = CreateFileW(L"\\\\.\\AMDBC250DreamV43",
                      GENERIC_READ | GENERIC_WRITE, 0, NULL,
                      OPEN_EXISTING, 0, NULL);
    if (gH == INVALID_HANDLE_VALUE) {
        printf("FAIL: cannot open GPU driver (err=%lu)\n", GetLastError());
        return 1;
    }

    UCHAR ib[32] = { 0 };
    *(UINT64*)(ib + 0)  = 0xFE800000ULL;
    *(UINT32*)(ib + 8)  = 0x00080000;
    *(UINT32*)(ib + 12) = AMDBC250_INIT_FLAG_NBIO_MAP;
    *(UINT64*)(ib + 16) = 0xC0000000ULL;
    *(UINT32*)(ib + 24) = 0x20000000;
    DWORD br = 0;
    DeviceIoControl(gH, IOCTL_INIT_HW, ib, sizeof(ib), NULL, 0, &br, NULL);
    printf("[ok] INIT_HARDWARE NBIO_MAP\n");

    /* Two passes over the DCN region 0x0000-0x9000, step 4.
     * Report registers that change. Group by 0x100 block for readability. */
    printf("\n--- Pass 1: snapshot ---\n");
    UINT32 *a = malloc((0x9000/4) * sizeof(UINT32));
    UINT32 *b = malloc((0x9000/4) * sizeof(UINT32));
    if (!a || !b) { printf("alloc fail\n"); return 1; }

    for (int i = 0; i < 0x9000/4; i++) a[i] = R(i*4);
    Sleep(300);
    for (int i = 0; i < 0x9000/4; i++) b[i] = R(i*4);

    printf("Live registers (changed between reads):\n");
    int count = 0;
    int lastblock = -1;
    for (int i = 0; i < 0x9000/4; i++) {
        if (a[i] != b[i]) {
            int block = i/64; /* 0x100 per block */
            if (block != lastblock) {
                printf("\n  --- block 0x%04X ---\n", block*0x100);
                lastblock = block;
            }
            printf("  [0x%04X] 0x%08X -> 0x%08X\n", i*4, a[i], b[i]);
            count++;
        }
    }
    printf("\nTotal live registers: %d\n", count);

    /* Also report any non-zero, non-0xFFFFFFFF registers in DCN block
     * (0x5000-0x9000) from the first snapshot — they indicate configured
     * display state even if static. */
    printf("\n--- Configured static registers in 0x4000-0x9000 ---\n");
    int s = 0;
    for (int i = 0x4000/4; i < 0x9000/4; i++) {
        if (a[i] != 0 && a[i] != 0xFFFFFFFF) {
            if (s < 60) printf("  [0x%04X] = 0x%08X\n", i*4, a[i]);
            s++;
        }
    }
    printf("Total configured regs in 0x4000-0x9000: %d\n", s);

    free(a); free(b);
    CloseHandle(gH);
    printf("\n=== Done ===\n");
    return 0;
}
