#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>

#define AMDBC250_DEVICE_PATH L"\\\\.\\AMDBC250DreamV43"
#define IOCTL_AMDBC250_READ_REG   ((ULONG)0x80000B88)
#define IOCTL_AMDBC250_WRITE_REG  ((ULONG)0x80000B8C)
#define IOCTL_AMDBC250_INIT_HARDWARE ((ULONG)0x80000B80)

typedef struct { ULONG Offset, Value, Status; } REG_IOCTL;
typedef struct { ULONG64 MmioPhysicalBase; ULONG MmioSize, Flags; ULONG64 FbPhysicalBase; ULONG FbSize; } INIT_HW;

static HANDLE g_h;
static int OpenGpu(void) {
    g_h = CreateFileW(AMDBC250_DEVICE_PATH, GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    return g_h != INVALID_HANDLE_VALUE ? 0 : -1;
}
static ULONG ReadReg(ULONG offset) {
    REG_IOCTL req = {offset,0,0}; DWORD ret;
    DeviceIoControl(g_h, IOCTL_AMDBC250_READ_REG, &req,sizeof(req), &req,sizeof(req), &ret, NULL);
    return req.Value;
}
static int WriteReg(ULONG offset, ULONG value) {
    REG_IOCTL req = {offset,value,0}; DWORD ret;
    return DeviceIoControl(g_h, IOCTL_AMDBC250_WRITE_REG, &req,sizeof(req), &req,sizeof(req), &ret, NULL) ? 0 : -1;
}
static int InitHw(void) {
    INIT_HW ih = {0}; ih.MmioPhysicalBase = 0xFE800000ULL; ih.MmioSize = 0x80000; ih.Flags = 1; DWORD ret;
    return DeviceIoControl(g_h, IOCTL_AMDBC250_INIT_HARDWARE, &ih,sizeof(ih), &ih,sizeof(ih), &ret, NULL) ? 0 : -1;
}

int main(void) {
    if (OpenGpu() != 0) { printf("Cannot open GPU\n"); return 1; }
    if (InitHw() != 0) { printf("InitHw err=%lu\n", GetLastError()); return 1; }
    printf("=== BAR5 Writable Area Probe ===\n\n");

    /* Probe various offsets for writable memory areas */
    ULONG offsets[] = {
        0x70000, 0x71000, 0x72000, 0x73000, 0x74000, 0x75000,
        0x76000, 0x77000, 0x78000, 0x79000, 0x7A000, 0x7B000,
        0x7C000, 0x7D000, 0x7E000, 0x7F000,
        0x7FFFC, /* last 4 bytes of BAR5 */
        0x60000, 0x61000, 0x62000, 0x63000, 0x64000,
        0x50000, 0x51000, 0x52000, 0x53000,
        0x40000, 0x41000,
        0x30000, 0x31000,
        0x20000, 0x21000,
        0x10000, 0x11000,
        0x18000, 0x19000, 0x1A000, 0x1B000, 0x1C000,
    };
    int i, found = 0;
    ULONG test_val = 0x12345678;

    printf("Offset        | Read   | Write -> Readback | Writable?\n");
    printf("--------------|--------|--------------------|----------\n");
    for (i = 0; i < sizeof(offsets)/sizeof(offsets[0]); i++) {
        ULONG off = offsets[i];
        ULONG before = ReadReg(off);
        if (before == 0xFFFFFFFF) continue; /* dead area */
        WriteReg(off, test_val);
        ULONG after = ReadReg(off);
        ULONG restored = 0;
        if (after == test_val) {
            /* Writable! Restore original */
            WriteReg(off, before);
            restored = ReadReg(off);
            found++;
            printf("  0x%06X    | 0x%08X | 0x%08X -> 0x%08X | YES (restored=0x%08X)\n",
                off, before, test_val, after, restored);
        } else {
            printf("  0x%06X    | 0x%08X | 0x%08X -> 0x%08X | NO\n",
                off, before, test_val, after);
        }
    }
    printf("\nFound %d writable non-register areas\n", found);

    /* Also check: can we write to the MQD registers we plan to use? */
    printf("\n=== CP_HQD Register Write Test (with ME=1 select) ===\n");
    WriteReg(0x34D0, 0x00010000);  /* select ME=1 */
    ULONG mqd_lo = ReadReg(0x9104);
    ULONG mqd_hi = ReadReg(0x9108);
    WriteReg(0x9104, 0xA5A5A5A5);
    ULONG mqd_lo2 = ReadReg(0x9104);
    WriteReg(0x9104, mqd_lo);  /* restore */
    printf("CP_MQD_BASE_ADDR(0x9104): before=0x%08X wrote=0xA5A5A5A5 read=0x%08X\n", mqd_lo, mqd_lo2);
    printf("CP_MQD_BASE_ADDR_HI(0x9108): 0x%08X (read-only FW value)\n", mqd_hi);
    WriteReg(0x34D0, 0xE4000000);  /* restore broadcast */

    CloseHandle(g_h);
    return 0;
}
