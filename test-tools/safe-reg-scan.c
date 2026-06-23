/* safe-reg-scan.c — Read-only register scan + safe SCRATCH writes */
/* NO writes to GRBM_GFX_INDEX, GCVM registers, or any unknown registers */

#include <windows.h>
#include <stdio.h>
#include <string.h>

#define IOCTL_GPU_READ  0x80000B88
#define IOCTL_GPU_WRITE 0x80000B8C
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

static BOOL W(ULONG off, ULONG val) {
    UCHAR buf[8] = {0};
    *(ULONG*)(buf+0) = off;
    *(ULONG*)(buf+4) = val;
    DWORD br = 0;
    return DeviceIoControl(h, IOCTL_GPU_WRITE, buf, 8, buf, 8, &br, NULL);
}

static void InitDriver(void) {
    UCHAR init[32] = {0};
    *(unsigned __int64*)(init+0) = 0xFE800000ULL;
    *(unsigned*)(init+8) = 0x00080000;
    *(unsigned*)(init+12) = 1;
    *(unsigned __int64*)(init+16) = 0xC0000000ULL;
    *(unsigned*)(init+24) = 0x10000000;
    DWORD br = 0;
    DeviceIoControl(h, IOCTL_GPU_INIT, init, sizeof(init), NULL, 0, &br, NULL);
}

/* Known register blocks to scan (GC_BASE-shifted offsets) */
typedef struct {
    ULONG start;
    ULONG end;  /* inclusive */
    const char* name;
} RegBlock;

int main(void) {
    printf("=== Safe Register Scan ===\n");
    printf("Mode: READ-ONLY (no writes except SCRATCH)\n\n");

    h = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
                    0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("Cannot open driver (err=%lu)\n", GetLastError());
        return 1;
    }

    InitDriver();
    printf("Driver initialized.\n\n");

    /* Quick hardware ID check */
    ULONG hwId = R(0x0000);
    ULONG revId = R(0x1264);
    ULONG grbmStatus = R(0x3260);
    ULONG ccScArray = R(0x3264);
    printf("GPU_ID         = 0x%08X\n", hwId);
    printf("GC_REV_ID      = 0x%08X\n", revId);
    printf("GRBM_STATUS    = 0x%08X\n", grbmStatus);
    printf("CC_SC_ARRAY    = 0x%08X\n", ccScArray);
    printf("SCRATCH        = 0x%08X\n\n", R(0x32D4));

    /* Scan key register blocks */
    RegBlock blocks[] = {
        /* GC Base registers */
        {0x0000, 0x0004, "GPU_ID"},
        {0x1260, 0x126C, "GC_BASE+IP"},
        
        /* GRBM */
        {0x3260, 0x3270, "GRBM"},
        {0x32D0, 0x32E0, "SCRATCH/GRBM_GFX_INDEX"},
        {0x3400, 0x3500, "GFX pipeline (sparse)"},
        
        /* CP registers (GC_BASE-shifted) */
        /* These are the actual GC_BASE-shifted offsets for CP: 
           Linux CP_* + 0x1260, converted: DWORD offset = (Linux_offset/4) + 0x498 */
        {0x3460, 0x3470, "CP_STATUS (sparse scan)"},
        {0x3AD8, 0x3AEC, "CP_FENCE/DOORBELL"},
        {0x4A00, 0x4B00, "CP/ME block"},
        
        /* Ring registers */
        {0xDA60, 0xDA80, "GFX RING0"},
        {0xDB60, 0xDB80, "COMPUTE RING"},
        {0xE060, 0xE080, "KIQ RING"},
        
        /* HQD registers (seem to be at different offsets) */
        {0xDAC0, 0xDAE0, "HQD (KIQ)"},
        {0xDB90, 0xDBA0, "HQD (COMPUTE)"},
        
        /* GCVM */
        {0x0B300, 0x0B370, "GCVM L2/TLB"},
        {0x0B400, 0x0B4E0, "GCVM CONTEXT0"},
        {0x0B600, 0x0B620, "GCVM CONTEXT0_PT_BASE"},
        
        /* Firmware IC base */
        {0x17360, 0x17390, "CP FW IC_BASE"},
        
        /* NBIO */
        {0xC000, 0xC200, "NBIO"},
        
        /* HDP */
        {0x0F20, 0x0F40, "HDP"},
        
        /* RLC */
        {0x4CB0, 0x4CC0, "RLC"},
        
        /* UMC */
        {0x14000, 0x14100, "UMC (sparse)"},
        
        /* SDMA - might be at different offsets */
        {0x3060, 0x30A0, "SDMA (speculative)"},
    };

    int blockCount = sizeof(blocks) / sizeof(blocks[0]);
    
    for (int bi = 0; bi < blockCount; bi++) {
        RegBlock* blk = &blocks[bi];
        printf("--- %s (0x%04X-0x%04X) ---\n", blk->name, blk->start, blk->end);
        
        /* Print up to 16 values per block, spaced by 0x10 */
        ULONG step = 0x10;
        if (blk->end - blk->start <= 0x20) step = 4;
        
        for (ULONG off = blk->start; off <= blk->end; off += step) {
            ULONG val = R(off);
            if (val == 0xBAD0C0DE) {
                printf("  [0x%04X] = IOCTL_FAIL\n", off);
            } else if (val == 0xFFFFFFFF) {
                printf("  [0x%04X] = 0xFFFFFFFF (dead)\n", off);
            } else {
                printf("  [0x%04X] = 0x%08X\n", off, val);
            }
        }
    }

    /* SCRATCH write test */
    printf("\n=== SCRATCH Safe Write Test ===\n");
    ULONG scratchOrig = R(0x32D4);
    printf("  Original SCRATCH = 0x%08X\n", scratchOrig);
    
    /* Test only safe patterns (no bit 31 to avoid confusion) */
    ULONG safePats[] = {0x00000000, 0x00AABBCC, 0x007F7F7F, 0x00DEAD00};
    for (int i = 0; i < 4; i++) {
        W(0x32D4, safePats[i]);
        ULONG val = R(0x32D4);
        printf("  Write 0x%08X -> Read 0x%08X %s\n",
               safePats[i], val, (val == safePats[i]) ? "OK" : "DIFF");
    }
    
    W(0x32D4, scratchOrig);
    printf("  Restored SCRATCH = 0x%08X\n", R(0x32D4));

    CloseHandle(h);
    printf("\n=== Scan Complete ===\n");
    return 0;
}