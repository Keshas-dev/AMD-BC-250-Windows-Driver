/* smu-monitor.c — SMU telemetry monitoring tool for BC-250. Reads freq, voltage, temp, features, WGPs. */
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
    for(int i=0;i<500;i++){ uint32_t st=smnR(0x03B10A68); if(st==1) return 1; if(st==0xFF) return -1; if(st==0xFE) return -2; if(st==0xFD) return -3; if(st==0xFC) return -4; Sleep(1); }
    return -100;
}
static uint32_t q0_arg(){ return smnR(0x03B10A48); }

static int q3(uint32_t msg,uint32_t arg){
    smnW(0x03B10A80,0); smnW(0x03B10A88,arg); smnW(0x03B10A20,msg);
    for(int i=0;i<500;i++){ uint32_t st=smnR(0x03B10A80); if(st==1) return 1; if(st==0xFF) return -1; if(st==0xFE) return -2; if(st==0xFD) return -3; if(st==0xFC) return -4; Sleep(1); }
    return -100;
}
static uint32_t q3_arg(){ return smnR(0x03B10A88); }

/* Feature bit names */
static const char* feat_name(int bit){
    switch(bit){
        case 0: return "GFXCLK_DPM";
        case 1: return "SOCLK_DPM";
        case 2: return "GFXOFF";
        case 3: return "CG";
        case 4: return "PG";
        case 5: return "DS_GFXCLK";
        case 6: return "DS_SOCLK";
        case 7: return "DS_LCLK";
        case 8: return "DS_FCLK";
        case 9: return "SOCCLK_DPM";
        case 10: return "LCLK_DPM";
        case 11: return "FCLK_DPM";
        case 16: return "FSM";
        default: return NULL;
    }
}

static void print_features(uint32_t feat){
    printf("  Features (0x%08X):\n",feat);
    for(int b=0;b<32;b++){
        if(feat & (1<<b)){
            const char* n=feat_name(b);
            if(n) printf("    bit %2d (%s) = ON\n",b,n);
            else printf("    bit %2d = ON\n",b);
        }
    }
}

static int vid_to_mv(int vid){
    return (int)((-vid*0.00625 + 1.55) * 1000 + 0.5);
}

int main(){
    setvbuf(stdout,NULL,_IONBF,0);
    h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE){printf("FAIL gle=%lu\n",GetLastError());return 1;}
    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br=0;
    ZeroMemory(&ih,sizeof(ih)); ih.MmioPhysicalBase=0xFE800000ULL; ih.MmioSize=0x80000; ih.Flags=AMDBC250_INIT_FLAG_NBIO_MAP;
    if(!DeviceIoControl(h,IOCTL_AMDBC250_INIT_HARDWARE,&ih,sizeof(ih),&ih,sizeof(ih),&br,NULL)){printf("INIT fail\n");return 1;}

    /* Verify SMU alive */
    int r=q3(1,123); uint32_t tst=q3_arg();
    if(r!=1||tst!=124){printf("SMU dead (status=%d resp=%u)\n",r,tst);CloseHandle(h);return 1;}

    /* Get SMU version once */
    r=q0(2,0); uint32_t ver=q0_arg();
    r=q0(3,0); uint32_t drv=q0_arg();
    printf("SMU v%d.%d.%d (driver_if=%d)\n",(ver>>20)&0xFF,(ver>>8)&0xFF,ver&0xFF,drv);

    /* Single shot read */
    printf("\n=== Single Shot ===\n");

    r=q0(0x37,0); uint32_t freq=q0_arg();
    r=q0(0x38,0); int vid=q0_arg(); /* may return -2 if unsupported */
    r=q0(0x1E,0); uint32_t wgp=q0_arg();
    r=q0(0x3D,0); uint32_t feat=q0_arg();
    r=q0(0x0F,0); uint32_t gfxclk=q0_arg();

    printf("Frequency: %u MHz",freq);
    if(gfxclk!=freq) printf(" (QueryGfxclk=%u MHz)",gfxclk);
    printf("\n");

    if(vid>=0 && vid<=255) printf("Voltage VID=%d (%d mV)\n",vid,vid_to_mv(vid));
    else printf("Voltage VID=%d (unsupported)\n",vid);
    printf("Active WGPs: %u\n",wgp);
    print_features(feat);

    /* Read THM temperature register (may be frozen) */
    uint32_t thm_ctrl=R32(0x8000);
    uint32_t thm_temp=R32(0x8008);
    if(thm_ctrl!=0xFFFFFFFF && thm_temp!=0xFFFFFFFF){
        printf("THM temp: %u (raw 0x%08X, ctrl=0x%08X)\n",thm_temp&0xFF,thm_temp,thm_ctrl);
    } else {
        printf("THM registers frozen (ctrl=0x%08X temp=0x%08X)\n",thm_ctrl,thm_temp);
    }

    /* Try reading via SMN for more telemetry */
    uint32_t smn_temps[]={0x03B10000,0x03B10004,0x03B10008,0x03B1000C,0x03B10020};
    printf("SMN temperature probes:\n");
    for(int i=0;i<5;i++){
        uint32_t v=smnR(smn_temps[i]);
        if(v!=0 && v!=0xFFFFFFFF) printf("  SMN[0x%08X] = 0x%08X\n",smn_temps[i],v);
    }

    /* Live monitoring loop */
    printf("\n=== Live Monitoring (5 sec, 1s interval) ===\n");
    printf("%-6s %8s %8s %6s %8s %4s\n","Time","Freq","Volt","WGPs","Feat","Temp");
    for(int i=0;i<5;i++){
        r=q0(0x37,0); freq=q0_arg();
        r=q0(0x38,0); vid=q0_arg();
        r=q0(0x1E,0); wgp=q0_arg();
        r=q0(0x3D,0); feat=q0_arg();
        printf("t=%1ds %8u %7dmV %6u 0x%06X",i+1,freq,vid>=0?vid_to_mv(vid):0,wgp,feat);
        printf("\n");
        Sleep(1000);
    }

    CloseHandle(h);
    printf("\n=== Done ===\n");
    return 0;
}
