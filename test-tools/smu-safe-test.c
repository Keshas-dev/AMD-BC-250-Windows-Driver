#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE h;
static BOOL W32(uint32_t o, uint32_t v) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=v; return DeviceIoControl(h,IOCTL_AMDBC250_WRITE_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL); }
static uint32_t R32(uint32_t o) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=0; if(DeviceIoControl(h,IOCTL_AMDBC250_READ_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL)) return r.Value; return 0xFFFFFFFF; }
static uint32_t SR(uint32_t a) { W32(0x38,a); R32(0x38); return R32(0x3C); }
static BOOL SW(uint32_t a,uint32_t v) { return W32(0x38,a) && W32(0x3C,v); }

#define Q0_C 0x03B10A08  // SMN C2PMSG_66 (message)
#define Q0_A 0x03B10A48  // SMN C2PMSG_82 (arg/response)
#define Q0_R 0x03B10A68  // SMN C2PMSG_90 (ctrl: 0=busy, 1=OK, FF=err)
#define Q3_C 0x03B10A20  // Q3 cmd
#define Q3_A 0x03B10A88  // Q3 arg
#define Q3_R 0x03B10A80  // Q3 rsp
#define Q2_C 0x03B10528  // Q2 cmd
#define Q2_A 0x03B10998  // Q2 arg
#define Q2_R 0x03B10564  // Q2 rsp

static int WaitR(uint32_t ra, uint32_t exp, int ms) {
    for (int i = 0; i < ms; i++) { if (SR(ra) == exp) return 1; Sleep(1); }
    return 0;
}

static int QMsg(uint32_t ca, uint32_t aa, uint32_t ra, uint16_t msg, uint32_t arg, const char* name) {
    uint32_t rsp = SR(ra);
    if (rsp == 1) { SW(ra, 0); Sleep(5); }
    SW(aa, arg);
    SW(ca, msg);
    if (!WaitR(ra, 1, 100)) {
        printf("  %-30s: TIMEOUT (rsp=0x%08X)\n", name, SR(ra));
        return 0;
    }
    uint32_t val = SR(aa);
    printf("  %-30s: 0x%08X", name, val);
    return 1;
}

int main() {
    h = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { printf("FAIL: Open device\n"); return 1; }
    AMDBC250_IOCTL_INIT_HARDWARE ih = {0}; ih.MmioPhysicalBase = 0xFE800000ULL; ih.MmioSize = 0x80000; ih.Flags = 1; DWORD b;
    if (!DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &b, NULL)) {
        printf("FAIL: INIT_HARDWARE err=%lu\n", GetLastError()); CloseHandle(h); return 1;
    }
    printf("INIT_HARDWARE OK\n\n");

    printf("=== SMU alive check + safe messages ===\n");
    QMsg(Q0_C, Q0_A, Q0_R, 0x01, 42,  "TestMessage(42)"); printf("\n");
    QMsg(Q0_C, Q0_A, Q0_R, 0x02, 0,   "GetSmuVersion"); printf("\n");
    QMsg(Q0_C, Q0_A, Q0_R, 0x03, 0,   "GetDriverIfVersion"); printf("\n");
    QMsg(Q0_C, Q0_A, Q0_R, 0x37, 0,   "GetGfxFrequency"); printf("\n");
    QMsg(Q0_C, Q0_A, Q0_R, 0x38, 0,   "GetGfxVid"); printf("\n");
    QMsg(Q0_C, Q0_A, Q0_R, 0x3D, 0,   "GetEnabledSmuFeatures"); printf("\n");
    QMsg(Q0_C, Q0_A, Q0_R, 0x1E, 0,   "QueryActiveWgp"); printf("\n");
    QMsg(Q0_C, Q0_A, Q0_R, 0x0F, 0,   "QueryGfxclk"); printf("\n");
    QMsg(Q0_C, Q0_A, Q0_R, 0x13, 0,   "QueryDfPstate"); printf("\n");
    QMsg(Q0_C, Q0_A, Q0_R, 0x0C, 0,   "QueryCorePstate(0)"); printf("\n");
    QMsg(Q0_C, Q0_A, Q0_R, 0x11, 0,   "GetSocClock(idx0)"); printf("\n");
    QMsg(Q0_C, Q0_A, Q0_R, 0x11, 0x10000, "GetSocClock(idx1)"); printf("\n");

    // Q3 temperature/voltage
    printf("\n=== Q3: Temperature & Voltage ===\n");
    QMsg(Q3_C, Q3_A, Q3_R, 0x01, 42,  "Q3 TestMessage(42)"); printf("\n");
    QMsg(Q3_C, Q3_A, Q3_R, 0x36, 0,   "GetCurrentCpuVoltage"); printf("\n");
    QMsg(Q3_C, Q3_A, Q3_R, 0x37, 0,   "GetCurrentGpuVoltage"); printf("\n");
    QMsg(Q3_C, Q3_A, Q3_R, 0x40, 0,   "GetCpuTempMax"); printf("\n");

    // Q2 device info
    printf("\n=== Q2: Device info ===\n");
    QMsg(Q2_C, Q2_A, Q2_R, 0x03, 0,   "GetConstant"); printf("\n");
    QMsg(Q2_C, Q2_A, Q2_R, 0x04, 0,   "GetDeviceName(0)"); printf("\n");
    QMsg(Q2_C, Q2_A, Q2_R, 0x04, 1,   "GetDeviceName(1)"); printf("\n");
    QMsg(Q2_C, Q2_A, Q2_R, 0x04, 2,   "GetDeviceName(2)"); printf("\n");
    QMsg(Q2_C, Q2_A, Q2_R, 0x04, 3,   "GetDeviceName(3)"); printf("\n");

    // Restore safe state
    printf("\n=== Restore ===\n");
    QMsg(Q0_C, Q0_A, Q0_R, 0x3C, 0, "UnforceGfxVid"); printf("\n");
    QMsg(Q0_C, Q0_A, Q0_R, 0x3A, 0, "UnforceGfxFreq"); printf("\n");

    printf("\n=== Final ===\n");
    printf("  GFX freq: %u MHz\n", SR(Q0_A)); // cached from GetGfxFrequency
    printf("  Q0_RSP: 0x%08X\n", SR(Q0_R));
    printf("  Q3_RSP: 0x%08X\n", SR(Q3_R));
    printf("  Q2_RSP: 0x%08X\n", SR(Q2_R));

    CloseHandle(h);
    return 0;
}
