// psp-ring-probe-wdm.c - READ-ONLY probe of PSP (MP0) C2PMSG register candidates
// via the GPU WDM driver (atikmdag Dream drivers) IOCTL path.
//
// Why this exists: the earlier probe (psp-ring-probe-esc.c) tested base C
// "ip_discovery MP0 0x16000" as a BYTE base -> C2PMSG_81 @ 0x16244, which is
// 4x too low. Linux treats ip_discovery base_addr in DWORD units, so the MP0
// byte base is 0x16000*4 = 0x58000. Proof from linux amdgpu_discovery.c:
//   #define mmMP0_SMN_C2PMSG_33 0x16061   (raw dword)
//   RREG32(mmMP0_SMN_C2PMSG_33) -> readl(rmmio + (0x16061 << 2)) = byte 0x58184
// and mp_11_0_8_offset.h BASE_IDX=0 for all C2PMSG_*.
//
// Linux BC-250 psp_ring_create PROVABLY succeeded (dmesg line 929 "reserve
// 0x400000 ... for PSP TMR" happens after psp_ring_create in psp_hw_start,
// and there is no "PSP create ring failed!" error). So the TOS-ready bit
// (bit31) of C2PMSG_64 MUST be set at the address Linux uses. If Linux uses
// byte base 0x58000, then C2PMSG_64 @ 0x58200 should have bit31 = 1.
//
// This probe reads C2PMSG_33/35/36/37/64/67/69/70/71/81/101 at every base
// candidate and reports which one shows TOS-ready / SOS-alive.
//
// Requires: GPU driver atikmdag.sys running (device \\.\AMDBC250DreamV43).
// READ-ONLY - performs no writes to hardware.
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

static HANDLE g_hDev = INVALID_HANDLE_VALUE;

#define IOCTL_GPU_READ     0x80000B88
#define IOCTL_GPU_INIT     0x80000B80

typedef struct { UINT32 RegisterOffset; UINT32 Value; } REG_IO;

static uint32_t ReadReg(uint32_t offset) {
    REG_IO r; DWORD returned = 0;
    r.RegisterOffset = offset; r.Value = 0;
    if (DeviceIoControl(g_hDev, IOCTL_GPU_READ, &r, sizeof(r), &r, sizeof(r), &returned, NULL)) return r.Value;
    return 0xFFFFFFFF;
}

/* C2PMSG register byte offsets within the MP0 block (mm * 4, BASE_IDX=0) */
#define OFF_33   0x0184   /* mm 0x61 */
#define OFF_35   0x018C   /* mm 0x63 */
#define OFF_36   0x0190   /* mm 0x64 */
#define OFF_37   0x0194   /* mm 0x65 */
#define OFF_64   0x0200   /* mm 0x80 */
#define OFF_67   0x020C   /* mm 0x83 */
#define OFF_69   0x0214   /* mm 0x85 */
#define OFF_70   0x0218   /* mm 0x86 */
#define OFF_71   0x021C   /* mm 0x87 */
#define OFF_81   0x0244   /* mm 0x91 */
#define OFF_101  0x0294   /* mm 0xA5 */

typedef struct { const char *name; uint32_t base; } MP0_BASE;

static const MP0_BASE g_bases[] = {
    { "A: byte base 0x103D0 (dword 0x40F4, old 'proven' 81@0x10614)", 0x103D0 },
    { "B: byte base 0x103E0 (code direct path +0x10)",               0x103E0 },
    { "C: byte base 0x16000 (old WRONG ip_discovery-as-byte)",       0x16000 },
    { "D: byte base 0x58000 (ip_discovery 0x16000 dword * 4) <== KEY",0x58000 },
};

static const char *g_regNames[11] = { "33","35","36","37","64","67","69","70","71","81","101" };
static const uint32_t g_off[11] = { OFF_33, OFF_35, OFF_36, OFF_37, OFF_64, OFF_67, OFF_69, OFF_70, OFF_71, OFF_81, OFF_101 };

static void DumpBase(const MP0_BASE *b) {
    printf("\n--- %s ---\n", b->name);
    for (int i = 0; i < 11; i++) {
        uint32_t addr = b->base + g_off[i];
        uint32_t v = ReadReg(addr);
        const char *tag = "";
        if (v == 0xFFFFFFFF) tag = "  <== UNMAPPED/RO-hole";
        if (i == 4 && (v & 0x80000000)) tag = "  <== TOS READY (bit31)!";
        if (i == 9 && (v & 0x80000000)) tag = "  <== SOS ALIVE (bit31)!";
        if (i == 0 && (v & 0x80000000)) tag = "  <== IFWI-ready (bit31)!";
        printf("  C2PMSG_%-3s @ 0x%05X = 0x%08X%s\n", g_regNames[i], addr, v, tag);
    }
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("BC-250 PSP C2PMSG ring probe v2 (WDM IOCTL, READ-ONLY)\n");

    g_hDev = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (g_hDev == INVALID_HANDLE_VALUE) { printf("FAIL: CreateFile gle=%lu\n", GetLastError()); return 1; }
    printf("CreateFile OK\n");

    /* INIT_HARDWARE maps BAR5 (required on Win11 26100 WDM fallback) */
    typedef struct { UINT64 MmioPhysicalBase; UINT32 MmioSize; UINT32 Flags; UINT64 FbPhysicalBase; UINT32 FbSize; } INIT_HW; /* FIX: full 32-byte struct */
    INIT_HW ih; DWORD ret = 0;
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL;
    ih.MmioSize = 0x80000;
    ih.Flags = 1; /* AMDBC250_INIT_FLAG_NBIO_MAP */
    BOOL ok = DeviceIoControl(g_hDev, IOCTL_GPU_INIT, &ih, sizeof(ih), &ih, sizeof(ih), &ret, NULL);
    printf("INIT_HW(0xFE800000/0x80000): ok=%d gle=%lu\n", ok, GetLastError());

    uint32_t gpuId = ReadReg(0x0000);
    printf("GPU_ID(0x0000) = 0x%08X %s\n", gpuId, (gpuId == 0xFFFFFFFF) ? "UNMAPPED" : "OK");
    if (gpuId == 0xFFFFFFFF) { printf("READ_REG not working\n"); return 1; }

    for (int i = 0; i < 4; i++) DumpBase(&g_bases[i]);

    printf("\n=== Summary ===\n");
    printf("TOS ready (bit31 C2PMSG_64 @ 0x58200): %s\n",
        (ReadReg(0x58200) & 0x80000000) ? "YES" : "no");
    printf("SOS alive (bit31 C2PMSG_81 @ 0x58244): %s\n",
        (ReadReg(0x58244) & 0x80000000) ? "YES" : "no");
    printf("IFWI bit31 C2PMSG_33 @ 0x58184:        %s\n",
        (ReadReg(0x58184) & 0x80000000) ? "YES" : "no");

    printf("\n=== DONE (no hardware writes) ===\n");
    CloseHandle(g_hDev);
    return 0;
}
