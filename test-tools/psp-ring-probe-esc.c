// psp-ring-probe-esc.c - READ-ONLY probe of PSP (MP0) C2PMSG register candidates
// via the BC-250 KMDOD Escape layer (no GPU WDM IOCTL driver needed).
//
// Purpose: find where the REAL PSP (MP0) C2PMSG registers live on BC-250 so we
// can implement the ring mechanism (C2PMSG_64/67/69/70/71) like Linux
// psp_v11_0_8.c. The old psp-ring-create-test.c read SMU C2PMSG_90
// (SMN 0x03B10A68) thinking it was PSP C2PMSG_64 - wrong register.
//
// Requires: SampleDisplay KMDOD v1.0.115+ with BC-250 Escape (READ_REG, READ_SMN).
//
// Candidates probed (all READ-ONLY):
//   A) BAR5 direct, MP0 base = 0x103D0  (C2PMSG_81 @ 0x10614 = proven SOS alive)
//   B) BAR5 direct, MP0 base = 0x103E0  (code's direct path C2PMSG_35 @ 0x1056C)
//   C) BAR5 direct, MP0 base = 0x16000  (ip_discovery MP0 base)
//   D) SMN  C2PMSG_n = 0x03B10900 + n*4 (formula verified via SMU mailbox)
//   E) SMN  addresses used by amdbc250_dream_psp_fw_load.c
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <d3dkmthk.h>

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

#define BC250_ESC_MAGIC 0xBC2501CA
#define BC250_ESC_READ_REG  1
#define BC250_ESC_READ_SMN  3

typedef struct _BC250_ESC_BUFFER {
    ULONG Magic;
    ULONG Command;
    ULONG Arg1;
    ULONG Arg2;
    ULONG Result;
    NTSTATUS Status;
} BC250_ESC_BUFFER;

static D3DKMT_HANDLE g_hAdapter = 0;

static int esc(ULONG cmd, ULONG arg1, ULONG arg2, ULONG* pResult)
{
    BC250_ESC_BUFFER buf;
    D3DKMT_ESCAPE escape;
    NTSTATUS status;
    memset(&buf, 0, sizeof(buf));
    buf.Magic = BC250_ESC_MAGIC;
    buf.Command = cmd;
    buf.Arg1 = arg1;
    buf.Arg2 = arg2;
    memset(&escape, 0, sizeof(escape));
    escape.hAdapter = g_hAdapter;
    escape.hDevice = 0;
    escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    escape.Flags.Value = 0;
    escape.pPrivateDriverData = &buf;
    escape.PrivateDriverDataSize = sizeof(buf);
    escape.hContext = 0;
    status = D3DKMTEscape(&escape);
    if (!NT_SUCCESS(status)) return -1;
    if (!NT_SUCCESS(buf.Status)) return -1;
    if (pResult) *pResult = buf.Result;
    return 0;
}

static uint32_t ReadReg(uint32_t offset) {
    ULONG r;
    if (esc(BC250_ESC_READ_REG, offset, 0, &r) == 0) return r;
    return 0xFFFFFFFF;
}
static uint32_t SmnRead(uint32_t smnAddr) {
    ULONG r;
    if (esc(BC250_ESC_READ_SMN, smnAddr, 0, &r) == 0) return r;
    return 0xFFFFFFFF;
}

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

static const uint32_t g_smnFormula[10] = {
    0x03B1098C, 0x03B10990, 0x03B10994, 0x03B10A00, 0x03B10A0C,
    0x03B10A14, 0x03B10A18, 0x03B10A1C, 0x03B10A44, 0x03B10A94,
};
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

static int open_adapter(void) {
    D3DKMT_OPENADAPTERFROMHDC open;
    HDC hdc = GetDC(NULL);
    if (hdc == NULL) { printf("GetDC(NULL) failed\n"); return -1; }
    memset(&open, 0, sizeof(open));
    open.hDc = hdc;
    NTSTATUS status = D3DKMTOpenAdapterFromHdc(&open);
    ReleaseDC(NULL, hdc);
    if (!NT_SUCCESS(status)) { printf("D3DKMTOpenAdapterFromHdc FAILED: 0x%08X\n", status); return -1; }
    g_hAdapter = open.hAdapter;
    printf("Adapter LUID: %08lx-%08lx\n", open.AdapterLuid.HighPart, open.AdapterLuid.LowPart);
    return 0;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("BC-250 PSP C2PMSG ring probe (via KMDOD Escape)\n");
    if (open_adapter() != 0) { printf("Cannot open display adapter\n"); return 1; }

    uint32_t gpuId = ReadReg(0x0000);
    printf("GPU_ID(0x0000) = 0x%08X %s\n", gpuId, (gpuId == 0xFFFFFFFF) ? "UNMAPPED" : "OK");
    if (gpuId == 0xFFFFFFFF) { printf("Escape READ_REG not working\n"); return 1; }

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

    printf("\n=== DONE (no GPU register writes) ===\n");
    return 0;
}