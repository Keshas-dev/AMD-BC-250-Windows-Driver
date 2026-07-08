/* smu-monitor.c — Full SMU telemetry tool for BC-250. CSV logging, fan, power, live monitoring. */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
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

static int vid_to_mv(int vid){
    return (int)((-vid*0.00625 + 1.55) * 1000 + 0.5);
}

static const char* feat_name(int bit){
    switch(bit){
        case 0: return "GFXCLK_DPM"; case 1: return "SOCLK_DPM"; case 2: return "GFXOFF";
        case 3: return "CG"; case 4: return "PG"; case 5: return "DS_GFXCLK"; case 6: return "DS_SOCLK";
        case 7: return "DS_LCLK"; case 8: return "DS_FCLK"; case 9: return "SOCCLK_DPM";
        case 10: return "LCLK_DPM"; case 11: return "FCLK_DPM"; case 16: return "FSM";
        default: return NULL;
    }
}

static void print_features(uint32_t feat){
    printf("  Features (0x%08X):\n",feat);
    for(int b=0;b<32;b++) if(feat & (1<<b)){
        const char* n=feat_name(b);
        if(n) printf("    bit %2d (%s) = ON\n",b,n); else printf("    bit %2d = ON\n",b);
    }
}

/* Probe a range of SMN addresses for telemetry data */
#define SMN_PROBE(addr,desc) do{ uint32_t v=smnR(addr); if(v!=0xFFFFFFFF) printf("  %s [0x%08X] = 0x%08X\n",desc,addr,v); }while(0)

static volatile int g_stop=0;
static BOOL WINAPI ctrl_handler(DWORD dwCtrlType){
    (void)dwCtrlType; g_stop=1; return TRUE;
}

