// psp-ring-load-ip-fw-test.c - load firmware via the NEW kernel PSP KM ring
// IOCTL IOCTL_AMDBC250_PSP_RING_LOAD_IP_FW (0x80000C20) on the GPU driver
// atikmdag.sys.
//
// The driver reads the firmware file itself (path passed in input), stages it
// into a GPU-visible contiguous buffer, and submits GFX_CMD_ID_LOAD_IP_FW (0x06)
// through the KM GPCOM ring created by PSP_RING_INIT (0x80000C18).
//
// Layout (see amdbc250_dream_kmd.c case 0x80000C20):
//   in  {FwType ULONG, FileName WCHAR[260]}
//   out {Result ULONG, FenceStatus ULONG, RespStatus ULONG,
//        RespFwAddrLo ULONG, RespFwAddrHi ULONG, RespTmrSize ULONG}
//
// GFX_FW_TYPE_* : 1=CP_ME 2=CP_PFP 3=CP_CE 4=CP_MEC 8=RLC_G 9=SDMA0 10=SDMA1 18=SMU
//
// Usage:
//   psp-ring-load-ip-fw-test.exe [type]   (load single type, e.g. 4 = MEC)
//   psp-ring-load-ip-fw-test.exe          (load all known firmware types)
//
// Firmware files must exist in C:\Windows\System32\drivers\bc-250\.
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
#define IOCTL_PSP_RING_LOAD_IP_FW 0x80000C20

#define GFX_FW_TYPE_CP_ME   1
#define GFX_FW_TYPE_CP_PFP  2
#define GFX_FW_TYPE_CP_CE   3
#define GFX_FW_TYPE_CP_MEC  4
#define GFX_FW_TYPE_RLC_G   8
#define GFX_FW_TYPE_SDMA0   9
#define GFX_FW_TYPE_SDMA1   10
#define GFX_FW_TYPE_SMU     18

/* NT kernel path (ZwCreateFile in the driver does NOT accept Win32 "C:\...").
   \SystemRoot = C:\Windows in kernel mode. */
#define FW_DIR  L"\\SystemRoot\\System32\\drivers\\bc-250\\"

typedef struct { UINT32 RegisterOffset; UINT32 Value; } REG_IO;

/* Must mirror the driver's byte layout exactly (Out is a PULONG array):
   driver writes {Result@0, RingPaLo@4, RingPaHi@8, RingSize@12,
                  C2pmsg64@16, C2pmsg81@20} ??? no UINT64 member here to avoid
   MSVC padding between Result and RingPa. */
typedef struct {
    UINT32 Result;
    UINT32 RingPaLo;
    UINT32 RingPaHi;
    UINT32 RingSize;
    UINT32 C2pmsg64;
    UINT32 C2pmsg81;
} RING_INIT_OUT;

typedef struct {
    UINT32 FwType;
    WCHAR  FileName[260];
} LOAD_IP_FW_IN;

typedef struct {
    UINT32 Result;
    UINT32 FenceStatus;
    UINT32 RespStatus;
    UINT32 RespFwAddrLo;
    UINT32 RespFwAddrHi;
    UINT32 RespTmrSize;
} LOAD_IP_FW_OUT;

typedef struct {
    UINT32 Type;
    PCWSTR File;
} FW_ENTRY;

static const FW_ENTRY g_FwTable[] = {
    { GFX_FW_TYPE_CP_ME,   L"cyan_skillfish2_me.bin"    },
    { GFX_FW_TYPE_CP_PFP,  L"cyan_skillfish2_pfp.bin"   },
    { GFX_FW_TYPE_CP_CE,   L"cyan_skillfish2_ce.bin"    },
    { GFX_FW_TYPE_CP_MEC,  L"cyan_skillfish2_mec.bin"   },
    { GFX_FW_TYPE_RLC_G,   L"cyan_skillfish2_rlc.bin"   },
    { GFX_FW_TYPE_SDMA0,   L"navi12_sdma.bin"           },
    { GFX_FW_TYPE_SDMA1,   L"navi12_sdma1.bin"          },
    { GFX_FW_TYPE_SMU,     L"Smu.bin"                   },
};

