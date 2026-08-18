/* wgp-unlock-test.c - BC-250 WGP unlock: try all paths to enable 3D */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE h;
static int g_smu_ok = 0;

static BOOL W32(uint32_t o, uint32_t v) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=v; return DeviceIoControl(h,IOCTL_AMDBC250_WRITE_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL); }
static uint32_t R32(uint32_t o) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=0; if(DeviceIoControl(h,IOCTL_AMDBC250_READ_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL)) return r.Value; return 0xFFFFFFFF; }

#define SPI_PG  0x5C3C
#define CC_ARRAY 0x9C1C
#define RLC_PG  0x3D64
#define GRBM_IDX 0x34D0
#define GRBM_BCAST 0xE0000000

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

/* SMU Q3 msg 0x98: ungated SMU-privileged SMN write (writes 0x00FF to addr) */
static int smu98(uint32_t smnAddr){
    return q3(0x98, smnAddr);
}

static void showState(const char *tag){
    uint32_t spi=R32(SPI_PG), cc=R32(CC_ARRAY), rlc=R32(RLC_PG), grbm=R32(0x2000);
    printf("  [%s] SPI_PG=0x%08X CC=0x%08X RLC=0x%08X GRBM_STATUS=0x%08X\n", tag, spi, cc, rlc, grbm);
}

/* Path 3: direct BAR5 writes with various GRBM_GFX_INDEX settings */
static int path3_directWrites(void){
    printf("\n=== PATH 3: Direct BAR5 writes ===\n");
    showState("before");

    /* 3a: broadcast */
    printf("  3a: GRBM broadcast then SPI=0x1F,CC=0xFFE00000,RLC=0x1F\n");
    W32(GRBM_IDX, GRBM_BCAST);
    W32(SPI_PG, 0x1F); W32(CC_ARRAY, 0xFFE00000); W32(RLC_PG, 0x1F);
    showState("after 3a");

    /* 3b: per-bank SE0/SH0 */
    W32(GRBM_IDX, 0x00000000);
    W32(SPI_PG, 0x1F); W32(CC_ARRAY, 0xFFE00000); W32(RLC_PG, 0x1F);
    uint32_t s00=R32(SPI_PG);
    printf("  3b: SE0/SH0 SPI readback=0x%08X %s\n", s00, (s00&0x1F)==0x1F?"STUCK!":"locked");

    /* 3c: per-bank SE0/SH1 */
    W32(GRBM_IDX, 0x01000000);
    W32(SPI_PG, 0x1F); W32(CC_ARRAY, 0xFFE00000); W32(RLC_PG, 0x1F);
    uint32_t s01=R32(SPI_PG);
    printf("  3c: SE0/SH1 SPI readback=0x%08X %s\n", s01, (s01&0x1F)==0x1F?"STUCK!":"locked");

    /* 3d: per-bank SE1/SH0 */
    W32(GRBM_IDX, 0x10000000);
    W32(SPI_PG, 0x1F); W32(CC_ARRAY, 0xFFE00000); W32(RLC_PG, 0x1F);
    uint32_t s10=R32(SPI_PG);
    printf("  3d: SE1/SH0 SPI readback=0x%08X %s\n", s10, (s10&0x1F)==0x1F?"STUCK!":"locked");

    /* 3e: per-bank SE1/SH1 */
    W32(GRBM_IDX, 0x11000000);
    W32(SPI_PG, 0x1F); W32(CC_ARRAY, 0xFFE00000); W32(RLC_PG, 0x1F);
    uint32_t s11=R32(SPI_PG);
    printf("  3e: SE1/SH1 SPI readback=0x%08X %s\n", s11, (s11&0x1F)==0x1F?"STUCK!":"locked");

    W32(GRBM_IDX, GRBM_BCAST);
    showState("after 3");
    return 0;
}

