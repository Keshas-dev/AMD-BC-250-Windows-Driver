// psp-ring-submit-test.c - exercise the NEW kernel PSP_RING_INIT / PSP_RING_SUBMIT
// IOCTLs (0x80000C18 / 0x80000C1C) on the GPU driver atikmdag.sys.
//
// The driver now owns the ring: PSP_RING_INIT allocates a 4KB contiguous ring,
// programs C2PMSG_69/70/71 and creates a KM GPCOM ring at the CORRECT MP0 base
// (BAR5 byte 0x58000). PSP_RING_SUBMIT builds a psp_gfx_cmd_resp, writes a
// psp_gfx_rb_frame, kicks C2PMSG_67 WPTR and polls the fence.
//
// Layouts (see amdbc250_dream_kmd.c case 0x80000C18 / 0x80000C1C):
//   INIT  : in  {Flags ULONG}
//           out {Result ULONG, RingPa ULONG64, RingSize ULONG, C2pmsg64 ULONG, C2pmsg81 ULONG}
//   SUBMIT: in  {CmdId ULONG, CmdDataSize ULONG, CmdData[0..511]}
//           out {Result ULONG, FenceStatus ULONG, RespStatus ULONG,
//                RespFwAddrLo ULONG, RespFwAddrHi ULONG, RespTmrSize ULONG}
//
// Safe first commands (read-only queries, no firmware load):
//   GFX_CMD_ID_GET_FW_ATTESTATION = 0xF
//   GFX_CMD_ID_GET_FW_ATTESTATION2 = 0x10
//   GFX_CMD_ID_FB_FW_RESERV_ADDR   = 0x50   (returns fw_addr + tmr_size, no args)
//
// Requires: GPU driver atikmdag.sys running (device \\.\AMDBC250DreamV43).
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static HANDLE g_hDev = INVALID_HANDLE_VALUE;

#define IOCTL_GPU_INIT            0x80000B80
#define IOCTL_GPU_READ            0x80000B88
#define IOCTL_GPU_WRITE           0x80000B8C
#define IOCTL_PSP_RING_INIT       0x80000C18
#define IOCTL_PSP_RING_SUBMIT     0x80000C1C

#define GFX_CMD_ID_GET_FW_ATTESTATION  0x0F
#define GFX_CMD_ID_GET_FW_ATTESTATION2 0x10
#define GFX_CMD_ID_FB_FW_RESERV_ADDR   0x50

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

/* Correct MP0 C2PMSG block (BAR5 byte base 0x58000) - for verification only */
#define C2PMSG_64  0x58200
#define C2PMSG_67  0x5820C
#define C2PMSG_81  0x58244

#pragma pack(push, 1)
typedef struct {
    UINT32 Result;
    UINT64 RingPa;
    UINT32 RingSize;
    UINT32 C2pmsg64;
    UINT32 C2pmsg81;
} RING_INIT_OUT;

typedef struct {
    UINT32 Result;
    UINT32 FenceStatus;
    UINT32 RespStatus;
    UINT32 RespFwAddrLo;
    UINT32 RespFwAddrHi;
    UINT32 RespTmrSize;
} RING_SUBMIT_OUT;
#pragma pack(pop)

static const char* GfxCmdName(uint32_t id) {
    switch (id) {
        case 0x01: return "INIT_GPCOM";
        case 0x02: return "INIT_GPCOM_RESP";
        case 0x03: return "LOAD_IP_FW";
        case 0x05: return "SETUP_TMR";
        case 0x07: return "LOAD_ASD";
        case 0x08: return "LOAD_TOC";
        case 0x09: return "LOAD_SOS";
        case 0x0B: return "SETUP_VMR";
        case 0x0F: return "GET_FW_ATTESTATION";
        case 0x10: return "GET_FW_ATTESTATION2";
        case 0x50: return "FB_FW_RESERV_ADDR";
        default: return "?";
    }
}

