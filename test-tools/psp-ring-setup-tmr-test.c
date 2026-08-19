// psp-ring-setup-tmr-test.c
//
// PSP SETUP_TMR via the KM GPCOM ring (IOCTL 0x80000C24, GFX_CMD_ID_SETUP_TMR 0x05).
// Gives the SOS a TMR region so subsequent LOAD_IP_FW commands have somewhere
// to place firmware (Linux PSP_TMR_SIZE default 4MB).
//
// Usage: psp-ring-setup-tmr-test.exe [size]   size = TMR bytes, 0 = default 4MB
//
// Requires: GPU driver atikmdag.sys running (device \\.\AMDBC250DreamV43).
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static HANDLE g_hDev = INVALID_HANDLE_VALUE;

#define IOCTL_GPU_INIT            0x80000B80
#define IOCTL_PSP_RING_INIT       0x80000C18
#define IOCTL_PSP_RING_SETUP_TMR  0x80000C24

typedef struct { UINT32 RegisterOffset; UINT32 Value; } REG_IO;

/* Must mirror the driver's byte layout exactly (Out is a PULONG array):
   {Result@0, RingPaLo@4, RingPaHi@8, RingSize@12, C2pmsg64@16, C2pmsg81@20}. */
typedef struct {
    UINT32 Result;
    UINT32 RingPaLo;
    UINT32 RingPaHi;
    UINT32 RingSize;
    UINT32 C2pmsg64;
    UINT32 C2pmsg81;
} RING_INIT_OUT;

typedef struct {
    UINT32 TmrSize;         /* 0 = default 4MB */
    UINT64 TmrPhysicalBase; /* 0 = driver uses fixed VRAM region (must be 0) */
} SETUP_TMR_IN;

typedef struct {
    UINT32 Result;
    UINT32 FenceStatus;
    UINT32 RespStatus;
    UINT32 RespFwAddrLo;
    UINT32 RespFwAddrHi;
    UINT32 RespTmrSize;
    UINT32 TmrPaLo;
    UINT32 TmrPaHi;
    UINT32 TmrMcLo;
    UINT32 TmrMcHi;
} SETUP_TMR_OUT;

int main(int argc, char* argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("BC-250 PSP ring SETUP_TMR test (kernel IOCTL 0x80000C24)\n");

    UINT32 size = 0;
    if (argc > 1) size = (UINT32)strtoul(argv[1], NULL, 0);
    printf("Requested TMR size: %u (0x%X)%s\n", size, size, size == 0 ? " [default 4MB]" : "");

    g_hDev = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (g_hDev == INVALID_HANDLE_VALUE) { printf("FAIL: CreateFile gle=%lu\n", GetLastError()); return 1; }
    printf("CreateFile OK\n");

    /* Map BAR5 (required on Win11 26100 WDM fallback) */
    typedef struct { UINT64 MmioPhysicalBase; UINT32 MmioSize; UINT32 Flags; } INIT_HW;
    INIT_HW ih; DWORD ret = 0;
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL;
    ih.MmioSize = 0x80000;
    ih.Flags = 1;
    BOOL ok = DeviceIoControl(g_hDev, IOCTL_GPU_INIT, &ih, sizeof(ih), &ih, sizeof(ih), &ret, NULL);
    printf("INIT_HW: ok=%d gle=%lu\n", ok, GetLastError());

    printf("\n--- PSP_RING_INIT (0x80000C18) ---\n");
    RING_INIT_OUT ri;
    DWORD returned = 0;
    ULONG flags = 0;
    ZeroMemory(&ri, sizeof(ri));
    ok = DeviceIoControl(g_hDev, IOCTL_PSP_RING_INIT, &flags, sizeof(flags),
                         &ri, sizeof(ri), &returned, NULL);
    printf("  ok=%d gle=%lu\n", ok, GetLastError());
    if (!ok) { CloseHandle(g_hDev); return 1; }
    {
        UINT64 ringPa = ((UINT64)ri.RingPaHi << 32) | ri.RingPaLo;
        printf("  Result=0x%08X RingPa=0x%llX RingSize=0x%X\n", ri.Result, ringPa, ri.RingSize);
        printf("  C2pmsg64=0x%08X C2pmsg81=0x%08X\n", ri.C2pmsg64, ri.C2pmsg81);
        if (ri.Result != 1 || ringPa == 0) {
            printf("  FAIL: ring not created\n");
            CloseHandle(g_hDev);
            return 1;
        }
    }

    printf("\n--- PSP_RING_SETUP_TMR (0x80000C24) ---\n");
    SETUP_TMR_IN in;
    SETUP_TMR_OUT out;
    ZeroMemory(&in, sizeof(in));
    ZeroMemory(&out, sizeof(out));
    in.TmrSize = size;
    in.TmrPhysicalBase = 0;

    ok = DeviceIoControl(g_hDev, IOCTL_PSP_RING_SETUP_TMR,
                         &in, sizeof(in), &out, sizeof(out), &returned, NULL);
    if (!ok) {
        printf("  FAIL: DeviceIoControl gle=%lu\n", GetLastError());
        CloseHandle(g_hDev);
        return 1;
    }
    printf("  Result=0x%08X FenceStatus=%u RespStatus=0x%08X\n",
           out.Result, out.FenceStatus, out.RespStatus);
    printf("  RespFwAddrLo=0x%08X RespFwAddrHi=0x%08X RespTmrSize=0x%08X\n",
           out.RespFwAddrLo, out.RespFwAddrHi, out.RespTmrSize);
    {
        UINT64 tmrPa = ((UINT64)out.TmrPaHi << 32) | out.TmrPaLo;
        UINT64 tmrMc = ((UINT64)out.TmrMcHi << 32) | out.TmrMcLo;
        printf("  system_phy_addr=0x%llX  buf_phy_addr(MC)=0x%llX\n", tmrPa, tmrMc);
        printf("  (align 1MB: %s / %s)\n",
               (tmrPa & 0xFFFFF) == 0 ? "pa yes" : "pa NO",
               (tmrMc & 0xFFFFF) == 0 ? "mc yes" : "mc NO");
    }
    if (out.Result != 1) { printf("  -> rejected\n"); CloseHandle(g_hDev); return 1; }
    if (out.FenceStatus != 1) { printf("  -> FENCE TIMEOUT\n"); CloseHandle(g_hDev); return 1; }
    if (out.RespStatus == 0) {
        printf("  -> SUCCESS: TMR region set up (size reported 0x%X)\n", out.RespTmrSize);
        CloseHandle(g_hDev);
        return 0;
    } else if (out.RespStatus == 0x00000100) {
        printf("  -> UNKNOWN_COMMAND (0x100): SOS does not implement SETUP_TMR\n");
        CloseHandle(g_hDev);
        return 0;
    } else {
        printf("  -> status=0x%08X (nonzero)\n", out.RespStatus);
        CloseHandle(g_hDev);
        return 1;
    }
}