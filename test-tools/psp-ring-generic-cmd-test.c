// psp-ring-generic-cmd-test.c
//
// PSP KM (GPCOM) ring generic GFX_CMD_ID probe via IOCTL 0x80000C1C
// (PSP_RING_SUBMIT). Submits a table of candidate commands with optional
// union-cmd payloads and reports Result/Fence/RespStatus + response fields.
//
// Requires: GPU driver atikmdag.sys running (device \\.\AMDBC250DreamV43).
// Run INIT + PSP_RING_INIT first (done here), then each command in order.
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static HANDLE g_hDev = INVALID_HANDLE_VALUE;

#define IOCTL_GPU_INIT        0x80000B80
#define IOCTL_PSP_RING_INIT   0x80000C18
#define IOCTL_PSP_RING_SUBMIT 0x80000C1C

typedef struct { UINT32 RegisterOffset; UINT32 Value; } REG_IO;

typedef struct {
    UINT32 Result;
    UINT32 RingPaLo;
    UINT32 RingPaHi;
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
} SUBMIT_OUT;

/* GFX_CMD_ID_* (psp_gfx_if.h) + union cmd payloads (dwords, copied at +28). */
typedef struct {
    const char* name;
    UINT32 id;
    UINT32 payload[4];
    UINT32 payloadDw;   /* 0 = no payload */
} CMD_SPEC;

static void decode_status(UINT32 st) {
    switch (st) {
    case 0x00000000: printf("SUCCESS"); break;
    case 0x00000100: printf("PSP_ERR_UNKNOWN_COMMAND"); break;
    case 0xFFFF0006: printf("TEE_ERROR_BAD_PARAMETERS"); break;
    case 0xFFFF0008: printf("TEE_ERROR_ITEM_NOT_FOUND"); break;
    case 0xFFFF0000: printf("TEE_ERROR_GENERIC"); break;
    default:
        if (st & 0x80000000) printf("bit31|0x%X", st & 0x7FFFFFFF);
        else printf("0x%08X", st);
    }
}

static int submit(const CMD_SPEC* c) {
    BYTE in[8 + 64];
    SUBMIT_OUT out;
    ZeroMemory(in, sizeof(in));
    ZeroMemory(&out, sizeof(out));
    UINT32 dataSize = c->payloadDw * 4;
    memcpy(in + 0, &c->id, 4);
    memcpy(in + 4, &dataSize, 4);
    if (dataSize) memcpy(in + 8, c->payload, dataSize);

    DWORD returned = 0;
    BOOL ok = DeviceIoControl(g_hDev, IOCTL_PSP_RING_SUBMIT,
                              in, 8 + dataSize, &out, sizeof(out), &returned, NULL);
    printf("[%s] (0x%02X)", c->name, c->id);
    if (!ok) { printf(" IOCTL FAIL gle=%lu\n", GetLastError()); return 1; }
    printf(" Result=%u Fence=%u status=", out.Result, out.FenceStatus);
    decode_status(out.RespStatus);
    printf(" fwAddr=0x%08X%08X tmrSize=0x%X\n",
           out.RespFwAddrHi, out.RespFwAddrLo, out.RespTmrSize);
    if (out.FenceStatus != 1) { printf("  -> FENCE TIMEOUT\n"); return 1; }
    return 0;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("BC-250 PSP ring generic GFX_CMD probe\n");

    g_hDev = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (g_hDev == INVALID_HANDLE_VALUE) { printf("FAIL: CreateFile gle=%lu\n", GetLastError()); return 1; }
    printf("CreateFile OK\n");

    typedef struct { UINT64 MmioPhysicalBase; UINT32 MmioSize; UINT32 Flags; UINT64 FbPhysicalBase; UINT32 FbSize; } INIT_HW; /* FIX: full 32-byte struct */
    INIT_HW ih; DWORD ret = 0;
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL;
    ih.MmioSize = 0x80000;
    ih.Flags = 1;
    BOOL ok = DeviceIoControl(g_hDev, IOCTL_GPU_INIT, &ih, sizeof(ih), &ih, sizeof(ih), &ret, NULL);
    printf("INIT_HW: ok=%d gle=%lu\n", ok, GetLastError());

    printf("\n--- PSP_RING_INIT ---\n");
    RING_INIT_OUT ri; DWORD returned = 0; ULONG flags = 0;
    ZeroMemory(&ri, sizeof(ri));
    ok = DeviceIoControl(g_hDev, IOCTL_PSP_RING_INIT, &flags, sizeof(flags), &ri, sizeof(ri), &returned, NULL);
    printf("  ok=%d gle=%lu Result=%u RingPa=0x%08X%08X C2pmsg64=0x%08X C2pmsg81=0x%08X\n",
           ok, GetLastError(), ri.Result, ri.RingPaHi, ri.RingPaLo, ri.C2pmsg64, ri.C2pmsg81);
    if (!ok || ri.Result != 1 || (ri.RingPaHi == 0 && ri.RingPaLo == 0)) {
        printf("  FAIL: ring not created\n"); CloseHandle(g_hDev); return 1;
    }

    /* Control + candidates. DESTROY_TMR last (would tear down our TMR). */
    CMD_SPEC cmds[] = {
        { "GET_FW_ATTESTATION", 0x0F, {0,0,0,0}, 0 },
        { "SETUP_VMR",          0x09, {0,0,0,0}, 0 },
        { "DESTROY_VMR",        0x0A, {0,0,0,0}, 0 },
        { "PROG_REG",           0x0B, {0,0,0,0}, 2 },   /* reg_value=0, reg_id=0 */
        { "LOAD_TOC",           0x20, {0,0,0,0}, 3 },   /* toc_addr_lo/hi=0, size=0 */
        { "AUTOLOAD_RLC",       0x21, {0,0,0,0}, 0 },
        { "BOOT_CFG GET",       0x24, {0,0,0,0}, 3 },   /* sub_cmd=0 (GET) */
        { "FB_FW_RESERV_ADDR",  0x50, {0,0,0,0}, 0 },
        { "FB_FW_RESERV_EXT",   0x51, {0,0,0,0}, 0 },
        { "DESTROY_TMR",        0x07, {0,0,0,0}, 0 },
    };

    printf("\n--- SUBMIT probes ---\n");
    int rc = 0;
    for (int i = 0; i < (int)(sizeof(cmds) / sizeof(cmds[0])); i++) {
        if (submit(&cmds[i])) rc = 1;
    }

    CloseHandle(g_hDev);
    printf("\n=== DONE rc=%d ===\n", rc);
    return rc;
}
