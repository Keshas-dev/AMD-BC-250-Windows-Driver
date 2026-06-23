/* safe-init-test.c — Init without NBIO_MAP, then read registers */
/* flag=0: maps BAR5, enables PCI, does NOT touch CU unlock or PSP KIQ init */

#include <windows.h>
#include <stdio.h>

#define IOCTL_GPU_READ  0x80000B88
#define IOCTL_GPU_INIT  0x80000B80

static HANDLE h;

static ULONG R(ULONG off) {
    UCHAR buf[8] = {0};
    *(ULONG*)(buf+0) = off;
    *(ULONG*)(buf+4) = 0xDEADBEEF;
    DWORD br = 0;
    if (!DeviceIoControl(h, IOCTL_GPU_READ, buf, 8, buf, 8, &br, NULL))
        return 0xBAD0C0DE;
    return *(ULONG*)(buf+4);
}

static BOOL InitSafe(void) {
    UCHAR init[16] = {0};             /* old 16B struct (no FB fields) */
    *(unsigned __int64*)(init+0) = 0xFE800000ULL;  /* BAR5 physical */
    *(unsigned*)(init+8) = 0x00080000;              /* 512KB */
    *(unsigned*)(init+12) = 0;                      /* flag=0 (no NBIO_MAP) */
    DWORD br = 0;
    return DeviceIoControl(h, IOCTL_GPU_INIT, init, sizeof(init), NULL, 0, &br, NULL);
}

int main(void) {
    printf("=== Safe Init + Register List ===\n");

    h = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
                    0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("FAIL: Cannot open (err=%lu)\n", GetLastError());
        return 1;
    }
    printf("Device opened\n");

    if (!InitSafe()) {
        printf("FAIL: InitDriver (flag=0) failed\n");
        CloseHandle(h);
        return 1;
    }
    printf("Init OK (flag=0)\n\n");

    /* Quick ID check */
    ULONG gpuId = R(0x0000);
    printf("GPU_ID       = 0x%08X\n", gpuId);
    printf("SCRATCH      = 0x%08X\n", R(0x32D4));
    printf("GRBM_STATUS  = 0x%08X\n", R(0x3260));
    printf("CC_ARRAY     = 0x%08X\n", R(0x3264));
    printf("GRBM_GFX_IDX = 0x%08X (read only, no write)\n", R(0x34D0));
    
    /* Simple register list - all reads only */
    typedef struct { ULONG off; const char* name; } Reg;
    
    Reg regs[] = {
        /* Status */
        {0x1264, "GC_REV_ID"},
        {0x1268, "GC_HW_ID"},
        {0x126C, "GC_IP_VERSION"},
        {0x3268, "GRBM_STATUS2"},
        {0x326C, "GRBM_STATUS_SE0"},
        {0x3270, "GRBM_STATUS_SE1"},
        {0x32D0, "GRBM_SOFT_RESET"},
        {0x32D4, "SCRATCH"},
        {0x32D8, "SCRATCH1"},
        {0x32DC, "SCRATCH2"},
        {0x32E0, "SCRATCH3"},
        
        /* GFX pipeline */
        {0x3400, "CP_STRMER"},
        {0x3404, "CP_ME_CNTL"},
        {0x3408, "CP_ME_RAM"},
        
        /* CP config */
        {0x4A00, "CP_INT_CNTL"},
        {0x4A04, "CP_INT_STATUS"},
        {0x4A10, "CP_MAX_CONTEXT"},
        {0x4A74, "ME_CNTL"},
        
        /* Ring base */
        {0xDA60, "GFX_RING0_BASE_LO"},
        {0xDA64, "GFX_RING0_BASE_HI"},
        {0xDA68, "GFX_RING0_CNTL"},
        {0xDA6C, "GFX_RING0_RPTR"},
        {0xDA70, "GFX_RING0_RPTR_HI"},
        {0xDA74, "GFX_RING0_WPTR_HI"},
        {0xDA78, "GFX_RING0_WPTR"},
        {0xDA7C, "GFX_RING0_DOORBELL"},
        
        /* KIQ ring */
        {0xE060, "KIQ_BASE_LO"},
        {0xE064, "KIQ_BASE_HI"},
        {0xE068, "KIQ_CNTL"},
        {0xE06C, "KIQ_RPTR"},
        {0xE070, "KIQ_RPTR_HI"},
        {0xE074, "KIQ_WPTR_HI"},
        {0xE078, "KIQ_WPTR"},
        {0xE07C, "KIQ_DOORBELL"},
        
        /* HQD block */
        {0xDAC0, "HQD_ACTIVE"},
        {0xDAC4, "HQD_VMID"},
        {0xDAC8, "HQD_PERSISTENT"},
        {0xDAD0, "HQD_PQ_RPTR"},
        {0xDAD4, "HQD_PQ_WPTR"},
        {0xDAD8, "HQD_PQ_BASE_LO"},
        {0xDADC, "HQD_PQ_BASE_HI"},
        {0xDAE0, "HQD_PQ_CNTL"},
        {0xDAE4, "HQD_PQ_DOORBELL"},
        {0xDB90, "COMPUTE_PQ_WPTR_LO"},
        {0xDB94, "COMPUTE_PQ_WPTR_HI"},
        
        /* GCVM */
        {0x0B300, "GCVM_L2_TLB_TAG"},
        {0x0B360, "GCVM_L2_CNTL"},
        {0x0B408, "GCVM_CTX0_REGS0"},
        {0x0B404, "GCVM_CTX0_BASE"},
        {0x0B460, "GCVM_CTX0_CNTL"},
        {0x0B608, "GCVM_PT_BASE_LO"},
        {0x0B60C, "GCVM_PT_BASE_HI"},
        
        /* NBIO */
        {0xC100, "NBIO_ID"},
        {0xC104, "NBIO_STATUS"},
        
        /* FW IC */
        {0x17360, "PFP_IC_BASE_LO"},
        {0x17364, "PFP_IC_BASE_HI"},
        {0x17370, "ME_IC_BASE_LO"},
        {0x17374, "ME_IC_BASE_HI"},
        {0x17380, "CE_IC_BASE_LO"},
        {0x17384, "CE_IC_BASE_HI"},
        
        /* Fence/Doorbell addrs in CP */
        {0x3AD8, "CP_FENCE_ADDR_LO"},
        {0x3ADC, "CP_FENCE_ADDR_HI"},
        {0x3AE0, "CP_DOORBELL_ADDR_LO"},
        {0x3AE4, "CP_DOORBELL_ADDR_HI"},
        {0x3AE8, "CP_SEM_ADDR_LO"},
        {0x3AEC, "CP_SEM_ADDR_HI"},
    };
    
    for (int i = 0; i < sizeof(regs)/sizeof(regs[0]); i++) {
        ULONG val = R(regs[i].off);
        if (val == 0xFFFFFFFF)
            printf("  %-22s [0x%04X] = 0xFFFFFFFF (dead)\n", regs[i].name, regs[i].off);
        else
            printf("  %-22s [0x%04X] = 0x%08X\n", regs[i].name, regs[i].off, val);
    }

    CloseHandle(h);
    printf("\n=== OK ===\n");
    return 0;
}