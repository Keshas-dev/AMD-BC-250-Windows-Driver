/*
 * bc250ctl.c — BC-250 Control Suite (unified CLI).
 *
 * Consolidates the PROVEN Windows-host control surface of the BC-250
 * (Cyan Skillfish / RDNA2 mining ASIC). The host is LOCKED OUT of
 * everything SOS owns (GFX/KIQ rings, DCN timing, FB_LOCATION,
 * doorbell, WGP fuse) — see memory mem_1783592466205. What the
 * host CAN do (proven):
 *   - SMU via SMN (NBIO BAR5+0x38/0x3C) on the GPU driver
 *   - PSP mailbox firmware load on the PSP driver
 *   - register read/write proxy (writable aliases) on the GPU driver
 *
 * Subcommands:
 *   info                 GPU id, SMU ver, features, freq, active wgp
 *   features             read + decode enabled SMU features
 *   freq <mhz> [mv]    SAFE set: unforce, force vid(mv), force freq
 *   vid <mv>             force GFX voltage (mV)
 *   unforce              release forced freq + vid
 *   psp status           PSP alive / fwLoaded / ringCreated
 *   psp load <type>     load fw via mailbox (rlc mec me pfp ce)
 *   reg r <off>          read BAR5 register
 *   reg w <off> <val>    write BAR5 register
 *
 * Uses existing driver IOCTLs (no driver rebuild needed).
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

/* ---- GPU driver (\\.\AMDBC250DreamV43) ---- */
static HANDLE gGpu = INVALID_HANDLE_VALUE;
static BOOL GpuWriteReg(UINT32 off, UINT32 val) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD b = 0;
    r.RegisterOffset = off; r.Value = val;
    return DeviceIoControl(gGpu, IOCTL_AMDBC250_WRITE_REG, &r, sizeof(r), &r, sizeof(r), &b, NULL);
}
static UINT32 GpuReadReg(UINT32 off) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD b = 0;
    r.RegisterOffset = off; r.Value = 0;
    if (DeviceIoControl(gGpu, IOCTL_AMDBC250_READ_REG, &r, sizeof(r), &r, sizeof(r), &b, NULL)) return r.Value;
    return 0xFFFFFFFF;
}
static UINT32 SmnRead(UINT32 smn) {
    GpuWriteReg(0x38, smn); GpuReadReg(0x38); return GpuReadReg(0x3C);
}
static BOOL SmnWrite(UINT32 smn, UINT32 val) {
    if (!GpuWriteReg(0x38, smn)) return FALSE;
    return GpuWriteReg(0x3C, val);
}
#define C2PMSG_66 0x03B10A08   /* Q0 cmd */
#define C2PMSG_82 0x03B10A48   /* Q0 arg/resp */
#define C2PMSG_90 0x03B10A68   /* Q0 ctrl/rsp */
#define Q3_CMD     0x03B10A20   /* Q3 cmd (cyan-skillfish-governor) */
#define Q3_RSP     0x03B10A80   /* Q3 ctrl/rsp */
#define Q3_ARG     0x03B10A88   /* Q3 arg */

static int WaitRsp(UINT32 rspReg, UINT32 exp, int ms) {
    for (int i = 0; i < ms; i++) { if (SmnRead(rspReg) == exp) return 1; Sleep(1); }
    return 0;
}
/* Generic SMU query: clear rsp, write arg, write cmd, poll (per bc250_smu_oc protocol) */
static UINT32 SmuQueryEx(UINT32 cmdReg, UINT32 rspReg, UINT32 argReg,
                        UINT16 msg, UINT32 param) {
    if (SmnRead(rspReg) == 1) SmnWrite(rspReg, 0);
    SmnWrite(argReg, param);
    SmnWrite(cmdReg, msg);
    if (!WaitRsp(rspReg, 1, 1000)) return 0xFFFFFFFF;
    return SmnRead(argReg);
}
/* Queue 0 (force freq/vid — requires SOS allow_queue0, proven safe w/ vid+profile) */
static UINT32 SmuQuery(UINT16 msg)                     { return SmuQueryEx(C2PMSG_66, C2PMSG_90, C2PMSG_82, msg, 0); }
static UINT32 SmuQueryParam(UINT16 msg, UINT32 p)     { return SmuQueryEx(C2PMSG_66, C2PMSG_90, C2PMSG_82, msg, p); }
/* Queue 3 (temp 0x8C, perf profile 0x1E) */
static UINT32 SmuQueryQ3(UINT16 msg)                  { return SmuQueryEx(Q3_CMD, Q3_RSP, Q3_ARG, msg, 0); }
static UINT32 SmuQueryParamQ3(UINT16 msg, UINT32 p)  { return SmuQueryEx(Q3_CMD, Q3_RSP, Q3_ARG, msg, p); }

