/* dcn-init-test.c — DCN 2.1 display engine probe for BC-250. Reads all display blocks, tests writability. */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

/* DCN register offsets (BAR5) — from amdbc250_dream_hw.h */
#define HUBPREQ0_PRIMARY_SURFACE_ADDRESS    0x5080
#define HUBPREQ0_FLIP_CONTROL               0x5088
#define HUBPREQ0_SURFACE_PITCH              0x508C
#define HUBPREQ0_SURFACE_DIMENSIONS         0x5090
#define HUBPREQ0_TILING_CONFIG              0x5098

#define OTG0_CONTROL                        0x6000
#define OTG0_V_TOTAL                        0x6010
#define OTG0_H_TOTAL                        0x6014
#define OTG0_V_BLANK_START_END              0x6018
#define OTG0_H_BLANK_START_END              0x601C
#define OTG0_V_SYNC_START_END               0x6020
#define OTG0_H_SYNC_START_END               0x6024
#define OTG0_CRTC_STATUS                    0x6028

#define DMCUB_SCRATCH0                      0x7000
#define DMCUB_SCRATCH1                      0x7004
#define DMCUB_INBOX0_RPTR                   0x7010
#define DMCUB_INBOX0_WPTR                   0x7014
#define DMCUB_INBOX0_BASE                   0x7018
#define DMCUB_INBOX0_SIZE                   0x701C

#define OTG_CNTL__ENABLE                    (1 << 0)

static HANDLE h;
static BOOL W32(uint32_t o, uint32_t v) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=v; return DeviceIoControl(h,IOCTL_AMDBC250_WRITE_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL); }
static uint32_t R32(uint32_t o) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=0; if(DeviceIoControl(h,IOCTL_AMDBC250_READ_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL)) return r.Value; return 0xFFFFFFFF; }

static int test_reg(const char* name, uint32_t off, uint32_t test_val){
    uint32_t orig=R32(off);
    if(orig==0xFFFFFFFF){ printf("  %-40s [0x%04X] DEAD\n",name,off); return -1; }
    W32(off,test_val); uint32_t readback=R32(off); W32(off,orig);
    if(readback==test_val) printf("  %-40s [0x%04X] WRITABLE orig=0x%08X\n",name,off,orig);
    else if(readback==orig) printf("  %-40s [0x%04X] RO orig=0x%08X\n",name,off,orig);
    else printf("  %-40s [0x%04X] PARTIAL orig=0x%08X w=0x%08X r=0x%08X\n",name,off,orig,test_val,readback);
    return 0;
}

