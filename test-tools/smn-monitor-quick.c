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
    for(int i=0;i<500;i++){ uint32_t st=smnR(0x03B10A68); if(st==1) return 1; if(st==0xFF) return -1; Sleep(1); }
    return -100;
}
static uint32_t q0_arg(){ return smnR(0x03B10A48); }

static int vid_to_mv(int vid){
    return (int)((-vid*0.00625 + 1.55) * 1000 + 0.5);
}

#define SMN_SHOW(addr,desc) do{ uint32_t v=smnR(addr); printf("  %s [0x%08X] = 0x%08X",desc,(uint32_t)addr,v); if(v==0xFFFFFFFF) printf(" (dead)"); printf("\n"); }while(0)

int main(){
    h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE){printf("FAIL gle=%lu\n",GetLastError());return 1;}
    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br=0;
    ZeroMemory(&ih,sizeof(ih)); ih.MmioPhysicalBase=0xFE800000ULL; ih.MmioSize=0x80000; ih.Flags=AMDBC250_INIT_FLAG_NBIO_MAP;
    if(!DeviceIoControl(h,IOCTL_AMDBC250_INIT_HARDWARE,&ih,sizeof(ih),&ih,sizeof(ih),&br,NULL)){printf("INIT fail\n");CloseHandle(h);return 1;}

    /* Verify SMU alive */
    smnW(0x03B10A80,0); smnW(0x03B10A88,123); smnW(0x03B10A20,1);
    int alive=0;
    for(int wi=0;wi<500;wi++){ uint32_t st=smnR(0x03B10A80); if(st==1){alive=1;break;} if(st==0xFF)break; Sleep(1);}
    uint32_t tst=smnR(0x03B10A88);
    if(alive&&tst==124) printf("SMU: ALIVE (q3 test=%u)\n",tst);
    else { printf("SMU DEAD (alive=%d test=%u)\n",alive,tst); CloseHandle(h); return 1; }

    /* SMU version */
    q0(2,0); uint32_t ver=q0_arg();
    q0(3,0); uint32_t drv=q0_arg();
    printf("SMU v%u.%u.%u (driver_if=%u)\n",(ver>>16)&0xFF,(ver>>8)&0xFF,ver&0xFF,drv);

    /* SMU queries */
    printf("\n=== SMU Telemetry ===\n");
    q0(0x37,0); uint32_t freq=q0_arg();
    q0(0x38,0); int vid=q0_arg();
    q0(0x1E,0); uint32_t wgp=q0_arg();
    q0(0x3D,0); uint32_t feat=q0_arg();
    q0(0x0F,0); uint32_t gfxclk=q0_arg();
    q0(0x0C,0); uint32_t pstate=q0_arg();
    q0(0x03,0); uint32_t drvif=q0_arg();

    printf("Freq: %u MHz (GFXCLK: %u MHz)\n",freq,gfxclk);
    if(vid>=0&&vid<=255) printf("VID: %d (%d mV)\n",vid,vid_to_mv(vid));
    printf("Active WGPs: %u\n",wgp);
    printf("Features: 0x%08X\n",feat);
    printf("Core Pstate: 0x%08X\n",pstate);

    /* GRBM status */
    uint32_t grbm=R32(0x3260);
    printf("\n=== GPU Status ===\n");
    printf("GRBM_STATUS: 0x%08X (GUI=%d IA=%d WD=%d)\n",grbm,(grbm>>31)&1,(grbm>>19)&1,(grbm>>18)&1);
    printf("SCRATCH0: 0x%08X\n",R32(0x32D4));
    printf("GPU_ID: 0x%08X\n",R32(0x0000));
    printf("CC_ARRAY: 0x%08X\n",R32(0x9C1C));
    printf("SPI_PG: 0x%08X\n",R32(0x5C3C));

    /* SMN sensor scan */
    printf("\n=== SMN Sensor Dump ===\n");
    SMN_SHOW(0x03B10000,"Edge Temp");
    SMN_SHOW(0x03B10004,"Sensor 1");
    SMN_SHOW(0x03B10008,"Sensor 2");
    SMN_SHOW(0x03B1000C,"Sensor 3");
    SMN_SHOW(0x03B10010,"Sensor 4");
    SMN_SHOW(0x03B10014,"Sensor 5");
    SMN_SHOW(0x03B10018,"Sensor 6");
    SMN_SHOW(0x03B1001C,"Sensor 7");
    SMN_SHOW(0x03B10020,"Junction Temp");
    SMN_SHOW(0x03B10024,"FW Flags");
    SMN_SHOW(0x03B10028,"Memory Temp");
    SMN_SHOW(0x03B1002C,"Sensor 11");
    SMN_SHOW(0x03B10030,"Power (est)");
    SMN_SHOW(0x03B10034,"Power (soc)");
    SMN_SHOW(0x03B10038,"Power (gfx)");
    SMN_SHOW(0x03B1003C,"Power (total)");
    SMN_SHOW(0x03B10040,"Current (gfx)");
    SMN_SHOW(0x03B10044,"Current (soc)");
    SMN_SHOW(0x03B10048,"Voltage (gfx)");
    SMN_SHOW(0x03B1004C,"Voltage (soc)");
    SMN_SHOW(0x03B10050,"Energy (acc)");
    SMN_SHOW(0x03B10060,"Fan Ctrl");
    SMN_SHOW(0x03B10064,"Fan RPM");
    SMN_SHOW(0x03B10068,"Fan PWM");
    SMN_SHOW(0x03B1006C,"Fan Tach");
    SMN_SHOW(0x03B10080,"Thermal Policy");
    SMN_SHOW(0x03B10100,"Throttle Status");
    SMN_SHOW(0x03B10400,"VDDGFX");
    SMN_SHOW(0x03B10404,"VDDSOC");
    SMN_SHOW(0x03B10408,"VDDCI");
    SMN_SHOW(0x03B1040C,"VDD_MEM");

    /* Scan for live SMN in MP1 (0x03B10xxx) range */
    printf("\n=== SMU Mailbox Status ===\n");
    printf("C2PMSG_66: 0x%08X\n",smnR(0x03B10A08));
    printf("C2PMSG_82: 0x%08X\n",smnR(0x03B10A48));
    printf("C2PMSG_90: 0x%08X\n",smnR(0x03B10A68));
    printf("C2PMSG_00: 0x%08X\n",smnR(0x03B10A00));
    printf("C2PMSG_60: 0x%08X\n",smnR(0x03B10A60));
    printf("C2PMSG_40: 0x%08X\n",smnR(0x03B10A40));
    printf("C2PMSG_20: 0x%08X\n",smnR(0x03B10A20));
    printf("C2PMSG_80: 0x%08X\n",smnR(0x03B10A80));
    printf("C2PMSG_88: 0x%08X\n",smnR(0x03B10A88));

    /* Scan SMN 0x03B00000-0x03B20000 for live registers */
    printf("\n=== SMN Range Scan (live regs) ===\n");
    int count=0;
    for(uint32_t addr=0x03B00000; addr<0x03B20000; addr+=0x100){
        uint32_t v=smnR(addr);
        if(v!=0xFFFFFFFF){
            printf("  0x%08X = 0x%08X\n",addr,v);
            if(++count>=64) break;
        }
    }

    CloseHandle(h);
    printf("\nDONE\n");
    return 0;
}
