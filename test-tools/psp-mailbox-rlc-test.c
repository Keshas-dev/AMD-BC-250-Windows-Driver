#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#pragma pack(push, 1)

#define IOCTL_PSP_INIT_HW          0x22200C
#define IOCTL_PSP_GET_STATUS       0x222020
#define IOCTL_PSP_BOOT_SEQUENCE    0x222040
#define IOCTL_PSP_LOAD_IP_FW_DIRECT 0x222090

typedef struct {
    ULONG64 PhysicalAddress;
    ULONG Size;
} PSP_INIT_HW_REQUEST;

typedef struct {
    ULONG C2PMSG_81;
    ULONG C2PMSG_35;
    ULONG C2PMSG_36;
    ULONG PspAlive;
    ULONG FwLoaded;
    ULONG FwSize;
    ULONG FwPaShifted;
    ULONG NbioSig1;
    ULONG NbioSig2;
    ULONG GrbmStatus;
    ULONG MmhubCheck;
    ULONG MmioVA;
    ULONG MmioSize;
    ULONG RingCreated;
    ULONG C2PMSG_37;
    ULONG C2PMSG_64;
    ULONG GcCheck;
    ULONG HdpCheck;
    ULONG MeCntl;
    ULONG GrbmGfxIndex;
} PSP_STATUS_INFO;

typedef struct {
    ULONG FwType;
    ULONG FwSize;
} PSP_LOAD_IP_FW_REQUEST;

typedef struct {
    ULONG Status;
    ULONG C2Pmsg35;
    ULONG C2Pmsg81;
    ULONG Reserved;
} PSP_LOAD_IP_FW_RESPONSE;

#pragma pack(pop)

static HANDLE gH = INVALID_HANDLE_VALUE;

static int OpenPsp(void) {
    gH = CreateFileW(L"\\\\.\\AmdBcPsp", GENERIC_READ|GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL);
    if (gH == INVALID_HANDLE_VALUE) {
        printf("  FAILED: cannot open \\\\.\\AmdBcPsp (err=%lu)\n", GetLastError());
        return 0;
    }
    printf("  \\\\.\\AmdBcPsp opened OK\n");
    return 1;
}

static int InitHw(void) {
    PSP_INIT_HW_REQUEST req = { 0xFE800000ULL, 0x80000 };
    DWORD br = 0;
    ULONG out = 0;
    BOOL ok = DeviceIoControl(gH, IOCTL_PSP_INIT_HW, &req, sizeof(req), &out, sizeof(out), &br, NULL);
    printf("  INIT_HW(PA=0xFE800000, size=0x80000): %s (VA=0x%08lX, err=%lu)\n",
        ok ? "OK" : "FAIL", out, ok ? 0 : GetLastError());
    return ok ? 1 : 0;
}

static int GetStatus(void) {
    PSP_STATUS_INFO info = {0};
    DWORD br = 0;
    BOOL ok = DeviceIoControl(gH, IOCTL_PSP_GET_STATUS, NULL, 0, &info, sizeof(info), &br, NULL);
    if (!ok) {
        printf("  GET_STATUS FAILED (err=%lu)\n", GetLastError());
        return 0;
    }
    printf("  PSP: alive=%u fwLoaded=%u ringCreated=%u\n",
        info.PspAlive, info.FwLoaded, info.RingCreated);
    printf("  C2PMSG: 35=0x%08X 36=0x%08X 37=0x%08X 81=0x%08X\n",
        info.C2PMSG_35, info.C2PMSG_36, info.C2PMSG_37, info.C2PMSG_81);
    printf("  GRBM_STATUS=0x%08X ME_CNTL=0x%08X\n",
        info.GrbmStatus, info.MeCntl);
    return info.FwLoaded;
}

static int BootSequence(void) {
    DWORD br = 0;
    printf("  Loading SYSDRV+SOS firmware (takes ~10s)...\n");
    BOOL ok = DeviceIoControl(gH, IOCTL_PSP_BOOT_SEQUENCE, NULL, 0, NULL, 0, &br, NULL);
    printf("  BOOT_SEQUENCE: %s (err=%lu)\n", ok ? "OK" : "FAIL", ok ? 0 : GetLastError());
    return ok ? 1 : 0;
}

