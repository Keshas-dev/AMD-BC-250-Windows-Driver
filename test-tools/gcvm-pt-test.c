/* gcvm-pt-test.c — Test GCVM page table setup IOCTL (0x8000098C) */

#include <windows.h>
#include <stdio.h>

#define IOCTL_GPU_READ      0x80000B88
#define IOCTL_GPU_WRITE     0x80000B8C
#define IOCTL_GPU_INIT      0x80000B80
#define IOCTL_GCVM_PT_SETUP 0x8000098C
#define PSP_DEVICE          L"\\\\.\\AmdBcPsp"
#define PSP_IOCTL_INIT      CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define PSP_IOCTL_KIQ       CTL_CODE(FILE_DEVICE_UNKNOWN, 0x818, METHOD_BUFFERED, FILE_ANY_ACCESS)

static HANDLE hGpu, hPsp = INVALID_HANDLE_VALUE;

typedef struct {
    ULONG CommandCount;
    ULONG Reserved[3];
    ULONG Commands[64];
} PSP_KIQ_REQ;

static ULONG R(ULONG off) {
    UCHAR buf[8] = {0};
    *(ULONG*)(buf+0) = off;
    *(ULONG*)(buf+4) = 0xBAD0C0DE;
    DWORD br = 0;
    if (!DeviceIoControl(hGpu, IOCTL_GPU_READ, buf, 8, buf, 8, &br, NULL))
        return 0xBAD0C0DE;
    return *(ULONG*)(buf+4);
}

static void GpuInit(void) {
    UCHAR init[32] = {0};
    *(unsigned __int64*)(init+0) = 0xFE800000ULL;
    *(unsigned*)(init+8) = 0x00080000;
    *(unsigned*)(init+12) = 1;
    *(unsigned __int64*)(init+16) = 0xC0000000ULL;
    *(unsigned*)(init+24) = 0x20000000;
    DWORD br = 0;
    DeviceIoControl(hGpu, IOCTL_GPU_INIT, init, sizeof(init), NULL, 0, &br, NULL);
}

static void PspInit(void) {
    struct { unsigned __int64 PA; unsigned size; } req = {0xFE800000ULL, 0x00080000};
    DWORD br = 0;
    DeviceIoControl(hPsp, PSP_IOCTL_INIT, &req, sizeof(req), NULL, 0, &br, NULL);
}

static void PspKiqNop(void) {
    PSP_KIQ_REQ req;
    ZeroMemory(&req, sizeof(req));
    req.CommandCount = 1;
    req.Commands[0] = 0xC0001000; /* IT_NOP */
    DWORD br = 0;
    DeviceIoControl(hPsp, PSP_IOCTL_KIQ, &req, sizeof(req), NULL, 0, &br, NULL);
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
    ULONG PtPhysLo[3];
    ULONG PtPhysHi[3];
    ULONG InvStatus;
    ULONG KIQ_WPTR;
    ULONG KIQ_RPTR;
    ULONG Reserved[8];
} GCVM_PT_RESP;

