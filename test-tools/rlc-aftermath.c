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

static int InitHw(void) {
    INIT_HW ih = {0}; ih.MmioPhysicalBase = 0xFE800000ULL; ih.MmioSize = 0x80000; ih.Flags = 1; DWORD ret;
    return DeviceIoControl(g_h, IOCTL_AMDBC250_INIT_HARDWARE, &ih,sizeof(ih), &ih,sizeof(ih), &ret, NULL) ? 0 : -1;
}

typedef struct { const char *name; ULONG addr; } REG;

int main(void) {
    if (OpenGpu() != 0) { printf("ERROR: Cannot open GPU driver\n"); return 1; }
    if (InitHw() != 0) { printf("INIT_HW FAILED err=%lu\n", GetLastError()); return 1; }

    printf("=== GFX Engine Status After RLC Firmware ===\n\n");

    REG critical[] = {
        {"GRBM_STATUS",        0x3260},
        {"GRBM_GFX_INDEX",     0x34D0},
        {"GRBM_GFX_CNTL",      0x2022},
        {"ME_CNTL",            0x4A74},
        {"CP_RB0_BASE_LO",     0x89E0},
        {"CP_RB0_BASE_HI",     0x89E4},
        {"CP_RB0_CNTL",        0x89E8},
        {"CP_RB0_WPTR",        0x8A30},
        {"CP_RB0_RPTR",        0x8A34},
        {"SCRATCH",            0x32D4},
        {"CC_ARRAY_CONFIG",    0x9C1C},
        {"SPI_PG_ENABLE_STATIC_WGP_MASK", 0x5C3C},
        {"RLC_PG_ALWAYS_ON_WGP_MASK", 0x3D64},
        {"DISPATCH_INITIATOR", 0x80E0},
        {"PGM_LO",             0x8110},
        {"PGM_HI",             0x8114},
        {"CP_MQD_BASE_ADDR",   0x9104},
        {"CP_HQD_ACTIVE",      0x910C},
        {"C2PMSG_81",          0x10614},
    };
    for (int i = 0; i < sizeof(critical)/sizeof(critical[0]); i++) {
        ULONG v = ReadReg(critical[i].addr);
        printf("%-35s 0x%05X = 0x%08X\n", critical[i].name, critical[i].addr, v);
    }

    printf("\n=== Ring Register Block (0xDA60-0xDA80) ===\n");
    for (ULONG off = 0xDA60; off <= 0xDA80; off += 4) {
        printf("0x%05X = 0x%08X\n", off, ReadReg(off));
    }

    printf("\n=== COMPUTE Block (0x80E0-0x8140) ===\n");
    ULONG comp[] = {0x80E0,0x80E4,0x80E8,0x80EC,0x80F0,0x80F4,0x80F8,0x80FC,0x8100,0x8104,0x8110,0x8114,0x8128,0x812C,0x8138,0x8140,0x81E0};
    for (int i = 0; i < sizeof(comp)/sizeof(comp[0]); i++)
        printf("0x%05X = 0x%08X\n", comp[i], ReadReg(comp[i]));

    CloseHandle(g_h);
    return 0;
}
