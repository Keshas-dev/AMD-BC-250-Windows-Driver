/* psp-ring-probe.c - READ-ONLY probe of PSP C2PMSG register candidates.
 *
 * Purpose: find where the REAL PSP (MP0) C2PMSG registers live on BC-250 so we
 * can implement the ring mechanism (C2PMSG_64/67/69/70/71) like Linux
 * psp_v11_0_8.c. The old psp-ring-create-test.c read SMU C2PMSG_90
 * (SMN 0x03B10A68) thinking it was PSP C2PMSG_64 - wrong register, so the
 * "TOS ready never sets" conclusion was never actually verified.
 *
 * Candidates probed (all READ-ONLY):
 *   A) BAR5 direct, MP0 base = 0x103D0  (C2PMSG_81 @ 0x10614 = proven SOS alive)
 *   B) BAR5 direct, MP0 base = 0x103E0  (C2PMSG_35 @ 0x1056C = code's direct path)
 *   C) BAR5 direct, MP0 base = 0x16000  (ip_discovery MP0 base)
 *   D) SMN  C2PMSG_n = 0x03B10900 + n*4 (formula verified via SMU mailbox)
 *   E) SMN  addresses used by amdbc250_dream_psp_fw_load.c
 *
 * Requires: KMDOD/GPU driver with INIT_HARDWARE done (mapped BAR5).
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static HANDLE g_hDev = INVALID_HANDLE_VALUE;

#define IOCTL_GPU_READ     0x80000B88
#define IOCTL_GPU_WRITE    0x80000B8C
#define IOCTL_GPU_INIT     0x80000B80

typedef struct { UINT32 RegisterOffset; UINT32 Value; } REG_IO;

static BOOL WriteReg(uint32_t offset, uint32_t value) {
    REG_IO r; DWORD returned = 0;
    r.RegisterOffset = offset; r.Value = value;
    return DeviceIoControl(g_hDev, IOCTL_GPU_WRITE, &r, sizeof(r), &r, sizeof(r), &returned, NULL);
}
static uint32_t ReadReg(uint32_t offset) {
    REG_IO r; DWORD returned = 0;
    r.RegisterOffset = offset; r.Value = 0;
    if (DeviceIoControl(g_hDev, IOCTL_GPU_READ, &r, sizeof(r), &r, sizeof(r), &returned, NULL)) return r.Value;
    return 0xFFFFFFFF;
}
static uint32_t SmnRead(uint32_t smnAddr) {
    if (!WriteReg(0x38, smnAddr)) return 0xFFFFFFFF;
    ReadReg(0x38);
    return ReadReg(0x3C);
}

/* C2PMSG register byte offsets within MP0 block (from amdbc250_psp.h,
   matches mp_11_0_8_offset.h numbering). */
#define C2PMSG_35_OFF   0x018C
#define C2PMSG_36_OFF   0x0190
#define C2PMSG_37_OFF   0x0194
#define C2PMSG_64_OFF   0x0200
#define C2PMSG_67_OFF   0x020C
#define C2PMSG_69_OFF   0x0214
#define C2PMSG_70_OFF   0x0218
#define C2PMSG_71_OFF   0x021C
#define C2PMSG_81_OFF   0x0244
#define C2PMSG_101_OFF  0x0294

typedef struct { const char *name; uint32_t bar5Offsets[10]; } BAR5_BASE;

static const BAR5_BASE g_bases[] = {
    { "A: BAR5 MP0=0x103D0 (proven 81@10614)", {0x1055C,0x10560,0x10564,0x105D0,0x105DC,0x105E4,0x105E8,0x105EC,0x10614,0x10664} },
    { "B: BAR5 MP0=0x103E0 (code direct path)", {0x1056C,0x10570,0x10574,0x105E0,0x105EC,0x105F4,0x105F8,0x105FC,0x10624,0x10674} },
    { "C: BAR5 MP0=0x16000 (ip_discovery)",      {0x1618C,0x16190,0x16194,0x16200,0x1620C,0x16214,0x16218,0x1621C,0x16244,0x16294} },
};

