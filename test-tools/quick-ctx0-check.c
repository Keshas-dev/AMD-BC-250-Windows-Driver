#include <windows.h>
#include <stdio.h>

#define IOCTL_AMDBC250_INIT_HARDWARE  0x80000B80
#define IOCTL_AMDBC250_READ_REG       0x80000B88

typedef struct { UINT32 Offset; UINT32 Value; } REG_OP;
static HANDLE g_h = INVALID_HANDLE_VALUE;

static UINT32 R(UINT32 off) {
    REG_OP in = {off,0}, out = {0}; DWORD br = 0;
    DeviceIoControl(g_h, IOCTL_AMDBC250_READ_REG, &in, sizeof(in), &out, sizeof(out), &br, NULL);
    return out.Value;
}

int main() {
    printf("=== Quick Context0 check (BEFORE INIT_HARDWARE) ===\n");
    g_h = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (g_h == INVALID_HANDLE_VALUE) { printf("Cannot open\n"); return 1; }

    /* Read Context0 WITHOUT INIT_HARDWARE first */
    printf("\n--- BEFORE INIT_HARDWARE ---\n");
    for (UINT32 off = 0x0B400; off <= 0x0B4FC; off += 4) {
        UINT32 val = R(off);
        if (val != 0) printf("  0x%05X: 0x%08X\n", off, val);
    }
    printf("(zeros omitted)\n");

    /* Now do INIT_HARDWARE */
    UCHAR ii[32]={0}; *(UINT64*)ii=0xFE800000ULL; *(UINT32*)(ii+8)=0x80000; *(UINT32*)(ii+12)=1;
    *(UINT64*)(ii+16)=0xC0000000ULL; *(UINT32*)(ii+24)=0x10000000;
    UCHAR io[32]={0}; DWORD br=0;
    DeviceIoControl(g_h, IOCTL_AMDBC250_INIT_HARDWARE, ii, 32, io, 32, &br, NULL);

    printf("\n--- AFTER INIT_HARDWARE ---\n");
    for (UINT32 off = 0x0B400; off <= 0x0B4FC; off += 4) {
        UINT32 val = R(off);
        if (val != 0) printf("  0x%05X: 0x%08X\n", off, val);
    }
    printf("(zeros omitted)\n");

    /* Also check L2 TLB region */
    printf("\n--- L2 TLB region ---\n");
    for (UINT32 off = 0x0B31C; off <= 0x0B36C; off += 4) {
        printf("  0x%05X: 0x%08X\n", off, R(off));
    }

    CloseHandle(g_h);
    return 0;
}
