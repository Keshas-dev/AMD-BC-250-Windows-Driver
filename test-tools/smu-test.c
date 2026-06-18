#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define IOCTL_AMDBC250_READ_REG   0x900
#define IOCTL_AMDBC250_WRITE_REG  0x901

static HANDLE g_hGpu = INVALID_HANDLE_VALUE;

static BOOL GpuReadReg(ULONG offset, ULONG *value) {
    ULONG out = 0; DWORD br = 0;
    BOOL ok = DeviceIoControl(g_hGpu, IOCTL_AMDBC250_READ_REG,
        &offset, sizeof(offset), &out, sizeof(out), &br, NULL);
    if (ok && value) *value = out;
    return ok;
}

static BOOL GpuWriteReg(ULONG offset, ULONG value) {
    ULONG params[2] = { offset, value }; DWORD br = 0;
    return DeviceIoControl(g_hGpu, IOCTL_AMDBC250_WRITE_REG,
        params, sizeof(params), NULL, 0, &br, NULL);
}

/* SMU v11.8 registers (MP1_BASE = 0x16000, GC_BASE-shifted) */
#define SMU_C2PMSG_66  0x16A08  /* Message register (write msg -> triggers SMU) */
#define SMU_C2PMSG_82  0x16A48  /* Argument register (write param, read result) */
#define SMU_C2PMSG_83  0x16A4C  /* Extended data */
#define SMU_C2PMSG_90  0x16A68  /* Response register (0=busy, 1=OK, FF=err) */

/* SMU message IDs for BC-250 v11.8 */
#define SMU_MSG_TestMessage       0x01
#define SMU_MSG_GetSmuVersion     0x02
#define SMU_MSG_RequestActiveWgp  0x18
#define SMU_MSG_QueryActiveWgp    0x1E
#define SMU_MSG_SetCoreEnableMask 0x2C
#define SMU_MSG_GetEnabledSmuFeatures 0x3D

static BOOL SmuSendMailbox(ULONG msgId, ULONG param, ULONG *result) {
    ULONG val;
    int timeout;

    /* Step 1: Clear response register */
    GpuWriteReg(SMU_C2PMSG_90, 0);
    Sleep(1);

    /* Step 2: Write argument */
    GpuWriteReg(SMU_C2PMSG_82, param);
    Sleep(1);

    /* Step 3: Write message (triggers SMU) */
    GpuWriteReg(SMU_C2PMSG_66, msgId);
    Sleep(1);

    /* Step 4: Poll response register */
    for (timeout = 0; timeout < 1000; timeout++) {
        GpuReadReg(SMU_C2PMSG_90, &val);
        if (val != 0) break;
        Sleep(1);
    }

    if (timeout >= 1000) {
        printf("    TIMEOUT waiting for SMU response!\n");
        return FALSE;
    }

    /* Read result */
    ULONG resp = 0;
    GpuReadReg(SMU_C2PMSG_90, &resp);
    GpuReadReg(SMU_C2PMSG_82, &val);

    printf("    Response: C2PMSG_90=0x%08X C2PMSG_82=0x%08X (after %d ms)\n",
        resp, val, timeout);

    if (result) *result = val;

    if (resp == 1) {
        printf("    SMU OK!\n");
        return TRUE;
    } else if (resp == 0xFFFFFFFF || resp == 0xFF) {
        printf("    SMU FAILED (error response)\n");
        return FALSE;
    } else {
        printf("    SMU response unexpected: 0x%08X\n", resp);
        return TRUE; /* might still be valid */
    }
}

static void ProbeSmuRegisters(void) {
    ULONG val;
    printf("=== SMU v11.8 Register Probe ===\n\n");

    /* MP1 base range */
    printf("MP1 (0x16000+) registers:\n");
    struct { const char *name; ULONG off; } mp1[] = {
        {"MP1_C2PMSG_66 (msg)",     0x16A08},
        {"MP1_C2PMSG_82 (arg)",     0x16A48},
        {"MP1_C2PMSG_83 (ext)",     0x16A4C},
        {"MP1_C2PMSG_90 (resp)",    0x16A68},
        {"MP1_C2PMSG_66 alt1",      0x16104},
        {"MP1_C2PMSG_82 alt1",      0x16148},
        {"MP1_C2PMSG_90 alt1",      0x16168},
        {"MP1_SMU_MSG_66 (native)", 0x16098},
        {"MP1_SMU_MSG_82 (native)", 0x160C8},
        {"MP1_SMU_MSG_90 (native)", 0x160E8},
    };
    for (int i = 0; i < (int)(sizeof(mp1)/sizeof(mp1[0])); i++) {
        if (GpuReadReg(mp1[i].off, &val))
            printf("  %-36s = 0x%08X\n", mp1[i].name, val);
    }

    /* Check THM */
    printf("\nTHM (0x16600+) registers:\n");
    struct { const char *name; ULONG off; } thm[] = {
        {"THM_BASE+0x00", 0x16600},
        {"THM_BASE+0x04", 0x16604},
        {"THM_BASE+0x08", 0x16608},
        {"THM_BASE+0x0C", 0x1660C},
    };
    for (int i = 0; i < (int)(sizeof(thm)/sizeof(thm[0])); i++) {
        if (GpuReadReg(thm[i].off, &val))
            printf("  %-36s = 0x%08X\n", thm[i].name, val);
    }

    /* Also probe NBIO range for PSP */
    printf("\nPSP (0x16000+) NBIO alias:\n");
    struct { const char *name; ULONG off; } psp[] = {
        {"PSP_C2PMSG_35 (cmd)",     0x16058},
        {"PSP_C2PMSG_36 (pa_lo)",   0x1605C},
        {"PSP_C2PMSG_37 (pa_hi)",   0x16060},
        {"PSP_C2PMSG_81 (resp)",    0x160E4},
    };
    for (int i = 0; i < (int)(sizeof(psp)/sizeof(psp[0])); i++) {
        if (GpuReadReg(psp[i].off, &val))
            printf("  %-36s = 0x%08X\n", psp[i].name, val);
    }
}

