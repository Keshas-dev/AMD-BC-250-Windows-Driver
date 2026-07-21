#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE h;
static BOOL W32(uint32_t o, uint32_t v) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=v; return DeviceIoControl(h,IOCTL_AMDBC250_WRITE_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL); }
static uint32_t R32(uint32_t o) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=0; if(DeviceIoControl(h,IOCTL_AMDBC250_READ_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL)) return r.Value; return 0xFFFFFFFF; }
static uint32_t SR(uint32_t a) { W32(0x38,a); R32(0x38); return R32(0x3C); }
static BOOL SW(uint32_t a,uint32_t v) { return W32(0x38,a) && W32(0x3C,v); }

static int WR(uint32_t ra, int ms) {
    for (int i = 0; i < ms; i++) { if (SR(ra) == 1) return 1; Sleep(1); }
    return 0;
}

// Q0: GPU control
static uint32_t Q0(uint16_t m, uint32_t a) { uint32_t r=SR(0x03B10A68); if(r==1){SW(0x03B10A68,0);Sleep(5);} SW(0x03B10A48,a); SW(0x03B10A08,m); if(!WR(0x03B10A68,500))return 0xFFFFFFFF; return SR(0x03B10A48); }
// Q2: SMU features
static uint32_t Q2(uint16_t m, uint32_t a) { uint32_t r=SR(0x03B10564); if(r==1){SW(0x03B10564,0);Sleep(5);} SW(0x03B10998,a); SW(0x03B10528,m); if(!WR(0x03B10564,500))return 0xFFFFFFFF; return SR(0x03B10998); }
// Q3: Temperature/perf
static uint32_t Q3(uint16_t m, uint32_t a) { uint32_t r=SR(0x03B10A80); if(r==1){SW(0x03B10A80,0);Sleep(5);} SW(0x03B10A88,a); SW(0x03B10A20,m); if(!WR(0x03B10A80,500))return 0xFFFFFFFF; return SR(0x03B10A88); }

static void TestQ(const char* qname, uint32_t ca, uint32_t aa, uint32_t ra, uint16_t msg, uint32_t arg, const char* desc) {
    uint32_t rsp = SR(ra);
    if (rsp == 1) { SW(ra, 0); Sleep(5); }
    SW(aa, arg);
    SW(ca, msg);
    if (WR(ra, 200)) {
        uint32_t v = SR(aa);
        printf("  %-25s msg=0x%04X arg=0x%08X -> 0x%08X", desc, msg, arg, v);
        if (v < 0x10000) printf(" (%u)", v);
        printf("\n");
    } else {
        uint32_t vs = SR(aa), vr = SR(ra);
        printf("  %-25s msg=0x%04X arg=0x%08X -> TIMEOUT (rsp=0x%08X arg=0x%08X)\n", desc, msg, arg, vr, vs);
    }
}

int main() {
    h = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { printf("FAIL: Open device\n"); return 1; }
    AMDBC250_IOCTL_INIT_HARDWARE ih = {0}; ih.MmioPhysicalBase = 0xFE800000ULL; ih.MmioSize = 0x80000; ih.Flags = 1; DWORD b;
    if (!DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &b, NULL)) {
        printf("FAIL: INIT_HARDWARE err=%lu\n", GetLastError()); CloseHandle(h); return 1;
    }
    printf("INIT_HARDWARE OK\n\n");

    // Q0: Scan higher message IDs (0x40-0xFF) that we haven't tested
    printf("=== Q0: Extended scan (0x40-0x5F) ===\n");
    for (uint16_t m = 0x40; m <= 0x5F; m++) {
        char lbl[32]; sprintf_s(lbl, sizeof(lbl), "Q0:0x%04X", m);
        TestQ(lbl, 0x03B10A08, 0x03B10A48, 0x03B10A68, m, 0, lbl);
    }

    // Q0: 0x80-0x9F (LOAD_TOC and friends)
    printf("\n=== Q0: Extended scan (0x80-0x9F) ===\n");
    for (uint16_t m = 0x80; m <= 0x9F; m++) {
        char lbl[32]; sprintf_s(lbl, sizeof(lbl), "Q0:0x%04X", m);
        TestQ(lbl, 0x03B10A08, 0x03B10A48, 0x03B10A68, m, 0, lbl);
    }

    // Q3: Extended scan (0x80-0xFF) - these might have temperature/power features
    printf("\n=== Q3: Extended scan (0x80-0x9F) ===\n");
    for (uint16_t m = 0x80; m <= 0x9F; m++) {
        char lbl[32]; sprintf_s(lbl, sizeof(lbl), "Q3:0x%04X", m);
        TestQ(lbl, 0x03B10A20, 0x03B10A88, 0x03B10A80, m, 0, lbl);
    }

    // Q3: 0xA0-0xFF
    printf("\n=== Q3: Extended scan (0xA0-0xBF) ===\n");
    for (uint16_t m = 0xA0; m <= 0xBF; m++) {
        char lbl[32]; sprintf_s(lbl, sizeof(lbl), "Q3:0x%04X", m);
        TestQ(lbl, 0x03B10A20, 0x03B10A88, 0x03B10A80, m, 0, lbl);
    }

    // Q2: Extended scan (0x40-0xFF)
    printf("\n=== Q2: Extended scan (0x40-0x5F) ===\n");
    for (uint16_t m = 0x40; m <= 0x5F; m++) {
        char lbl[32]; sprintf_s(lbl, sizeof(lbl), "Q2:0x%04X", m);
        TestQ(lbl, 0x03B10528, 0x03B10998, 0x03B10564, m, 0, lbl);
    }

    printf("\n=== Known messages with params ===\n");
    // Try temperature-related Q0 messages
    TestQ("Q0 temp?", 0x03B10A08, 0x03B10A48, 0x03B10A68, 0x09, 0, "Q0:0x09");
    TestQ("Q0 temp2?", 0x03B10A08, 0x03B10A48, 0x03B10A68, 0x12, 0, "Q0:0x12");
    TestQ("Q0 temp3?", 0x03B10A08, 0x03B10A48, 0x03B10A68, 0x14, 0, "Q0:0x14");

    // Try telemetry
    TestQ("Telemetry", 0x03B10A08, 0x03B10A48, 0x03B10A68, 0x1B, 1, "Q0 StartTelemetry");
    TestQ("Telemetry2", 0x03B10A08, 0x03B10A48, 0x03B10A68, 0x1C, 0, "Q0 StopTelemetry");

    // Q3 temperature-related
    TestQ("Q3 temp?", 0x03B10A20, 0x03B10A88, 0x03B10A80, 0x0A, 0, "Q3:0x0A");
    TestQ("Q3 temp2?", 0x03B10A20, 0x03B10A88, 0x03B10A80, 0x0B, 0, "Q3:0x0B");
    TestQ("Q3 temp3?", 0x03B10A20, 0x03B10A88, 0x03B10A80, 0x0C, 0, "Q3:0x0C");
    TestQ("Q3 temp4?", 0x03B10A20, 0x03B10A88, 0x03B10A80, 0x35, 0, "Q3:0x35");
    TestQ("Q3 temp5?", 0x03B10A20, 0x03B10A88, 0x03B10A80, 0x38, 0, "Q3:0x38");
    TestQ("Q3 temp6?", 0x03B10A20, 0x03B10A88, 0x03B10A80, 0x39, 0, "Q3:0x39");

    printf("\nDone.\n");
    CloseHandle(h);
    return 0;
}
