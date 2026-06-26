/*
 * bios-ring-pm4-test.c — Test PM4 via PSP KIQ (uses BIOS ring at 0x7E512000)
 *
 * Key insight: BIOS already configured GCVM for KIQ ring at 0x7E512000.
 * The PSP KIQ submit path sends PM4 through the PSP driver, which programs
 * GPU KIQ registers with the BIOS ring address. Since BIOS GCVM mapping is
 * already active, the GPU should be able to read the PM4 commands.
 *
 * This test uses the driver's SEND_PM4 IOCTL (PATH 1: PSP KIQ).
 */
#include <windows.h>
#include <stdio.h>

#define IOCTL_AMDBC250_INIT_HARDWARE  0x80000B80
#define IOCTL_AMDBC250_READ_REG       0x80000B88
#define IOCTL_AMDBC250_WRITE_REG      0x80000B8C
#define IOCTL_AMDBC250_SEND_PM4       0x80000B84

typedef struct {
    UINT32 Offset;
    UINT32 Value;
} REG_IO;

typedef struct {
    UINT32 Commands[64];        /* Up to 64 DWORDs of PM4 commands */
    UINT32 CommandCount;        /* Number of DWORDs */
    UINT32 Padding;             /* alignment */
    UINT64 FenceValue;          /* 64-bit fence */
    UINT32 QueueType;           /* 0=GFX, 1=Compute, 2=SDMA */
    UINT32 Padding2;
} SEND_PM4_IO;

static HANDLE OpenGpuDriver(void) {
    return CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
}

static UINT32 ReadReg(HANDLE h, UINT32 off) {
    REG_IO io = { off, 0 };
    DWORD br = 0;
    DeviceIoControl(h, IOCTL_AMDBC250_READ_REG, &io, sizeof(io), &io, sizeof(io), &br, NULL);
    return io.Value;
}

static void WriteReg(HANDLE h, UINT32 off, UINT32 val) {
    REG_IO io = { off, val };
    DWORD br = 0;
    DeviceIoControl(h, IOCTL_AMDBC250_WRITE_REG, &io, sizeof(io), &io, sizeof(io), &br, NULL);
}

static BOOL SendPm4(HANDLE h, UINT32* cmds, UINT32 count, UINT32 fence) {
    SEND_PM4_IO io = {0};
    io.CommandCount = count;
    io.FenceValue = fence;
    io.QueueType = 0;  /* GFX */
    for (UINT32 i = 0; i < count && i < 64; i++) {
        io.Commands[i] = cmds[i];
    }
    DWORD br = 0;
    return DeviceIoControl(h, IOCTL_AMDBC250_SEND_PM4, &io, sizeof(io), NULL, 0, &br, NULL);
}

