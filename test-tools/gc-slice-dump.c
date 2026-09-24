/* gc-slice-dump.c — READ-ONLY dump of SMN 0x024 fabric slices via PCI config SMN.
 * Targets: GC slice 0x02402C00 (1KB, contents unknown) + MP0/MP1 slice
 * 0x0243FC00 (positive control: mailbox lives there, must read live).
 * NO WRITES anywhere. Uses GPU IOCTL_AMDBC250_PCI_SMN_ACCESS (read path). */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE h = INVALID_HANDLE_VALUE;

static uint32_t smn_read(uint32_t addr) {
    DWORD br = 0;
    AMDBC250_IOCTL_PCI_SMN_ACCESS in, out;
    ZeroMemory(&in, sizeof(in));
    in.SmnAddress = addr;
    in.Bus = 0; in.Device = 0; in.Function = 0;
    ZeroMemory(&out, sizeof(out));
    if (!DeviceIoControl(h, IOCTL_AMDBC250_PCI_SMN_ACCESS,
                         &in, sizeof(in), &out, sizeof(out), &br, NULL)) {
        return 0xFFFFFFFF;
    }
    return out.SmnData;
}

static void dump_slice(const char *name, uint32_t base, int dwords) {
    int i, nz = 0;
    uint32_t first_nz = 0, last_nz = 0;
    printf("--- %s @0x%08X (%d dwords) ---\n", name, base, dwords);
    for (i = 0; i < dwords; i++) {
        uint32_t v = smn_read(base + (uint32_t)i * 4);
        if (v != 0 && v != 0xFFFFFFFF) {
            if (nz == 0) first_nz = (uint32_t)i;
            last_nz = (uint32_t)i;
            nz++;
            printf("  +0x%03X = 0x%08X\n", i * 4, v);
        }
    }
    printf("%s: %d/%d nonzero (first +0x%X, last +0x%X)\n\n",
           name, nz, dwords, first_nz * 4, last_nz * 4);
}

int main(void) {
    AMDBC250_IOCTL_INIT_HARDWARE ih;
    DWORD br = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== SMN 0x024 slice dump (READ-ONLY, PCI config 0xB8/0xBC) ===\n\n");

    h = CreateFileA("\\\\.\\AMDBC250DreamV43",
        GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("FAIL: CreateFile gle=%lu\n", GetLastError());
        return 1;
    }

    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL;
    ih.MmioSize = 0x80000;
    ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
    DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE,
                    &ih, sizeof(ih), &ih, sizeof(ih), &br, NULL);

    /* Positive control first: MP0/MP1 slice must be live (mailbox). */
    dump_slice("MP0/MP1 slice", 0x0243FC00, 16);
    /* The unknown: GC 1KB slice. */
    dump_slice("GC slice", 0x02402C00, 256);
    /* Anchors for comparison. */
    dump_slice("THM slice", 0x02400C00, 16);
    dump_slice("VCN slice", 0x02403000, 16);
    /* dom6 sequencer, LOW frame (vcn-team: live here, dead at 0x0116D1xx).
     * Expect status=0x01010101, ctrl=0x02 if model holds. */
    dump_slice("dom6 low-frame", 0x0006D0F0, 17);
    dump_slice("core mask hi/lo", 0x0115A870, 1);

    CloseHandle(h);
    printf("Done. No writes performed.\n");
    return 0;
}