/* SMN formula C2PMSG_n = 0x03B10900 + n*4 */
static const uint32_t g_smnFormula[10] = {
    0x03B1098C, 0x03B10990, 0x03B10994, 0x03B10A00, 0x03B10A0C,
    0x03B10A14, 0x03B10A18, 0x03B10A1C, 0x03B10A44, 0x03B10A94,
};
/* SMN addresses used by amdbc250_dream_psp_fw_load.c */
static const uint32_t g_smnLegacy[10] = {
    0x03B10A08, 0x03B10A48, 0x03B10A68, 0x03B10A00, 0x03B10A0C,
    0x03B10A14, 0x03B10A18, 0x03B10A1C, 0x03B10A44, 0x03B10A94,
};

static const char *g_regNames[10] = {
    "35", "36", "37", "64", "67", "69", "70", "71", "81", "101"
};

static void DumpBar5(const BAR5_BASE *b) {
    printf("\n--- %s ---\n", b->name);
    for (int i = 0; i < 10; i++) {
        uint32_t v = ReadReg(b->bar5Offsets[i]);
        const char *tag = "";
        if (v == 0xFFFFFFFF) tag = "  <== UNMAPPED/RO-hole";
        if (i == 3 && (v & 0x80000000)) tag = "  <== TOS READY!";
        if (i == 8 && (v & 0x80000000)) tag = "  <== SOS ALIVE!";
        printf("  C2PMSG_%-3s @ 0x%05X = 0x%08X%s\n", g_regNames[i], b->bar5Offsets[i], v, tag);
    }
}

static void DumpSmn(const char *name, const uint32_t *addrs) {
    printf("\n--- %s ---\n", name);
    for (int i = 0; i < 10; i++) {
        uint32_t v = SmnRead(addrs[i]);
        const char *tag = "";
        if (v == 0xFFFFFFFF) tag = "  <== UNMAPPED/RO-hole";
        if (i == 3 && (v & 0x80000000)) tag = "  <== TOS READY!";
        if (i == 8 && (v & 0x80000000)) tag = "  <== SOS ALIVE!";
        printf("  C2PMSG_%-3s @ 0x%08X = 0x%08X%s\n", g_regNames[i], addrs[i], v, tag);
    }
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    g_hDev = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (g_hDev == INVALID_HANDLE_VALUE) {
        printf("FAIL: CreateFile gle=%lu\n", GetLastError());
        return 1;
    }
    printf("CreateFile OK\n");

    typedef struct { UINT64 MmioPhysicalBase; UINT32 MmioSize; UINT32 Flags;
                     UINT64 FbPhysicalBase; UINT32 FbSize; } INIT_HW;
    INIT_HW ih; DWORD ret = 0;
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL;
    ih.MmioSize = 0x80000;
    ih.Flags = 1; /* AMDBC250_INIT_FLAG_NBIO_MAP */
    BOOL ok = DeviceIoControl(g_hDev, IOCTL_GPU_INIT, &ih, sizeof(ih), &ih, sizeof(ih), &ret, NULL);
    printf("INIT_HW(0xFE800000/0x80000): ok=%d gle=%lu\n", ok, GetLastError());
    if (!ok) { CloseHandle(g_hDev); return 1; }

    /* Sanity: GPU_ID at 0x0000 must read real silicon, not 0xFFFFFFFF. */
    uint32_t gpuId = ReadReg(0x0000);
    printf("GPU_ID(0x0000) = 0x%08X %s\n", gpuId, (gpuId == 0xFFFFFFFF) ? "UNMAPPED" : "OK");

    DumpBar5(&g_bases[0]);
    DumpBar5(&g_bases[1]);
    DumpBar5(&g_bases[2]);
    DumpSmn("D: SMN formula 0x03B10900+n*4", g_smnFormula);
    DumpSmn("E: SMN legacy fw_load.c", g_smnLegacy);

    printf("\n=== Summary ===\n");
    printf("SOS alive (bit31 C2PMSG_81) in BAR5 A: %s\n",
        (ReadReg(0x10614) & 0x80000000) ? "YES" : "no");
    printf("TOS ready (bit31 C2PMSG_64) in BAR5 A: %s\n",
        (ReadReg(0x105D0) & 0x80000000) ? "YES" : "no");
    printf("TOS ready (bit31 C2PMSG_64) SMN 0x03B10A00: %s\n",
        (SmnRead(0x03B10A00) & 0x80000000) ? "YES" : "no");

    printf("\n=== DONE (no GPU register writes; SMN reads use the 0x38 index bridge) ===\n");
    CloseHandle(g_hDev);
    return 0;
}