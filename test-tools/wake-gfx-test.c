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
    printf("GfxFreq=%u MHz\n",smuMsgX(0x37,0)/100);
    printf("ActiveWgp=%u\n",smuMsgX(0x1E,0));
    printf("CorePstate=%u\n",smuMsgX(0x0C,0));
    printf("SocClock=%u\n",smuMsgX(0x11,0)/100);

    printf("\n=== Probe RLC/SPI PG registers via SMN ===\n");

    /* Try multiple possible SMN addresses for RLC_PG_ALWAYS_ON_WGP_MASK */
    /* BAR5 offset = 0x3D64. SMN formulas: */
    uint32_t addresses[]={
        0x3D64,     /* BASE_IDX=0: GC_BASE + offset */
        0xDDA64,    /* BASE_IDX=1: GC_BASE + 0xA000 + offset */
        /* SPI_PG_ENABLE_STATIC_WGP_MASK at BAR5 0x34FC */
        0x34FC,     /* BASE_IDX=0 */
        0xD4FC,     /* BASE_IDX=1: 0x1260+0xA000+0x229C = 0xD4FC */
        /* CC_GC_SHADER_ARRAY_CONFIG at BAR5 0x3264 */
        0x3264,     /* BASE_IDX=0 */
        0xD264,     /* BASE_IDX=1 */
    };
    const char* names[]={
        "RLC_PG_ALWAYS_ON_WGP (0)",
        "RLC_PG_ALWAYS_ON_WGP (1)",
        "SPI_PG_ENABLE_STATIC (0)",
        "SPI_PG_ENABLE_STATIC (1)",
        "CC_GC_SHADER_ARRAY (0)",
        "CC_GC_SHADER_ARRAY (1)",
    };

    for(int i=0;i<6;i++){
        printf("--- %s ---\n",names[i]);
        probe(names[i],addresses[i]);
    }

    printf("\n=== Try BAR5 direct reads for same registers ===\n");
    printf("BAR5[0x3D64] = 0x%08X (RLC PG)\n",R32(0x3D64));
    printf("BAR5[0x34FC] = 0x%08X (SPI PG)\n",R32(0x34FC));
    printf("BAR5[0x3264] = 0x%08X (CC ARRAY)\n",R32(0x3264));

    /* Now try writing the working SMN addresses with meaningful values */
    printf("\n=== Try writing PG configs to enable WGPs ===\n");

    /* Identify which addresses work (non-FFFFFFFF, non-0 when written) */
    /* First read all to see baseline */
    uint32_t rlc0=smnR(0x3D64);
    uint32_t rlc1=smnR(0xDDA64);
    uint32_t spi0=smnR(0x34FC);
    uint32_t spi1=smnR(0xD4FC);

    /* Determine which SMN address for each is the real one */
    uint32_t rlcAddr = (rlc0==0xFFFFFFFF) ? 0xDDA64 : 0x3D64;
    uint32_t spiAddr = (spi0==0xFFFFFFFF) ? 0xD4FC : 0x34FC;

    printf("RLC PG at SMN 0x%05X (0=0x%08X, 1=0x%08X)\n",rlcAddr,rlc0,rlc1);
    printf("SPI PG at SMN 0x%05X (0=0x%08X, 1=0x%08X)\n",spiAddr,spi0,spi1);

    /* Save original values */
    uint32_t origRlc=smnR(rlcAddr);
    uint32_t origSpi=smnR(spiAddr);
    printf("Original: RLC=0x%08X SPI=0x%08X\n",origRlc,origSpi);

    /* Write mask for ~40 WGPs */
    smnW(rlcAddr,0xFFFFFFFF);
    smnW(spiAddr,0xFFFFFFFF);

    uint32_t newRlc=smnR(rlcAddr);
    uint32_t newSpi=smnR(spiAddr);
    printf("Written: RLC=0x%08X (wanted 0xFFFFFFFF) SPI=0x%08X (wanted 0xFFFFFFFF)\n",newRlc,newSpi);

    /* Check SMU state */
    printf("\n=== SMU State (after PG write) ===\n");
    uint32_t f=smuMsgX(0x37,0);
    uint32_t w=smuMsgX(0x1E,0);
    uint32_t c=smuMsgX(0x0C,0);
    printf("GfxFreq=%u MHz (was 15)\n",f/100);
    printf("ActiveWgp=%u (was 0)\n",w);
    printf("CorePstate=%u\n",c);
    printf("Features=0x%08X\n",smuMsgX(0x3D,0));

    /* Also try SetCoreEnableMask SMU message */
    printf("\n=== Try SMU SetCoreEnableMask (0x2C) ===\n");
    printf("Before: %u\n",smuMsgX(0x1E,0));
    smuMsgX(0x2C,0xFFFFFFFF); /* enable all cores */
    Sleep(100);
    printf("After SetCoreEnableMask(0xFFFFFFFF): ActiveWgp=%u\n",smuMsgX(0x1E,0));

    /* Try SetSoftMinCclk/SetSoftMaxCclk again AFTER PG changes */
    printf("\n=== Try SetSoftMinCclk/SetSoftMaxCclk after PG write ===\n");
    smuMsgX(0x35,10000); /* 100 MHz */
    smuMsgX(0x36,20000); /* 200 MHz */
    Sleep(100);
    printf("After soft limits: GfxFreq=%u MHz\n",smuMsgX(0x37,0)/100);

    /* Try RequestCorePstate(0) for max perf */
    printf("\n=== Try RequestCorePstate(0) ===\n");
    smuMsgX(0x0B,0);
    Sleep(100);
    printf("GfxFreq=%u MHz CorePstate=%u ActiveWgp=%u\n",
        smuMsgX(0x37,0)/100, smuMsgX(0x0C,0), smuMsgX(0x1E,0));

    printf("\nDONE\n");
    CloseHandle(h);
    return 0;
}