static int RunSubmit(uint32_t cmdId, const void* data, uint32_t dataSize) {
    UCHAR in[8 + 512];
    RING_SUBMIT_OUT out;
    DWORD returned = 0;
    ZeroMemory(in, sizeof(in));
    ZeroMemory(&out, sizeof(out));
    memcpy(in + 0, &cmdId, 4);
    memcpy(in + 4, &dataSize, 4);
    if (data && dataSize > 0) memcpy(in + 8, data, dataSize);
    BOOL ok = DeviceIoControl(g_hDev, IOCTL_PSP_RING_SUBMIT, in, 8 + dataSize,
                              &out, sizeof(out), &returned, NULL);
    printf("  SUBMIT %-20s (0x%02X): ok=%d gle=%lu\n", GfxCmdName(cmdId), cmdId, ok, GetLastError());
    if (!ok) return -1;
    printf("    Result=0x%08X FenceStatus=%u RespStatus=0x%08X\n",
           out.Result, out.FenceStatus, out.RespStatus);
    printf("    RespFwAddrLo=0x%08X RespFwAddrHi=0x%08X RespTmrSize=0x%08X\n",
           out.RespFwAddrLo, out.RespFwAddrHi, out.RespTmrSize);
    if (out.FenceStatus != 1) return -2;
    if (out.RespStatus == 0) {           /* SUCCESS */
        printf("    -> SUCCESS (status=0)\n");
    } else if (out.RespStatus == 0x00000100) {  /* PSP_ERR_UNKNOWN_COMMAND: not implemented by this SOS */
        printf("    -> UNKNOWN_COMMAND (0x100): SOS does not implement this command (protocol OK)\n");
        return 0;
    } else {
        printf("    -> status=0x%08X (nonzero)\n", out.RespStatus);
        return -3;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("BC-250 PSP ring init+submit test (kernel IOCTLs, correct MP0 base 0x58000)\n");

    /* Optional: restrict to a single cmd id, e.g. "psp-ring-submit-test.exe 0x50" */
    int onlyCmd = -1;
    if (argc > 1) {
        onlyCmd = (int)strtoul(argv[1], NULL, 16);
        printf("Only testing cmd 0x%02X (%s)\n", onlyCmd, GfxCmdName(onlyCmd));
    }

    g_hDev = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (g_hDev == INVALID_HANDLE_VALUE) { printf("FAIL: CreateFile gle=%lu\n", GetLastError()); return 1; }
    printf("CreateFile OK\n");

    /* Map BAR5 (required on Win11 26100 WDM fallback) */
    typedef struct { UINT64 MmioPhysicalBase; UINT32 MmioSize; UINT32 Flags; UINT64 FbPhysicalBase; UINT32 FbSize; } INIT_HW; /* FIX: full 32-byte struct */
    INIT_HW ih; DWORD ret = 0;
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL;
    ih.MmioSize = 0x80000;
    ih.Flags = 1;
    BOOL ok = DeviceIoControl(g_hDev, IOCTL_GPU_INIT, &ih, sizeof(ih), &ih, sizeof(ih), &ret, NULL);
    printf("INIT_HW: ok=%d gle=%lu\n", ok, GetLastError());

    uint32_t gpuId = ReadReg(0x0000);
    if (gpuId == 0xFFFFFFFF) { printf("READ_REG not working\n"); return 1; }
    printf("GPU_ID = 0x%08X\n", gpuId);

    printf("\n--- PSP_RING_INIT (0x80000C18) ---\n");
    RING_INIT_OUT ri;
    DWORD returned = 0;
    ULONG flags = 0;
    ZeroMemory(&ri, sizeof(ri));
    ok = DeviceIoControl(g_hDev, IOCTL_PSP_RING_INIT, &flags, sizeof(flags),
                         &ri, sizeof(ri), &returned, NULL);
    printf("  ok=%d gle=%lu bytesReturned=%lu\n", ok, GetLastError(), returned);
    if (!ok) { CloseHandle(g_hDev); return 1; }
    printf("  Result=0x%08X RingPa=0x%llX RingSize=0x%X\n", ri.Result, ri.RingPa, ri.RingSize);
    printf("  C2pmsg64=0x%08X C2pmsg81=0x%08X\n", ri.C2pmsg64, ri.C2pmsg81);

    if (ri.Result != 1 || ri.RingPa == 0) {
        printf("  FAIL: ring not created (Result=%u)\n", ri.Result);
        CloseHandle(g_hDev);
        return 1;
    }

    /* Verify the driver programmed the hardware (registers readable from user mode too) */
    printf("\n--- Hardware verification (direct BAR5 reads) ---\n");
    printf("  C2PMSG_64 = 0x%08X\n", ReadReg(C2PMSG_64));
    printf("  C2PMSG_67 (wptr) = 0x%08X\n", ReadReg(C2PMSG_67));
    printf("  C2PMSG_81 = 0x%08X\n", ReadReg(C2PMSG_81));

    printf("\n--- PSP_RING_SUBMIT (0x80000C1C) ---\n");

    int rc = 0;
    const uint32_t cmds[] = {
        GFX_CMD_ID_GET_FW_ATTESTATION,
        GFX_CMD_ID_GET_FW_ATTESTATION2,
        GFX_CMD_ID_FB_FW_RESERV_ADDR,
    };
    for (size_t i = 0; i < sizeof(cmds)/sizeof(cmds[0]); i++) {
        if (onlyCmd >= 0 && (int)cmds[i] != onlyCmd) continue;
        printf("[%d] cmd 0x%02X (%s)\n", (int)i, cmds[i], GfxCmdName(cmds[i]));
        int r = RunSubmit(cmds[i], NULL, 0);
        if (r != 0) { rc = r; }
        printf("  C2PMSG_67 (wptr) now = 0x%08X\n", ReadReg(C2PMSG_67));
        Sleep(100);
    }

    /* FB_FW_RESERV_ADDR can also be called with a tmr_size hint arg */
    if (onlyCmd < 0 || onlyCmd == GFX_CMD_ID_FB_FW_RESERV_ADDR) {
        printf("[extra] FB_FW_RESERV_ADDR with tmr_size=0x400000 (4MB)\n");
        uint32_t args[4] = { 0x400000, 0, 0, 0 };
        int r = RunSubmit(GFX_CMD_ID_FB_FW_RESERV_ADDR, args, sizeof(args));
        if (r != 0) rc = r;
    }

    printf("\n=== DONE (rc=%d) ===\n", rc);
    CloseHandle(g_hDev);
    return rc == 0 ? 0 : 1;
}
