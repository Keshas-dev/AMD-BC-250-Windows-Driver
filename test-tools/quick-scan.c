#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>

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

int main(void) {
    ULONG val;
    printf("=== Quick NBIO + SMU Offset Scan ===\n\n");

    g_hGpu = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_hGpu == INVALID_HANDLE_VALUE) {
        printf("Cannot open GPU driver\n"); return 1;
    }

    /* Known working offsets */
    printf("=== Known Working Registers ===\n");
    struct { const char *name; ULONG off; } known[] = {
        {"GPU_ID",           0x0000},
        {"GRBM_STATUS",      0x3260},
        {"SCRATCH",          0x32D4},
        {"CP_ME_CNTL",       0x4A74},
        {"GRBM_GFX_INDEX",   0x34D0},
        {"KIQ_BASE_LO",      0xE060},
        {"KIQ_WPTR",         0xE078},
        {"HQD_ACTIVE",       0xDAC0},
        {"NBIO_ID_0xC100",   0xC100},
        {"NBIO_0xC180",      0xC180},
    };
    for (int i = 0; i < (int)(sizeof(known)/sizeof(known[0])); i++) {
        if (GpuReadReg(known[i].off, &val))
            printf("  %-24s (0x%X) = 0x%08X\n", known[i].name, known[i].off, val);
        else
            printf("  %-24s (0x%X) = FAILED\n", known[i].name, known[i].off);
    }

    /* NBIO SMN index/data range */
    printf("\n=== NBIO SMN Interface Scan ===\n");
    /* Try common SMN index/data pairs */
    struct { const char *name; ULONG idx; ULONG data; } smnPairs[] = {
        {"NBIO+0x13C/0x140", 0xC13C, 0xC140},
        {"NBIO+0x15C/0x160", 0xC15C, 0xC160},
        {"NBIO+0x0/0x4",     0xC000, 0xC004},
        {"NBIO+0x100/0x104", 0xC100, 0xC104},
    };
    for (int i = 0; i < (int)(sizeof(smnPairs)/sizeof(smnPairs[0])); i++) {
        ULONG idxVal = 0, dataVal = 0;
        BOOL idxOk = GpuReadReg(smnPairs[i].idx, &idxVal);
        BOOL dataOk = GpuReadReg(smnPairs[i].data, &dataVal);
        printf("  %-20s: idx=0x%08X data=0x%08X\n",
            smnPairs[i].name, idxOk ? idxVal : 0xDEAD, dataOk ? dataVal : 0xDEAD);
    }

    /* MP1 range — try more offsets */
    printf("\n=== MP1 Range Extended Scan ===\n");
    /* Scan 0x16000-0x16FFF in steps of 0x100 */
    for (ULONG off = 0x16000; off <= 0x16F00; off += 0x100) {
        if (GpuReadReg(off, &val) && val != 0x00000000 && val != 0xFFFFFFFF) {
            printf("  MP1+0x%X = 0x%08X\n", off - 0x16000, val);
        }
    }
    /* Also check specific known offsets */
    ULONG mp1Check[] = {
        0x16000, 0x16004, 0x16008, 0x1600C,
        0x16058, 0x1605C, 0x16060, 0x16064,
        0x160E4, 0x160E8,
        0x16104, 0x16148, 0x16168,
        0x16A08, 0x16A48, 0x16A68,
        0x16B00, 0x16B04, 0x16B08,
    };
    for (int i = 0; i < (int)(sizeof(mp1Check)/sizeof(mp1Check[0])); i++) {
        if (GpuReadReg(mp1Check[i], &val) && val != 0x00000000) {
            printf("  [0x%05X] = 0x%08X\n", mp1Check[i], val);
        }
    }

    /* Try writing to SMU C2PMSG_66 and see if it echoes back */
    printf("\n=== SMU Write Test ===\n");
    GpuReadReg(0x16A08, &val);
    printf("  C2PMSG_66 before: 0x%08X\n", val);
    GpuWriteReg(0x16A08, 0xDEADBEEF);
    Sleep(10);
    GpuReadReg(0x16A08, &val);
    printf("  C2PMSG_66 after write 0xDEADBEEF: 0x%08X\n", val);

    /* Try PSP mailbox via GPU proxy to verify proxy works */
    printf("\n=== PSP Mailbox via GPU Proxy ===\n");
    GpuReadReg(0x160E4, &val);
    printf("  PSP_C2PMSG_81 (0x160E4): 0x%08X\n", val);
    /* Read from known PSP BAR0 range */
    GpuReadReg(0x16058, &val);
    printf("  PSP_C2PMSG_35 (0x16058): 0x%08X\n", val);

    CloseHandle(g_hGpu);
    printf("\nDone.\n");
    return 0;
}
