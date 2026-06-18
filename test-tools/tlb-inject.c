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
    return r[2];  /* ScratchAfter */
}

static void restore_all(UINT32 *saved) {
    for (int i = 0; i < 64; i++) W(0x0B400 + i*4, saved[i]);
}

int main() {
    printf("=== GCVM TLB Entry Injection Test ===\n");
    g_h = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (g_h == INVALID_HANDLE_VALUE) { printf("Cannot open\n"); return 1; }

    UCHAR ii[32]={0}; *(UINT64*)ii=0xFE800000ULL; *(UINT32*)(ii+8)=0x80000; *(UINT32*)(ii+12)=1;
    *(UINT64*)(ii+16)=0xC0000000ULL; *(UINT32*)(ii+24)=0x10000000;
    UCHAR io[32]={0}; DWORD br=0;
    DeviceIoControl(g_h, IOCTL_AMDBC250_INIT_HARDWARE, ii, 32, io, 32, &br, NULL);

    /* Step 1: Save ALL Context0 state (0x0B400-0x0B4FC = 64 DWORDs) */
    printf("\n--- Step 1: Save BIOS Context0 state ---\n");
    UINT32 saved[64];
    for (int i = 0; i < 64; i++) {
        saved[i] = R(0x0B400 + i * 4);
    }
    printf("Saved 64 DWORDs from 0x0B400-0x0B4FC\n");

    /* Print Context0 entries (0x0B408-0x0B4AC, saved[2]..saved[43]) */
    printf("Context0 entries with VALID bit (bit0=1):\n");
    for (int i = 2; i <= 43; i++) {
        if (saved[i] & 1) {
            printf("  0x%05X: 0x%08X\n", 0x0B400 + i*4, saved[i]);
        }
    }
    printf("Context0 entries without VALID bit (bit0=0, non-zero):\n");
    for (int i = 2; i <= 43; i++) {
        if (!(saved[i] & 1) && saved[i] != 0) {
            printf("  0x%05X: 0x%08X\n", 0x0B400 + i*4, saved[i]);
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

    /* Step 3: Brute-force ALL Context0 entries with PTE values
     * saved[2]=0x0B408 ... saved[43]=0x0B4AC
     * For each entry: save original, try PTE values, restore
     */
    printf("\n--- Step 3: Brute-force ALL Context0 entries ---\n");
    UINT32 pte_vals[] = {
        0x00100063,  /* PA=1MB, VALID|SYSTEM|RW */
        0x00200063,  /* PA=2MB */
        0x00400063,  /* PA=4MB */
        0x01000063,  /* PA=16MB */
        0x02000063,  /* PA=32MB */
        0x04000063,  /* PA=64MB */
        0x08000063,  /* PA=128MB */
        0x10000063,  /* PA=256MB */
        0x20000063,  /* PA=512MB */
        0x40000063,  /* PA=1GB */
        0x80000063,  /* PA=2GB */
        0xC0000063,  /* PA=3GB */
        0x00000063,  /* PA=0 (invalid but test) */
        0xFFFFFFFF,  /* all ones */
        0x00000000,  /* zero (disable entry) */
    };
    int npte = sizeof(pte_vals) / sizeof(pte_vals[0]);

    for (int i = 2; i <= 43; i++) {
        UINT32 off = 0x0B400 + i * 4;
        UINT32 old = saved[i];
        
        for (int v = 0; v < npte; v++) {
            W(off, pte_vals[v]);
            UINT32 rb = R(off);
            if (rb == pte_vals[v]) {
                scratch = try_gpu_kiq();
                if (scratch == 0xCAFEBABE) {
                    printf("*** PM4 SUCCESS! Entry 0x%05X = 0x%08X ***\n", off, pte_vals[v]);
                    restore_all(saved);
                    goto done;
                }
                if (scratch != old) {
                    printf("  0x%05X: wrote 0x%08X SCRATCH=0x%08X\n", off, pte_vals[v], scratch);
                }
            }
            W(off, old);  /* restore after each try */
        }
        /* Show progress every 10 entries */
        if (i % 10 == 0) printf("  ... tested through 0x%05X\n", off);
    }

    printf("\nNo TLB injection worked.\n");

done:
    /* Final restore */
    printf("Restoring all Context0 state...\n");
    restore_all(saved);
    
    /* Verify restore */
    printf("Verify CONTEXT0_CNTL: 0x%08X (expect 0x010CA88D)\n", R(0x0B460));

    CloseHandle(g_h);
    printf("\n=== Done ===\n");
    return 0;
}