static const char* FwTypeName(uint32_t t) {
    switch (t) {
        case GFX_FW_TYPE_CP_ME:  return "CP_ME";
        case GFX_FW_TYPE_CP_PFP: return "CP_PFP";
        case GFX_FW_TYPE_CP_CE:  return "CP_CE";
        case GFX_FW_TYPE_CP_MEC: return "CP_MEC";
        case GFX_FW_TYPE_RLC_G:  return "RLC_G";
        case GFX_FW_TYPE_SDMA0:  return "SDMA0";
        case GFX_FW_TYPE_SDMA1:  return "SDMA1";
        case GFX_FW_TYPE_SMU:    return "SMU";
        default: return "?";
    }
}

static int RunLoad(uint32_t fwType, PCWSTR file) {
    LOAD_IP_FW_IN in;
    LOAD_IP_FW_OUT out;
    DWORD returned = 0;
    ZeroMemory(&in, sizeof(in));
    ZeroMemory(&out, sizeof(out));
    in.FwType = fwType;
    wcsncpy_s(in.FileName, 260, FW_DIR, _TRUNCATE);
    wcsncat_s(in.FileName, 260, file, _TRUNCATE);

    printf("[%s] loading '%ls'\n", FwTypeName(fwType), in.FileName);
    BOOL ok = DeviceIoControl(g_hDev, IOCTL_PSP_RING_LOAD_IP_FW,
                              &in, sizeof(in), &out, sizeof(out), &returned, NULL);
    if (!ok) {
        printf("  FAIL: DeviceIoControl gle=%lu\n", GetLastError());
        return -1;
    }
    printf("  Result=0x%08X FenceStatus=%u RespStatus=0x%08X\n",
           out.Result, out.FenceStatus, out.RespStatus);
    printf("  RespFwAddrLo=0x%08X RespFwAddrHi=0x%08X RespTmrSize=0x%08X\n",
           out.RespFwAddrLo, out.RespFwAddrHi, out.RespTmrSize);
    if (out.Result != 1) { printf("  -> rejected\n"); return -2; }
    if (out.FenceStatus != 1) { printf("  -> FENCE TIMEOUT (driver kept staging buffer)\n"); return -3; }
    if (out.RespStatus == 0) {
        printf("  -> SUCCESS: firmware loaded\n");
        return 0;
    } else if (out.RespStatus == 0x00000100) {
        printf("  -> UNKNOWN_COMMAND (0x100): SOS does not implement LOAD_IP_FW for this type\n");
        return 0;
    } else {
        printf("  -> status=0x%08X (nonzero)\n", out.RespStatus);
        return -4;
    }
}

int main(int argc, char* argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("BC-250 PSP ring LOAD_IP_FW test (kernel IOCTL 0x80000C20)\n");

    int onlyType = -1;
    if (argc > 1) {
        onlyType = (int)strtoul(argv[1], NULL, 0);
        printf("Only loading fw_type %u (%s)\n", onlyType, FwTypeName((uint32_t)onlyType));
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

    printf("\n--- PSP_RING_LOAD_IP_FW (0x80000C20) ---\n");
    int rc = 0;
    for (size_t i = 0; i < sizeof(g_FwTable)/sizeof(g_FwTable[0]); i++) {
        if (onlyType >= 0 && (int)g_FwTable[i].Type != onlyType) continue;
        int r = RunLoad(g_FwTable[i].Type, g_FwTable[i].File);
        if (r != 0) rc = r;
        Sleep(150);
    }

    printf("\n=== DONE (rc=%d) ===\n", rc);
    CloseHandle(g_hDev);
    return rc == 0 ? 0 : 1;
}
