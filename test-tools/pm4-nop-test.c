#include <windows.h>
#include <stdio.h>

#define IOCTL_AMDBC250_INIT_HARDWARE  0x80000B80
#define IOCTL_AMDBC250_READ_REG       0x80000B88
#define IOCTL_AMDBC250_WRITE_REG      0x80000B8C
#define IOCTL_AMDBC250_LOAD_CP_FW     0x80000BD4
#define IOCTL_AMDBC250_GPU_KIQ_TEST   0x80000BD0

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

static BOOL LoadFW(const char *path, UINT32 type) {
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    DWORD sz = GetFileSize(h, NULL);
    UINT32 total = 16 + sz;
    UINT8 *buf = (UINT8 *)malloc(total);
    memset(buf, 0, 16);
    *(UINT32*)(buf+0) = type;
    *(UINT32*)(buf+4) = sz;
    DWORD br = 0;
    ReadFile(h, buf+16, sz, &br, NULL);
    CloseHandle(h);
    UINT32 out[4] = {0};  /* 16 bytes: Result, UcodeVersion, padding */
    DeviceIoControl(g_h, IOCTL_AMDBC250_LOAD_CP_FW, buf, total, out, sizeof(out), &br, NULL);
    free(buf);
    printf("  FW type=%u: Result=0x%08X Ver=0x%08X\n", type, out[0], out[1]);
    return out[0] == 1;
}

int main() {
    printf("=== PM4 Debug: NOP-only + WPTR test ===\n");
    g_h = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (g_h == INVALID_HANDLE_VALUE) { printf("Cannot open driver\n"); return 1; }

    UCHAR ii[32]={0}; *(UINT64*)ii=0xFE800000ULL; *(UINT32*)(ii+8)=0x80000; *(UINT32*)(ii+12)=1;
    *(UINT64*)(ii+16)=0xC0000000ULL; *(UINT32*)(ii+24)=0x10000000;
    UCHAR io[32]={0}; DWORD br=0;
    DeviceIoControl(g_h, IOCTL_AMDBC250_INIT_HARDWARE, ii, 32, io, 32, &br, NULL);
    printf("INIT_HARDWARE: OK\n");

    /* Load firmware first */
    printf("\n--- Load ME+PFP firmware ---\n");
    LoadFW("C:\\AMD-BC-250\\AMD-BC-250-PSP-Windows-Driver\\output\\cyan_skillfish2_me.bin", 1);
    LoadFW("C:\\AMD-BC-250\\AMD-BC-250-PSP-Windows-Driver\\output\\cyan_skillfish2_pfp.bin", 2);

    printf("  ME_CNTL after FW load = 0x%08X\n", R(0x4A74));

    /* Now try GPU_KIQ_TEST with firmware loaded */
    printf("\n--- GPU_KIQ_TEST (firmware loaded + PTE SYSTEM bit) ---\n");
    typedef struct { UINT32 a,b,c,d,e,f,g,h; } KIQ_OUT;
    KIQ_OUT kiq = {0};
    DeviceIoControl(g_h, IOCTL_AMDBC250_GPU_KIQ_TEST, NULL, 0, &kiq, sizeof(kiq), &br, NULL);
    printf("  Result=%08X Mmio=%u Ring=%u Hqd=%u Pm4=%u\n", kiq.a, kiq.d, kiq.e, kiq.f, kiq.g);
    printf("  SCRATCH: 0x%08X -> 0x%08X\n", kiq.b, kiq.c);
    printf("  WPTR: 0x%08X\n", kiq.h);

    if (kiq.c == 0xCAFEBABE)
        printf("\n*** PM4 EXECUTED! SCRATCH = 0xCAFEBABE ***\n");
    else if (kiq.c != kiq.b)
        printf("\n*** SCRATCH CHANGED! ***\n");
    else
        printf("\nSCRATCH unchanged\n");

    /* Final state */
    printf("\n--- Final HW state ---\n");
    printf("  SCRATCH    = 0x%08X\n", R(0x32D4));
    printf("  GCVM_CNTL  = 0x%08X\n", R(0x0B460));
    printf("  PT_BASE_LO = 0x%08X (NOTE: NOT WRITABLE!)\n", R(0x0B608));
    printf("  PT_BASE_HI = 0x%08X\n", R(0x0B60C));
    printf("  ME_CNTL    = 0x%08X\n", R(0x4A74));

    CloseHandle(g_h);
    return 0;
}
