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

static uint32_t Q0(uint16_t m, uint32_t a) { uint32_t r=SR(0x03B10A68); if(r==1){SW(0x03B10A68,0);Sleep(5);} SW(0x03B10A48,a); SW(0x03B10A08,m); if(!WR(0x03B10A68,500))return 0xFFFFFFFF; return SR(0x03B10A48); }
static uint32_t Q3(uint16_t m, uint32_t a) { uint32_t r=SR(0x03B10A80); if(r==1){SW(0x03B10A80,0);Sleep(5);} SW(0x03B10A88,a); SW(0x03B10A20,m); if(!WR(0x03B10A80,500))return 0xFFFFFFFF; return SR(0x03B10A88); }

static void TQ0(uint16_t m, uint32_t a, const char* d) {
    uint32_t r=SR(0x03B10A68); if(r==1){SW(0x03B10A68,0);Sleep(5);}
    SW(0x03B10A48,a); SW(0x03B10A08,m);
    int ok=WR(0x03B10A68,500); uint32_t v=SR(0x03B10A48);
    printf("  %-35s msg=0x%04X arg=0x%08X -> %s0x%08X (%u)\n", d, m, a, ok?"":"TIMEOUT ", v, ok?v:0);
}
static void TQ3(uint16_t m, uint32_t a, const char* d) {
    uint32_t r=SR(0x03B10A80); if(r==1){SW(0x03B10A80,0);Sleep(5);}
    SW(0x03B10A88,a); SW(0x03B10A20,m);
    int ok=WR(0x03B10A80,500); uint32_t v=SR(0x03B10A88);
    printf("  %-35s msg=0x%04X arg=0x%08X -> %s0x%08X (%u)\n", d, m, a, ok?"":"TIMEOUT ", v, ok?v:0);
}

int main() {
    h = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { printf("FAIL: Open device\n"); return 1; }
    AMDBC250_IOCTL_INIT_HARDWARE ih = {0}; ih.MmioPhysicalBase = 0xFE800000ULL; ih.MmioSize = 0x80000; ih.Flags = 1; DWORD b;
    if (!DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &b, NULL)) {
        printf("FAIL: INIT_HARDWARE err=%lu\n", GetLastError()); CloseHandle(h); return 1;
    }
    printf("INIT_HARDWARE OK\n\n");

    printf("=== Q0: Telemetry ===\n");
    TQ0(0x1B, 1, "StartTelemetry(1)");
    Sleep(100);
    TQ0(0x1B, 0, "StartTelemetry(0)");
    TQ0(0x1B, 0x10, "StartTelemetry(16)");
    TQ0(0x1B, 0x100, "StartTelemetry(256)");
    TQ0(0x1B, 0x10000, "StartTelemetry(65536)");
    TQ0(0x1C, 0, "StopTelemetry");

    printf("\n=== Q0: Driver table (read SMU metrics) ===\n");
    TQ0(0x06, 0, "TransferTableSmu2Dram");
    TQ0(0x07, 0, "TransferTableDram2Smu");
    TQ0(0x04, 0, "SetDramAddrHigh(0)");
    TQ0(0x05, 0, "SetDramAddrLow(0)");
    TQ0(0x06, 0, "TransferTableSmu2Dram(after)");

    printf("\n=== Q0: Deep sleep ===\n");
    TQ0(0x19, 0, "SetMinDeepSleepGfxclk(0)");
    TQ0(0x19, 5000, "SetMinDeepSleepGfxclk(5000=50MHz)");
    TQ0(0x1A, 1, "SetMaxDeepSleepDiv(1)");
    TQ0(0x19, 0, "Reset deep sleep");

    printf("\n=== Q3: CPU-specific ===\n");
    TQ3(0x0F, 0, "SetCpuGpuVid(0,0)");
    TQ3(0x0F, 0x10000, "SetCpuGpuVid(cpu<<16,0)");
    TQ3(0x0F, 0x00010000, "SetCpuGpuVid(1<<16=GPU,0)");
    TQ3(0x0F, 99, "SetCpuGpuVid(0,99=931mV)");
    TQ3(0x10, 0, "UnforceCpuGpuVid");

    printf("\n=== Q3: Temperature probes (with params) ===\n");
    TQ3(0x40, 0, "GetCpuTempMax");
    TQ3(0x35, 0, "Q3:0x35");
    TQ3(0x38, 0, "Q3:0x38");
    TQ3(0x39, 0, "Q3:0x39");
    TQ3(0x0A, 0, "Q3:0x0A");
    TQ3(0x0B, 0, "Q3:0x0B");
    TQ3(0x0C, 0, "Q3:0x0C");
    TQ3(0x0D, 0, "Q3:0x0D");
    TQ3(0x0E, 0, "Q3:0x0E");

    printf("\n=== Restore ===\n");
    Q0(0x3C, 0); Q0(0x3A, 0); Q3(0x10, 0);
    printf("  State restored\n");

    printf("\nDone.\n");
    CloseHandle(h);
    return 0;
}
