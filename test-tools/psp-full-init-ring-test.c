#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#pragma pack(push, 1)

/* PSP IOCTLs */
#define IOCTL_PSP_INIT_HW          0x22200C
#define IOCTL_PSP_LOAD_IP_FW_DIRECT 0x222090

typedef struct { ULONG64 PhysicalAddress; ULONG Size; } PSP_INIT_HW_REQUEST;
typedef struct { ULONG FwType; ULONG FwSize; } PSP_LOAD_IP_FW_REQUEST;
typedef struct { ULONG Status; ULONG C2Pmsg35; ULONG C2Pmsg81; ULONG Reserved; } PSP_LOAD_IP_FW_RESPONSE;

/* GPU IOCTLs */
#define IOCTL_INIT_HW     0x80000B80
#define IOCTL_READ_REG    0x80000B88
#define IOCTL_WRITE_REG   0x80000B8C
#define IOCTL_WRITE_PHYS  0x80000C10
#define IOCTL_READ_PHYS   0x80000C14
#define IOCTL_GCVM_SETUP  0x8000098C
#define IOCTL_LOAD_CP_FW  0x80000BD4

typedef struct { UINT32 Off; UINT32 Val; } REG_IO;
#pragma pack(pop)

static HANDLE gPsp = INVALID_HANDLE_VALUE;
static HANDLE gGpu = INVALID_HANDLE_VALUE;
static UINT8 fwBuf[512*1024];
static ULONG fwSize;
static UINT64 ringPa = 0;
#define FENCE_ADDR 0xC0600000ULL

/* Firmware types for PSP */
static const int FW_ME  = 1;
static const int FW_PFP = 2;
static const int FW_CE  = 3;
static const int FW_MEC = 4;
static const int FW_RLC = 8;

static int loadFirmware(const char* path) {
    HANDLE f = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    DWORD read = 0;
    ReadFile(f, fwBuf, sizeof(fwBuf), &read, NULL);
    fwSize = read; CloseHandle(f);
    return (fwSize > 44) ? 1 : 0;
}

static int pspOpen(void) {
    gPsp = CreateFileW(L"\\\\.\\AmdBcPsp", GENERIC_READ|GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL);
    if (gPsp == INVALID_HANDLE_VALUE) { printf("  FAIL: open PSP\n"); return 0; }
    printf("  PSP opened OK\n"); return 1;
}

static int pspInitHw(void) {
    PSP_INIT_HW_REQUEST req = { 0xFE800000ULL, 0x80000 };
    DWORD br = 0; ULONG out = 0;
    BOOL ok = DeviceIoControl(gPsp, IOCTL_PSP_INIT_HW, &req, sizeof(req), &out, sizeof(out), &br, NULL);
    printf("  PSP INIT_HW: %s (VA=0x%08lX)\n", ok ? "OK" : "FAIL", out);
    return ok ? 1 : 0;
}

static int pspLoadFw(int fwType, const char* label) {
    size_t bufSize = sizeof(PSP_LOAD_IP_FW_REQUEST) + fwSize;
    PSP_LOAD_IP_FW_REQUEST* req = (PSP_LOAD_IP_FW_REQUEST*)malloc(bufSize);
    if (!req) return 0;
    req->FwType = fwType;
    req->FwSize = fwSize;
    memcpy((UINT8*)(req + 1), fwBuf, fwSize);
    PSP_LOAD_IP_FW_RESPONSE resp = {0};
    DWORD br = 0;
    BOOL ok = DeviceIoControl(gPsp, IOCTL_PSP_LOAD_IP_FW_DIRECT,
        req, (DWORD)bufSize, &resp, sizeof(resp), &br, NULL);
    int ret = (ok && resp.Status == 0) ? 1 : 0;
    printf("  %s: %s Status=0x%08X C2Pmsg81=0x%08X\n",
        label, ret ? "OK" : "FAIL", resp.Status, resp.C2Pmsg81);
    free(req);
    return ret;
}

