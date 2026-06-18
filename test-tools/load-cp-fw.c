#include <windows.h>
#include <stdio.h>

#define IOCTL_AMDBC250_INIT_HARDWARE  0x80000B80
#define IOCTL_AMDBC250_LOAD_CP_FW     0x80000BD4
#define IOCTL_AMDBC250_GPU_KIQ_TEST   0x80000BD0

typedef struct {
    UINT32 Result;
    UINT32 ScratchBefore;
    UINT32 ScratchAfter;
    UINT32 MmioMapped;
    UINT32 RingAllocated;
    UINT32 HqdProgrammed;
    UINT32 Pm4Submitted;
    UINT32 RingWptr;
} GPU_KIQ_TEST_OUT;

typedef struct {
    UINT32 FwType;
    UINT32 FwSize;
    UINT32 Result;
    UINT32 UcodeVersion;
} LOAD_CP_FW_HDR;

#define MAX_FW_SIZE (4 * 1024 * 1024)

static BOOL LoadFirmware(HANDLE hDev, const char *path, UINT32 fwType, const char *typeName) {
    HANDLE hFw = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFw == INVALID_HANDLE_VALUE) {
        printf("  Cannot open %s firmware: %s (err=%lu)\n", typeName, path, GetLastError());
        return FALSE;
    }

    DWORD fwSize = GetFileSize(hFw, NULL);
    if (fwSize == INVALID_FILE_SIZE || fwSize < 44 || fwSize > MAX_FW_SIZE) {
        printf("  Invalid %s firmware size: %lu\n", typeName, fwSize);
        CloseHandle(hFw);
        return FALSE;
    }

    UINT32 totalBuf = sizeof(LOAD_CP_FW_HDR) + fwSize;
    UINT8 *buf = (UINT8 *)malloc(totalBuf);
    if (!buf) { CloseHandle(hFw); return FALSE; }

    LOAD_CP_FW_HDR *hdr = (LOAD_CP_FW_HDR *)buf;
    hdr->FwType = fwType;
    hdr->FwSize = fwSize;
    hdr->Result = 0;
    hdr->UcodeVersion = 0;

    DWORD br = 0;
    ReadFile(hFw, buf + sizeof(LOAD_CP_FW_HDR), fwSize, &br, NULL);
    CloseHandle(hFw);

    if (br != fwSize) {
        printf("  Short read on %s firmware: %lu < %lu\n", typeName, br, fwSize);
        free(buf);
        return FALSE;
    }

    LOAD_CP_FW_HDR out = {0};
    DWORD br2 = 0;
    BOOL ok = DeviceIoControl(hDev, IOCTL_AMDBC250_LOAD_CP_FW,
        buf, totalBuf, &out, sizeof(out), &br2, NULL);

    free(buf);

    if (!ok) {
        printf("  LOAD_CP_FW %s FAILED (err=%lu)\n", typeName, GetLastError());
        return FALSE;
    }

    printf("  LOAD_CP_FW %s: Result=0x%08X UcodeVersion=0x%08X\n",
        typeName, out.Result, out.UcodeVersion);
    return (out.Result == 1);
}

int main(int argc, char *argv[]) {
    printf("=== Load CP Firmware + GPU KIQ Test ===\n");

    HANDLE h = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("Cannot open GPU driver (err=%lu)\n", GetLastError());
        return 1;
    }
    printf("GPU driver opened\n");

    DWORD br = 0;

    /* INIT_HARDWARE */
    printf("\n--- INIT_HARDWARE ---\n");
    UCHAR initIn[32] = {0};
    UCHAR initOut[32] = {0};
    *(UINT64*)(initIn + 0)  = 0xFE800000ULL;
    *(UINT32*)(initIn + 8)  = 0x00080000;
    *(UINT32*)(initIn + 12) = 1;  /* Flags=1 NBIO_MAP */
    *(UINT64*)(initIn + 16) = 0xC0000000ULL;
    *(UINT32*)(initIn + 24) = 0x10000000;
    BOOL ok = DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE,
        initIn, sizeof(initIn), initOut, sizeof(initOut), &br, NULL);
    printf("INIT_HARDWARE: %s (err=%lu)\n", ok ? "OK" : "FAIL", ok ? 0 : GetLastError());

    /* Step 1: Load ME firmware */
    printf("\n--- Load ME firmware (type=1) ---\n");
    BOOL meOk = LoadFirmware(h,
        "C:\\AMD-BC-250\\AMD-BC-250-PSP-Windows-Driver\\output\\cyan_skillfish2_me.bin",
        1, "ME");

    /* Step 2: Load PFP firmware */
    printf("\n--- Load PFP firmware (type=2) ---\n");
    BOOL pfpOk = LoadFirmware(h,
        "C:\\AMD-BC-250\\AMD-BC-250-PSP-Windows-Driver\\output\\cyan_skillfish2_pfp.bin",
        2, "PFP");

    printf("\nFirmware load results: ME=%s PFP=%s\n",
        meOk ? "OK" : "FAIL", pfpOk ? "OK" : "FAIL");

    /* Step 3: Try GPU KIQ Test */
    printf("\n--- GPU KIQ Test (PM4 with firmware loaded) ---\n");
    GPU_KIQ_TEST_OUT kiqOut = {0};
    ok = DeviceIoControl(h, IOCTL_AMDBC250_GPU_KIQ_TEST,
        NULL, 0, &kiqOut, sizeof(kiqOut), &br, NULL);
    if (ok) {
        printf("MmioMapped=%u RingAlloc=%u HqdProg=%u Pm4Sub=%u\n",
            kiqOut.MmioMapped, kiqOut.RingAllocated, kiqOut.HqdProgrammed, kiqOut.Pm4Submitted);
        printf("SCRATCH before: 0x%08X\n", kiqOut.ScratchBefore);
        printf("SCRATCH after:  0x%08X\n", kiqOut.ScratchAfter);
        printf("WPTR readback:  0x%08X\n", kiqOut.RingWptr);
        if (kiqOut.ScratchAfter == 0xCAFEBABE)
            printf("\n*** PM4 EXECUTED! SCRATCH = 0xCAFEBABE ***\n");
        else if (kiqOut.ScratchAfter != kiqOut.ScratchBefore)
            printf("\n*** SCRATCH CHANGED! 0x%08X -> 0x%08X ***\n",
                kiqOut.ScratchBefore, kiqOut.ScratchAfter);
        else
            printf("\nSCRATCH unchanged - PM4 did not execute\n");
    } else {
        printf("KIQ test FAILED (err=%lu)\n", GetLastError());
    }

    CloseHandle(h);
    return 0;
}
