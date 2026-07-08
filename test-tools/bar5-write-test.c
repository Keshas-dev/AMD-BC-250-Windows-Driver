#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE g_hDev = INVALID_HANDLE_VALUE;

static uint32_t ReadReg(uint32_t offset) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD ret = 0;
    r.RegisterOffset = offset; r.Value = 0;
    if (DeviceIoControl(g_hDev, IOCTL_AMDBC250_READ_REG, &r, sizeof(r), &r, sizeof(r), &ret, NULL))
        return r.Value;
    return 0xFFFFFFFF;
}

static BOOL WriteReg(uint32_t offset, uint32_t value) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD ret = 0;
    r.RegisterOffset = offset; r.Value = value;
    return DeviceIoControl(g_hDev, IOCTL_AMDBC250_WRITE_REG, &r, sizeof(r), &r, sizeof(r), &ret, NULL);
}

static const char* ResultStr(uint32_t orig, uint32_t written, uint32_t readback) {
    if (readback == written) return "RW";
    if (readback == orig) return "RO";
    if (readback == 0xFFFFFFFF) return "DEAD";
    return "OTHER";
}

int main() {
    g_hDev = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (g_hDev == INVALID_HANDLE_VALUE) {
        printf("FAIL: CreateFile gle=%lu\n", GetLastError());
        return 1;
    }
    printf("CreateFile OK\n");

    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD ret = 0;
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL;
    ih.MmioSize = 0x80000;
    ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
    BOOL ok = DeviceIoControl(g_hDev, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &ret, NULL);
    printf("INIT_HW: ok=%d gle=%lu\n", ok, GetLastError());
    if (!ok) { CloseHandle(g_hDev); return 1; }

    printf("\n=== BAR5 4KB-aligned write test (0x20000-0x7F000) ===\n");
    printf("Format: OFFSET: orig -> write -> readback [RESULT]\n\n");

    int rwCount = 0, roCount = 0, deadCount = 0, otherCount = 0;
    int rangeStart = -1, rangeEnd = -1;
    const char* rangeType = NULL;

    for (uint32_t off = 0x20000; off <= 0x7F000; off += 0x1000) {
        uint32_t orig = ReadReg(off);
        uint32_t testVal = 0xDEADBEEF;
        /* Avoid false RW from already-being-0xDEADBEEF */
        if (orig == testVal) testVal = 0xCAFEBABE;
        WriteReg(off, testVal);
        uint32_t readback = ReadReg(off);
        WriteReg(off, orig);
        uint32_t verify = ReadReg(off);

        const char* res = ResultStr(orig, testVal, readback);

        /* Track contiguous ranges */
        if (rangeStart < 0) {
            rangeStart = off; rangeEnd = off; rangeType = res;
        } else if (res == rangeType && off == rangeEnd + 0x1000) {
            rangeEnd = off;
        } else {
            if (rangeStart == rangeEnd)
                printf("  0x%05X: 0x%08X -> 0x%08X -> 0x%08X [%s]\n",
                    rangeStart, orig, testVal, readback, rangeType);
            else
                printf("  0x%05X-0x%05X: [%s]  (last readback: 0x%08X)\n",
                    rangeStart, rangeEnd, rangeType, readback);
            rangeStart = off; rangeEnd = off; rangeType = res;
        }

        if (res[0] == 'R' && res[1] == 'W') rwCount++;
        else if (res[0] == 'R' && res[1] == 'O') roCount++;
        else if (res[0] == 'D') deadCount++;
        else otherCount++;

        /* Highlight RW or OTHER */
        if (res[0] == 'R' && res[1] == 'W')
            printf("  >>> 0x%05X: 0x%08X -> 0x%08X -> 0x%08X [RW]\n", off, orig, testVal, readback);
        else if (res[0] != 'D')
            printf("  *** 0x%05X: 0x%08X -> 0x%08X -> 0x%08X [%s]\n", off, orig, testVal, readback, res);
    }

    /* Flush final range */
    if (rangeStart >= 0) {
        if (rangeStart == rangeEnd)
            printf("  0x%05X: [%s]\n", rangeStart, rangeType);
        else
            printf("  0x%05X-0x%05X: [%s]\n", rangeStart, rangeEnd, rangeType);
    }

    printf("\nSummary: RW=%d RO=%d DEAD=%d OTHER=%d\n", rwCount, roCount, deadCount, otherCount);

    printf("\n=== Specific Register Writes ===\n");
    struct { uint32_t off; const char* name; } spec[] = {
        {0x32D4, "SCRATCH_REG0"},
        {0x910C, "CP_HQD_ACTIVE"},
        {0x9104, "CP_MQD_BASE_ADDR"},
        {0x9124, "CP_HQD_PQ_BASE"},
        {0x90F0, "CP_HQD_PQ_CONTROL"},
        {0x9110, "CP_HQD_PERSISTENT_STATE"},
        {0x91DC, "CP_HQD_PQ_WPTR_LO"},
    };
    for (int i = 0; i < sizeof(spec)/sizeof(spec[0]); i++) {
        uint32_t orig = ReadReg(spec[i].off);
        uint32_t test = (orig == 0xDEADBEEF) ? 0x12345678 : 0xDEADBEEF;
        WriteReg(spec[i].off, test);
        uint32_t readback = ReadReg(spec[i].off);
        WriteReg(spec[i].off, orig);
        uint32_t restored = ReadReg(spec[i].off);
        printf("  %-25s 0x%05X: 0x%08X -> 0x%08X -> 0x%08X [%s] (restored: 0x%08X)\n",
            spec[i].name, spec[i].off, orig, test, readback,
            (readback == test) ? "RW" : (readback == orig ? "RO" : "OTHER"),
            restored);
    }

    printf("\n=== GRBM_GFX_INDEX (0x34D0) test ===\n");
    uint32_t ggi = ReadReg(0x34D0);
    printf("  0x34D0 initial: 0x%08X\n", ggi);
    WriteReg(0x34D0, 0x00010000);
    ggi = ReadReg(0x34D0);
    printf("  0x34D0 after write ME=1: 0x%08X\n", ggi);
    WriteReg(0x34D0, 0);
    ggi = ReadReg(0x34D0);
    printf("  0x34D0 after write 0: 0x%08X\n", ggi);

    CloseHandle(g_hDev);
    printf("\nDONE\n");
    return 0;
}