/* Path 1: SMU 0x98 to candidate SMN addresses */
static int path1_smu98(void){
    printf("\n=== PATH 1: SMU Q3 msg 0x98 (ungated SMN write) ===\n");
    if(!g_smu_ok){printf("  SMU not OK, skip\n");return 0;}

    /* Candidate SMN addresses to try for SPI_PG alias.
     * We look for an address where readback changes after 0x98 write.
     * Try addresses near known working core-unlock region and GC-ish spots. */
    uint32_t candidates[] = {
        0x0115A870,  /* core-unlock mask (known working) - for sanity */
        0x0115A874,  /* adjacent */
        0x0115A878,  /* adjacent */
        0x0115A800,  /* page start */
        0x0115B000,  /* next page */
        0x01160000,  /* next region */
        0x01170000,
        0x01180000,
        0x02000000,  /* high SMN */
        0x03000000,  /* SMU-related */
        0x03B10000,  /* SMU mailbox base */
        0x00000000,  /* low - DANGER, skip if risky */
    };
    int n = sizeof(candidates)/sizeof(candidates[0]);
    int found = 0;

    for(int i=0; i<n; i++){
        uint32_t addr = candidates[i];
        if(addr == 0) continue; /* skip 0 - dangerous */
        uint32_t before = smnR(addr);
        int r = smu98(addr);
        uint32_t after = smnR(addr);
        int changed = (after != before);
        printf("  addr=0x%08X before=0x%08X 0x98->%d after=0x%08X %s\n",
            addr, before, r, after, changed?"*** CHANGED ***":"");
        if(changed && i==0) printf("    (sanity: core-unlock region responds to 0x98)\n");
    }
    return found;
}

/* Path 2: PSP mailbox - try GFX_CMD IDs for power/WGP */
static int path2_psp(void){
    printf("\n=== PATH 2: PSP mailbox GFX_CMD scan ===\n");
    printf("  (PSP driver not installed - scan via GPU driver PSP proxy if available)\n");
    /* The GPU driver has PSP proxy IOCTLs. Try a few known GFX_CMD IDs. */
    /* GFX_CMD_ID_LOAD_IP_FW=0x06 is known. Try power-related IDs. */
    return 0;
}

/* Path 4: VBIOS - check if VBIOS can be read for init sequences */
static int path4_vbios(void){
    printf("\n=== PATH 4: VBIOS-based unlock ===\n");
    printf("  (VBIOS fetch from ACPI VFCT not implemented in driver yet)\n");
    return 0;
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE){printf("FAIL: open GPU device err=%lu\n",GetLastError());return 1;}

    printf("=== INIT_HARDWARE ===\n");
    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br=0;
    memset(&ih,0,sizeof(ih)); ih.MmioPhysicalBase=0xFE800000ULL; ih.MmioSize=0x80000; ih.Flags=AMDBC250_INIT_FLAG_NBIO_MAP;
    if(!DeviceIoControl(h,IOCTL_AMDBC250_INIT_HARDWARE,&ih,sizeof(ih),&ih,sizeof(ih),&br,NULL)){printf("INIT fail\n");return 1;}
    printf("OK\n");

    /* SMU sanity */
    int r=q3(1,123); uint32_t tst=q3_arg();
    printf("SMU Q3 test: status=%d resp=%u\n",r,tst);
    g_smu_ok = (r==1 && tst==124);

    showState("initial");

    /* Disable GFXOFF first (known working) */
    printf("\n=== Disable GFXOFF+CG+PG ===\n");
    /* Q2 disable_smu_features: msg=6, arg=mask (bit2=GFXOFF,bit3=CG,bit4=PG) */
    smnW(0x03B10564,0); smnW(0x03B10998,(1<<2)|(1<<3)|(1<<4)); smnW(0x03B1099C,0); smnW(0x03B10528,6);
    Sleep(200);
    uint32_t feat; q0(0x3D,0); feat=q0_arg();
    printf("Features after disable: 0x%08X (GFXOFF=%d CG=%d PG=%d)\n", feat, (feat>>2)&1,(feat>>3)&1,(feat>>4)&1);

    showState("after gfxoff off");

    path3_directWrites();
    path1_smu98();
    path2_psp();
    path4_vbios();

    printf("\n=== DONE ===\n");
    CloseHandle(h);
    return 0;
}
