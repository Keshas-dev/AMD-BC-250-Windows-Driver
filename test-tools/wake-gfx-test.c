/* wake-gfx-test.c — try to wake GFX from GFXOFF via SMN writes to RLC PG registers */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE h;
static BOOL W32(uint32_t o, uint32_t v) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=v;
    return DeviceIoControl(h,IOCTL_AMDBC250_WRITE_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL);
}
static uint32_t R32(uint32_t o) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=0;
    if(DeviceIoControl(h,IOCTL_AMDBC250_READ_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL)) return r.Value;
    return 0xFFFFFFFF;
}
static uint32_t smnR(uint32_t a) { W32(0x38,a); R32(0x38); return R32(0x3C); }
static void smnW(uint32_t a, uint32_t v) { W32(0x38,a); W32(0x3C,v); }

static uint32_t smuMsgX(uint16_t msg, uint32_t param) {
    uint32_t c;
    for(int w=0;w<100;w++){c=smnR(0x03B10A68);if(c==0||c==1)break;smnW(0x03B10A68,0);Sleep(1);}
    if(c!=1) smnW(0x03B10A68,0);
    smnW(0x03B10A48,param);
    smnW(0x03B10A08,msg);
    for(int i=0;i<1500;i++){
        c=smnR(0x03B10A68);
        if(c==1){Sleep(5);return smnR(0x03B10A48);}
        if(c!=0) return 0xFFFF0000|(c&0xFF);
        Sleep(1);
    }
    return 0xFFFFFFFF;
}

static void probe(const char* name, uint32_t smnAddr) {
    uint32_t v=smnR(smnAddr);
    printf("  SMN[0x%05X] = 0x%08X",smnAddr,v);
    /* Try write a test value and read back */
    smnW(smnAddr,v|0x1F);
    uint32_t v2=smnR(smnAddr);
    printf(" -> write 0x%08X, read 0x%08X %s\n",v|0x1F,v2,v2==(v|0x1F)?"WRITABLE":"RO");
    smnW(smnAddr,v); /* restore */
}

int main() {
    setvbuf(stdout,NULL,_IONBF,0);
    h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE){printf("FAIL CreateFile gle=%lu\n",GetLastError());return 1;}
    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br=0;
    ZeroMemory(&ih,sizeof(ih)); ih.MmioPhysicalBase=0xFE800000ULL; ih.MmioSize=0x80000; ih.Flags=AMDBC250_INIT_FLAG_NBIO_MAP;
    if(!DeviceIoControl(h,IOCTL_AMDBC250_INIT_HARDWARE,&ih,sizeof(ih),&ih,sizeof(ih),&br,NULL)){printf("INIT fail\n");return 1;}
    printf("INIT OK\n\n");

    printf("=== SMU State (before) ===\n");
    printf("GfxFreq=%u MHz\n",smuMsgX(0x37,0));
    printf("ActiveWgp=%u\n",smuMsgX(0x1E,0));
    printf("CorePstate=%u\n",smuMsgX(0x0C,0));
    printf("SocClock=%u\n",smuMsgX(0x11,0));

    printf("\n=== Probe WGP/PG registers via BAR5 direct (NOT SMN) ===\n");
    printf("  NOTE: These are BAR5 MMIO offsets, NOT SMN addresses.\n");
    printf("  Using R32() for direct BAR5 reads, NOT smnR().\n");

    printf("\n  BAR5[0x3D64] = 0x%08X (RLC_PG)\n",R32(0x3D64));
    printf("  BAR5[0x34FC] = 0x%08X (SPI_PG)\n",R32(0x34FC));
    printf("  BAR5[0x3264] = 0x%08X (CC_ARRAY)\n",R32(0x3264));

    printf("  BAR5[0x5C3C] = 0x%08X (SPI_PG_ENABLE_STATIC_WGP_MASK)\n",R32(0x5C3C));
    printf("  BAR5[0x9C1C] = 0x%08X (CC_GC_SHADER_ARRAY_CONFIG)\n",R32(0x9C1C));

    /* Check SMU state */
    printf("\n=== SMU State ===\n");
    uint32_t f=smuMsgX(0x37,0);
    uint32_t w=smuMsgX(0x1E,0);
    printf("GfxFreq=%u MHz ActiveWgp=%u Features=0x%08X\n",f,w,smuMsgX(0x3D,0));

    /* Try SetCoreEnableMask SMU message */
    printf("\n=== Try SMU SetCoreEnableMask (0x2C) ===\n");
    printf("Before: %u\n",smuMsgX(0x1E,0));
    smuMsgX(0x2C,0xFFFFFFFF); /* enable all cores */
    Sleep(100);
    printf("After SetCoreEnableMask(0xFFFFFFFF): ActiveWgp=%u\n",smuMsgX(0x1E,0));

    /* Try RequestCorePstate(0) for max perf */
    printf("\n=== Try RequestCorePstate(0) ===\n");
    smuMsgX(0x0B,0);
    Sleep(100);
    printf("GfxFreq=%u MHz CorePstate=%u ActiveWgp=%u\n",
        smuMsgX(0x37,0), smuMsgX(0x0C,0), smuMsgX(0x1E,0));

    printf("\nDONE\n");
    CloseHandle(h);
    return 0;
}
