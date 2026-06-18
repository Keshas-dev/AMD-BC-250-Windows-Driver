#include <windows.h>
#include <stdio.h>

#define IOCTL_AMDBC250_INIT_HARDWARE  0x80000B80
#define IOCTL_AMDBC250_READ_REG       0x80000B88
#define IOCTL_AMDBC250_WRITE_REG      0x80000B8C
#define IOCTL_AMDBC250_GPU_KIQ_TEST   0x80000BD0
#define IOCTL_AMDBC250_LOAD_CP_FW     0x80000BD4

typedef struct { UINT32 Offset; UINT32 Value; } REG_OP;
#pragma pack(push, 1)
typedef struct { UINT32 FwType; UINT32 FwSize; UINT32 Result; UINT32 UcodeVer; } LOAD_CP_FW_REQ;
#pragma pack(pop)

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

static void load_fw(const char* path, UINT32 fwType) {
    HANDLE fh = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (fh == INVALID_HANDLE_VALUE) return;
    DWORD sz = GetFileSize(fh, NULL);
    UCHAR *buf = (UCHAR*)malloc(sz); DWORD br;
    ReadFile(fh, buf, sz, &br, NULL); CloseHandle(fh);
    UINT32 hdrSz = sizeof(LOAD_CP_FW_REQ);
    UCHAR *in = (UCHAR*)malloc(hdrSz + sz);
    RtlZeroMemory(in, hdrSz);
    ((LOAD_CP_FW_REQ*)in)->FwType = fwType;
    ((LOAD_CP_FW_REQ*)in)->FwSize = sz;
    memcpy(in + hdrSz, buf, sz);
    UCHAR out[256] = {0};
    DeviceIoControl(g_h, IOCTL_AMDBC250_LOAD_CP_FW, in, hdrSz+sz, out, 256, &br, NULL);
    LOAD_CP_FW_REQ *resp = (LOAD_CP_FW_REQ*)out;
    printf("  FW type=%u: Result=0x%08X UcodeVer=0x%X\n", fwType, resp->Result, resp->UcodeVer);
    free(in); free(buf);
}

static UINT32 try_gpu_kiq(void) {
    UCHAR kiq_in[8]={0}, kiq_out[32]={0}; DWORD br=0;
    DeviceIoControl(g_h, IOCTL_AMDBC250_GPU_KIQ_TEST, kiq_in, 8, kiq_out, 32, &br, NULL);
    UINT32 *r = (UINT32*)kiq_out;
    /* r[0]=Result(WPTR), r[1]=ScratchBefore, r[2]=ScratchAfter */
    return r[2];  /* ScratchAfter */
}

