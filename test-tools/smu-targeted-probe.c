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
    printf("INIT_HARDWARE OK\n");

    printf("\n=== Q0: Previously unknown messages ===\n");
    QMsg(Q0_C, Q0_A, Q0_R, 0x4E, 0, "Q0:0x4E arg=0");
    QMsg(Q0_C, Q0_A, Q0_R, 0x4E, 1, "Q0:0x4E arg=1");
    QMsg(Q0_C, Q0_A, Q0_R, 0x53, 0, "Q0:0x53 arg=0");
    QMsg(Q0_C, Q0_A, Q0_R, 0x53, 1, "Q0:0x53 arg=1");
    QMsg(Q0_C, Q0_A, Q0_R, 0x55, 0, "Q0:0x55 arg=0");
    QMsg(Q0_C, Q0_A, Q0_R, 0x55, 1, "Q0:0x55 arg=1");
    QMsg(Q0_C, Q0_A, Q0_R, 0x56, 0, "Q0:0x56 arg=0");
    QMsg(Q0_C, Q0_A, Q0_R, 0x56, 1, "Q0:0x56 arg=1");

    printf("\n=== Q3: Temperature candidates ===\n");
    QMsg(Q3_C, Q3_A, Q3_R, 0x0A, 0, "Q3:0x0A arg=0");
    QMsg(Q3_C, Q3_A, Q3_R, 0x0A, 1, "Q3:0x0A arg=1");
    QMsg(Q3_C, Q3_A, Q3_R, 0x0B, 0, "Q3:0x0B arg=0");
    QMsg(Q3_C, Q3_A, Q3_R, 0x0B, 1, "Q3:0x0B arg=1");
    QMsg(Q3_C, Q3_A, Q3_R, 0x0C, 0, "Q3:0x0C arg=0");
    QMsg(Q3_C, Q3_A, Q3_R, 0x0C, 1, "Q3:0x0C arg=1");
    QMsg(Q3_C, Q3_A, Q3_R, 0x80, 0, "Q3:0x80 arg=0");
    QMsg(Q3_C, Q3_A, Q3_R, 0x80, 1, "Q3:0x80 arg=1");
    QMsg(Q3_C, Q3_A, Q3_R, 0x8D, 0, "Q3:0x8D arg=0");
    QMsg(Q3_C, Q3_A, Q3_R, 0x8D, 1, "Q3:0x8D arg=1");

    /* Restore */
    printf("\n=== Restore ===\n");
    QMsg(Q0_C, Q0_A, Q0_R, 0x3C, 0, "UnforceGfxVid");
    QMsg(Q0_C, Q0_A, Q0_R, 0x3A, 0, "UnforceGfxFreq");
    QMsg(Q3_C, Q3_A, Q3_R, 0x1E, 1, "SetPerfProfile(1)");

    CloseHandle(h);
    return 0;
}
