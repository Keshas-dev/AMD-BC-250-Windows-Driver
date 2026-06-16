#define INITGUID
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

static FILE *g = NULL;
static void Log(const char *fmt, ...) {
    va_list a; va_start(a, fmt);
    vfprintf(stdout, fmt, a); va_end(a); fflush(stdout);
    if (g) { va_start(a, fmt); vfprintf(g, fmt, a); va_end(a); fflush(g); }
}

static HANDLE OpenKmd(void) {
    return CreateFileW(L"\\\\.\\AMDBC250DreamV43",
        GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
}

static BOOL ReadReg(HANDLE h, UINT32 offset, UINT32 *val) {
    UINT32 ra[2] = {offset, 0xDEADBEEF};
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, 0x80000B88, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
    *val = ra[1];
    return ok;
}

static BOOL WriteReg(HANDLE h, UINT32 offset, UINT32 val) {
    UINT32 ra[2] = {offset, val};
    DWORD br = 0;
    return DeviceIoControl(h, 0x80000B8C, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
}

static void InitHardware(HANDLE h) {
    UCHAR initIn[32] = {0}, initOut[32] = {0};
    DWORD br = 0;
    *(UINT64*)(initIn + 0)  = 0xFE800000ULL;
    *(UINT32*)(initIn + 8)  = 0x00080000;
    *(UINT32*)(initIn + 12) = 1;
    *(UINT64*)(initIn + 16) = 0xC0000000ULL;
    *(UINT32*)(initIn + 24) = 0x10000000;
    DeviceIoControl(h, 0x80000B80, initIn, sizeof(initIn), initOut, sizeof(initOut), &br, NULL);
}

int main(void) {
    g = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\psp-c2pmsg-alias-probe.log", "w");
    if (!g) { printf("Cannot open log\n"); return 1; }
    Log("=== PSP C2PMSG alias probe ===\n\n"); fflush(g);

    HANDLE h = OpenKmd();
    if (h == INVALID_HANDLE_VALUE) {
        Log("KMD NOT FOUND\n"); fclose(g); return 1;
    }

    InitHardware(h);

    /* PSP BAR0 expected at 0xFD600000.
     * GPU BAR5 is at 0xFE800000.
     * PSP BASE (MP0_BASE) is discovered via PSP driver mailbox reads.
     * We probe both native C2PMSG offsets and GC_BASE-shifted aliases
     * to see which path actually returns data. */

    UINT32 v;
    int i;

    Log("--- A) Native C2PMSG offsets (0x018C..0x0250) ---\n");
    UINT32 c2pNative[] = {0x018C,0x0190,0x0194,0x0198,0x01A0,0x01A4,
                          0x0200,0x0204,0x0208,0x020C,0x0210,0x0214,
                          0x0240,0x0244};
    const char *c2pName[] = {
        "C2PMSG_35_BYTE","C2PMSG_36_BYTE","C2PMSG_37_BYTE","C2PMSG_64_BYTE",
        "C2PMSG_65_BYTE","C2PMSG_66_BYTE",
        "C2PMSG_69_BYTE","C2PMSG_70_BYTE","C2PMSG_71_BYTE","C2PMSG_72_BYTE",
        "C2PMSG_73_BYTE","C2PMSG_74_BYTE",
        "C2PMSG_81_BYTE","C2PMSG_82_BYTE"};
    for (i = 0; i < _countof(c2pNative); i++) {
        ReadReg(h, c2pNative[i], &v);
        Log("  [0x%04X] %s = 0x%08X\n", c2pNative[i], c2pName[i], v);
    }

    Log("\n--- B) PSP base-area raw dump (0x0000..0x0080, step 0x10) ---\n");
    for (i = 0; i <= 0x80; i += 0x10) {
        ReadReg(h, (UINT32)i, &v);
        Log("  [0x%04X] = 0x%08X\n", i, v);
    }

    Log("\n--- C) NBIO PS5 signatures (0xC100 & 0xC180 native vs shifted) ---\n");
    ReadReg(h, 0xC100, &v);  Log("  native  0xC100 = 0x%08X\n", v);
    ReadReg(h, 0xC180, &v);  Log("  native  0xC180 = 0x%08X\n", v);
    ReadReg(h, 0x1260 + 0x9A0, &v);  Log("  shifted 0x1C00 (GC_BASE+0x9A0) = 0x%08X\n", v);

    Log("\n--- D) Write probe: try write to 0x0190, then read back ---\n");
    ReadReg(h, 0x0190, &v);
    Log("  before = 0x%08X\n", v);
    WriteReg(h, 0x0190, 0x12345678);
    ReadReg(h, 0x0190, &v);
    Log("  after  = 0x%08X\n", v);
    WriteReg(h, 0x0190, 0x00000000);

    Log("\n--- E) Re-read C2PMSG_35/36/64/81 after write probe ---\n");
    ReadReg(h, 0x018C, &v); Log("  C2PMSG_35 = 0x%08X\n", v);
    ReadReg(h, 0x0190, &v); Log("  C2PMSG_36 = 0x%08X\n", v);
    ReadReg(h, 0x0198, &v); Log("  C2PMSG_64 = 0x%08X\n", v);
    ReadReg(h, 0x0240, &v); Log("  C2PMSG_81 = 0x%08X\n", v);

    CloseHandle(h);
    Log("\n=== Done ===\n"); fflush(g);
    if (g) fclose(g);
    printf("Done. Check output\\psp-c2pmsg-alias-probe.log\n");
    return 0;
}
