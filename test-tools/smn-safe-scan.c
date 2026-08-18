/* smn-safe-scan.c - safely scan SMN for SPI_PG alias using SMU 0x98 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE h;
static BOOL W32(uint32_t o, uint32_t v) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=v; return DeviceIoControl(h,IOCTL_AMDBC250_WRITE_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL); }
static uint32_t R32(uint32_t o) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=0; if(DeviceIoControl(h,IOCTL_AMDBC250_READ_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL)) return r.Value; return 0xFFFFFFFF; }

static void smnW(uint32_t a,uint32_t v){W32(0x38,a);W32(0x3C,v);}
static uint32_t smnR(uint32_t a){W32(0x38,a);R32(0x38);return R32(0x3C);}

static int q3(uint32_t msg,uint32_t arg){
    smnW(0x03B10A80,0); smnW(0x03B10A88,arg); smnW(0x03B10A20,msg);
    for(int i=0;i<500;i++){uint32_t st=smnR(0x03B10A80);if(st==1)return 1;if(st==0xFF)return -1;if(st==0xFE)return -2;if(st==0xFD)return -3;if(st==0xFC)return -4;Sleep(1);}return -100;
}

static int q0(uint32_t msg,uint32_t arg){
    smnW(0x03B10A68,0); smnW(0x03B10A48,arg); smnW(0x03B10A08,msg);
    for(int i=0;i<500;i++){uint32_t st=smnR(0x03B10A68);if(st==1)return 1;if(st==0xFF)return -1;if(st==0xFE)return -2;if(st==0xFD)return -3;if(st==0xFC)return -4;Sleep(1);}return -100;
}
static uint32_t q0_arg(){return smnR(0x03B10A48);}

/* Q3 arg read helper */
static uint32_t q3_arg(void){return smnR(0x03B10A88);}

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

    /* Baseline */
    r=q0(0x1E,0); uint32_t baseWgp=q0_arg();
    uint32_t baseSpi=R32(0x5C3C);
    uint32_t baseGrbm=R32(0x2000);
    printf("Baseline: Wgp=%u SPI_PG=0x%08X GRBM=0x%08X\n", baseWgp, baseSpi, baseGrbm);
    printf("Core mask @ 0x0115A870 = 0x%08X (reference)\n", smnR(0x0115A870));

    /* Safe scan: only addresses that respond to read AND are writable */
    printf("\n=== Safe SMN Scan (read-responsive addresses only) ===\n");

    /* Scan strategy: check if address responds to read, try 0x98 write, check GPU state */
    uint32_t scan_ranges[][2] = {
        {0x0115A800, 0x0115B000},  /* Near core mask */
        {0x01150000, 0x01160000},  /* Extended core region */
        {0x01100000, 0x01200000},  /* Full APU control */
        {0x01000000, 0x01300000},  /* Wider APU region */
        {0x03B10000, 0x03B20000},  /* SMU mailbox extended */
    };
    int ranges = 5;
    int found = 0;
    int scanned = 0;

    for(int ri=0; ri<ranges; ri++){
        uint32_t lo = scan_ranges[ri][0];
        uint32_t hi = scan_ranges[ri][1];
        printf("\n--- Scanning 0x%08X - 0x%08X (step 0x100) ---\n", lo, hi);
        for(uint32_t addr=lo; addr<hi; addr+=0x100){
            scanned++;
            uint32_t before = smnR(addr);
            if(before == 0xFFFFFFFF) continue; /* dead address, skip */
            if(before == 0x0) continue; /* probably empty, skip */

            /* Safe to try: address responds to read */
            /* Write 0xFF via 0x98 */
            r = q3(0x98, addr);
            if(r != 1) continue; /* SMU rejected, skip */

            /* Check if write stuck */
            uint32_t after = smnR(addr);
            if(after == before) continue; /* write didn't stick */

            /* Write stuck! Check GPU state */
            uint32_t spi = R32(0x5C3C);
            uint32_t grbm = R32(0x2000);
            q0(0x1E,0); uint32_t wgp = q0_arg();

            /* Restore original value (best effort) */
            /* Note: 0x98 writes 0xFF, can't restore arbitrary value easily */
            /* But core mask we can restore if needed */

            if(wgp != baseWgp || spi != baseSpi){
                printf("  *** GPU AFFECTED *** addr=0x%08X before=0x%08X after=0x%08X Wgp=%u SPI=0x%08X GRBM=0x%08X\n",
                    addr, before, after, wgp, spi, grbm);
                found++;
            } else {
                printf("  addr=0x%08X before=0x%08X after=0x%08X (stuck, no GPU effect)\n", addr, before, after);
            }
        }
    }

    printf("\n=== Scan complete. Scanned %u addresses, found %u GPU-affecting ===\n", scanned, found);
    CloseHandle(h);
    return 0;
}
