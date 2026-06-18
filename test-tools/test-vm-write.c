/* test-vm-write.c — Write/readback test for VM registers at 0x9B00 range */
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

static FILE *g = NULL;
static void Log(const char *fmt, ...) {
    va_list a; va_start(a, fmt); vfprintf(stdout, fmt, a); va_end(a);
    if (g) { va_start(a, fmt); vfprintf(g, fmt, a); va_end(a); }
}

static BOOL ReadReg(HANDLE h, unsigned offset, unsigned *val) {
    unsigned ra[2] = {offset, 0xDEADBEEF};
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, 0x80000B88, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
    if (ok) *val = ra[1]; else *val = 0xDEADBEEF;
    return ok;
}

static BOOL WriteReg(HANDLE h, unsigned offset, unsigned val) {
    unsigned ra[2] = {offset, val};
    DWORD br = 0;
    return DeviceIoControl(h, 0x80000B8C, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
}

static void TestWriteRead(HANDLE h, unsigned off, unsigned val, const char *name) {
    unsigned before, after;
    ReadReg(h, off, &before);
    WriteReg(h, off, val);
    ReadReg(h, off, &after);
    Log("  %s [0x%04X]: 0x%08X -> write 0x%08X -> read 0x%08X %s\n",
        name, off, before, val, after,
        after == val ? "OK!" : (after == before ? "NO CHANGE" : "CHANGED (partial)"));
}

int main(void) {
    g = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\test-vm-write.log", "w");
    Log("=== VM Register Write/Readback Test ===\n\n");

    HANDLE h = CreateFileW(L"\\\\.\\AMDBC250DreamV43",
        GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        Log("Cannot open device error=%lu\n", GetLastError());
        if (g) fclose(g); return 1;
    }

    /* INIT_HARDWARE */
    UCHAR initIn[32] = {0}; DWORD br = 0;
    *(unsigned __int64*)(initIn + 0) = 0xFE800000ULL;
    *(unsigned*)(initIn + 8) = 0x00080000;
    *(unsigned*)(initIn + 12) = 1;
    *(unsigned __int64*)(initIn + 16) = 0xC0000000ULL;
    *(unsigned*)(initIn + 24) = 0x10000000;
    DeviceIoControl(h, 0x80000B80, initIn, sizeof(initIn), NULL, 0, &br, NULL);

    unsigned val;
    ReadReg(h, 0x3260, &val);
    Log("GRBM_STATUS[0x3260] = 0x%08X\n\n", val);

    /* === Test 1: Write/readback at 0x9B00 range (hw_extra VM) === */
    Log("--- Test 1: hw_extra VM registers (0x9B00-0x9B40) ---\n");
    TestWriteRead(h, 0x9B00, 0x12345678, "VM_PT_BASE_LO32");
    TestWriteRead(h, 0x9B04, 0x00000012, "VM_PT_BASE_HI32");
    TestWriteRead(h, 0x9B08, 0x00000000, "VM_PT_START_LO32");
    TestWriteRead(h, 0x9B0C, 0x00000000, "VM_PT_START_HI32");
    TestWriteRead(h, 0x9B10, 0x000FFFFF, "VM_PT_END_LO32");
    TestWriteRead(h, 0x9B14, 0x00000000, "VM_PT_END_HI32");

    /* === Test 2: Write/readback at INVALIDATE_ENG === */
    Log("\n--- Test 2: INVALIDATE_ENG registers ---\n");
    TestWriteRead(h, 0x9B40, 0x00000001, "INVALIDATE_ENG0_REQ");
    ReadReg(h, 0x9B80, &val);
    Log("  INVALIDATE_ENG0_ACK [0x9B80] = 0x%08X (read-only expected)\n", val);

    /* === Test 3: Try MC_VM at old 0x0520 range === */
    Log("\n--- Test 3: old MC_VM registers (0x0520-0x0548) ---\n");
    TestWriteRead(h, 0x0520, 0x00010000, "MC_VM_FB_LOC_BASE");
    TestWriteRead(h, 0x0524, 0x00020000, "MC_VM_FB_LOC_TOP");
    TestWriteRead(h, 0x0528, 0x00010000, "MC_VM_AGP_BASE");
    TestWriteRead(h, 0x052C, 0x00020000, "MC_VM_AGP_TOP");
    TestWriteRead(h, 0x0530, 0x00010000, "MC_VM_AGP_BOT");

    /* === Test 4: Try MC_VM at hw_extra 0x9520 range === */
    Log("\n--- Test 4: hw_extra MC_VM registers (0x9520-0x9548) ---\n");
    TestWriteRead(h, 0x9520, 0x00010000, "MC_VM_FB_LOC (9520)");
    TestWriteRead(h, 0x9524, 0x00020000, "MC_VM_AGP_BASE (9524)");
    TestWriteRead(h, 0x9528, 0x00020000, "MC_VM_AGP_TOP (9528)");
    TestWriteRead(h, 0x952C, 0x00010000, "MC_VM_AGP_BOT (952C)");
    TestWriteRead(h, 0x9540, 0x00001000, "MC_VM_SYSAPO_LOW (9540)");
    TestWriteRead(h, 0x9544, 0x000FFFFF, "MC_VM_SYSAPO_HIGH (9544)");

    /* === Test 5: Scatter — probe for any writable register === */
    Log("\n--- Test 5: Writable register scan (0x0500-0x0600, 0x9500-0x9600, 0x9B00-0x9C00) ---\n");
    unsigned writeable = 0;
    unsigned ranges[][2] = {{0x0500, 0x0600}, {0x9500, 0x9600}, {0x9B00, 0x9C00}};
    for (int r = 0; r < 3; r++) {
        for (unsigned off = ranges[r][0]; off < ranges[r][1]; off += 4) {
            unsigned before, after;
            ReadReg(h, off, &before);
            if (before == 0xFFFFFFFF) continue;
            WriteReg(h, off, 0xDEADBEEF);
            ReadReg(h, off, &after);
            if (after != before) {
                Log("  WRITABLE: [0x%04X] 0x%08X -> 0x%08X\n", off, before, after);
                writeable++;
                /* Restore original */
                WriteReg(h, off, before);
            }
        }
    }
    Log("  Total writable: %u\n", writeable);

    CloseHandle(h);
    Log("\n=== Done ===\n");
    if (g) fclose(g);
    printf("Done. Check output\\test-vm-write.log\n");
    return 0;
}
