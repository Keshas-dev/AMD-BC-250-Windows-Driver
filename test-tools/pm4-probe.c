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
    /* Match AMDBC250_IOCTL_SEND_PM4 struct: Commands[64] first, then CommandCount */
    RtlCopyMemory(buf + 0, cmds, count * sizeof(UINT32));  /* Commands[64] at offset 0 */
    *(UINT32*)(buf + 256) = count;   /* CommandCount at offset 256 */
    *(UINT32*)(buf + 260) = fence;   /* FenceValue at offset 260 */
    *(UINT32*)(buf + 264) = 0;       /* QueueType = GFX at offset 264 */
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

    /* === Phase 7: SCRATCH write/readback via PM4 WRITE_REG === */
    printf("\n--- PM4 SCRATCH Write via KIQ ---\n");
    {
        /* PM4 Type 3: IT_WRITE_REG (0x12), count=2
         * Header: TYPE=3(11), COUNT=2(010), OPCODE=0x12 → 0xC0021200
         * DWord1: register byte offset (0x32D4 = SCRATCH)
         * DWord2: value to write */
        UINT32 cmds[3] = {
            0xC0021200,  /* PM4 header: type=3, count=2, opcode=IT_WRITE_REG */
            0x000032D4,  /* SCRATCH register offset */
            0xCAFEBABE   /* value to write */
        };
        ReadReg(h, 0x32D4, &v);
        printf("  SCRATCH before: 0x%08X\n", v);
        BOOL sent = SendPm4(h, cmds, 3, 0);
        printf("  PM4 WRITE_REG send: %s (err=%lu)\n", sent ? "OK" : "FAIL", sent ? 0 : GetLastError());
        /* Wait a bit for GPU to process */
        Sleep(100);
        ReadReg(h, 0x32D4, &v);
        printf("  SCRATCH after:  0x%08X (expect 0xCAFEBABE)\n", v);
        if (v == 0xCAFEBABE) {
            printf("  *** PM4 COMMAND EXECUTED BY GPU! ***\n");
        } else if ((v & 0x4FFFFFFF) == 0x0AFEBABE) {
            printf("  *** PM4 PARTIAL — bit 31 masked by hardware ***\n");
        }
    }

    /* === Phase 8: HQD register verification === */
    printf("\n--- HQD Register Check ---\n");
    {
        /* Select KIQ engine first (ME=1) so we read KIQ-specific HQD regs */
        WriteReg(h, 0x34D0, 0x00010000);  /* GRBM_GFX_INDEX: ME=1 for KIQ */

        UINT32 hqdRegs[] = {
            0x34D0,  /* GRBM_GFX_INDEX */
            0x4A74,  /* CP_ME_CNTL */
            0xDAC0,  /* CP_HQD_ACTIVE */
            0xDAC4,  /* CP_HQD_VMID */
            0xDAC8,  /* CP_HQD_PERSISTENT_STATE */
            0xDAD8,  /* CP_HQD_PQ_BASE */
            0xDADC,  /* CP_HQD_PQ_BASE_HI */
            0xDAFC,  /* CP_HQD_PQ_CONTROL */
            0xDB90,  /* CP_HQD_PQ_WPTR_LO */
            0xDB94,  /* CP_HQD_PQ_WPTR_HI */
            0xE060,  /* CP_KIQ_BASE_LO */
            0xE064,  /* CP_KIQ_BASE_HI */
            0xE078,  /* CP_KIQ_WPTR */
            0xE06C,  /* CP_KIQ_RPTR */
            0xECA1,  /* RLC_CP_SCHEDULERS */
        };
        char *names[] = {
            "GRBM_GFX_INDEX", "CP_ME_CNTL", "CP_HQD_ACTIVE", "CP_HQD_VMID",
            "CP_HQD_PERSISTENT_STATE", "CP_HQD_PQ_BASE", "CP_HQD_PQ_BASE_HI",
            "CP_HQD_PQ_CONTROL", "CP_HQD_PQ_WPTR_LO", "CP_HQD_PQ_WPTR_HI",
            "CP_KIQ_BASE_LO", "CP_KIQ_BASE_HI", "CP_KIQ_WPTR", "CP_KIQ_RPTR",
            "RLC_CP_SCHEDULERS"
        };
        for (int i = 0; i < sizeof(hqdRegs)/sizeof(hqdRegs[0]); i++) {
            ReadReg(h, hqdRegs[i], &v);
            printf("  %-25s [0x%04X] = 0x%08X\n", names[i], hqdRegs[i], v);
        }

        /* Restore broadcast */
        WriteReg(h, 0x34D0, 0xE0000000);
    }

    /* === Phase 9: SCRATCH direct MMIO write === */
    printf("\n--- SCRATCH Direct MMIO Write ---\n");
    {
        UINT32 testVals[] = { 0xDEADBEEF, 0xCAFEBABE, 0x12345678 };
        for (int i = 0; i < 3; i++) {
            WriteReg(h, 0x32D4, testVals[i]);
            ReadReg(h, 0x32D4, &v);
            printf("  W=0x%08X R=0x%08X %s\n", testVals[i], v, (v == testVals[i]) ? "OK" : "MISMATCH");
        }
    }

    /* === Phase 10: ME firmware probe (unhalt + check alive) === */
    printf("\n--- ME Firmware Probe ---\n");
    {
        /* Read CP_ME_CNTL via GC_BASE before any write */
        ReadReg(h, 0x4A74, &v);
        printf("  ME_CNTL before: 0x%08X (HALT bits: %s)\n", v,
            (v & 0x50000000) ? "SET (ME+PFP halted)" : "clear (running?)");

        /* Select KIQ engine */
        WriteReg(h, 0x34D0, 0x00010000);

        /* Unhalt ME+PFP: write 0 to ME_CNTL */
        WriteReg(h, 0x4A74, 0x00000000);
        Sleep(10);

        /* Read ME_CNTL back */
        ReadReg(h, 0x4A74, &v);
        printf("  ME_CNTL after unhalt: 0x%08X\n", v);
        if (v & 0x10000000)
            printf("  -> ME still halted (firmware NOT loaded, ME re-halts)\n");
        else
            printf("  -> ME running (firmware MAY be loaded)\n");

        /* Read ME_STATUS */
        ReadReg(h, 0xC064, &v);
        printf("  ME_STATUS: 0x%08X\n", v);

        /* Read MEC_CNTL */
        ReadReg(h, 0x4AE0, &v);
        printf("  MEC_CNTL [GC]: 0x%08X\n", v);

        /* Try reading CP ucode addr/data via NBIO to check if firmware is present */
        ReadReg(h, 0xC0A0, &v);
        printf("  PFP_UCODE_ADDR [NBIO]: 0x%08X\n", v);
        ReadReg(h, 0xC0B0, &v);
        printf("  ME_UCODE_ADDR [NBIO]: 0x%08X\n", v);

        /* Now try a simple PM4 NOP directly through MMIO ring write */
        printf("\n  Attempting direct ME_CNTL write test...\n");

        /* Halt ME, select KIQ engine, write NOP to ring, unhalt, check RPTR */
        {
            /* Halt ME+PFP first */
            WriteReg(h, 0x4A74, 0x50000000);
            Sleep(1);

            /* Select KIQ */
            WriteReg(h, 0x34D0, 0x00010000);

            /* Unhalt ME+PFP */
            WriteReg(h, 0x4A74, 0x00000000);
            Sleep(10);

            /* Read back */
            ReadReg(h, 0x4A74, &v);
            printf("  ME_CNTL after halt+unhalt: 0x%08X\n", v);
        }

        /* Restore broadcast */
        WriteReg(h, 0x34D0, 0xE0000000);
    }

    /* === Phase 10b: KIQ register write/readback test === */
    printf("\n--- KIQ Register Write/Readback Test ---\n");
    {
        /* Select KIQ engine */
        WriteReg(h, 0x34D0, 0x00010000);

        /* Try writing KIQ_BASE_LO with a known pattern */
        WriteReg(h, 0xE060, 0xDEADBEEF);
        ReadReg(h, 0xE060, &v);
        printf("  KIQ_BASE_LO: wrote 0xDEADBEEF, read 0x%08X %s\n", v,
            (v == 0xDEADBEEF) ? "OK" : "MISMATCH - register may be read-only");

        WriteReg(h, 0xE064, 0x12345678);
        ReadReg(h, 0xE064, &v);
        printf("  KIQ_BASE_HI: wrote 0x12345678, read 0x%08X %s\n", v,
            (v == 0x12345678) ? "OK" : "MISMATCH");

        WriteReg(h, 0xE078, 0x00000008);
        ReadReg(h, 0xE078, &v);
        printf("  KIQ_WPTR:    wrote 0x00000008, read 0x%08X %s\n", v,
            (v == 0x00000008) ? "OK" : "MISMATCH");

        /* Also test PQ_BASE */
        WriteReg(h, 0xDAD8, 0xCAFEBABE);
        ReadReg(h, 0xDAD8, &v);
        printf("  PQ_BASE:     wrote 0xCAFEBABE, read 0x%08X %s\n", v,
            (v == 0xCAFEBABE) ? "OK" : "MISMATCH");

        /* Test HQD_ACTIVE */
        WriteReg(h, 0xDAC0, 1);
        ReadReg(h, 0xDAC0, &v);
        printf("  HQD_ACTIVE:  wrote 1, read 0x%08X %s\n", v,
            (v == 1) ? "OK" : "MISMATCH");

        /* Test ME_CNTL */
        WriteReg(h, 0x4A74, 0x50000000);
        ReadReg(h, 0x4A74, &v);
        printf("  ME_CNTL:     wrote 0x50000000, read 0x%08X", v);
        if (v == 0x50000000)
            printf(" OK\n");
        else
            printf(" (bits 0x%08X are read-only)\n", v & ~0x50000000u);

        WriteReg(h, 0x4A74, 0x00000000);
        ReadReg(h, 0x4A74, &v);
        printf("  ME_CNTL:     wrote 0x00000000, read 0x%08X\n", v);

        /* Deactivate and restore */
        WriteReg(h, 0xDAC0, 0);
        WriteReg(h, 0x34D0, 0xE0000000);
    }

    /* === Phase 11: GPU-local KIQ test (no PSP) === */
    printf("\n--- GPU-Local KIQ Test (Phase 11) ---\n");
    {
        UINT32 kiqOut[8] = {0};
        DWORD br2 = 0;
        BOOL ok2 = DeviceIoControl(h, 0x80000BD0, NULL, 0, kiqOut, sizeof(kiqOut), &br2, NULL);
        if (ok2 && br2 >= sizeof(UINT32)) {
            UINT32 result     = kiqOut[0];
            UINT32 scratchB   = kiqOut[1];
            UINT32 scratchA   = kiqOut[2];
            UINT32 mmioMapped = kiqOut[3];
            UINT32 ringAlloc  = kiqOut[4];
            UINT32 hqdProg    = kiqOut[5];
            UINT32 pm4Sub     = kiqOut[6];
            printf("  MmioMapped=%u RingAlloc=%u HqdProg=%u Pm4Sub=%u\n",
                mmioMapped, ringAlloc, hqdProg, pm4Sub);
            printf("  SCRATCH before: 0x%08X\n", scratchB);
            printf("  SCRATCH after:  0x%08X\n", scratchA);
            printf("  WPTR readback:  0x%08X\n", result);
            if (scratchA == 0xCAFEBABE) {
                printf("  *** GPU PM4 EXECUTION CONFIRMED — CP_ME FIRMWARE IS RUNNING! ***\n");
            } else if (scratchA != scratchB) {
                printf("  *** SCRATCH changed — partial PM4 execution ***\n");
            } else {
                printf("  SCRATCH unchanged — CP_ME firmware NOT running or PM4 not executed\n");
            }
        } else {
            printf("  GPU_KIQ_TEST IOCTL failed (err=%lu)\n", GetLastError());
        }
    }

    CloseHandle(h);
    printf("\n=== Done ===\n");
    return 0;
}
