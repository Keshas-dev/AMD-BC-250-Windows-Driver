#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AMDBC250_DEVICE_PATH L"\\\\.\\AMDBC250DreamV43"

#define IOCTL_AMDBC250_PSP_LOAD_TOS ((ULONG)0x800024A0)
#define IOCTL_AMDBC250_INIT_HARDWARE ((ULONG)0x80000B80)

typedef struct {
    ULONG64 MmioPhysicalBase;
    ULONG MmioSize;
    ULONG Flags;
    ULONG64 FbPhysicalBase;
    ULONG FbSize;
} INIT_HW;

typedef struct {
    ULONG FwSize;
    ULONG Result;
    ULONG C2Pmsg64Before;
    ULONG C2Pmsg64After;
    ULONG C2Pmsg35After;
    ULONG C2Pmsg81After;
} PSP_LOAD_TOS;

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
    snprintf(buf, sizeof(buf), "C:\\Windows\\System32\\drivers\\bc-250\\%s", name);
    f = fopen(buf, "rb"); if (f) { fclose(f); return buf; }
    return name;
}

static int LoadTos(const char *filename) {
    const char *path = FindFw(filename);
    FILE *fp = fopen(path, "rb");
    if (!fp) { printf("  FAIL: cannot open %s\n", filename); return -1; }
    fseek(fp, 0, SEEK_END);
    long fwSize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (fwSize <= 0 || fwSize > 1024*1024) {
        printf("  FAIL: invalid size %ld\n", fwSize); fclose(fp); return -1;
    }

    size_t bufSize = sizeof(PSP_LOAD_TOS) + fwSize;
    BYTE *buf = (BYTE*)malloc(bufSize);
    if (!buf) { fclose(fp); return -1; }

    PSP_LOAD_TOS *req = (PSP_LOAD_TOS*)buf;
    req->FwSize = (ULONG)fwSize;
    req->Result = 0; req->C2Pmsg64Before = 0; req->C2Pmsg64After = 0;
    req->C2Pmsg35After = 0; req->C2Pmsg81After = 0;
    fread(req + 1, 1, fwSize, fp);
    fclose(fp);

    PSP_LOAD_TOS resp;
    DWORD ret = 0;
    BOOL ok = DeviceIoControl(g_h, IOCTL_AMDBC250_PSP_LOAD_TOS,
        buf, (DWORD)bufSize, &resp, sizeof(resp), &ret, NULL);
    free(buf);

    if (ok) {
        printf("  C64 before=0x%08X  after=0x%08X  C35=0x%08X  C81=0x%08X\n",
            resp.C2Pmsg64Before, resp.C2Pmsg64After,
            resp.C2Pmsg35After, resp.C2Pmsg81After);
        printf("  TOS ready (bit31): %s\n",
            (resp.C2Pmsg64After & 0x80000000) ? "YES!" : "NO");
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

    const char *names[] = { "Ta.bin", "cyan_skillfish2_ta.bin" };

    printf("=== PSP TOS (Ta.bin) Load Test ===\n");
    printf("Loads Ta.bin as PSP_BL__LOAD_TOS_SPL_TABLE (0x10000000)\n");
    printf("then checks C2PMSG_64 bit31 (TOS ready).\n\n");

    if (OpenGpu() != 0) { printf("ERROR: Cannot open GPU driver\n"); return 1; }
    if (InitHw() != 0) { printf("Init HW: FAILED (err=%lu)\n", GetLastError()); return 1; }
    printf("Driver OK\n\n");

    const char *used = NULL;
    for (int i = 0; i < (int)(sizeof(names)/sizeof(names[0])); i++) {
        const char *path = FindFw(names[i]);
        FILE *f = fopen(path, "rb");
        if (f) { fclose(f); used = names[i]; break; }
    }
    if (!used) {
        printf("ERROR: cannot find Ta.bin anywhere\n");
        printf("Expected at C:\\Windows\\System32\\drivers\\bc-250\\Ta.bin\n");
        CloseHandle(g_h);
        return 1;
    }

    printf("Loading TOS from: %s\n\n", FindFw(used));
    int r = LoadTos(used);

    printf("\n=== TOS test %s ===\n", (r == 0) ? "PASS (TOS ready)" : "FAIL (TOS not ready)");
    CloseHandle(g_h);
    return r;
}