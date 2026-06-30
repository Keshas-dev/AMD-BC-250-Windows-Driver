#include <windows.h>
#include <stdio.h>

#define IOCTL_INIT_HW            0x80000B80
#define IOCTL_READ_REG           0x80000B88
#define IOCTL_WRITE_REG          0x80000B8C

typedef struct { UINT32 Off; UINT32 Val; } REG_IO;

static HANDLE gH = INVALID_HANDLE_VALUE;
static UINT32 R(UINT32 off) {
    REG_IO in={off,0}, out={0}; DWORD br=0;
    DeviceIoControl(gH, IOCTL_READ_REG, &in, sizeof(in), &out, sizeof(out), &br, NULL);
    return out.Val;
}
static void W(UINT32 off, UINT32 val) {
    REG_IO in={off,val}, out={0}; DWORD br=0;
    DeviceIoControl(gH, IOCTL_WRITE_REG, &in, sizeof(in), &out, sizeof(out), &br, NULL);
}

int main(void) {
    printf("=== ENGINE STATUS DUMP ===\n\n");
    gH = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (gH == INVALID_HANDLE_VALUE) { printf("ERROR\n"); return 1; }

    UINT8 initBuf[32] = {0};
    *(UINT64*)(initBuf+0)  = 0xFE800000ULL;
    *(UINT32*)(initBuf+8)  = 0x00080000;
    *(UINT32*)(initBuf+12) = 1;
    *(UINT64*)(initBuf+16) = 0xC0000000ULL;
    *(UINT32*)(initBuf+24) = 0x20000000;
    DWORD br = 0;
    DeviceIoControl(gH, IOCTL_INIT_HW, initBuf, sizeof(initBuf), NULL, 0, &br, NULL);

    struct { UINT32 off; const char* name; } regs[] = {
        /* GRBM status */
        {0x3260, "GRBM_STATUS"},
        {0x3264, "GRBM_STATUS2/CC"},
        {0x3268, "GRBM_STATUS2"},
        {0x326C, "GRBM_SOFT_RESET"},
        {0x3270, "GRBM_GFX_CNTL"},
        /* CP status */
        {0x33C0, "CP_STALLED_STAT1/CP_CPC_BUSY"},
        {0x33C4, "CP_STALLED_STAT2/CP_STAT"},
        {0x33C8, "CP_ME_CNTL"},
        {0x33CC, "CP_MEC_CNTL"},
        {0x33D0, "CP_MEC_CNTL2"},
        /* Compute pipe */
        {0x1A00, "CP_COMPUTE_STAT (if exists)"},
        /* RLC */
        {0x3394, "RLC_STAT"},
        {0x3398, "RLC_CNTL"},
        /* GFX index */
        {0x34D0, "GRBM_GFX_INDEX"},
        /* Compute registers at SEG1 */
        {0xDC60, "COMPUTE_DIR"},
        {0xDC64, "COMPUTE_START"},
        {0xDC68, "COMPUTE_RSRC1"},
        {0xDC6C, "COMPUTE_RSRC2"},
        {0xDC70, "COMPUTE_PGM_LO"},
        {0xDC74, "COMPUTE_PGM_HI"},
        {0xDC78, "COMPUTE_LIMITS"},
        {0xDC7C, "COMPUTE_THREAD_MGMT"},
        {0xDC80, "COMPUTE_MISC"},
        /* CP_MEC_CNTL at various guessed BAR5 offsets */
        /* mmCP_MEC_CNTL = 0x21B5 (DWORD), offset = GC_BASE + 0x21B5*4 = 0x1260 + 0x86D4 = 0x9934 */
        {0x9934, "CP_MEC_CNTL @ mm0x21B5"},
        /* Also try at the mmRegister gap area */
        {0x21B5, "CP_MEC_CNTL @ raw 0x21B5"},
        /* MEC halt bits in CP_ME_CNTL */
        {0x33C8, "CP_ME_CNTL"},
        /* Try at SEG1 + 0x21B5 */
        {0x9C15, "CP_MEC_CNTL @ SEG1+0x21B5"},
        /* POWER / PG */
        {0x16600, "THM_STATUS (TMP4116)"},
    };

    for (int i = 0; i < sizeof(regs)/sizeof(regs[0]); i++) {
        UINT32 val = R(regs[i].off);
        printf("  0x%04X %-30s = 0x%08X\n", regs[i].off, regs[i].name, val);
    }

    /* Try to find CP_MEC_CNTL by probing the GC_BASE+mm*4 range for a known halt signature */
    printf("\n=== Probing for MEC halt register near GC_BASE + 0x21B5*4 ===\n");
    /* mmCP_MEC_CNTL = 0x21B5 (from AGENTS.md). 
     * On Navi10: BAR5 = GC_BASE + 0x21B5*4 = 0x1260 + 0x86D4 = 0x9934
     * But on BC-250, maybe it's at GC_BASE + 0xA000 + 0x21B5 = 0x1260 + 0xCA15 = ... hmm
     * Or maybe the offset is just 0x21B5*4 from BAR5 start (not GC_BASE)?
     * Or at SEG1 + 0x21B5*4?
     */
    UINT32 probeAddrs[] = {
        0x1260 + 0x21B5,          /* GC_BASE + 0x21B5 (as byte) */
        0x1260 + 0x21B5*4,        /* GC_BASE + 0x21B5*4 = 0x9934 */
        0x1260 + 0xA000 + 0x21B5, /* +SEG1 */
        0x1260 + 0xA000 + 0x21B5*4, /* +SEG1 + *4 */
        0x21B5,                   /* raw byte offset */
        0x21B5*4,                 /* raw *4 = 0x86D4 */
    };
    for (int i = 0; i < 6; i++) {
        UINT32 val = R(probeAddrs[i]);
        /* Halt bits would be bit28 (ME_HALT), bit30 (PFP_HALT), bit29 (CE_HALT) */
        printf("  0x%05X = 0x%08X %s\n",
               probeAddrs[i], val,
               (val & 0x30000000) ? "(HALT bits SET!)" :
               (val & 0xE0000000) ? "(some high bits)" : "");
    }

    CloseHandle(gH);
    printf("\n=== Done ===\n");
    return 0;
}