/* ---- PSP driver (\\.\AmdBcPsp) ---- */
#define IOCTL_PSP_INIT_HW           0x22200C
#define IOCTL_PSP_GET_STATUS        0x222020
#define IOCTL_PSP_BOOT_SEQUENCE   0x222040
#define IOCTL_PSP_LOAD_IP_FW_DIRECT 0x222090

#pragma pack(push, 1)
typedef struct { ULONG64 PhysicalAddress; ULONG Size; } PSP_INIT_HW_REQUEST;
typedef struct {
    ULONG C2PMSG_81, C2PMSG_35, C2PMSG_36, PspAlive, FwLoaded, FwSize,
          FwPaShifted, NbioSig1, NbioSig2, GrbmStatus, MmhubCheck, MmioVA,
          MmioSize, RingCreated, C2PMSG_37, C2PMSG_64, GcCheck, HdpCheck,
          MeCntl, GrbmGfxIndex;
} PSP_STATUS_INFO;
typedef struct { ULONG FwType; ULONG FwSize; } PSP_LOAD_IP_FW_REQUEST;
typedef struct { ULONG Status; ULONG C2Pmsg35; ULONG C2Pmsg81; ULONG Reserved; } PSP_LOAD_IP_FW_RESPONSE;
#pragma pack(pop)

static HANDLE gPsp = INVALID_HANDLE_VALUE;

static int PspOpen(void) {
    gPsp = CreateFileW(L"\\\\.\\AmdBcPsp", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (gPsp == INVALID_HANDLE_VALUE) { printf("  [psp] cannot open \\\\.\\AmdBcPsp (err=%lu)\n", GetLastError()); return 0; }
    return 1;
}
static int PspInitHw(void) {
    PSP_INIT_HW_REQUEST req = { 0xFE800000ULL, 0x80000 };
    DWORD br = 0; ULONG out = 0;
    BOOL ok = DeviceIoControl(gPsp, IOCTL_PSP_INIT_HW, &req, sizeof(req), &out, sizeof(out), &br, NULL);
    printf("  [psp] INIT_HW: %s (VA=0x%08lX)\n", ok ? "OK" : "FAIL", out);
    return ok ? 1 : 0;
}
static int PspGetStatus(void) {
    PSP_STATUS_INFO info = {0}; DWORD br = 0;
    if (!DeviceIoControl(gPsp, IOCTL_PSP_GET_STATUS, NULL, 0, &info, sizeof(info), &br, NULL)) {
        printf("  [psp] GET_STATUS FAILED (err=%lu)\n", GetLastError()); return 0;
    }
    printf("  [psp] alive=%u fwLoaded=%u ringCreated=%u\n", info.PspAlive, info.FwLoaded, info.RingCreated);
    printf("  [psp] C2PMSG: 35=0x%08X 36=0x%08X 81=0x%08X\n", info.C2PMSG_35, info.C2PMSG_36, info.C2PMSG_81);
    return info.FwLoaded;
}
static int PspLoadFw(ULONG fwType, const char* name, const char* path) {
    FILE* fp = fopen(path, "rb");
    if (!fp) { printf("  [psp] cannot open %s\n", path); return 0; }
    fseek(fp, 0, SEEK_END); long sz = ftell(fp); fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 1024 * 1024) { printf("  [psp] bad size %ld for %s\n", sz, name); fclose(fp); return 0; }
    UINT8* buf = (UINT8*)malloc(sz); fread(buf, 1, sz, fp); fclose(fp);
    size_t reqSz = sizeof(PSP_LOAD_IP_FW_REQUEST) + sz;
    PSP_LOAD_IP_FW_REQUEST* req = (PSP_LOAD_IP_FW_REQUEST*)malloc(reqSz);
    req->FwType = fwType; req->FwSize = (ULONG)sz; memcpy(req + 1, buf, sz);
    PSP_LOAD_IP_FW_RESPONSE resp = {0}; DWORD br = 0;
    BOOL ok = DeviceIoControl(gPsp, IOCTL_PSP_LOAD_IP_FW_DIRECT, req, (DWORD)reqSz, &resp, sizeof(resp), &br, NULL);
    int rc = 0;
    if (ok) {
        printf("  [psp] %s: OK Status=0x%08X C2Pmsg35=0x%08X C2Pmsg81=0x%08X\n",
               name, resp.Status, resp.C2Pmsg35, resp.C2Pmsg81);
        rc = (resp.Status == 0) ? 1 : 0;
    } else printf("  [psp] %s FAILED (err=%lu)\n", name, GetLastError());
    free(req); free(buf); return rc;
}