static int LoadIpFwDirect(ULONG fwType, const char* label, const void* fwData, ULONG fwSize) {
    size_t bufSize = sizeof(PSP_LOAD_IP_FW_REQUEST) + fwSize;
    PSP_LOAD_IP_FW_REQUEST* req = (PSP_LOAD_IP_FW_REQUEST*)malloc(bufSize);
    if (!req) { printf("  %s: alloc failed\n", label); return 0; }
    req->FwType = fwType;
    req->FwSize = fwSize;
    memcpy(req + 1, fwData, fwSize);

    PSP_LOAD_IP_FW_RESPONSE resp = {0};
    DWORD br = 0;
    BOOL ok = DeviceIoControl(gH, IOCTL_PSP_LOAD_IP_FW_DIRECT,
        req, (DWORD)bufSize, &resp, sizeof(resp), &br, NULL);
    if (ok) {
        printf("  %s: OK Status=0x%08X C2Pmsg35=0x%08X C2Pmsg81=0x%08X\n",
            label, resp.Status, resp.C2Pmsg35, resp.C2Pmsg81);
    } else {
        printf("  %s: FAILED (err=%lu)\n", label, GetLastError());
    }
    free(req);
    return (ok && resp.Status == 0) ? 1 : 0;
}

int main(void) {
    printf("PSP Mailbox RLC Firmware Load Test\n");
    printf("==================================\n\n");

    /* Step 1: Open PSP driver */
    printf("[1/5] Open PSP driver\n");
    if (!OpenPsp()) return 1;

    /* Step 2: Init HW (map BAR5) */
    printf("\n[2/5] Init HW\n");
    InitHw();

    /* Step 3: Get status (check if PSP firmware already loaded) */
    printf("\n[3/5] PSP Status\n");
    int fwLoaded = GetStatus();

    /* Step 4: Boot sequence if needed */
    if (!fwLoaded) {
        printf("\n[4/5] Boot sequence (SYSDRV+SOS)\n");
        if (!BootSequence()) {
            printf("  WARNING: Boot sequence failed, trying LOAD_IP_FW anyway...\n");
        }
    } else {
        printf("\n[4/5] PSP firmware already loaded, skipping boot\n");
    }

    /* Step 5: Load RLC firmware via mailbox */
    printf("\n[5/5] Load RLC firmware via PSP mailbox\n");
    const char* fwPath = "..\\firmware\\cyan_skillfish2_rlc.bin";
    FILE* fp = fopen(fwPath, "rb");
    if (!fp) { printf("  Cannot open %s\n", fwPath); return 1; }
    fseek(fp, 0, SEEK_END);
    long fwSize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (fwSize <= 0 || fwSize > 1024*1024) {
        printf("  Invalid firmware size %ld\n", fwSize);
        fclose(fp); return 1;
    }
    UINT8* fwBuf = (UINT8*)malloc(fwSize);
    fread(fwBuf, 1, fwSize, fp);
    fclose(fp);
    printf("  Firmware: %s, %ld bytes\n", fwPath, fwSize);

    int rlcOk = LoadIpFwDirect(8, "RLC (fwType=8)", fwBuf, (ULONG)fwSize);

    /* Also try loading MEC for comparison */
    printf("\n--- Also try MEC firmware for comparison ---\n");
    const char* mecPath = "..\\firmware\\cyan_skillfish2_mec.bin";
    fp = fopen(mecPath, "rb");
    if (fp) {
        fseek(fp, 0, SEEK_END);
        long mSize = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        UINT8* mBuf = (UINT8*)malloc(mSize);
        fread(mBuf, 1, mSize, fp);
        fclose(fp);
        printf("  Firmware: %s, %ld bytes\n", mecPath, mSize);
        LoadIpFwDirect(4, "MEC (fwType=4)", mBuf, (ULONG)mSize);
        free(mBuf);
    }

    printf("\n");
    if (rlcOk) {
        printf("RESULT: RLC firmware loaded SUCCESS via PSP mailbox!\n");
    } else {
        printf("RESULT: RLC firmware loading FAILED\n");
        printf("  -> Check PSP C2Pmsg81 status code for SOS firmware support\n");
    }

    free(fwBuf);
    if (gH != INVALID_HANDLE_VALUE) CloseHandle(gH);
    printf("\nDone\n");
    return rlcOk ? 0 : 1;
}
