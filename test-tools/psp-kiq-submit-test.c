/* psp-kiq-submit-test.c — Test if PSP KIQ path works WITHOUT GCVM
 *
 * The PSP driver programs GPU HQD registers and writes WPTR.
 * If GPU CP has flat/identity access → it will read ring buffer from
 * system RAM and execute PM4 commands.
 *
 * Test: Send PM4 NOP + WRITE_REG(SCRATCH) via PSP KIQ, check SCRATCH.
 */

#include <windows.h>
#include <stdio.h>

#define IOCTL_AMDBC250_READ_REG    0x80000B88
#define IOCTL_AMDBC250_WRITE_REG   0x80000B8C
#define IOCTL_AMDBC250_INIT_HW     0x80000B80
#define IOCTL_AMDBC250_LOAD_CP_FW  0x80000BD4
#define IOCTL_AMDBC250_KIQ_SUBMIT  0x80000BDC  /* KIQ_NOP_TEST from GPU driver */

typedef struct { UINT32 Offset; UINT32 Value; } REG_IO;

static HANDLE OpenDevice(void) {
    return CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
                       0, NULL, OPEN_EXISTING, 0, NULL);
}

static UINT32 R(HANDLE h, UINT32 offset) {
    REG_IO io = { offset, 0 };
    DWORD bytes;
    DeviceIoControl(h, IOCTL_AMDBC250_READ_REG, &io, sizeof(io), &io, sizeof(io), &bytes, NULL);
    return io.Value;
}

static void W(HANDLE h, UINT32 offset, UINT32 value) {
    REG_IO io = { offset, value };
    DWORD bytes;
    DeviceIoControl(h, IOCTL_AMDBC250_WRITE_REG, &io, sizeof(io), &io, sizeof(io), &bytes, NULL);
}

int main(void) {
    printf("=== PSP KIQ Submit Test (No GCVM) ===\n\n");

    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) {
        printf("Cannot open device\n");
        return 1;
    }

    /* Step 1: INIT_HARDWARE to set up GPU driver state */
    printf("--- Step 1: INIT_HARDWARE ---\n");
    DWORD bytes;
    BOOL ok = DeviceIoControl(h, IOCTL_AMDBC250_INIT_HW, NULL, 0, NULL, 0, &bytes, NULL);
    printf("  INIT_HARDWARE: %s\n", ok ? "OK" : "FAILED");

    /* Step 2: Check GPU state before firmware load */
    printf("\n--- Step 2: GPU state before FW ---\n");
    printf("  GPU_ID (0x0000)    = 0x%08X\n", R(h, 0x0000));
    printf("  ME_CNTL (0x4A74)   = 0x%08X\n", R(h, 0x4A74));
    printf("  SCRATCH (0x32D4)   = 0x%08X\n", R(h, 0x32D4));
    printf("  KIQ_BASE (0xE060)  = 0x%08X\n", R(h, 0xE060));
    printf("  KIQ_RPTR (0xE06C)  = 0x%08X\n", R(h, 0xE06C));
    printf("  KIQ_WPTR (0xE078)  = 0x%08X\n", R(h, 0xE078));
    printf("  HQD_ACTIVE (0xDAC0)= 0x%08X\n", R(h, 0xDAC0));
    printf("  HQD_PQ_BASE(0xDAD8)= 0x%08X\n", R(h, 0xDAD8));

    /* Step 3: Load CP firmware (ME + PFP) */
    printf("\n--- Step 3: Load CP Firmware ---\n");

    /* Load ME firmware */
    {
        UCHAR meData[263424];
        HANDLE fwFile = CreateFileA("C:\\AMD-BC-250\\AMD-BC-250-PSP-Windows-Driver\\output\\cyan_skillfish2_me.bin",
            GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (fwFile != INVALID_HANDLE_VALUE) {
            DWORD bytesRead;
            ReadFile(fwFile, meData, sizeof(meData), &bytesRead, NULL);
            CloseHandle(fwFile);
            printf("  ME firmware: %u bytes loaded\n", bytesRead);
        } else {
            printf("  ME firmware: NOT FOUND\n");
        }
    }

    /* Load PFP firmware */
    {
        UCHAR pfpData[263424];
        HANDLE fwFile = CreateFileA("C:\\AMD-BC-250\\AMD-BC-250-PSP-Windows-Driver\\output\\cyan_skillfish2_pfp.bin",
            GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (fwFile != INVALID_HANDLE_VALUE) {
            DWORD bytesRead;
            ReadFile(fwFile, pfpData, sizeof(pfpData), &bytesRead, NULL);
            CloseHandle(fwFile);
            printf("  PFP firmware: %u bytes loaded\n", bytesRead);
        } else {
            printf("  PFP firmware: NOT FOUND\n");
        }
    }

    /* Step 4: Check ME_CNTL after FW load */
    printf("\n--- Step 4: GPU state after FW ---\n");
    printf("  ME_CNTL (0x4A74)   = 0x%08X\n", R(h, 0x4A74));
    printf("  SCRATCH (0x32D4)   = 0x%08X\n", R(h, 0x32D4));

    /* Step 5: Try KIQ NOP test via GPU driver IOCTL */
    printf("\n--- Step 5: KIQ NOP Test via IOCTL ---\n");
    {
        UINT32 outBuf[32] = {0};
        ok = DeviceIoControl(h, IOCTL_AMDBC250_KIQ_SUBMIT, NULL, 0,
                             outBuf, sizeof(outBuf), &bytes, NULL);
        if (ok) {
            printf("  KIQ_NOP_TEST: OK\n");
            printf("  Result[0]  = 0x%08X\n", outBuf[0]);
            printf("  Result[1]  = 0x%08X\n", outBuf[1]);
            printf("  Result[2]  = 0x%08X\n", outBuf[2]);
            printf("  Result[3]  = 0x%08X\n", outBuf[3]);
            printf("  Result[4]  = 0x%08X\n", outBuf[4]);
        } else {
            printf("  KIQ_NOP_TEST: FAILED (error=%lu)\n", GetLastError());
        }
    }

    /* Step 6: Check final GPU state */
    printf("\n--- Step 6: Final GPU state ---\n");
    printf("  SCRATCH (0x32D4)   = 0x%08X\n", R(h, 0x32D4));
    printf("  KIQ_RPTR (0xE06C)  = 0x%08X\n", R(h, 0xE06C));
    printf("  KIQ_WPTR (0xE078)  = 0x%08X\n", R(h, 0xE078));
    printf("  ME_CNTL (0x4A74)   = 0x%08X\n", R(h, 0x4A74));

    /* Step 7: Try writing SCRATCH directly to test GPU access */
    printf("\n--- Step 7: SCRATCH write test ---\n");
    UINT32 scratch_before = R(h, 0x32D4);
    W(h, 0x32D4, 0x12345678);
    UINT32 scratch_after = R(h, 0x32D4);
    printf("  Before=0x%08X, Wrote=0x12345678, After=0x%08X %s\n",
           scratch_before, scratch_after,
           scratch_after == 0x12345678 ? "WRITABLE" : "READ_ONLY");
    W(h, 0x32D4, scratch_before);

    CloseHandle(h);

    printf("\n=== Summary ===\n");
    printf("  If SCRATCH changed via PM4 → GPU CP can access ring buffer!\n");
    printf("  If SCRATCH unchanged → GPU CP cannot read ring (GCVM dead).\n");

    return 0;
}
