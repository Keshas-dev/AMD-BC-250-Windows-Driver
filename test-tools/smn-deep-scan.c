#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE h;
static BOOL W32(uint32_t o, uint32_t v) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=v; return DeviceIoControl(h,IOCTL_AMDBC250_WRITE_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL); }
static uint32_t R32(uint32_t o) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=0; if(DeviceIoControl(h,IOCTL_AMDBC250_READ_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL)) return r.Value; return 0xFFFFFFFF; }
static void smnW(uint32_t a,uint32_t v){W32(0x38,a);W32(0x3C,v);}
static uint32_t smnR(uint32_t a){W32(0x38,a);R32(0x38);return R32(0x3C);}

static uint32_t interesting[] = {
    0x9FFF0000, 0x9FFF9700, 0x9FFF9714, 0xBA062100, 0x4D585042,  // known magic
    0x00000001, 0x00000007, 0xF0000010, 0xDD602C7D,              // known flags
    0x00580600  // SMU version
};

static int is_interesting(uint32_t v) {
    if (v == 0xFFFFFFFF || v == 0x00000000 || v == 0) return 0;
    for (int i = 0; i < sizeof(interesting)/sizeof(interesting[0]); i++)
        if (v == interesting[i]) return 2;
    return 1;
}

static void scan_range(const char *name, uint32_t start, uint32_t end, uint32_t step) {
    printf("\n=== %s (0x%08X-0x%08X) ===\n", name, start, end);
    int found = 0;
    for (uint32_t a = start; a < end; a += step) {
        uint32_t v = smnR(a);
        int interest = is_interesting(v);
        if (interest) {
            printf("  0x%08X = 0x%08X%s\n", a, v, interest == 2 ? " (known)" : "");
            found++;
        }
    }
    if (!found) printf("  (none)\n");
    else printf("  (%d registers)\n", found);
}

int main() {
    h = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { printf("FAIL gle=%lu\n", GetLastError()); return 1; }
    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br = 0;
    ZeroMemory(&ih, sizeof(ih)); ih.MmioPhysicalBase = 0xFE800000ULL; ih.MmioSize = 0x80000; ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
    if (!DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &br, NULL)) { printf("INIT fail\n"); CloseHandle(h); return 1; }

    printf("SMN Deep Scan - BC-250 (Cyan Skillfish)\n");
    printf("Only showing non-zero, non-0xFFFFFFFF registers\n\n");

    /* IP blocks mapped at BAR5 offsets (SMN = BAR5 offset) */
    scan_range("GC (0x0000-0x4000)",    0x00000000, 0x00004000, 4);
    scan_range("GC-CP (0x4000-0x6000)", 0x00004000, 0x00006000, 4);
    scan_range("GC-RLC (0x8000-A000)",  0x00008000, 0x0000A000, 4);
    scan_range("HDP (0x0F20-0x1000)",   0x00000F20, 0x00001000, 4);
    scan_range("OSSSYS (0x1000-0x2000)",0x00001000, 0x00002000, 4);
    scan_range("DF (0x7000-0x8000)",    0x00007000, 0x00008000, 4);
    scan_range("NBIO (0xC000-0xD000)",  0x0000C000, 0x0000D000, 4);
    scan_range("MMHUB (0x1A000-0x1C000)",0x0001A000, 0x0001C000, 0x100);
    scan_range("UMC (0x14000-0x15000)", 0x00014000, 0x00015000, 0x100);

    /* MP1 SMU range */
    scan_range("MP1 SMU (0x03B00000-0x03B00200)", 0x03B00000, 0x03B00200, 4);
    scan_range("MP1 Sensors (0x03B10000-0x03B10100)", 0x03B10000, 0x03B10100, 4);
    scan_range("MP1 Mailbox (0x03B10A00-0x03B10B00)", 0x03B10A00, 0x03B10B00, 4);
    scan_range("MP1 Misc (0x03B10400-0x03B10800)", 0x03B10400, 0x03B10800, 0x100);

    /* THM, SMUIO, CLK */
    scan_range("THM (0x16600-0x16800)", 0x00016600, 0x00016800, 0x10);
    scan_range("SMUIO (0x16800-0x16C00)", 0x00016800, 0x00016C00, 0x10);
    scan_range("CLK (0x16C00-0x17000)", 0x00016C00, 0x00017000, 0x10);

    /* PSP MP0 */
    scan_range("MP0 PSP (0x16000-0x16200)", 0x00016000, 0x00016200, 4);
    scan_range("FUSE (0x17400-0x17500)", 0x00017400, 0x00017500, 4);

    /* PCI config space via SMN (if accessible) */
    scan_range("PCIe (0x27000-0x28000)", 0x00027000, 0x00028000, 0x100);

    CloseHandle(h);
    printf("\nDONE\n");
    return 0;
}
