/* smn-gpu-scan.c - scan SMN space for GPU WGP enable register using SMU 0x98 */
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

static int q3(uint32_t msg,uint32_t arg){
    smnW(0x03B10A80,0); smnW(0x03B10A88,arg); smnW(0x03B10A20,msg);
    for(int i=0;i<500;i++){uint32_t st=smnR(0x03B10A80);if(st==1)return 1;if(st==0xFF)return -1;if(st==0xFE)return -2;if(st==0xFD)return -3;if(st==0xFC)return -4;Sleep(1);}return -100;
}
static uint32_t q3_arg(){return smnR(0x03B10A88);}

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

    /* SMU sanity */
    int r=q3(1,123); uint32_t tst=q3_arg();
    printf("SMU Q3 test: status=%d resp=%u %s\n",r,tst,(r==1&&tst==124)?"OK":"DEAD");
    if(r!=1||tst!=124){CloseHandle(h);return 1;}

    /* Query baseline GPU state */
    r=q0(0x1E,0); uint32_t baseWgp=q0_arg();
    printf("Baseline QueryActiveWgp = %u\n",baseWgp);
    printf("Baseline SPI_PG (BAR5) = 0x%08X\n",R32(0x5C3C));
    printf("Baseline GRBM_STATUS   = 0x%08X\n",R32(0x2000));

    /* Scan SMN addresses with 0x98 write, looking for GPU effect */
    printf("\n=== Scanning SMN space for GPU WGP enable ===\n");
    printf("Core mask @ 0x0115A870 = 0x%08X (reference)\n",smnR(0x0115A870));

    /* Scan strategy: try addresses near core mask and other likely spots */
    uint32_t scan_addrs[] = {
        /* Near core mask (APU shared control region?) */
        0x0115A800, 0x0115A810, 0x0115A820, 0x0115A830,
        0x0115A840, 0x0115A850, 0x0115A860, 0x0115A870, /* = core mask */
        0x0115A880, 0x0115A890, 0x0115A8A0, 0x0115A8B0,
        0x0115A900, 0x0115AA00, 0x0115AB00, 0x0115AC00,
        /* GPU-specific SMN region (guess) */
        0x01160000, 0x01170000, 0x01180000, 0x01190000,
        0x011A0000, 0x011B0000, 0x011C0000, 0x011D0000,
        /* Other APU control regions */
        0x01200000, 0x01300000, 0x01400000, 0x01500000,
        /* High SMN (GC aliases?) */
        0x02000000, 0x03000000,
    };
    int n = sizeof(scan_addrs)/sizeof(scan_addrs[0]);
    int found = 0;

    for(int i=0; i<n; i++){
        uint32_t addr = scan_addrs[i];
        /* Skip 0 (panic) and core mask (already known) */
        if(addr == 0 || addr == 0x0115A870) continue;

        uint32_t before = smnR(addr);
        if(before == 0xFFFFFFFF) continue; /* dead address */

        /* Try writing 0xFF via 0x98 */
        r = q3(0x98, addr);
        if(r != 1) continue; /* SMU rejected */

        uint32_t after = smnR(addr);
        if(after != before){
            /* Write stuck! Check if GPU state changed */
            uint32_t wgp = 0; q0(0x1E,0); wgp = q0_arg();
            uint32_t spi = R32(0x5C3C);
            uint32_t grbm = R32(0x2000);
            if(wgp != baseWgp || spi != 0 || grbm != R32(0x2000)){
                printf("  *** GPU AFFECTED *** addr=0x%08X before=0x%08X after=0x%08X Wgp=%u SPI=0x%08X GRBM=0x%08X\n",
                    addr, before, after, wgp, spi, grbm);
                found++;
            } else {
                printf("  addr=0x%08X before=0x%08X after=0x%08X (write stuck, no GPU effect)\n", addr, before, after);
            }
        }
    }

    printf("\n=== Scan complete. Found %d GPU-affecting registers ===\n", found);
    CloseHandle(h);
    return 0;
}
