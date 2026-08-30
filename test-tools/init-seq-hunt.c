/* init-seq-hunt.c
 *
 * INIT-SEQUENCE UNLOCK HUNT (2026-08-21)
 *
 * Mystery: SPI_PG write rejected in Windows AND at EFI Shell, but Linux
 * amdgpu writes it successfully during probe. Hypothesis: some stage of
 * the full amdgpu init sequence moves the GC block into a state where
 * SPI_PG accepts writes. This tool walks the stages we CAN replicate and
 * probes SPI_PG after each one.
 *
 * Stages:
 *  [0] Baseline (INIT_HW only)
 *  [1] PSP_RING_INIT + SETUP_TMR        (PSP handshake equivalent)
 *  [2] SMU wake (ForceVid+ForceFreq)    (GFX out of GFXOFF)
 *  [3] GFXOFF/CG/PG disable (Q2 0x06)
 *  [4] CP firmware load via ring (MEC + RLC)
 *  [5] RLC_CP_SCHEDULERS program
 *
 * After EVERY stage: per-bank + broadcast SPI_PG/CC write attempt + readback,
 * plus SMU QueryActiveWgp.
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

static HANDLE g_hDev = INVALID_HANDLE_VALUE;

#define IOCTL_GPU_READ       0x80000B88
#define IOCTL_GPU_WRITE      0x80000B8C
#define IOCTL_GPU_INIT       0x80000B80
#define IOCTL_PSP_RING_INIT  0x80000C18
#define IOCTL_PSP_RING_TMR   0x80000C24
#define IOCTL_PSP_RING_FW    0x80000C20

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
static uint32_t SmnRead(uint32_t a)  { WriteReg(0x38, a); ReadReg(0x38); return ReadReg(0x3C); }
static BOOL     SmnWrite(uint32_t a, uint32_t v) { WriteReg(0x38, a); return WriteReg(0x3C, v); }

/* SMU Q0 mailbox */
#define Q0_CMD 0x03B10A08
#define Q0_RSP 0x03B10A68
#define Q0_ARG 0x03B10A48
/* SMU Q2 mailbox (features) */
#define Q2_CMD 0x03B10528
#define Q2_RSP 0x03B10564
#define Q2_ARG 0x03B10998

static int WaitRsp(uint32_t addr, int ms) {
    for (int i = 0; i < ms; i++) {
        uint32_t v = SmnRead(addr);
        if (v != 0 && v != 0xFFFFFFFF) return (int)v;
        Sleep(1);
    }
    return -1;
}
static int SmuQ0(uint16_t msg, uint32_t arg, uint32_t* resp) {
    if (SmnRead(Q0_RSP) == 1) SmnWrite(Q0_RSP, 0);
    SmnWrite(Q0_ARG, arg);
    SmnWrite(Q0_CMD, msg);
    int r = WaitRsp(Q0_RSP, 500);
    if (r < 0) return -1;
    if (resp) *resp = SmnRead(Q0_ARG);
    return r; /* 1=OK */
}
static int SmuQ2(uint16_t msg, uint32_t maskLow) {
    SmnWrite(Q2_RSP, 0);
    SmnWrite(Q2_ARG, maskLow);
    SmnWrite(Q2_ARG + 4, 0);
    SmnWrite(Q2_CMD, msg);
    return WaitRsp(Q2_RSP, 1000);
}

#define GRBM_GFX_INDEX 0x34D0
#define SPI_PG   0x5C3C
#define CC_ARRAY 0x9C1C
#define RLC_SCHED 0xECA8

static void Probe(const char* label) {
    /* per-bank SE0/SH0 attempt */
    WriteReg(GRBM_GFX_INDEX, 0x00000000);
    WriteReg(SPI_PG, 0x1F);
    WriteReg(CC_ARRAY, 0x00000000);
    uint32_t spiBank = ReadReg(SPI_PG);
    uint32_t ccBank  = ReadReg(CC_ARRAY);

    /* broadcast attempt */
    WriteReg(GRBM_GFX_INDEX, 0x15000000);
    WriteReg(SPI_PG, 0x1F);
    uint32_t spiBc = ReadReg(SPI_PG);

    /* ActiveWgp */
    uint32_t wgp = 0;
    SmuQ0(0x1E, 0, &wgp);

    int opened = ((spiBank & 0x1F) == 0x1F) || ((spiBc & 0x1F) == 0x1F);
    printf("[%s] SPI(bank)=%08X SPI(bc)=%08X CC=%08X Wgp=%u %s\n",
           label, spiBank, spiBc, ccBank, wgp,
           opened ? "*** OPEN!!! ***" : "(locked)");
}

