#include <windows.h>
#include <stdio.h>

#define IOCTL_AMDBC250_INIT_HARDWARE  0x80000B80
#define IOCTL_AMDBC250_READ_REG       0x80000B88
#define IOCTL_AMDBC250_WRITE_REG      0x80000B8C
#define IOCTL_AMDBC250_LOAD_CP_FW     0x80000BD4
#define IOCTL_AMDBC250_BAR5_READ      0x900
#define IOCTL_AMDBC250_BAR5_WRITE     0x901

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
    if (fh == INVALID_HANDLE_VALUE) { printf("  Cannot open\n"); return; }
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

int main() {
    printf("=== Manual PM4 test (no GPU_KIQ_TEST) ===\n");
    g_h = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (g_h == INVALID_HANDLE_VALUE) { printf("Cannot open\n"); return 1; }

    UCHAR ii[32]={0}; *(UINT64*)ii=0xFE800000ULL; *(UINT32*)(ii+8)=0x80000; *(UINT32*)(ii+12)=1;
    *(UINT64*)(ii+16)=0xC0000000ULL; *(UINT32*)(ii+24)=0x10000000;
    UCHAR io[32]={0}; DWORD br=0;
    DeviceIoControl(g_h, IOCTL_AMDBC250_INIT_HARDWARE, ii, 32, io, 32, &br, NULL);

    printf("SCRATCH before: 0x%08X\n", R(0x32D4));
    printf("ME_CNTL before: 0x%08X\n", R(0x4A74));

    /* Load firmware */
    printf("\n--- Load firmware ---\n");
    load_fw("C:\\AMD-BC-250\\AMD-BC-250-PSP-Windows-Driver\\output\\cyan_skillfish2_me.bin", 1);
    load_fw("C:\\AMD-BC-250\\AMD-BC-250-PSP-Windows-Driver\\output\\cyan_skillfish2_pfp.bin", 2);
    printf("ME_CNTL after FW: 0x%08X\n", R(0x4A74));

    /* Try writing PM4 directly to BAR5 ring area? No, ring is in system RAM.
     * The issue is that GPU CP can't access system RAM via GCVM.
     * 
     * What if we try CONTEXT0_CNTL = 0x03 (Linux default)?
     * Or try disabling GCVM entirely (clear bit 0)?
     */
    
    /* Test A: CONTEXT0_CNTL = 0x03 (Linux default) */
    printf("\n--- Test A: CONTEXT0_CNTL = 0x03 (Linux default) ---\n");
    UINT32 orig = R(0x0B460);
    W(0x0B460, 0x03);
    printf("CONTEXT0_CNTL: 0x%08X -> 0x%08X\n", orig, R(0x0B460));
    
    /* Now try GPU KIQ test */
    printf("Running GPU_KIQ_TEST...\n");
    UCHAR kiq_in[8]={0}, kiq_out[32]={0};
    DeviceIoControl(g_h, 0x80000BD0, kiq_in, 8, kiq_out, 32, &br, NULL);
    UINT32 *r = (UINT32*)kiq_out;
    printf("  Result=%u ScratchBefore=0x%08X ScratchAfter=0x%08X WPTR=%u\n", r[0], r[1], r[2], r[3]);
    printf("SCRATCH: 0x%08X\n", R(0x32D4));
    
    /* Restore */
    W(0x0B460, orig);

    /* Test B: CONTEXT0_CNTL = 0x02 (DEFAULT_PAGE only, no ENABLE) */
    printf("\n--- Test B: CONTEXT0_CNTL = 0x02 (DEFAULT_PAGE only) ---\n");
    W(0x0B460, 0x02);
    printf("CONTEXT0_CNTL: 0x%08X\n", R(0x0B460));
    DeviceIoControl(g_h, 0x80000BD0, kiq_in, 8, kiq_out, 32, &br, NULL);
    printf("  Result=%u ScratchBefore=0x%08X ScratchAfter=0x%08X\n", r[0], r[1], r[2]);
    printf("SCRATCH: 0x%08X\n", R(0x32D4));
    W(0x0B460, orig);

    /* Test C: CONTEXT0_CNTL = 0x00 (disabled) */
    printf("\n--- Test C: CONTEXT0_CNTL = 0x00 (disabled) ---\n");
    W(0x0B460, 0x00);
    printf("CONTEXT0_CNTL: 0x%08X\n", R(0x0B460));
    DeviceIoControl(g_h, 0x80000BD0, kiq_in, 8, kiq_out, 32, &br, NULL);
    printf("  Result=%u ScratchBefore=0x%08X ScratchAfter=0x%08X\n", r[0], r[1], r[2]);
    printf("SCRATCH: 0x%08X\n", R(0x32D4));
    W(0x0B460, orig);

    /* Test D: CONTEXT0_CNTL = 0x010CA88F (DEFAULT_PAGE + BIOS bits) */
    printf("\n--- Test D: CONTEXT0_CNTL = 0x010CA88F (DEFAULT_PAGE + BIOS) ---\n");
    W(0x0B460, 0x010CA88F);
    printf("CONTEXT0_CNTL: 0x%08X\n", R(0x0B460));
    DeviceIoControl(g_h, 0x80000BD0, kiq_in, 8, kiq_out, 32, &br, NULL);
    printf("  Result=%u ScratchBefore=0x%08X ScratchAfter=0x%08X\n", r[0], r[1], r[2]);
    printf("SCRATCH: 0x%08X\n", R(0x32D4));
    W(0x0B460, orig);

    /* Test E: CONTEXT0_CNTL = original but try to also write PT_BASE with a valid PA */
    printf("\n--- Test E: PT_BASE with GCVM disabled ---\n");
    W(0x0B460, 0x00);  /* disable context */
    /* PT_BASE is RO, but let's try anyway after disable */
    W(0x0B608, 0x00001000);  /* try a small address */
    W(0x0B60C, 0x00000000);
    printf("PT_BASE: LO=0x%08X HI=0x%08X\n", R(0x0B608), R(0x0B60C));
    W(0x0B460, orig);

    printf("\n=== Done ===\n");
    return 0;
}