static UINT32 R(UINT32 off) {
    REG_IO in={off,0}, out={0}; DWORD br=0;
    DeviceIoControl(gGpu, IOCTL_READ_REG, &in, 8, &out, 8, &br, NULL);
    return out.Val;
}
static void W(UINT32 off, UINT32 val) {
    REG_IO in={off,val}, out={0}; DWORD br=0;
    DeviceIoControl(gGpu, IOCTL_WRITE_REG, &in, 8, &out, 8, &br, NULL);
}
static void WritePhys(UINT64 pa, const void* data, ULONG size) {
    UCHAR buf[4096 + 12];
    ((UINT32*)buf)[0] = (UINT32)(pa & 0xFFFFFFFF);
    ((UINT32*)buf)[1] = (UINT32)(pa >> 32);
    ((UINT32*)buf)[2] = size;
    memcpy(buf + 12, data, size);
    DWORD br = 0;
    DeviceIoControl(gGpu, IOCTL_WRITE_PHYS, buf, 12 + size, NULL, 0, &br, NULL);
}
static ULONG ReadPhys(UINT64 pa, void* data, ULONG size) {
    UCHAR inbuf[24], outbuf[4096];
    ((UINT32*)inbuf)[0] = (UINT32)(pa & 0xFFFFFFFF);
    ((UINT32*)inbuf)[1] = (UINT32)(pa >> 32);
    ((UINT32*)inbuf)[2] = size;
    DWORD br = 0;
    DeviceIoControl(gGpu, IOCTL_READ_PHYS, inbuf, 12, outbuf, sizeof(outbuf), &br, NULL);
    if (br > 0) memcpy(data, outbuf, min(br, size));
    return br;
}

static int gcvmSetup(void) {
    UCHAR ptBuf[256] = {0}; DWORD br = 0;
    if (!DeviceIoControl(gGpu, IOCTL_GCVM_SETUP, NULL, 0, ptBuf, sizeof(ptBuf), &br, NULL)) {
        printf("  GCVM FAIL\n"); return 0;
    }
    UINT32* pt = (UINT32*)ptBuf;
    if (pt[9] != 0xCAFEBABE) { printf("  Bad GCVM\n"); return 0; }
    ringPa = ((UINT64)pt[2] << 32) | pt[1];
    printf("  RingPA=0x%016llX\n", ringPa);
    UINT32 cntl = R(0xB460);
    if (!(cntl & 1)) { W(0xB460, cntl|1); W(0x6C10,1); W(0x6C0C,1); Sleep(10); }
    return (ringPa != 0) ? 1 : 0;
}

