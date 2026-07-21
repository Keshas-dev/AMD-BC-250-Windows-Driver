#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE h;
static BOOL W32(uint32_t o, uint32_t v) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=v; return DeviceIoControl(h,IOCTL_AMDBC250_WRITE_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL); }
static uint32_t R32(uint32_t o) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=0; if(DeviceIoControl(h,IOCTL_AMDBC250_READ_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL)) return r.Value; return 0xFFFFFFFF; }
static uint32_t SmnR(uint32_t a) { W32(0x38,a); R32(0x38); return R32(0x3C); }
static BOOL SmnW(uint32_t a,uint32_t v) { if(!W32(0x38,a)) return FALSE; return W32(0x3C,v); }

#define Q0_C 0x03B10A08
#define Q0_A 0x03B10A48
#define Q0_R 0x03B10A68
#define Q3_C 0x03B10A20
#define Q3_A 0x03B10A88
#define Q3_R 0x03B10A80
#define Q2_C 0x03B10528
#define Q2_A 0x03B10998
#define Q2_R 0x03B10564
#define Q1_C 0x03B10A00
#define Q1_A 0x03B10A40
#define Q1_R 0x03B10A60
#define Q4_C 0x03B10A24
#define Q4_A 0x03B10A8C
#define Q4_R 0x03B10A84

static int SmuMsg(uint32_t ca, uint32_t aa, uint32_t ra, uint16_t m, uint32_t a, const char* lbl) {
    uint32_t rsp = SmnR(ra);
    if (rsp == 1) { SmnW(ra, 0); } // ack previous response
    Sleep(5);
    SmnW(aa, a);                    // set argument
    SmnW(ca, m);                    // write cmd (triggers SMU)
    // poll for response with 100ms timeout
    for (int i = 0; i < 100; i++) {
        rsp = SmnR(ra);
        if (rsp == 1) {
            uint32_t ret = SmnR(aa);
            printf("  %-28s msg=0x%04X arg=0x%08X -> 0x%08X\n", lbl, m, a, ret);
            return 1;
        }
        Sleep(1);
    }
    printf("  %-28s msg=0x%04X arg=0x%08X -> TIMEOUT (rsp=0x%08X)\n", lbl, m, a, SmnR(ra));
    return 0;
}

