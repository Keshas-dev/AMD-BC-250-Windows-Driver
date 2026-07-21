#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE h;
static BOOL W32(uint32_t o, uint32_t v) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=v; return DeviceIoControl(h,IOCTL_AMDBC250_WRITE_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL); }
static uint32_t R32(uint32_t o) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=0; if(DeviceIoControl(h,IOCTL_AMDBC250_READ_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL)) return r.Value; return 0xFFFFFFFF; }
static uint32_t SmnRead(uint32_t a){W32(0x38,a);R32(0x38);return R32(0x3C);}
static BOOL SmnWrite(uint32_t a,uint32_t v){if(!W32(0x38,a))return FALSE;return W32(0x3C,v);}
#define C2PMSG_66 0x03B10A08
#define C2PMSG_82 0x03B10A48
#define C2PMSG_90 0x03B10A68
#define C2PMSG_80 0x03B10A80
#define C2PMSG_88 0x03B10A88

static int WaitC2p(uint32_t rspAddr, uint32_t exp, int ms){for(int i=0;i<ms;i++){if(SmnRead(rspAddr)==exp)return 1;Sleep(1);}return 0;}
static uint32_t SmuQ0(uint16_t msg,uint32_t arg){SmnWrite(C2PMSG_90,0);SmnWrite(C2PMSG_82,arg);SmnWrite(C2PMSG_66,msg);if(!WaitC2p(C2PMSG_90,1,200))return 0xFFFFFFFF;return SmnRead(C2PMSG_82);}
static uint32_t SmuQ3(uint16_t msg,uint32_t arg){SmnWrite(C2PMSG_80,0);SmnWrite(C2PMSG_88,arg);SmnWrite(C2PMSG_80+4,msg);if(!WaitC2p(C2PMSG_80,1,200))return 0xFFFFFFFF;return SmnRead(C2PMSG_88);}

int main() {
    h = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { printf("FAIL: Open device\n"); return 1; }

    AMDBC250_IOCTL_INIT_HARDWARE ih;
    ih.MmioPhysicalBase = 0xFE800000ULL;
    ih.MmioSize = 0x80000;
    ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
    DWORD b;
    if (!DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &b, NULL)) {
        printf("FAIL: INIT_HARDWARE err=%lu\n", GetLastError()); CloseHandle(h); return 1;
    }
    printf("INIT_HARDWARE OK\n\n");

    /* === BAR5 C2PMSG scan (0x1056C - 0x10618) === */
    printf("=== BAR5 C2PMSG Scan ===\n");
    for (uint32_t off = 0x1056C; off <= 0x10618; off += 4) {
        uint32_t regIdx = (off - 0x1056C) / 4;
        uint32_t v = R32(off);
        printf("  0x%05X [C2PMSG_%2u?]: 0x%08X\n", off, 35 + regIdx, v);
    }

    /* === SMN MP0 C2PMSG scan === */
    printf("\n=== SMN MP0 C2PMSG Scan ===\n");
    printf("Probing SMN 0x03B00000 - 0x03B00FFF for C2PMSG values...\n\n");
    for (uint32_t smn = 0x03B00000; smn < 0x03B01000; smn += 4) {
        uint32_t v = SmnRead(smn);
        if (v != 0 && v != 0xFFFFFFFF) {
            printf("  SMN 0x%08X: 0x%08X\n", smn, v);
        }
    }

    /* === SMN MP0 C2PMSG block (alternative range 0x03B14000+) === */
    printf("\n=== SMN MP0 C2PMSG Range 0x03B14000+ ===\n");
    for (uint32_t smn = 0x03B14000; smn < 0x03B15000; smn += 4) {
        uint32_t v = SmnRead(smn);
        if (v != 0 && v != 0xFFFFFFFF) {
            printf("  SMN 0x%08X: 0x%08X\n", smn, v);
        }
    }

    /* === SMU status === */
    printf("\n=== SMU Status ===\n");
    uint32_t fwFlags = SmnRead(0x03B10024);
    uint32_t pubCtrl = SmnRead(0x03B10B14);
    printf("  FW_FLAGS:  0x%08X\n", fwFlags);
    printf("  PUB_CTRL:  0x%08X\n", pubCtrl);
    uint32_t ver = SmuQ0(0x02, 0);
    printf("  SMU v%u.%u.%u\n", (ver >> 16) & 0xFF, (ver >> 8) & 0xFF, ver & 0xFF);
    uint32_t features = SmuQ0(0x3D, 0);
    printf("  Features:  0x%08X\n", features);
    uint32_t wgps = SmuQ0(0x1E, 0);
    printf("  WGPS:      %u\n", wgps);
    printf("  GFXOFF:    %s\n", (features & 0x4) ? "ON" : "OFF");

    CloseHandle(h);
    return 0;
}
