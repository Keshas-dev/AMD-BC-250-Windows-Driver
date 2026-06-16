#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>

static HANDLE OpenKmd(void) {
    return CreateFileW(L"\\\\.\\AMDBC250DreamV43",
        GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
}

static BOOL ReadReg(HANDLE h, UINT32 off, UINT32 *v) {
    UINT32 ra[2] = {off, 0};
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, 0x80000B88, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
    *v = ra[1]; return ok;
}

static BOOL WriteReg(HANDLE h, UINT32 off, UINT32 v) {
    UINT32 ra[2] = {off, v};
    DWORD br = 0;
    return DeviceIoControl(h, 0x80000B8C, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
}

static BOOL InitNBIO(HANDLE h) {
    UCHAR initIn[32] = {0};
    DWORD br = 0;
    *(UINT64*)(initIn + 0)  = 0xFE800000ULL;
    *(UINT32*)(initIn + 8)  = 0x00080000;
    *(UINT32*)(initIn + 12) = 1;
    *(UINT64*)(initIn + 16) = 0xC0000000ULL;
    *(UINT32*)(initIn + 24) = 0x10000000;
    return DeviceIoControl(h, 0x80000B80, initIn, sizeof(initIn), NULL, 0, &br, NULL);
}

static BOOL SendPm4(HANDLE h, UINT32 *cmds, UINT32 count, UINT32 fence) {
    UCHAR buf[272] = {0};
    *(UINT32*)(buf + 0) = count;
    *(UINT32*)(buf + 4) = fence;
    *(UINT32*)(buf + 8) = 0; /* QueueType = GFX */
    RtlCopyMemory(buf + 16, cmds, count * sizeof(UINT32));
    DWORD br = 0;
    return DeviceIoControl(h, 0x80000B84, buf, sizeof(buf), NULL, 0, &br, NULL);
}

static BOOL GetHwStatus(HANDLE h, UINT32 *mmio, UINT32 *rings, UINT32 *fence) {
    UCHAR out[64] = {0};
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, 0x80000B90, NULL, 0, out, sizeof(out), &br, NULL);
    if (ok) {
        *mmio = *(UINT32*)(out + 0);
        *rings = *(UINT32*)(out + 4);
        *fence = *(UINT32*)(out + 8);
    }
    return ok;
}

int main(void) {
    printf("=== PM4 / CP Probe Test ===\n\n");

    HANDLE h = OpenKmd();
    if (h == INVALID_HANDLE_VALUE) { printf("Driver not found\n"); return 1; }
    printf("Driver opened\n");

    BOOL ok = InitNBIO(h);
    printf("INIT_HARDWARE NBIO_MAP: %s\n\n", ok ? "OK" : "FAIL");
    if (!ok) { CloseHandle(h); return 1; }

    UINT32 v;
    DWORD br;

    /* === Phase 1: HW Status === */
    printf("--- HW Status ---\n");
    {
        UINT32 mmio, rings, fence;
        if (GetHwStatus(h, &mmio, &rings, &fence)) {
            printf("  MmioMapped=%u RingsInit=%u FenceInit=%u\n", mmio, rings, fence);
        } else {
            printf("  GET_HW_STATUS failed\n");
        }
    }

    /* === Phase 2: GC_BASE registers === */
    printf("\n--- GC Registers ---\n");
    ReadReg(h, 0x00000, &v); printf("  GPU_ID         = 0x%08X\n", v);
    ReadReg(h, 0x03260, &v); printf("  GRBM_STATUS    = 0x%08X\n", v);
    ReadReg(h, 0x03264, &v); printf("  CC_CONFIG      = 0x%08X\n", v);
    ReadReg(h, 0x0326C, &v); printf("  GRBM_CNTL      = 0x%08X\n", v);
    ReadReg(h, 0x032D4, &v); printf("  SCRATCH        = 0x%08X\n", v);
    ReadReg(h, 0x034FC, &v); printf("  SPI_WGP        = 0x%08X\n", v);

    /* === Phase 3: CP registers via NBIO range === */
    printf("\n--- CP Registers (0xC000+) ---\n");
    {
        UINT32 cpRegs[] = {
            0xC000, 0xC004, 0xC008, 0xC00C,
            0xC060, 0xC064, 0xC068, 0xC06C,
            0xC080, 0xC084, 0xC088, 0xC08C,
            0xC0A0, 0xC0A4, 0xC0A8, 0xC0AC,
            0xC0D8, 0xC0DC,
            0xC0E0, 0xC0E4, 0xC0E8, 0xC0EC,
            0xC100, 0xC104, 0xC108, 0xC10C,
        };
        for (int i = 0; i < sizeof(cpRegs)/sizeof(cpRegs[0]); i++) {
            ReadReg(h, cpRegs[i], &v);
            if (v != 0x00000000 && v != 0xFFFFFFFF)
                printf("  [0x%04X] = 0x%08X\n", cpRegs[i], v);
        }
        /* Also try known named ones */
        ReadReg(h, 0xC060, &v); printf("  CP_ME_CNTL     = 0x%08X\n", v);
        ReadReg(h, 0xC064, &v); printf("  CP_ME_STATUS   = 0x%08X\n", v);
        ReadReg(h, 0xC0E0, &v); printf("  CP_MEC_CNTL    = 0x%08X\n", v);
        ReadReg(h, 0xC0E4, &v); printf("  CP_MEC_STATUS  = 0x%08X\n", v);
        ReadReg(h, 0xC100, &v); printf("  NBIO_ID        = 0x%08X\n", v);
    }

    /* === Phase 4: CP registers via GC_BASE shifted === */
    printf("\n--- CP Registers via GC_BASE (0x3200+) ---\n");
    {
        UINT32 cpGc[] = {
            0x3A00, 0x3A04, 0x3A08, 0x3A0C,
            0x3A60, 0x3A64, 0x3A68, 0x3A6C,
            0x3A80, 0x3A84, 0x3A88, 0x3A8C,
            0x3AA0, 0x3AA4, 0x3AA8, 0x3AAC,
            0x3AD8, 0x3ADC,
            0x3AE0, 0x3AE4, 0x3AE8, 0x3AEC,
        };
        for (int i = 0; i < sizeof(cpGc)/sizeof(cpGc[0]); i++) {
            ReadReg(h, cpGc[i], &v);
            if (v != 0x00000000 && v != 0xFFFFFFFF)
                printf("  [0x%04X] = 0x%08X\n", cpGc[i], v);
        }
    }

    /* === Phase 5: SEND_PM4 — NOP test === */
    printf("\n--- SEND_PM4 NOP Test ---\n");
    {
        UINT32 nop = 0x30000000; /* PM4_TYPE2_NOP */
        BOOL sent = SendPm4(h, &nop, 1, 0);
        printf("  NOP send: %s (err=%lu)\n", sent ? "OK" : "FAIL", sent ? 0 : GetLastError());
    }

    /* === Phase 6: SEND_PM4 — NOP + EOP fence === */
    printf("\n--- SEND_PM4 NOP + EOP Fence Test ---\n");
    {
        UINT32 cmds[2] = { 0x30000000, 0x30000000 }; /* 2x NOP */
        BOOL sent = SendPm4(h, cmds, 2, 1);
        printf("  NOP+EOP send: %s (err=%lu)\n", sent ? "OK" : "FAIL", sent ? 0 : GetLastError());
    }

    /* === Phase 7: SCRATCH write/readback === */
    printf("\n--- SCRATCH Write/Readback ---\n");
    {
        UINT32 testVals[] = { 0xDEADBEEF, 0xCAFEBABE, 0x12345678 };
        for (int i = 0; i < 3; i++) {
            WriteReg(h, 0x32D4, testVals[i]);
            ReadReg(h, 0x32D4, &v);
            printf("  W=0x%08X R=0x%08X %s\n", testVals[i], v, (v == testVals[i]) ? "OK" : "MISMATCH");
        }
    }

    CloseHandle(h);
    printf("\n=== Done ===\n");
    return 0;
}