int main() {
    printf("=== GCVM TLB Entry Injection Test ===\n");
    g_h = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (g_h == INVALID_HANDLE_VALUE) { printf("Cannot open\n"); return 1; }

    UCHAR ii[32]={0}; *(UINT64*)ii=0xFE800000ULL; *(UINT32*)(ii+8)=0x80000; *(UINT32*)(ii+12)=1;
    *(UINT64*)(ii+16)=0xC0000000ULL; *(UINT32*)(ii+24)=0x10000000;
    UCHAR io[32]={0}; DWORD br=0;
    DeviceIoControl(g_h, IOCTL_AMDBC250_INIT_HARDWARE, ii, 32, io, 32, &br, NULL);

    /* Step 1: Save ALL Context0 state */
    printf("\n--- Step 1: Save BIOS Context0 state ---\n");
    UINT32 saved[64];  /* 0x0B400-0x0B4FC */
    for (int i = 0; i < 64; i++) {
        saved[i] = R(0x0B400 + i * 4);
    }
    printf("Saved 64 DWORDs from 0x0B400-0x0B4FC\n");

    /* Print which entries have VALID bit */
    printf("Entries with VALID bit (bit0=1):\n");
    for (int i = 0x408/4; i <= 0x4AC/4; i++) {
        if (saved[i] & 1) {
            printf("  0x%05X: 0x%08X\n", 0x0B000 + i*4, saved[i]);
        }
    }
    printf("Entries without VALID bit (bit0=0):\n");
    for (int i = 0x408/4; i <= 0x4AC/4; i++) {
        if (!(saved[i] & 1) && saved[i] != 0) {
            printf("  0x%05X: 0x%08X\n", 0x0B000 + i*4, saved[i]);
        }
    }

    /* Load firmware */
    printf("\n--- Load firmware ---\n");
    load_fw("C:\\AMD-BC-250\\AMD-BC-250-PSP-Windows-Driver\\output\\cyan_skillfish2_me.bin", 1);
    load_fw("C:\\AMD-BC-250\\AMD-BC-250-PSP-Windows-Driver\\output\\cyan_skillfish2_pfp.bin", 2);

    /* Step 2: Baseline test (no TLB injection) */
    printf("\n--- Baseline: GPU_KIQ_TEST without TLB injection ---\n");
    UINT32 scratch = try_gpu_kiq();
    printf("SCRATCH after: 0x%08X %s\n", scratch, scratch == 0xCAFEBABE ? "*** PM4 SUCCESS ***" : "(unchanged)");

    /* Step 3: Try PTE-format entries in empty slots
     * PTE format (RDNA2): bit0=VALID, bit1=SYSTEM, bit5=READABLE, bit6=WRITEABLE
     *                      bits[31:12] = PA[31:12]
     * Ring buffer will be allocated by GPU_KIQ_TEST at some PA below 4GB.
     * We don't know the PA in advance, so we'll try a few known low-memory addresses.
     * 
     * Strategy: Try identity-mapping a few common physical addresses.
     * If GPU_KIQ_TEST allocates ring at one of these, PM4 might work.
     */
    printf("\n--- Step 3: TLB injection in empty slots ---\n");
    
    /* Common physical addresses below 1MB (BIOS, VGA, etc.) */
    UINT32 test_pas[] = {
        0x00100000,  /* 1MB - common RAM start */
        0x00200000,  /* 2MB */
        0x00400000,  /* 4MB */
        0x01000000,  /* 16MB */
        0x02000000,  /* 32MB */
        0x04000000,  /* 64MB */
        0x08000000,  /* 128MB */
        0x10000000,  /* 256MB */
        0x20000000,  /* 512MB */
        0x40000000,  /* 1GB */
    };
    int ntest = sizeof(test_pas) / sizeof(test_pas[0]);

    /* Find empty slots (bit0=0) in Context0 range */
    int empty_slots[32];
    int nempty = 0;
    for (int i = 0x408/4; i <= 0x4AC/4 && nempty < 32; i++) {
        if (!(saved[i] & 1)) {
            empty_slots[nempty++] = i;
        }
    }
    printf("Found %d empty slots\n", nempty);

    /* Try injecting PTE entries */
    for (int t = 0; t < ntest; t++) {
        UINT32 pa = test_pas[t];
        /* PTE: VALID|SYSTEM|READABLE|WRITEABLE | PA */
        UINT32 pte = pa | 0x63;
        
        /* Write to first empty slot */
        if (nempty > 0) {
            int slot = empty_slots[0];
            UINT32 off = 0x0B000 + slot * 4;
            
            /* Save and write */
            UINT32 old = saved[slot];
            W(off, pte);
            UINT32 readback = R(off);
            
            if (readback == pte) {
                printf("  PA=0x%08X: wrote PTE 0x%08X to slot 0x%05X, readback OK\n", pa, pte, off);
                
                /* Test PM4 */
                scratch = try_gpu_kiq();
                if (scratch == 0xCAFEBABE) {
                    printf("*** PM4 SUCCESS with PA=0x%08X! ***\n", pa);
                    /* Restore all */
                    for (int i = 0; i < 64; i++) W(0x0B000 + i*4, saved[i]);
                    goto done;
                }
                printf("    SCRATCH: 0x%08X\n", scratch);
            } else {
                printf("  PA=0x%08X: wrote 0x%08X, readback 0x%08X (mismatch)\n", pa, pte, readback);
            }
            
            /* Restore */
            W(off, old);
        }
    }

    /* Step 4: Try writing to ALL writable entries with various PTE values */
    printf("\n--- Step 4: Brute-force writable entries ---\n");
    for (int i = 0x408/4; i <= 0x4AC/4; i++) {
        UINT32 off = 0x0B000 + i*4;
        UINT32 old = saved[i];
        
        /* Try a few PTE values */
        UINT32 pte_vals[] = {
            0x00100063,  /* PA=1MB, VALID|SYSTEM|RW */
            0x00200063,  /* PA=2MB */
            0x00400063,  /* PA=4MB */
            0x10000063,  /* PA=256MB */
            0x40000063,  /* PA=1GB */
        };
        
        for (int v = 0; v < 5; v++) {
            W(off, pte_vals[v]);
            UINT32 rb = R(off);
            if (rb == pte_vals[v]) {
                scratch = try_gpu_kiq();
                if (scratch == 0xCAFEBABE) {
                    printf("*** PM4 SUCCESS! Entry 0x%05X = 0x%08X ***\n", off, pte_vals[v]);
                    for (int j = 0; j < 64; j++) W(0x0B000 + j*4, saved[j]);
                    goto done;
                }
            }
            W(off, old);  /* restore */
        }
    }

    printf("\nNo TLB injection worked. Restoring all state.\n");
    for (int i = 0; i < 64; i++) W(0x0B000 + i*4, saved[i]);

done:
    CloseHandle(g_h);
    printf("\n=== Done ===\n");
    return 0;
}
