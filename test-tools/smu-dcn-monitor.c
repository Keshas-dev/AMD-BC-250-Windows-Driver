#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE h;
static BOOL W32(uint32_t o, uint32_t v) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=v; return DeviceIoControl(h,IOCTL_AMDBC250_WRITE_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL); }
static uint32_t R32(uint32_t o) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=0; if(DeviceIoControl(h,IOCTL_AMDBC250_READ_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL)) return r.Value; return 0xFFFFFFFF; }
static uint32_t SR(uint32_t a) { W32(0x38,a); R32(0x38); return R32(0x3C); }
static BOOL SW(uint32_t a,uint32_t v) { return W32(0x38,a) && W32(0x3C,v); }

#define Q0_C 0x03B10A08
#define Q0_A 0x03B10A48
#define Q0_R 0x03B10A68
#define Q3_C 0x03B10A20
#define Q3_A 0x03B10A88
#define Q3_R 0x03B10A80

static uint32_t QMsg(uint32_t ca, uint32_t aa, uint32_t ra, uint16_t msg, uint32_t arg, const char* name) {
    uint32_t rsp = SR(ra);
    if (rsp == 1) { SW(ra, 0); Sleep(5); }
    SW(aa, arg);
    SW(ca, msg);
    Sleep(50);
    uint32_t r = SR(ra);
    uint32_t val = (r == 1) ? SR(aa) : 0xFFFFFFFF;
    if (r == 1) printf("  %-30s: 0x%08X (%u)\n", name, val, val);
    else printf("  %-30s: TIMEOUT (rsp=0x%08X)\n", name, r);
    return r;
}

int main() {
    h = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { printf("FAIL: Open device\n"); return 1; }
    AMDBC250_IOCTL_INIT_HARDWARE ih = {0}; ih.MmioPhysicalBase = 0xFE800000ULL; ih.MmioSize = 0x80000; ih.Flags = 1; DWORD b;
    if (!DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &b, NULL)) {
        printf("FAIL: INIT_HARDWARE err=%lu\n", GetLastError()); CloseHandle(h); return 1;
    }
    printf("INIT_HARDWARE OK\n\n");

    printf("=== DCN Temperature & Pipe Status ===\n");
    uint32_t temp = R32(0x8008);
    printf("  DCN CURRENT_TEMP (0x8008): 0x%08X (%u)\n", temp, temp);
    for (int p = 0; p < 4; p++) {
        uint32_t otg = R32(0x6000 + p*0x100);
        uint32_t cnt = R32(0x6300 + p*0x100);
        printf("  Pipe %d: OTG=0x%08X CNT=0x%08X%s\n", p, otg, cnt, (otg&1) ? " (ACTIVE)" : "");
    }

    printf("\n=== SMU Status ===\n");
    uint32_t freq = 0, vid = 0, features = 0, wgp = 0, gfxclk = 0, df = 0, cpu_v = 0, gpu_v = 0, maxt = 0;
    uint32_t r0 = SR(Q0_R);
    if (r0 == 1) { SW(Q0_R, 0); Sleep(5); }
    SW(Q0_A, 0); SW(Q0_C, 0x37); Sleep(50); if (SR(Q0_R)==1) freq = SR(Q0_A);
    if (r0 == 1) { SW(Q0_R, 0); Sleep(5); }
    SW(Q0_A, 0); SW(Q0_C, 0x38); Sleep(50); if (SR(Q0_R)==1) vid = SR(Q0_A);
    if (r0 == 1) { SW(Q0_R, 0); Sleep(5); }
    SW(Q0_A, 0); SW(Q0_C, 0x3D); Sleep(50); if (SR(Q0_R)==1) features = SR(Q0_A);
    if (r0 == 1) { SW(Q0_R, 0); Sleep(5); }
    SW(Q0_A, 0); SW(Q0_C, 0x1E); Sleep(50); if (SR(Q0_R)==1) wgp = SR(Q0_A);
    if (r0 == 1) { SW(Q0_R, 0); Sleep(5); }
    SW(Q0_A, 0); SW(Q0_C, 0x0F); Sleep(50); if (SR(Q0_R)==1) gfxclk = SR(Q0_A);
    if (r0 == 1) { SW(Q0_R, 0); Sleep(5); }
    SW(Q0_A, 0); SW(Q0_C, 0x13); Sleep(50); if (SR(Q0_R)==1) df = SR(Q0_A);

    uint32_t r3 = SR(Q3_R);
    if (r3 == 1) { SW(Q3_R, 0); Sleep(5); }
    SW(Q3_A, 0); SW(Q3_C, 0x36); Sleep(50); if (SR(Q3_R)==1) cpu_v = SR(Q3_A);
    if (r3 == 1) { SW(Q3_R, 0); Sleep(5); }
    SW(Q3_A, 0); SW(Q3_C, 0x37); Sleep(50); if (SR(Q3_R)==1) gpu_v = SR(Q3_A);
    if (r3 == 1) { SW(Q3_R, 0); Sleep(5); }
    SW(Q3_A, 0); SW(Q3_C, 0x40); Sleep(50); if (SR(Q3_R)==1) maxt = SR(Q3_A);

    printf("  GPU Frequency: %u MHz\n", freq);
    printf("  GPU VID: 0x%02X (%u)\n", vid, vid);
    printf("  GPU Voltage (calc): %u mV\n", (uint32_t)((-vid*0.00625 + 1.55)*1000));
    printf("  GFXCLK: %u MHz\n", gfxclk);
    printf("  DF PState: %u\n", df);
    printf("  Active WGP: %u\n", wgp);
    printf("  Features: 0x%08X\n", features);
    printf("  GFXOFF: %s\n", (features & (1<<2)) ? "ENABLED" : "DISABLED");
    printf("  CG: %s\n", (features & (1<<3)) ? "ENABLED" : "DISABLED");
    printf("  PG: %s\n", (features & (1<<4)) ? "ENABLED" : "DISABLED");
    printf("  CPU Voltage: %u mV\n", cpu_v);
    printf("  GPU Voltage (SMU): %u mV\n", gpu_v);
    printf("  CPU Temp Max: %u C\n", maxt);

    printf("\n=== Final Status ===\n");
    printf("  Q0_RSP: 0x%08X\n", SR(Q0_R));
    printf("  Q3_RSP: 0x%08X\n", SR(Q3_R));

    CloseHandle(h);
    return 0;
}