int main(void) {
    printf("PSP Full Init + Ring Test\n");
    printf("=========================\n");

    /* === PHASE 1: Load ALL firmware via PSP mailbox === */
    printf("\n--- Phase 1: PSP Firmware Loading ---\n");

    if (!pspOpen()) return 1;
    pspInitHw();

    const struct { int type; const char* name; const char* file; } fwList[] = {
        { FW_ME,  "ME",  "..\\firmware\\cyan_skillfish2_me.bin" },
        { FW_PFP, "PFP", "..\\firmware\\cyan_skillfish2_pfp.bin" },
        { FW_CE,  "CE",  "..\\firmware\\cyan_skillfish2_ce.bin" },
        { FW_MEC, "MEC", "..\\firmware\\cyan_skillfish2_mec.bin" },
        { FW_RLC, "RLC", "..\\firmware\\cyan_skillfish2_rlc.bin" },
    };
    int allOk = 1;
    for (int i = 0; i < 5; i++) {
        printf("\n[%d/5] %s (%s)\n", i+1, fwList[i].name, fwList[i].file);
        if (!loadFirmware(fwList[i].file)) {
            printf("  Cannot open %s\n", fwList[i].file);
            allOk = 0; break;
        }
        if (!pspLoadFw(fwList[i].type, fwList[i].name)) {
            allOk = 0; break;
        }
        Sleep(10);
    }
    CloseHandle(gPsp); gPsp = INVALID_HANDLE_VALUE;
    if (!allOk) { printf("\nFirmware loading FAILED, aborting\n"); return 1; }
    printf("\n*** ALL firmware loaded via PSP OK! ***\n");

    /* === PHASE 2: Open GPU driver and test ring === */
    printf("\n--- Phase 2: MEC Ring Test ---\n");

    gGpu = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL);
    if (gGpu == INVALID_HANDLE_VALUE) { printf("FAIL: open GPU device\n"); return 1; }
    printf("GPU device opened\n");

    UINT8 initBuf[32] = {0};
    *(UINT64*)(initBuf+0)=0xFE800000; *(UINT32*)(initBuf+8)=0x80000;
    *(UINT32*)(initBuf+12)=1; *(UINT64*)(initBuf+16)=0xC0000000;
    *(UINT32*)(initBuf+24)=0x20000000;
    DWORD br=0; DeviceIoControl(gGpu, IOCTL_INIT_HW, initBuf, 32, NULL, 0, &br, NULL);
    UINT64 zero = 0; WritePhys(FENCE_ADDR, &zero, 8);

    printf("\n[1/3] GCVM page tables\n");
    if (!gcvmSetup()) return 1;

    UINT64 mqdPa = ringPa;
    UINT64 rbPa = ringPa + 0x1000;

    printf("\n[2/3] Write MQD + ring\n");
    UINT32 mqd[256] = {0};
    mqd[0] = 0;
    mqd[11] = 0;
    mqd[13] = (2<<0)|(1<<6);
    mqd[14] = (63<<0);
    mqd[64] = (UINT32)(rbPa & 0xFFFFFFFF);
    mqd[65] = (UINT32)(rbPa >> 32);
    mqd[66] = 0x0000000F;
    WritePhys(mqdPa, mqd, 1024);

    UINT32 ring[64] = {0};
    int dw = 0;
    ring[dw++] = 0xC0390003;
    ring[dw++] = 0x00000002;
    ring[dw++] = (UINT32)(FENCE_ADDR & 0xFFFFFFFF);
    ring[dw++] = (UINT32)(FENCE_ADDR >> 32);
    ring[dw++] = 0x0000CAFE;
    ring[dw++] = 0xC0001000;
    UINT32 ringBytes = dw * 4;
    WritePhys(rbPa, ring, ringBytes);
    printf("  MQD at 0x%016llX, Ring at 0x%016llX, %d dwords\n", mqdPa, rbPa, dw);

    UINT32 v[6]; ReadPhys(rbPa, v, 24);
    printf("  Ring[0..5]: 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X\n",
        v[0], v[1], v[2], v[3], v[4], v[5]);

    printf("\n[3/3] KIQ + CP_HQD setup + trigger\n");
    /* Check MEC halt status */
    printf("  CP_MEC_CNTL=0x%08X (bit28=ME1_HALT)\n", R(0x4B14));

    /* Configure KIQ registers */
    W(0xE060, (UINT32)(mqdPa & 0xFFFFFFFF));
    W(0xE064, (UINT32)(mqdPa >> 32));
    W(0xE078, 0);
    W(0xE07C, 0);
    W(0xE080, 1);
    W(0xECA8, 0xA0);
    W(0x9104, (UINT32)(mqdPa & 0xFFFFFFFF));
    W(0x910C, 1);
    Sleep(50);

    printf("  KIQ: BASE=0x%08X_%08X SIZE=0x%08X RPTR=0x%08X WPTR=0x%08X\n",
        R(0xE064), R(0xE060), R(0xE068), R(0xE06C), R(0xE078));
    printf("  KIQ_ACTIVE=0x%08X CP_HQD_ACTIVE=0x%08X CP_MQD_BASE=0x%08X\n",
        R(0xE080), R(0x910C), R(0x9104));
    printf("  PGM_LO=0x%08X PGM_HI=0x%08X\n", R(0x8110), R(0x8114));
    printf("  GRBM_STATUS=0x%08X\n", R(0x3260));

    UINT32 fence = 0; ReadPhys(FENCE_ADDR, &fence, 4);
    printf("\n  Before: RPTR=0x%08X WPTR=0x%08X FENCE=0x%04X\n",
        R(0xE06C), R(0xE078), fence);

    /* Trigger ring processing */
    W(0xE078, ringBytes);
    Sleep(200);

    fence = 0; ReadPhys(FENCE_ADDR, &fence, 4);
    printf("  After:  RPTR=0x%08X WPTR=0x%08X FENCE=0x%04X\n",
        R(0xE06C), R(0xE078), fence);

    printf("\nPolling RPTR+FENCE for 5s...\n");
    int activity = 0;
    for (int i = 0; i < 50; i++) {
        UINT32 rptr = R(0xE06C);
        ReadPhys(FENCE_ADDR, &fence, 4);
        if (rptr != 0 || fence == 0xCAFE) {
            printf("  [%d] RPTR=0x%08X FENCE=0x%04X *** ACTIVITY! ***\n", i, rptr, fence);
            activity = 1; break;
        }
        Sleep(100);
    }
    if (!activity) {
        printf("  No ring activity detected after 5s\n");
        UINT32 grbm = R(0x3260);
        printf("  GRBM_STATUS=0x%08X (GA=0x%08X, ME=0x%08X)\n",
            grbm, R(0x3264), R(0x7A00));
        printf("  KIQ_SIZE=0x%08X (read-only 0 = HW block)\n", R(0xE068));
    }

    fence = 0; ReadPhys(FENCE_ADDR, &fence, 4);
    printf("FINAL: RPTR=0x%08X FENCE=0x%04X GRBM=0x%08X\n",
        R(0xE06C), fence, R(0x3260));

    /* Cleanup */
    W(0x910C, 0); W(0xE080, 0);
    CloseHandle(gGpu);
    printf("\nDone\n");
    return activity ? 0 : 1;
}
