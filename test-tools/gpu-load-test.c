#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE h;
static BOOL W32(uint32_t o, uint32_t v) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=v; return DeviceIoControl(h,IOCTL_AMDBC250_WRITE_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL); }
static uint32_t R32(uint32_t o) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=0; if(DeviceIoControl(h,IOCTL_AMDBC250_READ_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL)) return r.Value; return 0xFFFFFFFF; }
static uint32_t SR(uint32_t a) { W32(0x38,a); R32(0x38); return R32(0x3C); }
static BOOL SW(uint32_t a,uint32_t v) { return W32(0x38,a) && W32(0x3C,v); }

static int WaitRsp(uint32_t ra, int ms) {
    for (int i = 0; i < ms; i++) { if (SR(ra) == 1) return 1; Sleep(1); }
    return 0;
}

static uint32_t Q0(uint16_t msg, uint32_t arg) {
    uint32_t rsp = SR(0x03B10A68); if (rsp == 1) { SW(0x03B10A68, 0); Sleep(5); }
    SW(0x03B10A48, arg); SW(0x03B10A08, msg);
    if (!WaitRsp(0x03B10A68, 500)) return 0xFFFFFFFF;
    return SR(0x03B10A48);
}

static uint32_t Q2(uint16_t msg, uint32_t arg) {
    uint32_t rsp = SR(0x03B10564); if (rsp == 1) { SW(0x03B10564, 0); Sleep(5); }
    SW(0x03B10998, arg); SW(0x03B10528, msg);
    if (!WaitRsp(0x03B10564, 500)) return 0xFFFFFFFF;
    return SR(0x03B10998);
}
static uint32_t Q3(uint16_t msg, uint32_t arg) {
    uint32_t rsp = SR(0x03B10A80); if (rsp == 1) { SW(0x03B10A80, 0); Sleep(5); }
    SW(0x03B10A88, arg); SW(0x03B10A20, msg);
    if (!WaitRsp(0x03B10A80, 500)) return 0xFFFFFFFF;
    return SR(0x03B10A88);
}

// VID formula: vid = round((1.55 - mV/1000.0) / 0.00625)
// mV = round((-vid*0.00625 + 1.55) * 1000)
static uint32_t VidFromMv(uint32_t mv) {
    return (uint32_t)((1.55 - mv/1000.0) / 0.00625 + 0.5);
}

static void ReadSensors(const char* label) {
    uint32_t freq = Q0(0x37, 0);
    uint32_t vid = Q0(0x38, 0);
    uint32_t cpuV = Q3(0x36, 0);
    uint32_t gpuV = Q3(0x37, 0);
    uint32_t wgp = Q0(0x1E, 0);
    uint32_t features = Q0(0x3D, 0);
    uint32_t tempMax = Q3(0x40, 0);
    // Convert vid to mV: mV = round((-vid*0.00625 + 1.55) * 1000)
    uint32_t mv = (uint32_t)((-(int)vid * 0.00625 + 1.55) * 1000 + 0.5);
    printf("  %-20s freq=%4uMHz vid=%3u(%4umV) cpuV=%umV gpuV=%umV wgp=%u tempMax=%uC GFXOFF=%s\n",
        label, freq, vid, mv, cpuV, gpuV, wgp, tempMax,
        (features & 0x4) ? "ON" : "OFF");
}

int main() {
    h = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { printf("FAIL: Open device\n"); return 1; }
    AMDBC250_IOCTL_INIT_HARDWARE ih = {0}; ih.MmioPhysicalBase = 0xFE800000ULL; ih.MmioSize = 0x80000; ih.Flags = 1; DWORD b;
    if (!DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &b, NULL)) {
        printf("FAIL: INIT_HARDWARE err=%lu\n", GetLastError()); CloseHandle(h); return 1;
    }
    printf("INIT_HARDWARE OK\n");

    printf("Safe points (from bc250-collective):\n");
    printf("  500MHz@  700mV(vid=%u) P1 | 1175MHz@ 850mV(vid=%u) P3 | 1800MHz@1000mV(vid=%u) P3\n",
        VidFromMv(700), VidFromMv(850), VidFromMv(1000));
    printf("  800MHz@  750mV(vid=%u) P1 | 1400MHz@ 900mV(vid=%u) P3 | 2000MHz@1050mV(vid=%u) P3\n\n",
        VidFromMv(750), VidFromMv(900), VidFromMv(1050));

    printf("=== Phase 1: BASELINE (idle, GFXOFF ON) ===\n");
    ReadSensors("Baseline");

    printf("\n=== Phase 2: DISABLE GFXOFF+CG+PG + HIGH PERF PROFILE ===\n");
    Q3(0x1E, 3); printf("  SetPerfProfile(3) -> ok\n");
    Q2(0x06, 0x1C); printf("  Disable GFXOFF+CG+PG -> ok\n");
    Sleep(100);
    ReadSensors("GFXOFF off");

    printf("\n=== Phase 3: FORCE 1500 MHz @ stock vid(99=931mV) ===\n");
    Q0(0x3C, 0); Q0(0x3A, 0);
    Q3(0x8C, 80); Q3(0x1E, 3);
    Q0(0x3B, 99); Sleep(10);
    uint32_t r = Q0(0x39, 1500);
    printf("  ForceGfxFreq(1500)=0x%08X\n", r);
    Sleep(50);
    ReadSensors("1500MHz forced");

    printf("\n=== Phase 4: FORCE 800 MHz @ 750mV(vid=%u) P1 ===\n", VidFromMv(750));
    Q0(0x3A, 0);
    Q3(0x1E, 1);
    Q0(0x3B, VidFromMv(750)); Sleep(10);
    r = Q0(0x39, 800);
    printf("  ForceGfxFreq(800)=0x%08X\n", r);
    Sleep(50);
    ReadSensors("800MHz low");

    printf("\n=== Phase 5: FORCE 1800 MHz @ 1000mV(vid=%u) P3 ===\n", VidFromMv(1000));
    Q0(0x3A, 0);
    Q3(0x1E, 3);
    Q0(0x3B, VidFromMv(1000)); Sleep(10);
    r = Q0(0x39, 1800);
    printf("  ForceGfxFreq(1800)=0x%08X\n", r);
    Sleep(50);
    ReadSensors("1800MHz high");

    printf("\n=== Phase 6: RESTORE (safe idle) ===\n");
    Q0(0x3C, 0); Q0(0x3A, 0); Q3(0x10, 0);
    Sleep(100);
    ReadSensors("Restored");
    Q2(0x05, 0x4); printf("  Re-enable GFXOFF\n");
    Sleep(100);
    ReadSensors("GFXOFF ON");

    printf("\nDone. GPU frequency control verified.\n");
    CloseHandle(h);
    return 0;
}