/* ---- helpers ---- */
static int mv_to_vid(int mv) {
    /* vid = round((1.55 - mv/1000) / 0.00625) */
    return (int)((1.55 - (double)mv / 1000.0) / 0.00625 + 0.5);
}
static void decode_features(UINT32 f) {
    printf("    GFXCLK DPM : %s\n", (f & 1) ? "ON" : "off");
    printf("    GFXOFF      : %s\n", (f & 4) ? "ON" : "off");
    printf("    CG (clk gating): %s\n", (f & 8) ? "ON" : "off");
    printf("    PG (pwr gating): %s\n", (f & 16) ? "ON" : "off");
}

/* ---- subcommands ---- */
static void cmd_info(void) {
    printf("=== BC-250 Info ===\n");
    printf("GPU_ID      : 0x%08X\n", GpuReadReg(0x0000));
    printf("SCRATCH     : 0x%08X\n", GpuReadReg(0x32D4));
    UINT32 v;
    v = SmuQuery(0x2); printf("SMU Version : 0x%08X (%u.%u.%u)\n", v, (v>>16)&0xFF, (v>>8)&0xFF, v&0xFF);
    v = SmuQuery(0x3); printf("Driver IF  : %u\n", v);
    v = SmuQuery(0x3D); printf("Features   : 0x%08X\n", v); decode_features(v);
    v = SmuQuery(0x37); printf("GFX Freq   : %u MHz\n", v);
    v = SmuQuery(0x38); printf("GFX Vid    : 0x%08X\n", v);
    v = SmuQuery(0x1E); printf("Active WGP : %u\n", v);
    v = SmuQuery(0x0C); printf("Core Pstate: 0x%08X\n", v);
    v = SmuQuery(0x13); printf("DF Pstate  : 0x%08X\n", v);
}

static void cmd_features(void) {
    UINT32 v = SmuQuery(0x3D);
    printf("Enabled SMU features: 0x%08X\n", v);
    decode_features(v);
}

/* governor safe points (AGENTS.md default-config.toml) */
static int safe_point(int mhz, int* mv, int* prof) {
    int tbl[][3] = { {500,700,1},{800,750,1},{1000,800,1},{1175,850,3},
                        {1400,900,3},{1600,950,3},{1800,1000,3},{2000,1050,3} };
    for (int i = 0; i < 8; i++)
        if (tbl[i][0] >= mhz) { *mv = tbl[i][1]; *prof = tbl[i][2]; return tbl[i][0]; }
    *mv = 1050; *prof = 3; return 2000;
}

