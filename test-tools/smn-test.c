#define INITGUID
#include <windows.h>
#include <stdio.h>

static HANDLE OpenDevice(void) {
    HANDLE h = CreateFileW(L"\\\\.\\AMDBC250DreamV43",
        GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    return h;
}

static BOOL ReadReg(HANDLE h, ULONG off, ULONG *val) {
    DWORD br;
    UCHAR buf[16] = {0};
    *(ULONG*)(buf+0) = off;
    if (!DeviceIoControl(h, 0x80000B88, buf, sizeof(buf), buf, sizeof(buf), &br, NULL))
        return FALSE;
    *val = *(ULONG*)(buf+4);
    return TRUE;
}

static BOOL WriteReg(HANDLE h, ULONG off, ULONG val) {
    DWORD br;
    UCHAR buf[16] = {0};
    *(ULONG*)(buf+0) = off;
    *(ULONG*)(buf+4) = val;
    return DeviceIoControl(h, 0x80000B8C, buf, sizeof(buf), NULL, 0, &br, NULL);
}

int main(void) {
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) {
        printf("Cannot open device error=%lu\n", GetLastError());
        return 1;
    }

    printf("=== NBIO SMN Interface Probe ===\n\n");

    /* Step 1: Read baseline NBIO registers */
    printf("Step 1: NBIO baseline (0xC100-0xC1FC):\n");
    for (ULONG off = 0xC100; off <= 0xC1FC; off += 4) {
        ULONG val;
        if (ReadReg(h, off, &val))
            printf("  0x%04X = 0x%08X\n", off, val);
    }

    printf("\nStep 2: Test candidate SMN index/data pairs\n");
    struct {
        ULONG idx, data;
        char *desc;
    } pairs[] = {
        {0xC100, 0xC104, "NBIO base +0x00/+0x04"},
        {0xC120, 0xC124, "NBIO base +0x20/+0x24"},
        {0xC140, 0xC144, "NBIO base +0x40/+0x44"},
        {0xC160, 0xC164, "NBIO base +0x60/+0x64"},
        {0xC180, 0xC184, "NBIO base +0x80/+0x84"},
        {0xC1A0, 0xC1A4, "NBIO base +0xA0/+0xA4"},
        {0xC1C0, 0xC1C4, "NBIO base +0xC0/+0xC4"},
        {0xC1E0, 0xC1E4, "NBIO base +0xE0/+0xE4"},
    };

    for (int i = 0; i < sizeof(pairs)/sizeof(pairs[0]); i++) {
        ULONG idx_off = pairs[i].idx;
        ULONG data_off = pairs[i].data;
        ULONG b4, after, after2, after3;

        /* Test SMN address 0x00008000 (potential GC SMN base) */
        WriteReg(h, idx_off, 0x00008000);
        Sleep(1);
        ReadReg(h, data_off, &b4);

        /* Test SMN address 0x00000000 (always valid) */
        WriteReg(h, idx_off, 0x00000000);
        Sleep(1);
        ReadReg(h, data_off, &after);

        /* Test SMN address 0x0000BEEF */
        WriteReg(h, idx_off, 0x0000BEEF);
        Sleep(1);
        ReadReg(h, data_off, &after2);

        /* Test SMN address 0x00020004 (GRBM via SMN) */
        WriteReg(h, idx_off, 0x00020004);
        Sleep(1);
        ReadReg(h, data_off, &after3);

        printf("  %s: idx=0x%04X data=0x%04X\n", pairs[i].desc, idx_off, data_off);
        printf("    idx(0x8000)->data=0x%08X\n", b4);
        printf("    idx(0x0000)->data=0x%08X\n", after);
        printf("    idx(0xBEEF)->data=0x%08X\n", after2);
        printf("    idx(0x2004)->data=0x%08X\n", after3);

        /* If data changes based on idx value, it might be SMN */
        if (b4 != after || after != after2)
            printf("    *** DATA CHANGES - possible SMN port ***\n");
        printf("\n");
    }

    printf("Step 3: Direct NBIO write persistence test\n");
    {
        ULONG orig;
        ReadReg(h, 0xC100, &orig);
        printf("  0xC100 before = 0x%08X\n", orig);

        WriteReg(h, 0xC100, 0xDEADBEEF);
        ULONG written;
        ReadReg(h, 0xC100, &written);
        printf("  0xC100 after write 0xDEADBEEF = 0x%08X\n", written);

        WriteReg(h, 0xC100, orig);
        ULONG restored;
        ReadReg(h, 0xC100, &restored);
        printf("  0xC100 restored to original = 0x%08X\n", restored);
    }

    CloseHandle(h);
    return 0;
}
