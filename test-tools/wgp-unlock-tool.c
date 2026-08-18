/* wgp-unlock-tool.c - BC-250 WGP unlock via multiple methods
 * Linux writes SPI_PG_ENABLE_STATIC_WGP_MASK=0x1F + RLC_PG_ALWAYS_ON_WGP_MASK=0x1F
 * via UMR (debugfs kernel context). We try the same via Windows WDM. */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE h;
static BOOL W32(uint32_t o, uint32_t v) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=v; return DeviceIoControl(h,IOCTL_AMDBC250_WRITE_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL); }
static uint32_t R32(uint32_t o) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=0; if(DeviceIoControl(h,IOCTL_AMDBC250_READ_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL)) return r.Value; return 0xFFFFFFFF; }

static void smnW(uint32_t a,uint32_t v){W32(0x38,a);W32(0x3C,v);}
static uint32_t smnR(uint32_t a){W32(0x38,a);R32(0x38);return R32(0x3C);}

static int q0(uint32_t msg,uint32_t arg){
    smnW(0x03B10A68,0); smnW(0x03B10A48,arg); smnW(0x03B10A08,msg);
    for(int i=0;i<500;i++){uint32_t st=smnR(0x03B10A68);if(st==1)return 1;if(st==0xFF)return -1;if(st==0xFE)return -2;if(st==0xFD)return -3;if(st==0xFC)return -4;Sleep(1);}return -100;
}
static uint32_t q0_arg(){return smnR(0x03B10A48);}

/* SMU Q3 msg 0x98: writes 0xFF to any SMN address (4-byte write) */
static int smu98(uint32_t smnAddr){
    smnW(0x03B10A80,0); smnW(0x03B10A88,smnAddr); smnW(0x03B10A20,0x98);
    for(int i=0;i<500;i++){uint32_t st=smnR(0x03B10A80);if(st==1)return 1;if(st==0xFF)return -1;if(st==0xFE)return -2;if(st==0xFD)return -3;if(st==0xFC)return -4;Sleep(1);}return -100;
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE){printf("FAIL: open err=%lu\n",GetLastError());return 1;}

    AMDBC250_IOCTL_INIT_HARDWARE ih;
    DWORD br=0;
    memset(&ih,0,sizeof(ih));
    ih.MmioPhysicalBase=0xFE800000ULL;
    ih.MmioSize=0x80000;
    ih.Flags=AMDBC250_INIT_FLAG_NBIO_MAP;
    if(!DeviceIoControl(h,IOCTL_AMDBC250_INIT_HARDWARE,&ih,sizeof(ih),&ih,sizeof(ih),&br,NULL)){printf("INIT fail\n");return 1;}
    printf("INIT OK\n");

    /* Baseline */
    int r=q0(0x1E,0); uint32_t baseWgp=q0_arg();
    printf("Baseline: Wgp=%u SPI_PG=0x%08X CC=0x%08X RLC=0x%08X\n", baseWgp, R32(0x5C3C), R32(0x9C1C), R32(0x3D64));

    /* Method 1: Direct BAR5 writes (proven NOT to work) */
    printf("\n=== Method 1: Direct BAR5 writes ===\n");
    W32(0x5C3C, 0x1F);
    printf("SPI_PG after direct write 0x1F: 0x%08X\n", R32(0x5C3C));

    /* Method 2: SMU 0x98 to SPI_PG physical address
     * SPI_PG is at BAR5 offset 0x5C3C = physical 0xFE805C3C
     * SMU 0x98 writes 0xFF to any SMN address. If SPI_PG has an SMN alias
     * (like core mask at 0x0115A870), this could work.
     * Try physical address as SMN: 0xFE805C3C → encoded how? */
    printf("\n=== Method 2: SMU 0x98 to SPI_PG alias candidates ===\n");
    /* The SMU msg 0x98 takes an SMN address. Core mask is at 0x0115A870.
     * We don't know SPI_PG's SMN address. Try some candidates. */
    uint32_t candidates[] = {
        0x0115A870,  /* core mask (known working) — for sanity */
        0x0115B000,  /* near core mask */
        0x01160000,  /* extended */
        0x01170000,
        0x01180000,
        0x00005C3C,  /* SPI_PG offset as SMN? */
        0x03B10000,  /* SMU mailbox base */
    };
    for(int i=0; i<sizeof(candidates)/sizeof(candidates[0]); i++){
        uint32_t addr = candidates[i];
        uint32_t before = smnR(addr);
        r = smu98(addr);
        uint32_t after = smnR(addr);
        if(r==1 && after!=before){
            printf("  addr=0x%08X: before=0x%08X after=0x%08X (STUCK)\n", addr, before, after);
        }
    }

    /* Method 3: Try RequestActiveWgp SMU msg (0x18) via Q0 */
    printf("\n=== Method 3: SMU RequestActiveWgp (0x18) ===\n");
    r=q0(0x18, 0x1F); uint32_t rsp=q0_arg();
    printf("  Q0 0x18(0x1F): status=%d rsp=0x%08X\n", r, rsp);
    Sleep(500);
    r=q0(0x1E,0); printf("  QueryActiveWgp after: %u\n", q0_arg());
    printf("  SPI_PG after: 0x%08X\n", R32(0x5C3C));

    /* Method 4: Combined — write all 3 registers like Linux */
    printf("\n=== Method 4: Combined CC+SPI+RLC (like Linux UMR) ===\n");
    W32(0x9C1C, 0xFFE00000);
    W32(0x5C3C, 0x1F);
    W32(0x3D64, 0x1F);
    printf("  After: CC=0x%08X SPI=0x%08X RLC=0x%08X\n", R32(0x9C1C), R32(0x5C3C), R32(0x3D64));
    r=q0(0x1E,0); printf("  QueryActiveWgp: %u\n", q0_arg());

    printf("\n=== DONE ===\n");
    CloseHandle(h);
    return 0;
}