int main(int argc, char *argv[]) {
    printf("=== BIOS Ring PM4 Test (via PSP KIQ) ===\n");
    printf("Strategy: Use PSP KIQ submit which programs GPU KIQ_BASE = BIOS ring (0x7E512000)\n");
    printf("BIOS already configured GCVM for this address.\n\n");

    HANDLE h = OpenGpuDriver();
    if (h == INVALID_HANDLE_VALUE) {
        printf("Cannot open GPU driver (err=%lu)\n", GetLastError());
        return 1;
    }
    printf("GPU driver opened\n");

    /* Step 1: Init hardware */
    printf("\n--- Step 1: INIT_HARDWARE ---\n");
    UCHAR initIn[32] = {0};
    UCHAR initOut[32] = {0};
    *(UINT64*)(initIn + 0)  = 0xFE800000ULL;
    *(UINT32*)(initIn + 8)  = 0x00080000;
    *(UINT32*)(initIn + 12) = 1;  /* NBIO_MAP */
    *(UINT64*)(initIn + 16) = 0xC0000000ULL;
    *(UINT32*)(initIn + 24) = 0x10000000;
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE,
        initIn, sizeof(initIn), initOut, sizeof(initOut), &br, NULL);
    printf("INIT_HARDWARE: %s (err=%lu)\n", ok ? "OK" : "FAIL", ok ? 0 : GetLastError());

    /* Step 2: Read current state */
    printf("\n--- Step 2: Current state ---\n");
    UINT32 scratch = ReadReg(h, 0x32D4);
    UINT32 meCntl = ReadReg(h, 0x4A74);
    UINT32 kiqBaseLo = ReadReg(h, 0xE060);
    UINT32 kiqBaseHi = ReadReg(h, 0xE064);
    UINT32 hqdWptrHi = ReadReg(h, 0xDB94);
    printf("  SCRATCH    = 0x%08X\n", scratch);
    printf("  ME_CNTL    = 0x%08X\n", meCntl);
    printf("  KIQ_BASE   = 0x%08X%08X\n", kiqBaseHi, kiqBaseLo);
    printf("  HQD_WPTR_HI = 0x%08X\n", hqdWptrHi);

    /* Step 3: Halt ME, configure HQD with BIOS ring, resume, send PM4 */
    printf("\n--- Step 3: Halt ME + reconfigure HQD ---\n");

    /* Halt ME+PFP */
    WriteReg(h, 0x4A74, meCntl | (1 << 28) | (1 << 30));
    Sleep(1);

    /* Select KIQ engine */
    WriteReg(h, 0x34D0, 0x00010000);
    Sleep(1);

    /* Deactivate queue */
    WriteReg(h, 0xDAC0, 0);
    Sleep(1);

    /* Set PQ_BASE = BIOS KIQ ring address */
    WriteReg(h, 0xDAD8, kiqBaseLo & 0xFFFFFF00);
    WriteReg(h, 0xDADC, kiqBaseHi);

    /* PQ_CONTROL = ring size (log2 DWORDs) — try 16 for 256KB */
    WriteReg(h, 0xDAFC, 16);

    /* Clear WPTR/RPTR */
    WriteReg(h, 0xDB90, 0);
    WriteReg(h, 0xDB94, 0);
    WriteReg(h, 0xE06C, 0);

    /* Set VMID=0, PERSISTENT_STATE */
    WriteReg(h, 0xDAC4, 0);
    WriteReg(h, 0xDAC8, 0xE001);

    /* Activate queue */
    WriteReg(h, 0xDAC0, 1);
    Sleep(1);

    /* Restore broadcast */
    WriteReg(h, 0x34D0, 0xE0000000);

    /* Resume CP */
    WriteReg(h, 0x4A74, meCntl & ~((1 << 28) | (1 << 30)));
    Sleep(100);

    printf("  HQD reconfigured with BIOS ring address\n");

    /* Read state after reconfigure */
    printf("  After reconfigure:\n");
    printf("    KIQ_BASE   = 0x%08X%08X\n", ReadReg(h, 0xE064), ReadReg(h, 0xE060));
    printf("    HQD_PQ_BASE = 0x%08X%08X\n", ReadReg(h, 0xDADC), ReadReg(h, 0xDAD8));
    printf("    SCRATCH    = 0x%08X\n", ReadReg(h, 0x32D4));

    /* Step 4: Send PM4 via SEND_PM4 IOCTL (PATH 1: PSP KIQ) */
    printf("\n--- Step 4: Send PM4 via PSP KIQ ---\n");
    UINT32 pm4[8];
    /* PM4 Type 3: IT_WRITE_DATA (0x37), count=3 (addr_lo + addr_hi + data)
     * header = 0xC0370003 */
    pm4[0] = 0xC0370003;  /* IT_WRITE_DATA */
    pm4[1] = 0x10100000;  /* CONTROL: DST_SEL=register, WR_CONFIRM */
    pm4[2] = 0x000032D4;  /* ADDR_LO = SCRATCH */
    pm4[3] = 0x00000000;  /* ADDR_HI */
    pm4[4] = 0xCAFEBABE;  /* DATA */
    pm4[5] = 0xC0001000;  /* NOP */
    pm4[6] = 0xC0001000;  /* NOP */
    pm4[7] = 0xC0001000;  /* NOP */

    ok = SendPm4(h, pm4, 8, 0);
    printf("  SEND_PM4 (PSP KIQ): %s (err=%lu)\n", ok ? "OK" : "FAIL", ok ? 0 : GetLastError());

    if (!ok) {
        printf("  PSP KIQ submit failed. Trying direct MMIO path...\n");

        /* Fallback: Try writing PM4 directly to KIQ ring via MMIO.
         * This requires the ring memory to be CPU-mapped, which we can't do
         * from userspace. But we can try advancing WPTR and see what happens. */

        /* Halt ME again */
        meCntl = ReadReg(h, 0x4A74);
        WriteReg(h, 0x4A74, meCntl | (1 << 28) | (1 << 30));
        Sleep(1);

        /* Select KIQ */
        WriteReg(h, 0x34D0, 0x00010000);
        Sleep(1);

        /* Deactivate */
        WriteReg(h, 0xDAC0, 0);
        Sleep(1);

        /* Set PQ_BASE = BIOS ring */
        WriteReg(h, 0xDAD8, kiqBaseLo & 0xFFFFFF00);
        WriteReg(h, 0xDADC, kiqBaseHi);

        /* Set RPTR = WPTR = 0 */
        WriteReg(h, 0xE06C, 0);
        WriteReg(h, 0xDB90, 0);
        WriteReg(h, 0xDB94, 0);

        /* PQ_CONTROL */
        WriteReg(h, 0xDAFC, 16);

        /* Activate */
        WriteReg(h, 0xDAC0, 1);
        Sleep(1);

        /* Restore */
        WriteReg(h, 0x34D0, 0xE0000000);

        /* Resume */
        WriteReg(h, 0x4A74, meCntl & ~((1 << 28) | (1 << 30)));
        Sleep(100);

        printf("  Direct MMIO: reconfigured HQD with BIOS ring\n");
        printf("  Cannot write PM4 from userspace — ring not CPU-mapped\n");
        printf("  Need driver-side IOCTL to write PM4 to BIOS ring\n");
    }

    /* Step 5: Wait and read results */
    printf("\n--- Step 5: Results ---\n");
    Sleep(200);
    UINT32 scratchAfter = ReadReg(h, 0x32D4);
    UINT32 kiqWptr = ReadReg(h, 0xE078);
    UINT32 kiqRptr = ReadReg(h, 0xE06C);
    printf("  SCRATCH before: 0x%08X\n", scratch);
    printf("  SCRATCH after:  0x%08X\n", scratchAfter);
    printf("  KIQ_WPTR:       0x%08X\n", kiqWptr);
    printf("  KIQ_RPTR:       0x%08X\n", kiqRptr);
    printf("  ME_CNTL:        0x%08X\n", ReadReg(h, 0x4A74));

    if (scratchAfter == 0xCAFEBABE) {
        printf("\n*** PM4 EXECUTED! SCRATCH = 0xCAFEBABE ***\n");
        printf("*** 3D ACCELERATION MAY BE POSSIBLE! ***\n");
    } else if (scratchAfter != scratch) {
        printf("\n*** SCRATCH CHANGED! 0x%08X -> 0x%08X ***\n", scratch, scratchAfter);
    } else {
        printf("\nSCRATCH unchanged — PM4 did not execute\n");
        printf("\nDiagnosis:\n");
        printf("  If SEND_PM4 failed: PSP KIQ ring not working (SOS firmware limitation)\n");
        printf("  If SEND_PM4 succeeded but no change: GCVM not mapping ring buffer\n");
        printf("  Need to write PM4 directly to BIOS ring from kernel mode\n");
    }

    CloseHandle(h);
    return 0;
}