static int cmd_freq(int argc, char** argv) {
    if (argc < 1) { printf("usage: bc250ctl freq <mhz>\n"); return 1; }
    int mhz = atoi(argv[0]);
    if (mhz <= 0) { printf("bad mhz\n"); return 1; }
    int mv, prof;
    int target = safe_point(mhz, &mv, &prof);
    if (mhz > target) { printf("  clamp %d->%d MHz (nearest safe point)\n", mhz, target); mhz = target; }
    int vid = mv_to_vid(mv);
    /* Full governor sequence (AGENTS.md "proven safe"): Q3 temp -> Q0 unforce
       -> Q3 profile -> Q0 force_vid -> Q0 force_freq */
    printf("  [q3] SetGpuMaxTemp(80)...\n"); SmuQueryParamQ3(0x8C, 80);
    printf("  [q0] UnforceGfxFreq(0x3A)...\n"); SmuQuery(0x3A);
    printf("  [q0] UnforceGfxVid(0x3C, ignore)...\n"); SmuQuery(0x3C);
    printf("  [q3] SetPerfProfile(%d)...\n", prof); SmuQueryParamQ3(0x1E, (UINT32)prof);
    printf("  [q0] ForceGfxVid %d mV -> vid=0x%X (0x3B)...\n", mv, vid); SmuQueryParam(0x3B, (UINT32)vid);
    printf("  [q0] ForceGfxFreq %d MHz (0x39)...\n", mhz); SmuQueryParam(0x39, (UINT32)mhz);
    Sleep(400);
    UINT32 f = SmuQuery(0x37);
    printf("  GFX Freq after: %u MHz (target %d)\n", f, mhz);
    return 0;
}

static int cmd_profile(int argc, char** argv) {
    if (argc < 1) { printf("usage: bc250ctl profile <1|3>\n"); return 1; }
    int p = atoi(argv[0]);
    if (p != 1 && p != 3) { printf("profile must be 1 or 3\n"); return 1; }
    printf("  [q3] SetPerfProfileIndex(%d)...\n", p);
    UINT32 r = SmuQueryParamQ3(0x1E, (UINT32)p);
    printf("  resp=0x%08X\n", r);
    return 0;
}

static void cmd_sensors(void) {
    printf("=== BC-250 Sensors (SMU) ===\n");
    UINT32 v;
    v = SmuQuery(0x37); printf("GFX Freq   : %u MHz\n", v);
    v = SmuQuery(0x38); printf("GFX Vid    : 0x%08X\n", v);
    v = SmuQuery(0x1E); printf("Active WGP : %u\n", v);
    v = SmuQuery(0x0C); printf("Core Pstate: 0x%08X\n", v);
    v = SmuQuery(0x13); printf("DF Pstate  : 0x%08X\n", v);
    v = SmuQuery(0x3D); printf("Features   : 0x%08X (GFXOFF=%s CG=%s PG=%s)\n",
        v, (v&4)?"ON":"off", (v&8)?"ON":"off", (v&16)?"ON":"off");
}

static int cmd_vid(int argc, char** argv) {
    if (argc < 1) { printf("usage: bc250ctl vid <mv>\n"); return 1; }
    int mv = atoi(argv[0]); if (mv <= 0) { printf("bad mv\n"); return 1; }
    int vid = mv_to_vid(mv);
    printf("  force vid %d mV -> vid=0x%X (0x3B)...\n", mv, vid);
    SmuQueryParam(0x3B, (UINT32)vid);
    return 0;
}

static int cmd_unforce(void) {
    printf("  unforce freq (0x3A)...\n"); SmuQuery(0x3A);
    printf("  unforce vid  (0x3C)...\n"); SmuQuery(0x3C);
    Sleep(200);
    printf("  GFX Freq: %u MHz\n", SmuQuery(0x37));
    return 0;
}

static int cmd_psp(int argc, char** argv) {
    if (argc < 1) { printf("usage: bc250ctl psp <status|load>\n"); return 1; }
    if (!PspOpen()) return 1;
    PspInitHw();
    if (!strcmp(argv[0], "status")) { PspGetStatus(); return 0; }
    if (!strcmp(argv[0], "load")) {
        if (argc < 2) { printf("usage: bc250ctl psp load <rlc|mec|me|pfp|ce>\n"); return 1; }
        const char* t = argv[1];
        ULONG fwType; const char *file, *label;
        if (!strcmp(t, "rlc")) { fwType = 8; file = "..\\firmware\\cyan_skillfish2_rlc.bin"; label = "RLC"; }
        else if (!strcmp(t, "mec")) { fwType = 4; file = "..\\firmware\\cyan_skillfish2_mec.bin"; label = "MEC"; }
        else if (!strcmp(t, "me"))  { fwType = 1; file = "..\\firmware\\cyan_skillfish2_me.bin";  label = "ME"; }
        else if (!strcmp(t, "pfp")) { fwType = 2; file = "..\\firmware\\cyan_skillfish2_pfp.bin"; label = "PFP"; }
        else if (!strcmp(t, "ce"))  { fwType = 3; file = "..\\firmware\\cyan_skillfish2_ce.bin";  label = "CE"; }
        else { printf("unknown fw type '%s'\n", t); return 1; }
        int fwLoaded = PspGetStatus();
        if (!fwLoaded) {
            DWORD br = 0;
            if (!DeviceIoControl(gPsp, IOCTL_PSP_BOOT_SEQUENCE, NULL, 0, NULL, 0, &br, NULL))
                printf("  [psp] boot sequence failed (continuing)\n");
            else printf("  [psp] boot sequence OK\n");
        }
        PspLoadFw(fwType, label, file);
        return 0;
    }
    printf("unknown psp subcommand '%s'\n", argv[0]);
    return 1;
}