static void TestSmuComm(void) {
    ULONG val, result;
    printf("\n=== SMU Communication Test ===\n\n");

    /* Read initial state */
    printf("Initial SMU registers:\n");
    GpuReadReg(SMU_C2PMSG_66, &val); printf("  C2PMSG_66 (msg)  = 0x%08X\n", val);
    GpuReadReg(SMU_C2PMSG_82, &val); printf("  C2PMSG_82 (arg)  = 0x%08X\n", val);
    GpuReadReg(SMU_C2PMSG_90, &val); printf("  C2PMSG_90 (resp) = 0x%08X\n", val);

    /* Test 1: TestMessage (0x1) */
    printf("\n--- Test 1: SMU TestMessage (0x1) ---\n");
    if (SmuSendMailbox(SMU_MSG_TestMessage, 0, &result)) {
        printf("  SMU is alive! Result: 0x%08X\n", result);
    } else {
        printf("  SMU not responding at this offset\n");

        /* Try alternative offsets */
        printf("\n  Trying alternative SMU offsets...\n");
        
        /* Try native mm offset (0x16098 = MP1 base + mm offset * 4) */
        printf("  Offset 0x16098: ");
        GpuWriteReg(0x16098, 0); Sleep(1);
        GpuWriteReg(0x160C8, 0); Sleep(1);
        GpuWriteReg(0x16098, SMU_MSG_TestMessage); Sleep(1);
        for (int t = 0; t < 100; t++) {
            GpuReadReg(0x160E8, &val);
            if (val != 0) break;
            Sleep(1);
        }
        GpuReadReg(0x160E8, &val);
        printf("resp=0x%08X\n", val);

        /* Try NBIO aliased SMU */
        printf("  Offset 0x16104: ");
        GpuWriteReg(0x16104, 0); Sleep(1);
        GpuWriteReg(0x16148, 0); Sleep(1);
        GpuWriteReg(0x16104, SMU_MSG_TestMessage); Sleep(1);
        for (int t = 0; t < 100; t++) {
            GpuReadReg(0x16168, &val);
            if (val != 0) break;
            Sleep(1);
        }
        GpuReadReg(0x16168, &val);
        printf("resp=0x%08X\n", val);
    }

    /* Test 2: GetSmuVersion (0x2) */
    printf("\n--- Test 2: SMU GetSmuVersion (0x2) ---\n");
    if (SmuSendMailbox(SMU_MSG_GetSmuVersion, 0, &result)) {
        printf("  SMU Version: 0x%08X\n", result);
        printf("  Major: %u Minor: %u\n", (result >> 16) & 0xFF, result & 0xFF);
    }

    /* Test 3: GetEnabledSmuFeatures (0x3D) */
    printf("\n--- Test 3: SMU GetEnabledSmuFeatures (0x3D) ---\n");
    if (SmuSendMailbox(SMU_MSG_GetEnabledSmuFeatures, 0, &result)) {
        printf("  Enabled Features: 0x%08X\n", result);
        if (result & (1 << 3)) printf("    [3] DPM_GFXCLK\n");
        if (result & (1 << 4)) printf("    [4] DPM_UCLK\n");
        if (result & (1 << 5)) printf("    [5] DPM_SOCCLK\n");
        if (result & (1 << 6)) printf("    [6] DPM_FCLK\n");
        if (result & (1 << 16)) printf("    [16] DS_GFXCLK\n");
    }
}

