#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AMDBC250_DEVICE_PATH L"\\\\.\\AMDBC250DreamV43"

#define IOCTL_AMDBC250_PSP_LOAD_IP_FW ((ULONG)0x80002480)
#define IOCTL_AMDBC250_INIT_HARDWARE  ((ULONG)0x80000B80)

typedef struct {
    ULONG64 MmioPhysicalBase;
    ULONG MmioSize;
    ULONG Flags;
    ULONG64 FbPhysicalBase;
    ULONG FbSize;
} INIT_HW;

typedef struct {
    ULONG FwType;
    ULONG FwSize;
    ULONG Result;
    ULONG C2Pmsg35After;
    ULONG C2Pmsg81After;
} PSP_LOAD_FW;

static HANDLE g_h = INVALID_HANDLE_VALUE;
static char g_exeDir[MAX_PATH] = "";

static int OpenGpu(void) {
    g_h = CreateFileW(AMDBC250_DEVICE_PATH, GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    return (g_h != INVALID_HANDLE_VALUE) ? 0 : -1;
}

static int InitHw(void) {
    INIT_HW ih = {0};
    ih.MmioPhysicalBase = 0xFE800000ULL;
    ih.MmioSize = 0x80000;
    ih.Flags = 1;
    DWORD ret = 0;
    return DeviceIoControl(g_h, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &ret, NULL) ? 0 : -1;
}

static const char *FindFw(const char *name) {
    static char buf[512];
    FILE *f = fopen(name, "rb");
    if (f) { fclose(f); return name; }
    snprintf(buf, sizeof(buf), "%s\\%s", g_exeDir, name);
    f = fopen(buf, "rb"); if (f) { fclose(f); return buf; }
    snprintf(buf, sizeof(buf), "%s\\firmware\\%s", g_exeDir, name);
    f = fopen(buf, "rb"); if (f) { fclose(f); return buf; }
    snprintf(buf, sizeof(buf), "%s\\..\\firmware\\%s", g_exeDir, name);
    f = fopen(buf, "rb"); if (f) { fclose(f); return buf; }
    return name;
}

static const char *FwName(ULONG t) {
    switch (t) { case 1: return "ME"; case 2: return "PFP"; case 3: return "CE";
    case 4: return "MEC"; case 5: return "MEC2"; case 8: return "RLC";
    case 9: return "SDMA0"; case 10: return "SDMA1"; default: return "?"; }
}

static int LoadFirmware(ULONG fwType, const char *filename) {
    const char *path = FindFw(filename);
    FILE *fp = fopen(path, "rb");
    if (!fp) { printf("  FAIL: cannot open %s\n", filename); return -1; }
    fseek(fp, 0, SEEK_END);
    long fwSize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (fwSize <= 0 || fwSize > 1024*1024) {
        printf("  FAIL: invalid size %ld\n", fwSize); fclose(fp); return -1;
    }

    size_t bufSize = sizeof(PSP_LOAD_FW) + fwSize;
    BYTE *buf = (BYTE*)malloc(bufSize);
    if (!buf) { fclose(fp); return -1; }

    PSP_LOAD_FW *req = (PSP_LOAD_FW*)buf;
    req->FwType = fwType;
    req->FwSize = (ULONG)fwSize;
    req->Result = 0; req->C2Pmsg35After = 0; req->C2Pmsg81After = 0;
    fread(req + 1, 1, fwSize, fp);
    fclose(fp);

    PSP_LOAD_FW resp;
    DWORD ret = 0;
    BOOL ok = DeviceIoControl(g_h, IOCTL_AMDBC250_PSP_LOAD_IP_FW,
        buf, (DWORD)bufSize, &resp, sizeof(resp), &ret, NULL);
    free(buf);

    if (ok) {
        printf("  Result=%u C2Pmsg35=0x%08X C2Pmsg81=0x%08X\n",
            resp.Result, resp.C2Pmsg35After, resp.C2Pmsg81After);
        return (resp.Result == 1) ? 0 : -1;
    } else {
        printf("  FAIL (err=%lu)\n", GetLastError());
        return -1;
    }
}

int main(int argc, char *argv[]) {
    GetModuleFileNameA(NULL, g_exeDir, sizeof(g_exeDir));
    char *p = strrchr(g_exeDir, '\\');
    if (p) *p = '\0';

    printf("=== PSP Firmware Loading Test ===\n");
    if (OpenGpu() != 0) { printf("ERROR: Cannot open GPU driver\n"); return 1; }
    if (InitHw() != 0) { printf("Init HW: FAILED (err=%lu)\n", GetLastError()); return 1; }
    printf("Driver OK\n\n");

    struct { ULONG t; const char *n; } fw[] = {
        {3, "cyan_skillfish2_ce.bin"},   {2, "cyan_skillfish2_pfp.bin"},
        {1, "cyan_skillfish2_me.bin"},   {4, "cyan_skillfish2_mec.bin"},
        {5, "cyan_skillfish2_mec2.bin"}, {8, "cyan_skillfish2_rlc.bin"},
        {9, "cyan_skillfish2_sdma.bin"}, {10, "cyan_skillfish2_sdma1.bin"},
    };

    int loaded = 0, n = sizeof(fw)/sizeof(fw[0]);
    for (int i = 0; i < n; i++) {
        printf("[%d/%d] %s (%s)...\n", i+1, n, FwName(fw[i].t), fw[i].n);
        if (LoadFirmware(fw[i].t, fw[i].n) == 0) loaded++;
    }

    printf("\n=== %d/%d firmware loaded ===\n", loaded, n);
    CloseHandle(g_h);
    return (loaded == n) ? 0 : 1;
}