int main(void) {
    printf("=== GCVM Page Table Setup Test ===\n");

    hGpu = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
                    0, NULL, OPEN_EXISTING, 0, NULL);
    if (hGpu == INVALID_HANDLE_VALUE) {
        printf("FAIL: Cannot open GPU (err=%lu)\n", GetLastError());
        return 1;
    }
    hPsp = CreateFileW(PSP_DEVICE, GENERIC_READ|GENERIC_WRITE,
                    0, NULL, OPEN_EXISTING, 0, NULL);
    if (hPsp == INVALID_HANDLE_VALUE) {
        printf("FAIL: Cannot open PSP (err=%lu)\n", GetLastError());
        CloseHandle(hGpu);
        return 1;
    }

    GpuInit();
    PspInit();
    printf("Init OK\n");

    /* Read KIQ state before triggering init */
    printf("\n--- Before KIQ Init ---\n");
    printf("  KIQ_BASE_LO  (0xE060) = 0x%08X\n", R(0xE060));
    printf("  KIQ_WPTR     (0xE078) = 0x%08X\n", R(0xE078));
    printf("  KIQ_RPTR     (0xE06C) = 0x%08X\n", R(0xE06C));

    /* Trigger PSP KIQ init by sending a NOP submit */
    printf("\n--- Triggering PSP KIQ Init ---\n");
    PspKiqNop();
    printf("  PSP KIQ submit sent\n");

    /* Check KIQ state after init */
    printf("\n--- After KIQ Init ---\n");
    ULONG kiqBaseLo = R(0xE060);
    ULONG kiqBaseHi = R(0xE064);
    ULONG kiqWptr = R(0xE078);
    ULONG kiqRptr = R(0xE06C);
    printf("  KIQ_BASE_LO  (0xE060) = 0x%08X\n", kiqBaseLo);
    printf("  KIQ_BASE_HI  (0xE064) = 0x%08X\n", kiqBaseHi);
    printf("  KIQ_WPTR     (0xE078) = 0x%08X\n", kiqWptr);
    printf("  KIQ_RPTR     (0xE06C) = 0x%08X\n", kiqRptr);

    if (kiqBaseLo == 0) {
        printf("\n*** KIQ still not initialized after NOP. Trying more submits...\n");
        for (int i = 0; i < 5; i++) {
            PspKiqNop();
            Sleep(100);
            kiqBaseLo = R(0xE060);
            if (kiqBaseLo) {
                kiqBaseHi = R(0xE064);
                printf("  KIQ init triggered after submit #%d: BASE=0x%08X_%08X\n", i+1, kiqBaseHi, kiqBaseLo);
                break;
            }
        }
    }

    /* Read full before state */
    printf("\n--- Before ---\n");
    printf("  CTX0_CNTL    (0xB460) = 0x%08X\n", R(0x0B460));
    printf("  PT_BASE0_LO  (0xB408) = 0x%08X\n", R(0x0B408));
    printf("  PT_BASE0_HI  (0xB40C) = 0x%08X\n", R(0x0B40C));
    printf("  KIQ_WPTR     (0xE078) = 0x%08X\n", R(0xE078));
    printf("  KIQ_RPTR     (0xE06C) = 0x%08X\n", R(0xE06C));

    /* Call GCVM page table setup */
    printf("\n--- GCVM_PT_SETUP IOCTL ---\n");
    GCVM_PT_RESP resp;
    DWORD br = 0;
    RtlZeroMemory(&resp, sizeof(resp));
    BOOL ok = DeviceIoControl(hGpu, IOCTL_GCVM_PT_SETUP, NULL, 0, &resp, sizeof(resp), &br, NULL);
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
        printf("  PT root PA:             0x%08X_%08X\n", resp.PtPhysHi[0], resp.PtPhysLo[0]);
        printf("  PT mid  PA:             0x%08X_%08X\n", resp.PtPhysHi[1], resp.PtPhysLo[1]);
        printf("  PT leaf PA:             0x%08X_%08X\n", resp.PtPhysHi[2], resp.PtPhysLo[2]);
        printf("  KIQ_WPTR (in driver):   0x%08X\n", resp.KIQ_WPTR);
        printf("  KIQ_RPTR (in driver):   0x%08X\n", resp.KIQ_RPTR);
        printf("  InvStatus:              0x%08X (%s)\n", resp.InvStatus,
               resp.InvStatus ? "TLB flushed" : "TIMEOUT/FAIL");

        printf("\n  Result: 0x%08X — ", resp.Result);
        switch (resp.Result) {
            case 0xCAFEBABE: printf("GCVM PT SETUP OK! Page tables active\n"); break;
            case 0xBAD0BAD0: printf("Wrong page table depth in CTX0_CNTL\n"); break;
            case 0xBAD0C0DE: printf("KIQ ring not initialized (KIQ_BASE=0)\n"); break;
            case 0xDEADF00D: printf("Failed to allocate page table pages\n"); break;
            default:         printf("Error code: 0x%08X\n", resp.Result); break;
        }
    }

    /* Read after state */
    printf("\n--- After (direct read) ---\n");
    printf("  CTX0_CNTL    (0xB460) = 0x%08X\n", R(0x0B460));
    printf("  PT_BASE0_LO  (0xB408) = 0x%08X\n", R(0x0B408));
    printf("  PT_BASE0_HI  (0xB40C) = 0x%08X\n", R(0x0B40C));
    printf("  KIQ_WPTR     (0xE078) = 0x%08X\n", R(0xE078));
    printf("  KIQ_RPTR     (0xE06C) = 0x%08X\n", R(0xE06C));

    /* Check CP status */
    printf("\n--- CP Status ---\n");
    printf("  ME_CNTL      (0x4A74) = 0x%08X\n", R(0x4A74));
    printf("  GRBM_STATUS  (0x3264) = 0x%08X\n", R(0x3264));
    printf("  KIQ_WPTR     (0xE078) = 0x%08X\n", R(0xE078));
    printf("  KIQ_RPTR     (0xE06C) = 0x%08X\n", R(0xE06C));

    /* Submit more PM4 and poll RPTR */
    printf("\n--- Polling RPTR after extra submits ---\n");
    for (int i = 0; i < 10; i++) {
        PspKiqNop();
        Sleep(50);
        ULONG rptr = R(0xE06C);
        ULONG wptr = R(0xE078);
        printf("  submit #%d: WPTR=0x%04X RPTR=0x%04X\n", i+1, wptr, rptr);
        if (rptr > 0) {
            printf("  >>> RPTR ADVANCED! CP is processing the ring!\n");
            break;
        }
    }

    if (hPsp != INVALID_HANDLE_VALUE) CloseHandle(hPsp);
    CloseHandle(hGpu);
    printf("\n=== Done ===\n");
    return 0;
}