int main(){
    setvbuf(stdout,NULL,_IONBF,0);
    h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE){printf("FAIL gle=%lu\n",GetLastError());return 1;}
    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br=0;
    ZeroMemory(&ih,sizeof(ih)); ih.MmioPhysicalBase=0xFE800000ULL; ih.MmioSize=0x80000; ih.Flags=AMDBC250_INIT_FLAG_NBIO_MAP;
    if(!DeviceIoControl(h,IOCTL_AMDBC250_INIT_HARDWARE,&ih,sizeof(ih),&ih,sizeof(ih),&br,NULL)){printf("INIT fail\n");CloseHandle(h);return 1;}

    printf("=== DCN 2.1 Display Engine Probe ===\n\n");

    printf("--- HUBPREQ0 (0x5080-0x50FF) ---\n");
    test_reg("PRIMARY_SURFACE_ADDRESS",  HUBPREQ0_PRIMARY_SURFACE_ADDRESS, 0x12345678);
    test_reg("FLIP_CONTROL",             HUBPREQ0_FLIP_CONTROL, 1);
    test_reg("SURFACE_PITCH",            HUBPREQ0_SURFACE_PITCH, 1920);
    test_reg("SURFACE_DIMENSIONS",       HUBPREQ0_SURFACE_DIMENSIONS, (1080<<16)|1920);
    test_reg("TILING_CONFIG",            HUBPREQ0_TILING_CONFIG, 0);
    test_reg("PIPE0_SURFACE_ADDR",       0x5180, 0x12345678);
    test_reg("PIPE1_SURFACE_ADDR",       0x5280, 0x12345678);
    test_reg("PIPE2_SURFACE_ADDR",       0x5380, 0x12345678);

    printf("\n--- OTG0 (timing generator, 0x6000-0x60FF) ---\n");
    test_reg("OTG_CONTROL",              OTG0_CONTROL, OTG_CNTL__ENABLE);
    test_reg("V_TOTAL",                  OTG0_V_TOTAL, 1125);
    test_reg("H_TOTAL",                  OTG0_H_TOTAL, 2200);
    test_reg("V_BLANK_START_END",        OTG0_V_BLANK_START_END, (1080<<16)|1081);
    test_reg("H_BLANK_START_END",        OTG0_H_BLANK_START_END, (1920<<16)|1921);
    test_reg("V_SYNC_START_END",         OTG0_V_SYNC_START_END, (1084<<16)|1083);
    test_reg("H_SYNC_START_END",         OTG0_H_SYNC_START_END, (2008<<16)|2000);
    test_reg("OTG_CRTC_STATUS",          OTG0_CRTC_STATUS, 0);
    test_reg("OTG1(0x6100)_CONTROL",     0x6100, OTG_CNTL__ENABLE);
    test_reg("OTG1(0x6100)_V_TOTAL",     0x6110, 1125);

    printf("\n--- DMCUB (0x7000-0x70FF) ---\n");
    test_reg("SCRATCH0",                 DMCUB_SCRATCH0, 0xDEAD);
    test_reg("SCRATCH1",                 DMCUB_SCRATCH1, 0xBEEF);
    test_reg("INBOX0_RPTR",              DMCUB_INBOX0_RPTR, 0);
    test_reg("INBOX0_WPTR",              DMCUB_INBOX0_WPTR, 0);
    test_reg("INBOX0_BASE",              DMCUB_INBOX0_BASE, 0);
    test_reg("INBOX0_SIZE",              DMCUB_INBOX0_SIZE, 0);

    printf("\n--- THM (0x8000-0x80FF) ---\n");
    test_reg("THERMAL_CTRL",             0x8000, 0);
    test_reg("CURRENT_TEMP",             0x8008, 0);
    test_reg("THERMAL_INT_ENA",          0x8050, 0);

    printf("\n--- Block scan (living register ranges) ---\n");
    uint32_t blocks[]={
        0x5000,0x5080,0x5100,0x5180,0x5200,0x5280,0x5300,0x5380,
        0x5400,0x5480,0x5500,0x5580,0x5600,0x5680,
        0x5C00,0x5D00,0x5E00,0x5F00,
        0x6000,0x6100,0x6200,0x6300,
        0x6C00,0x6D00,0x6E00,0x6F00,
        0x7000,0x7100,0x7200,
        0x8000,0x8100,
    };
    for(int i=0;i<sizeof(blocks)/sizeof(blocks[0]);i++){
        uint32_t v=R32(blocks[i]);
        if(v!=0xFFFFFFFF) printf("  [0x%04X] ALIVE = 0x%08X\n",blocks[i],v);
    }

    printf("\n--- HPD (hot plug detect, 0x5400+) ---\n");
    for(int phy=0;phy<4;phy++){
        uint32_t hpd=R32(0x5400+phy*0x100);
        if(hpd!=0xFFFFFFFF) printf("  HPD%d [0x%04X] = 0x%08X\n",phy,0x5400+phy*0x100,hpd);
    }

    printf("\n--- DIO (PHY, 0x5C00+) ---\n");
    for(int phy=0;phy<4;phy++){
        uint32_t v=R32(0x5C00+phy*0x80);
        if(v!=0xFFFFFFFF && v!=0) printf("  DIO%d [0x%04X] = 0x%08X\n",phy,0x5C00+phy*0x80,v);
    }

    printf("\n--- Active pipe detection ---\n");
    for(int pipe=0;pipe<4;pipe++){
        uint32_t otg=R32(0x6000+pipe*0x100);
        uint32_t surf=R32(0x5080+pipe*0x100);
        uint32_t dim=R32(0x5080+pipe*0x100+0x10);
        printf("  Pipe %d: otg=0x%08X surf=0x%08X dim=0x%08X%s\n",
               pipe,otg,surf,dim,(otg&1)?" (ACTIVE)":"");
    }

    printf("\n=== Done ===\n");
    CloseHandle(h);
    return 0;
}
