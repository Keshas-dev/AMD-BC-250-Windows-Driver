#include <windows.h>
#include <stdio.h>

#define IOCTL_AMDBC250_INIT_HARDWARE  0x80000B80
#define IOCTL_AMDBC250_READ_REG       0x80000B88
#define IOCTL_AMDBC250_WRITE_REG      0x80000B8C

typedef struct { UINT32 Offset; UINT32 Value; } REG_OP;
static HANDLE g_h = INVALID_HANDLE_VALUE;

static UINT32 R(UINT32 off) {
    REG_OP in = {off,0}, out = {0}; DWORD br = 0;
    DeviceIoControl(g_h, IOCTL_AMDBC250_READ_REG, &in, sizeof(in), &out, sizeof(out), &br, NULL);
    return out.Value;
}

static void W(UINT32 off, UINT32 val) {
    REG_OP in = {off,val}, out = {0}; DWORD br = 0;
    DeviceIoControl(g_h, IOCTL_AMDBC250_WRITE_REG, &in, sizeof(in), &out, sizeof(out), &br, NULL);
}

static void dump_ctx0_hex(UINT32 off, const char* label, UINT32 val) {
    printf("  %s (0x%05X): 0x%08X  [hex:", label, off, val);
    for (int i = 28; i >= 0; i -= 4) printf(" %X", (val >> i) & 0xF);
    printf("] [bits:");
    for (int i = 31; i >= 0; i--) printf("%c", (val & (1u<<i)) ? '1' : '0');
    printf("]\n");
}

int main() {
    printf("=== SAFE GCVM Context0 Deep Decode ===\n");
    printf("!! This tool ONLY READS, no writes to hardware !!\n\n");
    g_h = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (g_h == INVALID_HANDLE_VALUE) { printf("Cannot open driver\n"); return 1; }

    UCHAR ii[32]={0}; *(UINT64*)ii=0xFE800000ULL; *(UINT32*)(ii+8)=0x80000; *(UINT32*)(ii+12)=1;
    *(UINT64*)(ii+16)=0xC0000000ULL; *(UINT32*)(ii+24)=0x10000000;
    UCHAR io[32]={0}; DWORD br=0;
    DeviceIoControl(g_h, IOCTL_AMDBC250_INIT_HARDWARE, ii, 32, io, 32, &br, NULL);
    printf("INIT_HARDWARE: OK\n\n");

    /* 1. Full Context0 dump (0x0B400-0x0B500) */
    printf("--- 1. GCVM Context0 full dump (0x0B400-0x0B500) ---\n");
    printf("All values are READ-ONLY. Saving BIOS state.\n\n");
    
    UINT32 ctx0[64];  /* 0x0B400-0x0B4FC */
    for (int i = 0; i < 64; i++) {
        ctx0[i] = R(0x0B400 + i * 4);
    }
    
    for (int i = 0; i < 64; i++) {
        UINT32 off = 0x0B400 + i * 4;
        UINT32 val = ctx0[i];
        if (val == 0 && off < 0x0B404) continue;
        if (val == 0 && off > 0x0B4B0 && off < 0x0B4C0) continue;
        if (val == 0 && off > 0x0B4D8 && off < 0x0B500) continue;
        
        /* Try to identify what these registers might be */
        const char* hint = "";
        if (off == 0x0B404) hint = " <- CONTEXT0_CNTL base?";
        else if (off == 0x0B408) hint = " <- CONTEXT0 page table base LO?";
        else if (off == 0x0B40C) hint = " <- CONTEXT0 page table base HI?";
        else if (off == 0x0B460) hint = " <- CONTEXT0_CNTL (we know this)";
        else if (off >= 0x0B408 && off <= 0x0B4AC) {
            /* Check if this could be a page table entry */
            if (val & 1) hint = " <- VALID bit set!";
        }
        
        printf("  0x%05X: 0x%08X%s\n", off, val, hint);
    }

    /* 2. L2 TLB dump with bit analysis */
    printf("\n--- 2. L2 TLB region (0x0B31C-0x0B36C) with bit analysis ---\n");
    for (UINT32 off = 0x0B31C; off <= 0x0B36C; off += 4) {
        UINT32 val = R(off);
        if (off == 0x0B360) {
            printf("  0x%05X: 0x%08X  [L2_CNTL]\n", off, val);
            printf("    bit0(L2_CACHE_EN)=%d, bit1(PTE_CACHE)=%d, bit2(PDE_CACHE)=%d\n",
                val & 1, (val>>1)&1, (val>>2)&1);
            continue;
        }
        printf("  0x%05X: 0x%08X  bits:", off, val);
        for (int i = 31; i >= 0; i--) printf("%c", (val & (1u<<i)) ? '1' : '0');
        printf("\n");
    }

    /* 3. Context0 config region */
    printf("\n--- 3. Context0 config (0x0B4C0-0x0B4D4) ---\n");
    for (UINT32 off = 0x0B4C0; off <= 0x0B4D4; off += 4) {
        printf("  0x%05X: 0x%08X\n", off, R(off));
    }

    /* 4. Invalidate region */
    printf("\n--- 4. Invalidate region (0x0B51C-0x0B56C) ---\n");
    for (UINT32 off = 0x0B51C; off <= 0x0B56C; off += 4) {
        UINT32 val = R(off);
        if (val != 0) printf("  0x%05X: 0x%08X\n", off, val);
    }

    /* 5. PT_BASE (should be 0) */
    printf("\n--- 5. PT_BASE (0x0B608-0x0B60C) ---\n");
    printf("  0x0B608: 0x%08X\n", R(0x0B608));
    printf("  0x0B60C: 0x%08X\n", R(0x0B60C));

    /* 6. Search for any flat/identity mapping control bits */
    printf("\n--- 6. Context0_CNTL (0x0B460) bit decode ---\n");
    UINT32 cntl = R(0x0B460);
    printf("  Value: 0x%08X\n", cntl);
    printf("  Bits set:");
    for (int i = 0; i < 32; i++) if (cntl & (1u<<i)) printf(" %d", i);
    printf("\n");

    CloseHandle(g_h);
    printf("\n=== Done (NO WRITES performed) ===\n");
    return 0;
}
