#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE h;
static BOOL W32(uint32_t o, uint32_t v) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=v; return DeviceIoControl(h,IOCTL_AMDBC250_WRITE_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL); }
static uint32_t R32(uint32_t o) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=0; if(DeviceIoControl(h,IOCTL_AMDBC250_READ_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL)) return r.Value; return 0xFFFFFFFF; }

#define C2PMSG_35 0x1056C
#define C2PMSG_36 0x10570
#define C2PMSG_37 0x10574
#define C2PMSG_64 0x105E0
#define C2PMSG_67 0x105EC
#define C2PMSG_69 0x105F4
#define C2PMSG_70 0x105F8
#define C2PMSG_71 0x105FC
#define C2PMSG_81 0x10614
#define SOS_ALIVE 0xF0000010

// Check if a mailbox command actually changes C2PMSG registers
static int TryCmd(uint32_t cmd, uint32_t a_lo, uint32_t a_hi) {
    uint32_t c81b = R32(C2PMSG_81);
    uint32_t c35b = R32(C2PMSG_35);
    W32(C2PMSG_36, a_lo);
    W32(C2PMSG_37, a_hi);
    W32(C2PMSG_35, cmd);
    Sleep(10);
    uint32_t c35a = R32(C2PMSG_35);
    uint32_t c81a = R32(C2PMSG_81);
    int changed = (c35a != c35b || c81a != c81b);
    printf("  0x%08X: %s (C2PMSG_35:0x%08X->0x%08X C2PMSG_81:0x%08X->0x%08X)\n",
        cmd, changed ? "CHANGED" : "NO_CHANGE", c35b, c35a, c81b, c81a);
    return changed;
}

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
    printf("INIT_HARDWARE OK\n");
    printf("C2PMSG_64=0x%08X C2PMSG_67=0x%08X C2PMSG_69=0x%08X\n", R32(C2PMSG_64), R32(C2PMSG_67), R32(C2PMSG_69));
    printf("C2PMSG_70=0x%08X C2PMSG_71=0x%08X C2PMSG_81=0x%08X\n\n", R32(C2PMSG_70), R32(C2PMSG_71), R32(C2PMSG_81));

    printf("=== Mailbox command test (dummy addr 0x10000000) ===\n");
    // Test the full range of GFX command IDs from PspIoctl.h
    TryCmd(0x01 | (0 << 16), 0x10000000, 0);  // GFX_CMD_ID_INIT_TMR
    TryCmd(0x05 | (0 << 16), 0x10000000, 0);  // SETUP_TMR (Linux standard)
    TryCmd(0x06 | (1 << 16), 0x10000000, 0);  // LOAD_IP_FW ME
    TryCmd(0x06 | (2 << 16), 0x10000000, 0);  // LOAD_IP_FW PFP
    TryCmd(0x06 | (3 << 16), 0x10000000, 0);  // LOAD_IP_FW CE
    TryCmd(0x06 | (4 << 16), 0x10000000, 0);  // LOAD_IP_FW MEC
    TryCmd(0x06 | (8 << 16), 0x10000000, 0);  // LOAD_IP_FW RLC
    TryCmd(0x06 | (9 << 16), 0x10000000, 0);  // LOAD_IP_FW SDMA0
    TryCmd(0x06 | (99 << 16), 0x10000000, 0); // LOAD_IP_FW bad type
    TryCmd(0x0B, 0, 0);                       // PROG_REG
    TryCmd(0x20, 0, 0);                       // LOAD_TOC
    TryCmd(0x21, 0, 0);                       // AUTOLOAD_RLC

    printf("\n=== After tests ===\n");
    printf("C2PMSG_35=0x%08X C2PMSG_81=0x%08X\n", R32(C2PMSG_35), R32(C2PMSG_81));
    printf("C2PMSG_64=0x%08X C2PMSG_67=0x%08X C2PMSG_69=0x%08X\n", R32(C2PMSG_64), R32(C2PMSG_67), R32(C2PMSG_69));
    printf("C2PMSG_70=0x%08X C2PMSG_71=0x%08X\n", R32(C2PMSG_70), R32(C2PMSG_71));

    CloseHandle(h);
    return 0;
}
