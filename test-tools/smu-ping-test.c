#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"
static HANDLE h;
static BOOL W(uint32_t o, uint32_t v) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=v; return DeviceIoControl(h,IOCTL_AMDBC250_WRITE_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL); }
static uint32_t R(uint32_t o) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=0; if(DeviceIoControl(h,IOCTL_AMDBC250_READ_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL)) return r.Value; return 0xFFFFFFFF; }
static uint32_t S(uint32_t a) { W(0x38,a); R(0x38); return R(0x3C); }
int main() {
    setvbuf(stdout,NULL,_IONBF,0);
    h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE){printf("FAIL %lu\n",GetLastError());return 1;}
    printf("OK\n");
    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br=0;
    ZeroMemory(&ih,sizeof(ih)); ih.MmioPhysicalBase=0xFE800000ULL; ih.MmioSize=0x80000; ih.Flags=AMDBC250_INIT_FLAG_NBIO_MAP;
    if(!DeviceIoControl(h,IOCTL_AMDBC250_INIT_HARDWARE,&ih,sizeof(ih),&ih,sizeof(ih),&br,NULL)){printf("INIT fail\n");return 1;}
    printf("INIT OK\n");

    printf("=== SMU Alive Check ===\n");
    uint32_t c90=S(0x03B10A68);
    printf("C2PMSG_90=0x%08X\n",c90);
    if(c90==1){W(0x38,0x03B10A68);W(0x3C,0);}
    W(0x38,0x03B10A48);W(0x3C,0);
    W(0x38,0x03B10A68);W(0x3C,1);
    W(0x38,0x03B10A08);W(0x3C,0x01);
    for(int i=0;i<200;i++){c90=S(0x03B10A68);if(c90==1)break;Sleep(10);}
    printf("TestMessage: C2PMSG_90=%s 0x%08X, C2PMSG_82=0x%08X\n",c90==1?"OK":"TIMEOUT",c90,S(0x03B10A48));

    printf("\n=== CLK/Power registers probe ===\n");
    uint32_t clkProbe[] = {0x03B16C00, 0x03B16C04, 0x03B16C08, 0x03B16C0C,
                           0x03B16C10, 0x03B16C14, 0x03B16C18, 0x03B16C1C,
                           0x03B16C20, 0x03B16C24, 0x03B16C28, 0x03B16C2C,
                           0x03B16C30, 0x03B16C34, 0x03B16C38, 0x03B16C3C,
                           0x03B16C40, 0x03B16C44, 0x03B16C48, 0x03B16C4C,
                           0x03B16C50, 0x03B16C54, 0x03B16C58, 0x03B16C5C,
                           0x03B16C60, 0x03B16C64, 0x03B16C68, 0x03B16C6C,
                           0x03B16C70, 0x03B16C74, 0x03B16C78, 0x03B16C7C,
                           0x03B16C80, 0x03B16C84, 0x03B16C88, 0x03B16C8C,
                           0x03B16C90, 0x03B16C94, 0x03B16C98, 0x03B16C9C,
                           0x03B17400, 0x03B17404, 0x03B17408, 0x03B1740C};
    for(int i=0;i<sizeof(clkProbe)/sizeof(clkProbe[0]);i++){
        uint32_t v=S(clkProbe[i]);
        if(v!=0xFFFFFFFF&&v!=0) printf("  0x%08X: 0x%08X\n",clkProbe[i],v);
    }
    printf("  (all others zero or dead)\n");

    printf("\nDone.\n");
    CloseHandle(h);
    return 0;
}
