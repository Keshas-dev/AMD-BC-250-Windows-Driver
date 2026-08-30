/* cp-ucode-recheck.c
 *
 * Re-tests the CP/RLC direct-firmware-load registers with the CORRECT
 * amdgpu sequence: HALT the CP engines FIRST (gfx_v10_0_cp_gfx_load_* does
 * CP_ME_CNTL halt -> UCODE_ADDR/DATA writes), then probe both address
 * families (NBIO native 0xC0A0.. and GC-shifted HYP 0x172B0..).
 * Old "freeze zone / stuck" verdicts were obtained WITHOUT halting — stale.
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

static HANDLE g_hDev = INVALID_HANDLE_VALUE;
#define IOCTL_GPU_READ   0x80000B88
#define IOCTL_GPU_WRITE  0x80000B8C
#define IOCTL_GPU_INIT   0x80000B80

typedef struct { UINT32 RegisterOffset; UINT32 Value; } REG_IO;
static BOOL WriteReg(uint32_t o, uint32_t v) {
    REG_IO r; DWORD ret = 0; r.RegisterOffset = o; r.Value = v;
    return DeviceIoControl(g_hDev, IOCTL_GPU_WRITE, &r, sizeof(r), &r, sizeof(r), &ret, NULL);
}
static uint32_t ReadReg(uint32_t o) {
    REG_IO r; DWORD ret = 0; r.RegisterOffset = o; r.Value = 0;
    if (DeviceIoControl(g_hDev, IOCTL_GPU_READ, &r, sizeof(r), &r, sizeof(r), &ret, NULL)) return r.Value;
    return 0xFFFFFFFF;
}

#define CP_ME_CNTL          0x4A74
#define ME_HALT             0x10000000
#define PFP_HALT            0x40000000
#define CE_HALT             0x20000000

typedef struct { const char* name; uint32_t off; } REGDEF;
static REGDEF regs[] = {
    { "CP_PFP_UCODE_ADDR (NBIO 0xC0A0)", 0xC0A0 },
    { "CP_PFP_UCODE_DATA (NBIO 0xC0A4)", 0xC0A4 },
    { "CP_ME_UCODE_ADDR  (NBIO 0xC0B0)", 0xC0B0 },
    { "CP_HYP_PFP_UCODE_ADDR(0x172B0)",  0x172B0 },
    { "CP_HYP_ME_UCODE_ADDR (0x172B8)",  0x172B8 },
    { "CP_HYP_CE_UCODE_DATA (0x172C4)",  0x172C4 },
    { "RLC_CP_SCHEDULERS (0xECA8)",      0xECA8 },
};

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    g_hDev = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (g_hDev == INVALID_HANDLE_VALUE) { printf("FAIL: CreateFile gle=%lu\n", GetLastError()); return 1; }

    typedef struct {
        UINT64 MmioPhysicalBase; UINT32 MmioSize; UINT32 Flags;
        UINT64 FbPhysicalBase; UINT32 FbSize;
    } INIT_HW;
    INIT_HW ih; DWORD ret = 0;
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL; ih.MmioSize = 0x80000; ih.Flags = 1;
    DeviceIoControl(g_hDev, IOCTL_GPU_INIT, &ih, sizeof(ih), &ih, sizeof(ih), &ret, NULL);
    printf("GPU_ID: 0x%08X\n", ReadReg(0x0000));

    /* Step 1: halt all CP engines like amdgpu does before ucode load */
    uint32_t cntlBefore = ReadReg(CP_ME_CNTL);
    printf("\nCP_ME_CNTL before: 0x%08X\n", cntlBefore);
    WriteReg(CP_ME_CNTL, ME_HALT | PFP_HALT | CE_HALT);
    Sleep(10);
    printf("CP_ME_CNTL after halt: 0x%08X\n", ReadReg(CP_ME_CNTL));

    /* Step 2: probe each reg: read -> write 0x00000FFF/0xA5A5A5A5 -> read */
    printf("\n%-36s %-10s %-10s %-10s %s\n", "Register", "Before", "Wrote", "After", "Verdict");
    for (int i = 0; i < (int)(sizeof(regs)/sizeof(regs[0])); i++) {
        uint32_t b = ReadReg(regs[i].off);
        uint32_t w = (strstr(regs[i].name, "DATA")) ? 0xDEADBEEF : 0x00000FFF;
        WriteReg(regs[i].off, w);
        uint32_t a = ReadReg(regs[i].off);
        const char* cls = (a == w) ? "WRITABLE" : ((a != b) ? "PARTIAL" : "STUCK");
        printf("%-36s 0x%08X 0x%08X 0x%08X  %s\n", regs[i].name, b, w, a, cls);
        if (a != b && strcmp(regs[i].name, "RLC_CP_SCHEDULERS (0xECA8)") == 0)
            WriteReg(regs[i].off, b); /* restore */
    }

    /* Step 3: restore CP_ME_CNTL */
    WriteReg(CP_ME_CNTL, cntlBefore);
    Sleep(10);
    printf("\nCP_ME_CNTL restored: 0x%08X\n", ReadReg(CP_ME_CNTL));
    CloseHandle(g_hDev);
    return 0;
}