int main() {
    h = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { printf("FAIL: Open device\n"); return 1; }
    AMDBC250_IOCTL_INIT_HARDWARE ih = {0}; ih.MmioPhysicalBase = 0xFE800000ULL; ih.MmioSize = 0x80000; ih.Flags = 1; DWORD b;
    if (!DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &b, NULL)) {
        printf("FAIL: INIT_HARDWARE err=%lu\n", GetLastError()); CloseHandle(h); return 1;
    }
    printf("INIT_HARDWARE OK\n\n");

    // --- Queue 0 (confirmed working) ---
    printf("=== Q0: Standard (bc250-collective confirmed) ===\n");
    SmuMsg(Q0_C, Q0_A, Q0_R, 0x01, 42,  "TestMessage(42)");
    SmuMsg(Q0_C, Q0_A, Q0_R, 0x02, 0,   "GetSmuVersion");
    SmuMsg(Q0_C, Q0_A, Q0_R, 0x03, 0,   "GetDriverIfVersion");
    SmuMsg(Q0_C, Q0_A, Q0_R, 0x0C, 0,   "QueryCorePstate(core0)");
    SmuMsg(Q0_C, Q0_A, Q0_R, 0x0F, 0,   "QueryGfxclk");
    SmuMsg(Q0_C, Q0_A, Q0_R, 0x11, 0,   "QueryVddcrSocClock(i0)");
    SmuMsg(Q0_C, Q0_A, Q0_R, 0x11, 0x10000, "QueryVddcrSocClock(i1)");
    SmuMsg(Q0_C, Q0_A, Q0_R, 0x13, 0,   "QueryDfPstate");
    SmuMsg(Q0_C, Q0_A, Q0_R, 0x1E, 0,   "QueryActiveWgp");
    SmuMsg(Q0_C, Q0_A, Q0_R, 0x37, 0,   "GetGfxFrequency");
    SmuMsg(Q0_C, Q0_A, Q0_R, 0x38, 0,   "GetGfxVid");
    SmuMsg(Q0_C, Q0_A, Q0_R, 0x3D, 0,   "GetEnabledSmuFeatures");

    printf("\n=== Q0: Undocumented scan (0x04-0x0B, 0x10, 0x12, 0x14-0x1D, 0x20-0x36, 0x3E) ===\n");
    uint16_t msgs[] = {0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x10,0x12,0x14,0x15,0x16,0x17,0x19,0x1A,0x1C,0x1D,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2A,0x2B,0x2C,0x2D,0x2E,0x2F,0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x3E};
    for (int i = 0; i < sizeof(msgs)/sizeof(msgs[0]); i++) {
        char lbl[32]; sprintf_s(lbl, sizeof(lbl), "Q0:0x%04X", msgs[i]);
        SmuMsg(Q0_C, Q0_A, Q0_R, msgs[i], 0, lbl);
    }

    // --- Queue 3 (temperature/perf profiles, bc250-colective confirmed) ---
    printf("\n=== Q3: Temperature/Perf ===\n");
    SmuMsg(Q3_C, Q3_A, Q3_R, 0x01, 42,  "Q3 TestMessage(42)");
    SmuMsg(Q3_C, Q3_A, Q3_R, 0x36, 0,   "GetCurrentCpuVoltage");
    SmuMsg(Q3_C, Q3_A, Q3_R, 0x37, 0,   "GetCurrentGpuVoltage");
    SmuMsg(Q3_C, Q3_A, Q3_R, 0x40, 0,   "GetCpuTempMax");
    SmuMsg(Q3_C, Q3_A, Q3_R, 0x8C, 80,  "SetGpuMaxTemp(80)");
    SmuMsg(Q3_C, Q3_A, Q3_R, 0x8B, 80,  "SetCpuMaxTemp(80)");
    SmuMsg(Q3_C, Q3_A, Q3_R, 0x1E, 1,   "SetPerfProfile(1)");
    SmuMsg(Q3_C, Q3_A, Q3_R, 0x1E, 3,   "SetPerfProfile(3)");
    SmuMsg(Q3_C, Q3_A, Q3_R, 0x25, 0,   "SetOcClk(0,0)");

    printf("\n=== Q3: Undocumented scan (0x02-0x0E, 0x10-0x1D, 0x1F-0x24, 0x26-0x35, 0x38-0x3F) ===\n");
    uint16_t msgs3[] = {0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1F,0x20,0x21,0x22,0x23,0x24,0x26,0x27,0x28,0x29,0x2A,0x2B,0x2C,0x2D,0x2E,0x2F,0x30,0x31,0x32,0x33,0x34,0x35,0x38,0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F};
    for (int i = 0; i < sizeof(msgs3)/sizeof(msgs3[0]); i++) {
        char lbl[32]; sprintf_s(lbl, sizeof(lbl), "Q3:0x%04X", msgs3[i]);
        SmuMsg(Q3_C, Q3_A, Q3_R, msgs3[i], 0, lbl);
    }

    // --- Queue 2 (features enable/disable, bc250-colective confirmed) ---
    printf("\n=== Q2: Device info/Features ===\n");
    SmuMsg(Q2_C, Q2_A, Q2_R, 0x03, 0,   "GetConstant");
    SmuMsg(Q2_C, Q2_A, Q2_R, 0x04, 0,   "GetDeviceName(0)");
    SmuMsg(Q2_C, Q2_A, Q2_R, 0x04, 1,   "GetDeviceName(1)");
    SmuMsg(Q2_C, Q2_A, Q2_R, 0x04, 2,   "GetDeviceName(2)");
    SmuMsg(Q2_C, Q2_A, Q2_R, 0x04, 3,   "GetDeviceName(3)");
    SmuMsg(Q2_C, Q2_A, Q2_R, 0x17, 0,   "CpuDroopCalibration");

    printf("\n=== Q2: Undocumented scan (0x00-0x02, 0x05-0x16, 0x18-0x3F) ===\n");
    uint16_t msgs2[] = {0x00,0x01,0x02,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2A,0x2B,0x2C,0x2D,0x2E,0x2F,0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F};
    for (int i = 0; i < sizeof(msgs2)/sizeof(msgs2[0]); i++) {
        char lbl[32]; sprintf_s(lbl, sizeof(lbl), "Q2:0x%04X", msgs2[i]);
        SmuMsg(Q2_C, Q2_A, Q2_R, msgs2[i], 0, lbl);
    }

    // --- Quick Queue 1 + 4 scan (small range) ---
    printf("\n=== Q1: Unknown (0x01-0x08) ===\n");
    uint16_t msgs1[] = {0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08};
    for (int i = 0; i < sizeof(msgs1)/sizeof(msgs1[0]); i++) {
        char lbl[32]; sprintf_s(lbl, sizeof(lbl), "Q1:0x%04X", msgs1[i]);
        SmuMsg(Q1_C, Q1_A, Q1_R, msgs1[i], 0, lbl);
    }

    printf("\n=== Q4: Unknown (0x01-0x08) ===\n");
    uint16_t msgs4[] = {0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08};
    for (int i = 0; i < sizeof(msgs4)/sizeof(msgs4[0]); i++) {
        char lbl[32]; sprintf_s(lbl, sizeof(lbl), "Q4:0x%04X", msgs4[i]);
        SmuMsg(Q4_C, Q4_A, Q4_R, msgs4[i], 0, lbl);
    }

    printf("\n=== Final register state ===\n");
    printf("  Q0_RSP=0x%08X Q0_ARG=0x%08X Q0_CMD=0x%08X\n", SmnR(Q0_R), SmnR(Q0_A), SmnR(Q0_C));
    printf("  Q3_RSP=0x%08X Q2_RSP=0x%08X\n", SmnR(Q3_R), SmnR(Q2_R));

    CloseHandle(h);
    return 0;
}
