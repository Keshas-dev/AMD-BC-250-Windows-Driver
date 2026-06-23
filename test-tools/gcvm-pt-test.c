/* gcvm-pt-test.c — Test GCVM page table setup IOCTL (0x8000098C) */

#include <windows.h>
#include <stdio.h>

#define IOCTL_GPU_READ      0x80000B88
#define IOCTL_GPU_WRITE     0x80000B8C
#define IOCTL_GPU_INIT      0x80000B80
#define IOCTL_GCVM_PT_SETUP 0x8000098C

static HANDLE h;

static ULONG R(ULONG off) {
    UCHAR buf[8] = {0};
    *(ULONG*)(buf+0) = off;
    *(ULONG*)(buf+4) = 0xBAD0C0DE;
    DWORD br = 0;
    if (!DeviceIoControl(h, IOCTL_GPU_READ, buf, 8, buf, 8, &br, NULL))
        return 0xBAD0C0DE;
    return *(ULONG*)(buf+4);
}

static void Init(void) {
    UCHAR init[32] = {0};
    *(unsigned __int64*)(init+0) = 0xFE800000ULL;
    *(unsigned*)(init+8) = 0x00080000;
    *(unsigned*)(init+12) = 1;
    *(unsigned __int64*)(init+16) = 0xC0000000ULL;
    *(unsigned*)(init+24) = 0x20000000;
    DWORD br = 0;
    DeviceIoControl(h, IOCTL_GPU_INIT, init, sizeof(init), NULL, 0, &br, NULL);
}

typedef struct {
    ULONG CtxCntlBefore;
    ULONG RingBaseLo;
    ULONG RingBaseHi;
    ULONG PtBase0LoBefore;
    ULONG PtBase0HiBefore;
    ULONG PtBase0LoAfter;
    ULONG PtBase0HiAfter;
    ULONG PtBaseLoAfter;
    ULONG PtBaseHiAfter;
    ULONG Result;
} GCVM_PT_RESP;

int main(void) {
    printf("=== GCVM Page Table Setup Test ===\n");

    h = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
                    0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("FAIL: Cannot open (err=%lu)\n", GetLastError());
        return 1;
    }

    Init();
    printf("Init OK\n\n");

    /* Read before state */
    printf("--- Before ---\n");
    printf("  CTX0_CNTL    (0xB460) = 0x%08X\n", R(0x0B460));
    printf("  PT_BASE0_LO  (0xB408) = 0x%08X\n", R(0x0B408));
    printf("  PT_BASE0_HI  (0xB40C) = 0x%08X\n", R(0x0B40C));
    printf("  PT_BASE_LO   (0xB608) = 0x%08X\n", R(0x0B608));
    printf("  PT_BASE_HI   (0xB60C) = 0x%08X\n", R(0x0B60C));
    printf("  KIQ_BASE_LO  (0xE060) = 0x%08X\n", R(0xE060));
    printf("  KIQ_BASE_HI  (0xE064) = 0x%08X\n", R(0xE064));

    /* Call GCVM page table setup */
    printf("\n--- GCVM_PT_SETUP IOCTL ---\n");
    GCVM_PT_RESP resp = {0};
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_GCVM_PT_SETUP, NULL, 0, &resp, sizeof(resp), &br, NULL);
    printf("  IOCTL: %s (br=%lu)\n", ok ? "OK" : "FAILED", br);

    if (ok && br >= sizeof(resp)) {
        printf("\n=== Response ===\n");
        printf("  CTX0_CNTL (before):    0x%08X\n", resp.CtxCntlBefore);
        printf("  KIQ_BASE_LO:           0x%08X\n", resp.RingBaseLo);
        printf("  KIQ_BASE_HI:           0x%08X\n", resp.RingBaseHi);
        printf("  Ring PA:               0x%llX\n",
               ((ULONG64)resp.RingBaseHi << 32) | resp.RingBaseLo);
        printf("  PT_BASE0_LO (before):  0x%08X\n", resp.PtBase0LoBefore);
        printf("  PT_BASE0_HI (before):  0x%08X\n", resp.PtBase0HiBefore);
        printf("  PT_BASE0_LO (after):   0x%08X\n", resp.PtBase0LoAfter);
        printf("  PT_BASE0_HI (after):   0x%08X\n", resp.PtBase0HiAfter);
        printf("  PT_BASE_LO  (after):   0x%08X\n", resp.PtBaseLoAfter);
        printf("  PT_BASE_HI  (after):   0x%08X\n", resp.PtBaseHiAfter);

        printf("\n  Result: 0x%08X — ", resp.Result);
        switch (resp.Result) {
            case 0xCAFEBABE: printf("SHADOW COPY WORKED! PT_BASE populated!\n"); break;
            case 0x600DCAFE: printf("PT_BASE0 set OK but PT_BASE still 0 (no shadow)\n"); break;
            case 0xDEADBEEF: printf("Hardware rejected page table write\n"); break;
            default:         printf("Error code: 0x%08X\n", resp.Result); break;
        }
    }

    /* Read after state */
    printf("\n--- After (direct read) ---\n");
    printf("  CTX0_CNTL    (0xB460) = 0x%08X\n", R(0x0B460));
    printf("  PT_BASE0_LO  (0xB408) = 0x%08X\n", R(0x0B408));
    printf("  PT_BASE0_HI  (0xB40C) = 0x%08X\n", R(0x0B40C));
    printf("  PT_BASE_LO   (0xB608) = 0x%08X\n", R(0x0B608));
    printf("  PT_BASE_HI   (0xB60C) = 0x%08X\n", R(0x0B60C));

    CloseHandle(h);
    printf("\n=== Done ===\n");
    return 0;
}
