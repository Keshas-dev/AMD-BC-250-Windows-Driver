#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>

#define AMDBC250_DEVICE_PATH L"\\\\.\\AMDBC250DreamV43"
#define IOCTL_AMDBC250_READ_REG   ((ULONG)0x80000B88)
#define IOCTL_AMDBC250_WRITE_REG  ((ULONG)0x80000B8C)
#define IOCTL_AMDBC250_INIT_HARDWARE ((ULONG)0x80000B80)

typedef struct { ULONG Offset, Value, Status; } REG_IOCTL;
typedef struct { ULONG64 MmioPhysicalBase; ULONG MmioSize, Flags; ULONG64 FbPhysicalBase; ULONG FbSize; } INIT_HW;

static HANDLE g_h;
static int OpenGpu(void) {
    g_h = CreateFileW(AMDBC250_DEVICE_PATH, GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    return g_h != INVALID_HANDLE_VALUE ? 0 : -1;
}
static ULONG ReadReg(ULONG offset) {
    REG_IOCTL req = {offset,0,0}; DWORD ret;
    DeviceIoControl(g_h, IOCTL_AMDBC250_READ_REG, &req,sizeof(req), &req,sizeof(req), &ret, NULL);
    return req.Value;
}
static int WriteReg(ULONG offset, ULONG value) {
    REG_IOCTL req = {offset,value,0}; DWORD ret;
    return DeviceIoControl(g_h, IOCTL_AMDBC250_WRITE_REG, &req,sizeof(req), &req,sizeof(req), &ret, NULL) ? 0 : -1;
}
static int InitHw(void) {
    INIT_HW ih = {0}; ih.MmioPhysicalBase = 0xFE800000ULL; ih.MmioSize = 0x80000; ih.Flags = 1; DWORD ret;
    return DeviceIoControl(g_h, IOCTL_AMDBC250_INIT_HARDWARE, &ih,sizeof(ih), &ih,sizeof(ih), &ret, NULL) ? 0 : -1;
}

int main(void) {
    if (OpenGpu() != 0) { printf("Cannot open GPU\n"); return 1; }
    if (InitHw() != 0) { printf("InitHw FAILED err=%lu\n", GetLastError()); return 1; }

    printf("=== RLC Follow-up: correct offsets at 0x4Cxx range ===\n\n");

    /* Check SMU features after Queue 2 DisableSmuFeatures */
    printf("--- SMU Status After Q2 DisableSmuFeatures ---\n");
    /* Use SMN to read SMU FW_FLAGS */
    WriteReg(0x38, 0x03B10024); MemoryBarrier();
    ULONG fwFlags = ReadReg(0x3C);
    WriteReg(0x38, 0x03B10528); MemoryBarrier();
    ULONG q2Status = ReadReg(0x3C);
    printf("SMU FW_FLAGS (SMN 0x3B10024) = 0x%08X\n", fwFlags);
    printf("Q2 CMD reg (SMN 0x3B10528)   = 0x%08X\n", q2Status);

    /* RLC registers at Linux-correct offsets (mm * 4 + GC_BASE for BIDX=0) */
    printf("\n--- RLC Registers at 0x14xxx range (GC_BASE + mm*4, BIDX=0) ---\n");
    struct { const char *name; ULONG mm; ULONG addr; } rlcTab[] = {
        {"RLC_CNTL",            0x4C00, 0x1260 + 0x4C00*4},
        {"RLC_CNTL+1",          0x4C01, 0x1260 + 0x4C01*4},
        {"RLC_CNTL+2",          0x4C02, 0x1260 + 0x4C02*4},
        {"RLC_CNTL+3",          0x4C03, 0x1260 + 0x4C03*4},
        {"RLC_CNTL+4",          0x4C04, 0x1260 + 0x4C04*4},
        {"RLC_CNTL+5",          0x4C05, 0x1260 + 0x4C05*4},
        {"RLC_PG_CNTL",         0x4C43, 0x1260 + 0x4C43*4},
        {"RLC_CGCG_CGLS",       0x4C49, 0x1260 + 0x4C49*4}, /* BIDX=1 but try BIDX=0 anyway */
        {"RLC_SRM_CNTL",        0x4C80, 0x1260 + 0x4C80*4},
        {"RLC_CSIB_ADDR_LO",    0x4CA2, 0x1260 + 0x4CA2*4},
        {"RLC_CSIB_ADDR_HI",    0x4CA3, 0x1260 + 0x4CA3*4},
        {"RLC_CSIB_LENGTH",     0x4CA4, 0x1260 + 0x4CA4*4},
        {"RLC_SAFE_MODE",       0x4CA0, 0x1260 + 0x4CA0*4},
        {"RLC_CP_SCHEDULERS",   0x4CA1, 0x1260 + 0x4CA1*4},
        {"RLC_SPARE_INT",       0x4CA5, 0x1260 + 0x4CA5*4},
    };
    for (int i = 0; i < sizeof(rlcTab)/sizeof(rlcTab[0]); i++) {
        ULONG v = ReadReg(rlcTab[i].addr);
        printf("%-20s mm=0x%04X addr=0x%05X = 0x%08X\n",
            rlcTab[i].name, rlcTab[i].mm, rlcTab[i].addr, v);
    }

    /* Also try BIDX=1 range (GC_BASE + 0xA000 + mm*4) */
    printf("\n--- RLC Registers with BIDX=1 (GC_BASE+0xA000 + mm*4) ---\n");
    for (int i = 0; i < sizeof(rlcTab)/sizeof(rlcTab[0]); i++) {
        ULONG addr2 = 0x1260 + 0xA000 + rlcTab[i].mm * 4;
        ULONG v = ReadReg(addr2);
        printf("%-20s addr=0x%05X = 0x%08X\n", rlcTab[i].name, addr2, v);
    }

    /* Scan 0x14000-0x15000 for any non-zero/non-FF registers */
    printf("\n--- Scan 0x14000-0x15000 for live registers ---\n");
    int found = 0;
    for (ULONG off = 0x14000; off <= 0x15000; off += 4) {
        ULONG v = ReadReg(off);
        if (v != 0 && v != 0xFFFFFFFF) {
            if (found < 30) printf("  0x%05X = 0x%08X\n", off, v);
            found++;
        }
    }
    printf("Total non-zero/non-FF registers in range: %d\n", found);

    /* Also check 0x1D000-0x1E000 for BIDX=1 RLC_CGCG */
    printf("\n--- Scan 0x1D000-0x1E000 (BIDX=1 region) ---\n");
    found = 0;
    for (ULONG off = 0x1D000; off <= 0x1E000; off += 4) {
        ULONG v = ReadReg(off);
        if (v != 0 && v != 0xFFFFFFFF) {
            if (found < 20) printf("  0x%05X = 0x%08X\n", off, v);
            found++;
        }
    }
    printf("Total live registers: %d\n", found);

    printf("\nGRBM_STATUS = 0x%08X\n", ReadReg(0x3260));

    CloseHandle(g_h);
    return 0;
}