static BOOL RingInit(void) {
    ULONG flags = 0; DWORD ret = 0;
    typedef struct { UINT32 Result, RingPaLo, RingPaHi, RingSize, C2pmsg64, C2pmsg81; } R_OUT;
    R_OUT o; ZeroMemory(&o, sizeof(o));
    BOOL ok = DeviceIoControl(g_hDev, IOCTL_PSP_RING_INIT, &flags, sizeof(flags), &o, sizeof(o), &ret, NULL);
    printf("  RING_INIT: ok=%d Result=%u C64=%08X\n", ok, o.Result, o.C2pmsg64);
    return ok && o.Result == 1;
}
static BOOL RingTmr(void) {
    typedef struct { UINT32 TmrSize; UINT64 TmrPhysicalBase; } TMR_IN;
    TMR_IN ti; ti.TmrSize = 0; ti.TmrPhysicalBase = 0;
    DWORD ret = 0;
    typedef struct { UINT32 Result, FenceStatus, RespStatus, FwLo, FwHi, TmrSz, PaLo, PaHi, McLo, McHi; } R_OUT;
    R_OUT o; ZeroMemory(&o, sizeof(o));
    BOOL ok = DeviceIoControl(g_hDev, IOCTL_PSP_RING_TMR, &ti, sizeof(ti), &o, sizeof(o), &ret, NULL);
    printf("  SETUP_TMR: ok=%d Fence=%u Resp=0x%08X\n", ok, o.FenceStatus, o.RespStatus);
    return ok && o.RespStatus == 0;
}
static void RingLoadFw(UINT32 type, const WCHAR* file) {
    typedef struct { UINT32 FwType; WCHAR FileName[260]; } FW_IN;
    FW_IN fi;
    UCHAR out[64]; /* generous: driver checks >= sizeof(its OUT struct) */
    DWORD ret = 0;
    ZeroMemory(&fi, sizeof(fi)); ZeroMemory(out, sizeof(out));
    fi.FwType = type;
    wcscpy_s(fi.FileName, 260, file);
    BOOL ok = DeviceIoControl(g_hDev, IOCTL_PSP_RING_FW, &fi, sizeof(fi), out, sizeof(out), &ret, NULL);
    UINT32* o = (UINT32*)out; /* [0]=Result [1]=Fence [2]=Resp */
    wprintf(L"  LOAD_IP_FW %s: ok=%d Result=%u Fence=%u Resp=0x%08X\n",
            file, ok, o[0], o[1], o[2]);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    g_hDev = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (g_hDev == INVALID_HANDLE_VALUE) { printf("FAIL open\n"); return 1; }

    typedef struct { UINT64 MmioPhysicalBase; UINT32 MmioSize; UINT32 Flags;
                     UINT64 FbPhysicalBase; UINT32 FbSize; } INIT_HW;
    INIT_HW ih; DWORD ret = 0;
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL; ih.MmioSize = 0x80000; ih.Flags = 1;
    DeviceIoControl(g_hDev, IOCTL_GPU_INIT, &ih, sizeof(ih), &ih, sizeof(ih), &ret, NULL);
    printf("GPU_ID: 0x%08X\n\n", ReadReg(0x0000));

    Probe("[0] Baseline");

    /* ---- [1] PSP ring handshake ---- */
    printf("\n[1] PSP ring init + TMR:\n");
    if (RingInit()) RingTmr();
    Probe("[1] after PSP handshake");

    /* ---- [2] SMU wake ---- */
    printf("\n[2] SMU wake (Vid+Freq):\n");
    uint32_t resp = 0;
    int r1 = SmuQ0(0x3B, 99, &resp);   /* ForceGfxVid ~931mV */
    Sleep(50);
    int r2 = SmuQ0(0x39, 1500, &resp); /* ForceGfxFreq 1500MHz */
    printf("  Vid st=%d Freq st=%d\n", r1, r2);
    Probe("[2] after SMU wake");

    /* ---- [3] Disable GFXOFF/CG/PG ---- */
    printf("\n[3] Disable GFXOFF|CG|PG (Q2 0x06 mask 1C):\n");
    int rq = SmuQ2(0x06, 0x1C);
    printf("  Q2 resp=%d\n", rq);
    Sleep(200);
    Probe("[3] after PG disable");

    /* ---- [4] CP firmware via ring ---- */
    printf("\n[4] CP FW load via ring (MEC then RLC):\n");
    RingLoadFw(4, L"\\SystemRoot\\System32\\drivers\\bc-250\\cyan_skillfish2_mec.bin");
    RingLoadFw(8, L"\\SystemRoot\\System32\\drivers\\bc-250\\cyan_skillfish2_rlc.bin");
    Probe("[4] after CP FW load");

    /* ---- [5] RLC_CP_SCHEDULERS ---- */
    printf("\n[5] RLC_CP_SCHEDULERS program (0xA0):\n");
    WriteReg(RLC_SCHED, 0x000000A0);
    uint32_t rs = ReadReg(RLC_SCHED);
    printf("  readback=%08X\n", rs);
    Probe("[5] final");

    printf("\n=== HUNT COMPLETE ===\n");
    printf("If any stage showed OPEN, that's the missing init piece!\n");
    CloseHandle(g_hDev);
    return 0;
}