int main(){
    setvbuf(stdout,NULL,_IONBF,0);
    h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE){printf("FAIL gle=%lu\n",GetLastError());return 1;}
    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br=0;
    ZeroMemory(&ih,sizeof(ih)); ih.MmioPhysicalBase=0xFE800000ULL; ih.MmioSize=0x80000; ih.Flags=AMDBC250_INIT_FLAG_NBIO_MAP;
    if(!DeviceIoControl(h,IOCTL_AMDBC250_INIT_HARDWARE,&ih,sizeof(ih),&ih,sizeof(ih),&br,NULL)){printf("INIT fail\n");CloseHandle(h);return 1;}

    int r = 0;
    /* Use q3 for test message */
    smnW(0x03B10A80, 0); smnW(0x03B10A88, 123); smnW(0x03B10A20, 1);
    for(int wi=0;wi<500;wi++){ uint32_t st=smnR(0x03B10A80); if(st==1){r=1;break;} if(st==0xFF){r=-1;break;} Sleep(1); }
    uint32_t tst=smnR(0x03B10A88);
    if(r!=1||tst!=124){printf("SMU dead (status=%d resp=%u)\n",r,tst);CloseHandle(h);return 1;}

    r=q0(2,0); uint32_t ver=q0_arg();
    r=q0(3,0); uint32_t drv=q0_arg();
    /* SMU version format: bits[23:16]=major, bits[15:8]=minor, bits[7:0]=patch */
    printf("SMU v%d.%d.%d (driver_if=%d)\n",(ver>>16)&0xFF,(ver>>8)&0xFF,ver&0xFF,drv);

    /* Single shot read */
    printf("\n=== Single Shot ===\n");
    r=q0(0x37,0); uint32_t freq=q0_arg();
    r=q0(0x38,0); int vid=q0_arg();
    r=q0(0x1E,0); uint32_t wgp=q0_arg();
    r=q0(0x3D,0); uint32_t feat=q0_arg();
    r=q0(0x0F,0); uint32_t gfxclk=q0_arg();
    printf("Frequency: %u MHz%s",freq,(gfxclk!=freq)?" (contradicted by GetGfxFrequency)":"");
    if(gfxclk!=freq) printf(" QueryGfxclk=%u MHz",gfxclk);
    printf("\n");
    if(vid>=0 && vid<=255) printf("Voltage VID=%d (%d mV)\n",vid,vid_to_mv(vid));
    else printf("Voltage VID=%d (unsupported)\n",vid);
    printf("Active WGPs: %u\n",wgp);
    print_features(feat);

    /* GRBM_STATUS via GPU BAR5 */
    uint32_t grbm=R32(0x3260);
    printf("GRBM_STATUS(0x3260)=0x%08X\n",grbm);
    printf("  GUI_ACTIVE=%d, IA_BUSY=%d, WD_BUSY=%d\n",(grbm>>31)&1,(grbm>>19)&1,(grbm>>18)&1);

    /* SMN telemetry probe */
    printf("\n=== SMN Telemetry Probe ===\n");
    SMN_PROBE(0x03B10000,"Edge Temp");
    SMN_PROBE(0x03B10004,"Sensor 1");
    SMN_PROBE(0x03B10008,"Sensor 2");
    SMN_PROBE(0x03B1000C,"Sensor 3");
    SMN_PROBE(0x03B10020,"Junction Temp");
    SMN_PROBE(0x03B10024,"FW Flags");
    SMN_PROBE(0x03B10028,"Memory Temp");
    SMN_PROBE(0x03B10030,"Power (est)");
    SMN_PROBE(0x03B10060,"Fan Ctrl");
    SMN_PROBE(0x03B10064,"Fan RPM");
    SMN_PROBE(0x03B10068,"Fan PWM");
    SMN_PROBE(0x03B1006C,"Fan Tach");
    SMN_PROBE(0x03B10080,"Thermal Policy");
    SMN_PROBE(0x03B10400,"VDDGFX");
    SMN_PROBE(0x03B10404,"VDDSOC");

    /* Open CSV log */
    SYSTEMTIME st; GetLocalTime(&st);
    char csvname[256];
    sprintf(csvname,"smu-telemetry-%04d%02d%02d-%02d%02d%02d.csv",
        st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond);
    FILE*csv=fopen(csvname,"w");
    if(csv){
        fprintf(csv,"Time_s,Freq_MHz,VID,mV,WGPs,Features,GRBM_ACTIVE,GRBM_IA,GRBM_WD,"
                     "SMN_Temp0,SMN_Temp20,SMN_Temp28,SMN_Power,SMN_FanRPM,SMN_FanPWM\n");
        printf("\n=== CSV Logging to %s ===\n",csvname);
    } else {
        printf("\n=== Live Monitoring (no CSV) ===\n");
    }

    /* Live monitoring loop */
    SetConsoleCtrlHandler(ctrl_handler,TRUE);
    printf("%-4s %6s %5s %4s %6s %3s %3s %3s %s\n",
           "t(s)","Freq","V(mV)","WGP","Feat","G_A","I_B","W_B","Temps");
    int i=0;
    while(!g_stop){
        r=q0(0x37,0); freq=q0_arg();
        r=q0(0x38,0); vid=q0_arg();
        r=q0(0x1E,0); wgp=q0_arg();
        r=q0(0x3D,0); feat=q0_arg();
        grbm=R32(0x3260);

        uint32_t smn_temp0=smnR(0x03B10000);
        uint32_t smn_temp20=smnR(0x03B10020);
        uint32_t smn_temp28=smnR(0x03B10028);
        uint32_t smn_power=smnR(0x03B10030);
        uint32_t smn_fanrpm=smnR(0x03B10064);
        uint32_t smn_fanpwm=smnR(0x03B10068);

        int mv=(vid>=0&&vid<=255)?vid_to_mv(vid):0;
        printf("%4d %6u %5d %4u 0x%06X %3d %3d %3d  0x%02X/0x%02X/0x%02X\n",
               i++,freq,mv,wgp,feat,
               (grbm>>31)&1,(grbm>>19)&1,(grbm>>18)&1,
               smn_temp0&0xFF,smn_temp20&0xFF,smn_temp28&0xFF);

        if(csv){
            fprintf(csv,"%d,%u,%d,%d,%u,0x%08X,%d,%d,%d,"
                       "0x%02X,0x%02X,0x%02X,0x%08X,0x%08X,0x%08X\n",
                    i-1,freq,vid,mv,wgp,feat,
                    (grbm>>31)&1,(grbm>>19)&1,(grbm>>18)&1,
                    smn_temp0&0xFF,smn_temp20&0xFF,smn_temp28&0xFF,
                    smn_power,smn_fanrpm,smn_fanpwm);
            fflush(csv);
        }
        Sleep(1000);
    }

    if(csv) fclose(csv);
    CloseHandle(h);
    printf("\n=== Done (%d samples) ===\n",i);
    return 0;
}