static int cmd_reg(int argc, char** argv) {
    if (argc < 1) { printf("usage: bc250ctl reg <r|w> <off> [val]\n"); return 1; }
    if (!strcmp(argv[0], "r")) {
        if (argc < 2) { printf("usage: bc250ctl reg r <off>\n"); return 1; }
        UINT32 off = (UINT32)strtoul(argv[1], NULL, 0);
        printf("  REG[0x%04X] = 0x%08X\n", off, GpuReadReg(off));
    } else if (!strcmp(argv[0], "w")) {
        if (argc < 3) { printf("usage: bc250ctl reg w <off> <val>\n"); return 1; }
        UINT32 off = (UINT32)strtoul(argv[1], NULL, 0);
        UINT32 val = (UINT32)strtoul(argv[2], NULL, 0);
        GpuWriteReg(off, val);
        printf("  REG[0x%04X] := 0x%08X (readback 0x%08X)\n", off, val, GpuReadReg(off));
    } else { printf("unknown reg op '%s'\n", argv[0]); return 1; }
    return 0;
}

static int OpenGpu(void) {
    gGpu = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (gGpu == INVALID_HANDLE_VALUE) { printf("cannot open GPU driver \\\\.\\AMDBC250DreamV43 (err=%lu)\n", GetLastError()); return 0; }
    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br = 0;
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL; ih.MmioSize = 0x80000; ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
    if (!DeviceIoControl(gGpu, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &br, NULL)) {
        printf("INIT_HARDWARE failed (err=%lu) — register access may not work\n", GetLastError());
    }
    return 1;
}

static void usage(void) {
    printf("BC-250 Control Suite\n");
    printf("  bc250ctl info\n");
    printf("  bc250ctl features\n");
    printf("  bc250ctl freq <mhz>\n");
    printf("  bc250ctl vid <mv>\n");
    printf("  bc250ctl unforce\n");
    printf("  bc250ctl profile <1|3>\n");
    printf("  bc250ctl sensors\n");
    printf("  bc250ctl psp status\n");
    printf("  bc250ctl psp load <rlc|mec|me|pfp|ce>\n");
    printf("  bc250ctl reg r <off>\n");
    printf("  bc250ctl reg w <off> <val>\n");
}

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 0; }
    if (!OpenGpu()) return 1;
    int rc = 0;
    if      (!strcmp(argv[1], "info"))     cmd_info();
    else if (!strcmp(argv[1], "features")) cmd_features();
    else if (!strcmp(argv[1], "freq"))     rc = cmd_freq(argc - 2, argv + 2);
    else if (!strcmp(argv[1], "vid"))      rc = cmd_vid(argc - 2, argv + 2);
    else if (!strcmp(argv[1], "unforce"))  rc = cmd_unforce();
    else if (!strcmp(argv[1], "profile"))  rc = cmd_profile(argc - 2, argv + 2);
    else if (!strcmp(argv[1], "sensors"))  { cmd_sensors(); rc = 0; }
    else if (!strcmp(argv[1], "psp"))      rc = cmd_psp(argc - 2, argv + 2);
    else if (!strcmp(argv[1], "reg"))      rc = cmd_reg(argc - 2, argv + 2);
    else { printf("unknown command '%s'\n", argv[1]); usage(); rc = 1; }
    if (gGpu != INVALID_HANDLE_VALUE) CloseHandle(gGpu);
    if (gPsp != INVALID_HANDLE_VALUE) CloseHandle(gPsp);
    return rc;
}
