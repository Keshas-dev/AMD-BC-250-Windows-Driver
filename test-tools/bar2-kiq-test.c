/* bar2-kiq-test.c — Test KIQ ring allocated in VRAM (BAR2) */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

#define IOCTL_GPU_READ  0x80000B88
#define IOCTL_GPU_WRITE 0x80000B8C
#define IOCTL_GPU_INIT  0x80000B80
#define IOCTL_GPU_SEND_PM4 0x80000BC4
#define IOCTL_GPU_LOAD_CP_FW 0x80000BD4

static HANDLE hGpu;
static ULONG R(ULONG off) {
    UCHAR b[8] = {0}; *(ULONG*)b = off; *(ULONG*)(b+4) = 0xBAD0C0DE;
    DWORD br = 0;
    if (!DeviceIoControl(hGpu, IOCTL_GPU_READ, b, 8, b, 8, &br, NULL) || br < 8) return 0xBAD0C0DE;
    return *(ULONG*)(b+4);
}
static void W(ULONG off, ULONG v) {
    UCHAR b[8] = {0}; *(ULONG*)b = off; *(ULONG*)(b+4) = v;
    DWORD br = 0; DeviceIoControl(hGpu, IOCTL_GPU_WRITE, b, 8, NULL, 0, &br, NULL); Sleep(2);
}

int main(void) {
    hGpu = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE, 0,
                       NULL, OPEN_EXISTING, 0, NULL);
    if (hGpu == INVALID_HANDLE_VALUE) { printf("FAIL err=%lu\n", GetLastError()); return 1; }

    /* 1. INIT_HARDWARE with BAR2 (VRAM at 0xC0000000, 512MB) */
    struct { uint64_t mmioBase; uint32_t mmioSize; uint32_t flags;
             uint64_t fbBase; uint32_t fbSize; } init = {
        0xFE800000ULL, 0x80000, 1, 0xC0000000ULL, 0x20000000 };
    DWORD br = 0;
    DeviceIoControl(hGpu, IOCTL_GPU_INIT, &init, sizeof(init), NULL, 0, &br, NULL);
    printf("INIT_HARDWARE done\n");

    /* 2. Restore GRBM broadcast */
    W(0x34D0, 0xE0000000);

    /* 3. Read KIQ registers first */
    ULONG kiqBaseLo = R(0xE060);
    ULONG kiqBaseHi = R(0xE064);
    ULONG kiqSize   = R(0xE068);
    ULONG kiqActive = R(0xE080);
    ULONG kiqWPtr   = R(0xE078);
    ULONG kiqRPtr   = R(0xE06C);

    printf("\n=== KIQ Register State ===\n");
    printf("KIQ_BASE_LO  = 0x%08X\n", kiqBaseLo);
    printf("KIQ_BASE_HI  = 0x%08X\n", kiqBaseHi);
    printf("KIQ_SIZE     = 0x%08X (read-only)\n", kiqSize);
    printf("KIQ_ACTIVE   = 0x%08X\n", kiqActive);
    printf("KIQ_WPTR     = 0x%08X\n", kiqWPtr);
    printf("KIQ_RPTR     = 0x%08X\n", kiqRPtr);

    /* 4. Try to write KIQ registers */
    printf("\n=== KIQ Writability Check ===\n");
    ULONG physLo = 0xC0000000;  /* Start of VRAM */
    ULONG physHi = 0;
    W(0xE060, 0xA5A5A5A5); ULONG r1 = R(0xE060); W(0xE060, physLo);
    W(0xE064, 0xA5A5A5A5); ULONG r2 = R(0xE064); W(0xE064, physHi);
    W(0xE070, 0xA5A5A5A5); ULONG r3 = R(0xE070);
    W(0xE080, 0x00000000); /* deactivate first */
    Sleep(10);
    printf("KIQ_BASE_LO write test: 0xA5A5A5A5 -> 0x%08X (%s)\n", r1, r1 == 0xA5A5A5A5 ? "WRITABLE" : "READ-ONLY");
    printf("KIQ_BASE_HI write test: 0xA5A5A5A5 -> 0x%08X (%s)\n", r2, r2 == 0xA5A5A5A5 ? "WRITABLE" : "READ-ONLY");
    printf("KIQ_PQ_CTL  write test: 0xA5A5A5A5 -> 0x%08X (%s)\n", r3, r3 == 0xA5A5A5A5 ? "WRITABLE" : "READ-ONLY");

    /* 5. Restore KIQ_BASE to VRAM address */
    W(0xE064, physHi);
    W(0xE060, physLo);
    W(0xE06C, 0);  /* RPTR = 0 */
    printf("\nKIQ_BASE set to VRAM 0xC0000000\n");

    /* 6. Set KIQ_SIZE — read-only, so this is informational */
    W(0xE068, 0x200);  /* 512 dwords = 2048 bytes */
    ULONG sizeCheck = R(0xE068);
    printf("KIQ_SIZE write 0x200 -> read 0x%08X (%s)\n", sizeCheck,
           sizeCheck == 0x200 ? "WRITABLE!" : "READ-ONLY");

    /* 7. Read BAR2 (VRAM) first few bytes via READ_REG (BAR5 proxy) */
    printf("\n=== VRAM Probe via IOCTL ===\n");
    /* VRAM at 0xC0000000 is outside BAR5. READ_REG can only read BAR5.
     * We need a separate way to access VRAM. Try via BAR5 registers
     * that might give VRAM info. */
    ULong vramInfo[] = {
        R(0x1A00), R(0x1A04), R(0x1A08), R(0x1A0C),  /* MMHUB */
        R(0x3810), R(0x3814), /* BAR-related? */
        R(0x0E00), R(0x0E04), R(0x0E08),  /* GPU_ID area */
    };
    printf("VRAM info regs:");
    for (int i = 0; i < 9; i++) printf(" 0x%08X", vramInfo[i]);
    printf("\n");

    /* 8. Try KIQ setup with VRAM address — deactivate first */
    printf("\n=== KIQ via VRAM ===\n");
    W(0xE080, 0);  /* deactivate */
    Sleep(10);
    W(0xE064, physHi);
    W(0xE060, physLo);
    W(0xE06C, 0);  /* RPTR */
    W(0xE080, 1);  /* activate */
    Sleep(10);
    ULONG activeCheck = R(0xE080);
    printf("KIQ_ACTIVE after set=1: 0x%08X\n", activeCheck);

    /* 9. Write a NOP to VRAM (offset 0 = at 0xC0000000) and try to submit */
    /* Since we can't write to VRAM directly via IOCTL (WRITE_REG writes BAR5 only),
     * and we don't have BAR2 mapped in user space, we skip the actual ring write.
     * The KIQ will have BASE = 0xC0000000 but ring memory is unmapped. */

    /* 10. Cleanup — restore original state */
    W(0xE080, 0);
    W(0xE064, kiqBaseHi);
    W(0xE060, kiqBaseLo);
    W(0xE080, kiqActive);

    /* Restore GRBM broadcast */
    W(0x34D0, 0xE0000000);

    CloseHandle(hGpu);
    printf("\nDone. Reboot recommended.\n");
    return 0;
}
