#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

/* IOCTL codes must match GPU driver */
#define FILE_DEVICE_AMDBC250    0x8000
#define IOCTL_INDEX             0x270
#define CTL_CODE_AMDBC250(Function, Method, Access) \
    CTL_CODE(FILE_DEVICE_AMDBC250, IOCTL_INDEX + (Function), Method, Access)
#define IOCTL_AMDBC250_LOAD_CP_FW  CTL_CODE_AMDBC250(0x85, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct {
    UINT32 FwType;       /* IN: 1=ME, 2=PFP, 3=CE */
    UINT32 FwSize;       /* IN: total firmware blob size in bytes */
    UINT32 Result;       /* OUT: 0=fail, 1=success, error codes */
    UINT32 UcodeVersion; /* OUT: firmware version from header */
    /* Firmware data follows immediately after this struct */
} LOAD_CP_FW_REQUEST;

static HANDLE OpenGpu(void) {
    return CreateFileW(L"\\\\.\\AMDBC250DreamV43",
        GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
}

static BOOL ReadReg(HANDLE h, UINT32 off, UINT32 *v) {
    UINT32 ra[2] = {off, 0};
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, 0x80000B88, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
    *v = ra[1]; return ok;
}

static BOOL WriteReg(HANDLE h, UINT32 off, UINT32 v) {
    UINT32 ra[2] = {off, v};
    DWORD br = 0;
    return DeviceIoControl(h, 0x80000B8C, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
}

static BOOL InitNBIO(HANDLE h) {
    UCHAR initIn[32] = {0};
    DWORD br = 0;
    *(UINT64*)(initIn + 0)  = 0xFE800000ULL;
    *(UINT32*)(initIn + 8)  = 0x00080000;
    *(UINT32*)(initIn + 12) = 1;
    *(UINT64*)(initIn + 16) = 0xC0000000ULL;
    *(UINT32*)(initIn + 24) = 0x10000000;
    return DeviceIoControl(h, 0x80000B80, initIn, sizeof(initIn), NULL, 0, &br, NULL);
}

static BOOL LoadCpFw(HANDLE h, UINT32 fwType, const UINT8 *fwData, UINT32 fwSize, UINT32 *result, UINT32 *ucodeVer) {
    UINT32 totalSize = sizeof(LOAD_CP_FW_REQUEST) + fwSize;
    UINT8 *buf = (UINT8 *)malloc(totalSize);
    if (!buf) return FALSE;

    LOAD_CP_FW_REQUEST *req = (LOAD_CP_FW_REQUEST *)buf;
    req->FwType = fwType;
    req->FwSize = fwSize;
    req->Result = 0;
    req->UcodeVersion = 0;
    RtlCopyMemory(buf + sizeof(LOAD_CP_FW_REQUEST), fwData, fwSize);

    LOAD_CP_FW_REQUEST resp = {0};
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_AMDBC250_LOAD_CP_FW,
        buf, totalSize, &resp, sizeof(resp), &br, NULL);

    free(buf);

    if (ok && result) *result = resp.Result;
    if (ok && ucodeVer) *ucodeVer = resp.UcodeVersion;
    return ok;
}

static UINT8 *LoadFile(const char *path, UINT32 *size) {
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return NULL;

    LARGE_INTEGER li;
    GetFileSizeEx(f, &li);
    *size = (UINT32)li.QuadPart;

    UINT8 *buf = (UINT8 *)malloc(*size);
    if (!buf) { CloseHandle(f); return NULL; }

    DWORD br = 0;
    ReadFile(f, buf, *size, &br, NULL);
    CloseHandle(f);

    if (br != *size) { free(buf); return NULL; }
    return buf;
}

int main(int argc, char *argv[]) {
    printf("=== AMD BC-250 Direct CP Firmware Loader ===\n\n");

    /* Parse arguments */
    const char *fwPath = NULL;
    UINT32 fwType = 0;  /* 0 = auto-detect from filename */

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--type") == 0) && i + 1 < argc) {
            i++;
            if (_stricmp(argv[i], "ME") == 0 || _stricmp(argv[i], "1") == 0) fwType = 1;
            else if (_stricmp(argv[i], "PFP") == 0 || _stricmp(argv[i], "2") == 0) fwType = 2;
            else if (_stricmp(argv[i], "CE") == 0 || _stricmp(argv[i], "3") == 0) fwType = 3;
            else if (_stricmp(argv[i], "MEC") == 0 || _stricmp(argv[i], "4") == 0) fwType = 4;
            else { printf("ERROR: Unknown firmware type '%s'\n", argv[i]); return 1; }
        } else if (argv[i][0] != '-') {
            fwPath = argv[i];
        }
    }

    if (!fwPath) {
        printf("Usage: %s <firmware.bin> [-t ME|PFP|CE|MEC]\n", argv[0]);
        printf("\nFirmware types:\n");
        printf("  ME   (or 1) - Graphics ME firmware (command processor)\n");
        printf("  PFP  (or 2) - Pre-fetch parser firmware\n");
        printf("  CE   (or 3) - Copy engine firmware\n");
        printf("  MEC  (or 4) - Multi-engine compute firmware\n");
        printf("\nAuto-detects type from filename:\n");
        printf("  *_me.bin   -> ME\n");
        printf("  *_pfp.bin  -> PFP\n");
        printf("  *_ce.bin   -> CE\n");
        printf("  *_mec.bin  -> MEC\n");
        printf("\nExamples:\n");
        printf("  %s cyan_skillfish2_me.bin\n", argv[0]);
        printf("  %s cyan_skillfish2_pfp.bin -t PFP\n", argv[0]);
        printf("  %s cyan_skillfish2_mec.bin -t MEC\n", argv[0]);
        return 1;
    }

    /* Auto-detect firmware type from filename */
    if (fwType == 0) {
        if (strstr(fwPath, "_me.") || strstr(fwPath, "_ME.")) fwType = 1;
        else if (strstr(fwPath, "_pfp.") || strstr(fwPath, "_PFP.")) fwType = 2;
        else if (strstr(fwPath, "_ce.") || strstr(fwPath, "_CE.")) fwType = 3;
        else if (strstr(fwPath, "_mec") || strstr(fwPath, "_MEC")) fwType = 4;
        else {
            printf("ERROR: Cannot auto-detect firmware type from '%s'\n", fwPath);
            printf("Use -t ME, -t PFP, -t CE, or -t MEC\n");
            return 1;
        }
    }

    const char *typeNames[] = {"", "ME", "PFP", "CE", "MEC"};
    printf("Firmware type: %s (%u)\n", typeNames[fwType], fwType);
    printf("Firmware file: %s\n", fwPath);

    /* Load firmware file from disk */
    UINT32 fwSize = 0;
    UINT8 *fwData = LoadFile(fwPath, &fwSize);
    if (!fwData) {
        printf("ERROR: Cannot load firmware file '%s'\n", fwPath);
        return 1;
    }
    printf("Firmware size: %u bytes\n", fwSize);

    /* Parse header to show info */
    if (fwSize >= 52) {
        UINT32 *hdr = (UINT32 *)fwData;
        printf("Header: total=%u hdrSize=%u ver=%u.%u ucodeSize=%u ucodeOff=%u\n",
            hdr[0], hdr[1], hdr[4], hdr[5], hdr[7], hdr[8]);
    }

    /* Open GPU driver */
    printf("\nOpening GPU driver...\n");
    HANDLE hGpu = OpenGpu();
    if (hGpu == INVALID_HANDLE_VALUE) {
        printf("ERROR: Cannot open GPU driver (err=%u)\n", GetLastError());
        printf("Make sure driver is loaded and running as admin\n");
        free(fwData);
        return 1;
    }
    printf("GPU driver opened OK\n");

    /* Initialize NBIO BAR5 mapping */
    printf("Initializing NBIO BAR5 mapping...\n");
    if (!InitNBIO(hGpu)) {
        printf("WARNING: NBIO init failed (may already be mapped)\n");
    }

    /* Read SCRATCH before */
    UINT32 scratchBefore = 0;
    ReadReg(hGpu, 0x32D4, &scratchBefore);
    printf("SCRATCH before: 0x%08X\n", scratchBefore);

    /* Load firmware via direct MMIO */
    printf("\nLoading %s firmware via direct MMIO...\n", typeNames[fwType]);
    UINT32 result = 0, ucodeVer = 0;
    BOOL ok = LoadCpFw(hGpu, fwType, fwData, fwSize, &result, &ucodeVer);

    if (!ok) {
        printf("ERROR: DeviceIoControl failed (err=%u)\n", GetLastError());
    } else {
        printf("Result: 0x%08X\n", result);
        printf("Ucode version: 0x%08X\n", ucodeVer);

        if (result == 1) {
            printf("\n*** FIRMWARE LOADED SUCCESSFULLY ***\n");
        } else {
            printf("\nFirmware load failed with error code 0x%08X\n", result);
        }
    }

    /* Read SCRATCH after */
    UINT32 scratchAfter = 0;
    ReadReg(hGpu, 0x32D4, &scratchAfter);
    printf("SCRATCH after: 0x%08X\n", scratchAfter);

    /* Read ME_CNTL to check halt state */
    UINT32 meCntl = 0;
    ReadReg(hGpu, 0x4A74, &meCntl);
    printf("ME_CNTL: 0x%08X", meCntl);
    if (meCntl & (1 << 28)) printf(" [ME_HALT]");
    if (meCntl & (1 << 30)) printf(" [PFP_HALT]");
    if (meCntl & (1 << 29)) printf(" [CE_HALT]");
    printf("\n");

    CloseHandle(hGpu);
    free(fwData);

    printf("\nDone.\n");
    return (result == 1) ? 0 : 1;
}
