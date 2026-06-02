#include <windows.h>
#include <stdio.h>
#include "amdbc250_ioctl.h"

int main(void) {
    FILE *f = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\probe.log", "w");
    if (!f) { printf("Cannot open log\n"); return 1; }
    fprintf(f, "PROBE: program started\n"); fflush(f);

    fprintf(f, "PROBE: opening device\n"); fflush(f);
    HANDLE h = CreateFileA("\\\\.\\AMDBC250DreamV43",
        GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        fprintf(f, "PROBE: FAIL error %lu\n", GetLastError()); fflush(f);
        fclose(f); return 1;
    }
    fprintf(f, "PROBE: device opened\n"); fflush(f);

    /* Just scan BAR5 first 4KB - known safe */
    fprintf(f, "PROBE: BAR5 scan 0-4KB\n"); fflush(f);
    for (UINT64 off = 0; off < 0x1000; off += 4) {
        AMDBC250_IOCTL_MMIO_TEST m = {0};
        m.PhysicalAddress = 0xFE800000 + off; m.Size = 4; m.OffsetRead = 0;
        DWORD bytes = 0;
        DeviceIoControl(h, IOCTL_AMDBC250_MMIO_TEST, &m, sizeof(m), &m, sizeof(m), &bytes, NULL);
        if (m.MapResult && m.ValueRead != 0) {
            fprintf(f, "  +0x%llX: 0x%08X\n", off, m.ValueRead); fflush(f);
        }
    }
    fprintf(f, "PROBE: BAR5 4KB done\n"); fflush(f);

    /* NBIO 0xFEB00000 - known safe */
    fprintf(f, "PROBE: NBIO 0xFEB00000\n"); fflush(f);
    for (UINT64 off = 0; off < 0x1000; off += 4) {
        AMDBC250_IOCTL_MMIO_TEST m = {0};
        m.PhysicalAddress = 0xFEB00000 + off; m.Size = 4; m.OffsetRead = 0;
        DWORD bytes = 0;
        DeviceIoControl(h, IOCTL_AMDBC250_MMIO_TEST, &m, sizeof(m), &m, sizeof(m), &bytes, NULL);
        if (m.MapResult && m.ValueRead != 0) {
            fprintf(f, "  NBIO+0x%llX: 0x%08X\n", off, m.ValueRead); fflush(f);
        }
    }
    fprintf(f, "PROBE: NBIO done\n"); fflush(f);

    CloseHandle(h);
    fprintf(f, "PROBE: ALL DONE\n"); fflush(f);
    fclose(f);
    printf("Done. Check probe.log\n");
    return 0;
}