static void PowerUpGc(void) {
    ULONG val, result;
    printf("\n=== GC Power-Up via SMU ===\n\n");

    /* Step 1: Read GC state before */
    printf("GC state BEFORE power-up:\n");
    GpuReadReg(0x3264, &val); printf("  CC_GC_SHADER_ARRAY_CONFIG (0x3264) = 0x%08X\n", val);
    GpuReadReg(0x34FC, &val); printf("  SPI_PG_ENABLE_STATIC_WGP_MASK (0x34FC) = 0x%08X\n", val);
    GpuReadReg(0x3260, &val); printf("  GRBM_STATUS (0x3260) = 0x%08X\n", val);
    GpuReadReg(0x4A74, &val); printf("  CP_ME_CNTL (0x4A74) = 0x%08X\n", val);

    /* Step 2: RequestActiveWgp (0x18) */
    printf("\n--- Sending RequestActiveWgp (0x18) ---\n");
    if (SmuSendMailbox(SMU_MSG_RequestActiveWgp, 0, &result)) {
        printf("  GC power-up requested! Result: 0x%08X\n", result);
    } else {
        printf("  RequestActiveWgp FAILED\n");
        printf("  Trying with param=0xFFFFFFFF (all WGP)...\n");
        SmuSendMailbox(SMU_MSG_RequestActiveWgp, 0xFFFFFFFF, &result);
    }

    Sleep(100);

    /* Step 3: Read GC state after */
    printf("\nGC state AFTER power-up:\n");
    GpuReadReg(0x3264, &val); printf("  CC_GC_SHADER_ARRAY_CONFIG (0x3264) = 0x%08X\n", val);
    GpuReadReg(0x34FC, &val); printf("  SPI_PG_ENABLE_STATIC_WGP_MASK (0x34FC) = 0x%08X\n", val);
    GpuReadReg(0x3260, &val); printf("  GRBM_STATUS (0x3260) = 0x%08X\n", val);
    GpuReadReg(0x4A74, &val); printf("  CP_ME_CNTL (0x4A74) = 0x%08X\n", val);

    /* Step 4: Try writing SPI_PG_ENABLE_STATIC_WGP_MASK */
    printf("\n--- Trying to enable all WGPs via SPI ---\n");
    GpuReadReg(0x34FC, &val);
    printf("  SPI_PG before: 0x%08X\n", val);
    /* Enable all 6 WGPs (bits 8-13) */
    GpuWriteReg(0x34FC, 0x3F00);
    Sleep(10);
    GpuReadReg(0x34FC, &val);
    printf("  SPI_PG after:  0x%08X\n", val);
    if (val == 0x3F00)
        printf("  *** SPI_PG WRITABLE! GC has power! ***\n");
    else if (val == 0x2000)
        printf("  SPI_PG unchanged — GC still power-gated\n");
    else
        printf("  SPI_PG changed to unexpected value\n");

    /* Step 5: QueryActiveWgp */
    printf("\n--- QueryActiveWgp (0x1E) ---\n");
    SmuSendMailbox(SMU_MSG_QueryActiveWgp, 0, &result);
    printf("  Active WGP mask: 0x%08X\n", result);

    /* Step 6: SetCoreEnableMask (0x2C) — enable all CUs */
    printf("\n--- SetCoreEnableMask (0x2C) ---\n");
    SmuSendMailbox(SMU_MSG_SetCoreEnableMask, 0xFFFFFFFF, &result);
    printf("  Core enable result: 0x%08X\n", result);

    /* Step 7: Verify GC state */
    printf("\nFinal GC state:\n");
    GpuReadReg(0x3264, &val); printf("  CC_GC_SHADER_ARRAY_CONFIG = 0x%08X\n", val);
    GpuReadReg(0x34FC, &val); printf("  SPI_PG_ENABLE_STATIC_WGP_MASK = 0x%08X\n", val);
    GpuReadReg(0x32D4, &val); printf("  SCRATCH = 0x%08X\n", val);
    GpuReadReg(0x4A74, &val); printf("  CP_ME_CNTL = 0x%08X\n", val);

    /* Step 8: Check if GCVM registers are now accessible */
    printf("\n--- Checking GCVM after power-up ---\n");
    GpuReadReg(0x1A00, &val); printf("  VM_CONTEXT0_CNTL (raw 0x1A00) = 0x%08X\n", val);
    GpuReadReg(0x2C60, &val); printf("  VM_CONTEXT0_CNTL (shifted 0x2C60) = 0x%08X\n", val);
    GpuReadReg(0x9B00, &val); printf("  VM_CTX0_PGT_BASE (0x9B00) = 0x%08X\n", val);
}

int main(int argc, char *argv[]) {
    printf("=== SMU v11.8 Communication Test ===\n");
    printf("BC-250 (Cyan Skillfish) — GC Power-Up\n\n");

    g_hGpu = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_hGpu == INVALID_HANDLE_VALUE) {
        printf("Cannot open GPU driver (err=%lu)\n", GetLastError());
        printf("Make sure GPU driver is loaded!\n");
        return 1;
    }
    printf("GPU driver opened\n\n");

    if (argc > 1 && strcmp(argv[1], "--probe") == 0) {
        ProbeSmuRegisters();
    } else if (argc > 1 && strcmp(argv[1], "--powerup") == 0) {
        PowerUpGc();
    } else {
        TestSmuComm();
        printf("\n");
        PowerUpGc();
    }

    CloseHandle(g_hGpu);
    printf("\nDone.\n");
    return 0;
}
