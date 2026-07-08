/* gfxoff-kill-v2.c — disable GFXOFF+CG+PG, try compute dispatch, check all probe points */
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

static int q2(uint32_t msg,uint32_t arg,uint32_t arg_h){
    smnW(0x03B10564,0); smnW(0x03B10998,arg); smnW(0x03B1099C,arg_h); smnW(0x03B10528,msg);
    for(int i=0;i<500;i++){ uint32_t st=smnR(0x03B10564); if(st==1) return 1; if(st==0xFF) return -1; if(st==0xFE) return -2; if(st==0xFD) return -3; if(st==0xFC) return -4; Sleep(1); }
    return -100;
}
static uint32_t q2_arg(){ return smnR(0x03B10998); }

static int q3(uint32_t msg,uint32_t arg){
    smnW(0x03B10A80,0); smnW(0x03B10A88,arg); smnW(0x03B10A20,msg);
    for(int i=0;i<500;i++){ uint32_t st=smnR(0x03B10A80); if(st==1) return 1; if(st==0xFF) return -1; if(st==0xFE) return -2; if(st==0xFD) return -3; if(st==0xFC) return -4; Sleep(1); }
    return -100;
}
static uint32_t q3_arg(){ return smnR(0x03B10A88); }

static int mv_to_vid(int mv){ return (int)((1.55 - (double)mv/1000.0) / 0.00625 + 0.5); }

/* Compute dispatch initiator bits */
#define DISPATCH_VALID          0x00000002  /* W1C - valid dispatch */
#define COMPUTE_SHADER_EN       0x00000001
#define USE_THREAD_DIM          0x00008000

int main(){
    setvbuf(stdout,NULL,_IONBF,0);
    h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE){printf("FAIL gle=%lu\n",GetLastError());return 1;}
    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br=0;
    ZeroMemory(&ih,sizeof(ih)); ih.MmioPhysicalBase=0xFE800000ULL; ih.MmioSize=0x80000; ih.Flags=AMDBC250_INIT_FLAG_NBIO_MAP;
    if(!DeviceIoControl(h,IOCTL_AMDBC250_INIT_HARDWARE,&ih,sizeof(ih),&ih,sizeof(ih),&br,NULL)){printf("INIT fail\n");return 1;}

    printf("=== GFXOFF KILL V2: Disable all power gating + try compute ===\n\n");

    /* ===== PHASE 0: SMU verification ===== */
    int r=q3(1,123); uint32_t tst=q3_arg();
    printf("SMU Q3 test(123): status=%d resp=%u\n",r,tst);
    if(r!=1||tst!=124){printf("SMU dead\n");CloseHandle(h);return 1;}

    /* Save initial state */
    uint32_t savedFeat=q0_arg();
    r=q0(0x37,0); uint32_t savedFreq=q0_arg();
    r=q0(0x1E,0); uint32_t savedWgp=q0_arg();
    printf("Initial: Freq=%u Wgp=%u\n",savedFreq,savedWgp);

    /* Save BAR5 registers we'll modify */
    uint32_t savedCcCfg=R32(0x9C1C);
    uint32_t savedSpiPgm=R32(0x5C3C);
    uint32_t savedSpiMask=R32(0x34FC);
    printf("Saved: CC_ARRAY(0x9C1C)=0x%08X SPI_PG(0x5C3C)=0x%08X SPI_MASK(0x34FC)=0x%08X\n",
        savedCcCfg,savedSpiPgm,savedSpiMask);

    /* ===== PHASE 1: Probe ALL GRBM status registers ===== */
    printf("\n=== PHASE 1: GRBM_STATUS Register Probe ===\n");
    uint32_t grbm_probes[]={0x2000,0x2004,0x3260,0x3264,0x3268,0x326C,0x3270,0x3274,0x34D0,0x34D4,0x34D8,0x34DC};
    for(int i=0;i<sizeof(grbm_probes)/sizeof(grbm_probes[0]);i++){
        uint32_t v=R32(grbm_probes[i]);
        printf("  0x%04X = 0x%08X%s\n",grbm_probes[i],v,v!=0xFFFFFFFF&&v!=0?" <-- non-zero":"");
    }

    /* ===== PHASE 2: Probe ALL compute registers ===== */
    printf("\n=== PHASE 2: COMPUTE Register Probe ===\n");
    uint32_t comp_addrs[]={0x80E0,0x80E4,0x80E8,0x80EC,0x80F0,0x80F4,0x80F8,0x80FC,0x8100,0x8104,0x8110,0x8114,0x8128,0x812C,0x8138,0x8140,0x81E0};
    const char* comp_names[]={"DISPATCH_INIT","DIM_X","DIM_Y","DIM_Z","START_X","START_Y","START_Z","NUM_THREAD_X","NUM_THREAD_Y","NUM_THREAD_Z","PGM_LO","PGM_HI","PGM_RSRC1","PGM_RSRC2","STATIC_THREAD_MGMT","TMPRING_SIZE","USER_DATA_0"};
    for(int i=0;i<sizeof(comp_addrs)/sizeof(comp_addrs[0]);i++){
        uint32_t v=R32(comp_addrs[i]);
        printf("  %-20s 0x%04X = 0x%08X%s\n",comp_names[i],comp_addrs[i],v,v!=0xFFFFFFFF?"":" (dead)");
    }

    /* ===== PHASE 3: Probe CP_HQD registers ===== */
    printf("\n=== PHASE 3: CP_HQD Register Probe ===\n");
    uint32_t hqd_addrs[]={0x9104,0x9108,0x910C,0x9110,0x9124,0x9128,0x912C,0x9148,0x91DC};
    const char* hqd_names[]={"MQD_BASE","MQD_BASE_HI","HQD_ACTIVE","VMID","PQ_BASE","PQ_BASE_HI","PQ_RPTR","PQ_CONTROL","WPTR_LO"};
    for(int i=0;i<sizeof(hqd_addrs)/sizeof(hqd_addrs[0]);i++){
        uint32_t v=R32(hqd_addrs[i]);
        printf("  %-20s 0x%04X = 0x%08X\n",hqd_names[i],hqd_addrs[i],v);
    }

    /* ===== PHASE 4: Disable ALL power gating ===== */
    printf("\n=== PHASE 4: Disable ALL power gating ===\n");
    printf("Initial features: 0x%08X\n",savedFeat);

    /* Try Q2 disable: GFXOFF(bit2) + CG(bit3) + PG(bit4) */
    uint32_t disable_mask = (1<<2) | (1<<3) | (1<<4);
    printf("Disabling features mask=0x%02X (GFXOFF+CG+PG)...\n",disable_mask);
    r=q2(6,disable_mask,0);
    printf("Q2 disable_smu_features -> %d\n",r);
    Sleep(200);

    r=q0(0x3D,0); uint32_t feat2=q0_arg();
    printf("Features after: 0x%08X\n",feat2);
    printf("  GFXOFF(bit2)=%d CG(bit3)=%d PG(bit4)=%d\n",
        (feat2>>2)&1,(feat2>>3)&1,(feat2>>4)&1);

    /* Write CC_ARRAY_CONFIG to enable all SEs/SHs/CUs */
    printf("\nWriting CC_ARRAY_CONFIG(0x9C1C)=0xFFE00000...\n");
    W32(0x9C1C,0xFFE00000);
    printf("  Reads back: 0x%08X\n",R32(0x9C1C));

    /* Write SPI PG mask to enable all WGPs */
    printf("Writing SPI_PG_ENABLE_STATIC_WGP_MASK(0x5C3C)=0xFFFFFFFF...\n");
    W32(0x5C3C,0xFFFFFFFF);
    printf("  Reads back: 0x%08X\n",R32(0x5C3C));

    /* Write SPI_PG_MASK (0x34FC) */
    printf("Writing SPI_PG_MASK(0x34FC)=0xFFFFFFFF...\n");
    W32(0x34FC,0xFFFFFFFF);
    printf("  Reads back: 0x%08X\n",R32(0x34FC));

    /* ===== PHASE 5: Force frequency with GFXOFF+CG+PG off ===== */
    printf("\n=== PHASE 5: Force 1500MHz ===\n");
    r=q3(0x8C,80);     printf("set_gpu_max_temp(80C)=%d\n",r);
    r=q0(0x3A,0);      printf("unforce_gfx_freq=%d\n",r);
    r=q0(0x3C,0);      printf("unforce_gfx_vid=%d\n",r);

    int vid=mv_to_vid(850);
    r=q3(0x1E,3);      printf("set_perf_profile(3)=%d\n",r); Sleep(50);
    r=q0(0x3B,vid);    printf("force_gfx_vid(850mV->VID=%d)=%d\n",vid,r); Sleep(50);
    r=q0(0x39,1500);   printf("force_gfx_freq(1500MHz)=%d\n",r); Sleep(200);

    r=q0(0x37,0); uint32_t freq2=q0_arg();
    r=q0(0x1E,0); uint32_t wgp2=q0_arg();
    r=q0(0x3D,0); uint32_t feat3=q0_arg();
    printf("After force: Freq=%u Wgp=%u Feat=0x%08X\n",freq2,wgp2,feat3);

    if(wgp2>0) printf("*** WGPs ACTIVE: %u ***\n",wgp2);
    else printf("WGPs still 0\n");

    /* ===== PHASE 6: Re-probe registers after power gating off ===== */
    printf("\n=== PHASE 6: Re-probe Registers ===\n");
    printf("GRBM_STATUS probes after:\n");
    for(int i=0;i<sizeof(grbm_probes)/sizeof(grbm_probes[0]);i++){
        uint32_t v=R32(grbm_probes[i]);
        if(v!=0) printf("  0x%04X = 0x%08X\n",grbm_probes[i],v);
    }

    printf("\nCOMPUTE registers after:\n");
    for(int i=0;i<sizeof(comp_addrs)/sizeof(comp_addrs[0]);i++){
        uint32_t v=R32(comp_addrs[i]);
        printf("  %-20s 0x%04X = 0x%08X\n",comp_names[i],comp_addrs[i],v);
    }

    printf("\nCP_HQD registers after:\n");
    for(int i=0;i<sizeof(hqd_addrs)/sizeof(hqd_addrs[0]);i++){
        uint32_t v=R32(hqd_addrs[i]);
        printf("  %-20s 0x%04X = 0x%08X\n",hqd_names[i],hqd_addrs[i],v);
    }

    /* ===== PHASE 7: Try compute dispatch trigger ===== */
    printf("\n=== PHASE 7: Compute Dispatch Trigger ===\n");

    /* Write GRBM_GFX_INDEX to broadcast */
    W32(0x34D0,0xE0000000);

    /* Write compute registers */
    W32(0x80E4,1);  printf("DIM_X=1 reads=0x%08X\n",R32(0x80E4));
    W32(0x80E8,1);  printf("DIM_Y=1 reads=0x%08X\n",R32(0x80E8));
    W32(0x80EC,1);  printf("DIM_Z=1 reads=0x%08X\n",R32(0x80EC));
    W32(0x80F0,0);  printf("START_X=0 reads=0x%08X\n",R32(0x80F0));
    W32(0x80F4,0);  printf("START_Y=0 reads=0x%08X\n",R32(0x80F4));
    W32(0x80F8,0);  printf("START_Z=0 reads=0x%08X\n",R32(0x80F8));
    W32(0x80FC,32); printf("NUM_THREAD_X=32 reads=0x%08X\n",R32(0x80FC));
    W32(0x8100,1);  printf("NUM_THREAD_Y=1 reads=0x%08X\n",R32(0x8100));
    W32(0x8104,1);  printf("NUM_THREAD_Z=1 reads=0x%08X\n",R32(0x8104));
    W32(0x8110,0xC01000); printf("PGM_LO=0xC01000 reads=0x%08X\n",R32(0x8110));
    W32(0x8114,0);  printf("PGM_HI=0 reads=0x%08X\n",R32(0x8114));
    W32(0x8128,0);  printf("PGM_RSRC1=0 reads=0x%08X\n",R32(0x8128));
    W32(0x812C,0);  printf("PGM_RSRC2=0 reads=0x%08X\n",R32(0x812C));

    /* GRBM status before dispatch */
    uint32_t grbm_before=R32(0x3260);
    printf("GRBM_STATUS before dispatch = 0x%08X\n",grbm_before);

    /* Write scratch to detect execution */
    printf("Scratch(0x32D4) before = 0x%08X\n",R32(0x32D4));
    W32(0x32D4,0x585042);
    printf("Scratch(0x32D4) after write = 0x%08X\n",R32(0x32D4));

    /* Trigger dispatch */
    printf("\n>>> Triggering DISPATCH_INITIATOR (0x80E0) with COMPUTE_SHADER_EN | VALID <<<\n");
    uint32_t dispatch_val = COMPUTE_SHADER_EN | DISPATCH_VALID;
    W32(0x80E0,dispatch_val);

    uint32_t init_after=R32(0x80E0);
    printf("DISPATCH_INITIATOR after = 0x%08X (VALID consumed=%s)\n",
        init_after,(init_after&DISPATCH_VALID)?"NO":"YES");

    /* Poll for activity */
    for(int i=0;i<50;i++){
        uint32_t gs=R32(0x3260);
        uint32_t sc=R32(0x32D4);
        uint32_t di=R32(0x80E0);
        if(gs!=grbm_before||sc!=0x585042||di!=init_after){
            printf("  t=%dms: GRBM=0x%08X Scratch=0x%08X DI=0x%08X\n",i*10,gs,sc,di);
        }
        if(sc!=0x585042){printf("*** SCRATCH CHANGED! Shader executed! ***\n");break;}
        if(gs!=grbm_before){printf("  GRBM status changed at t=%dms\n",i*10);}
        Sleep(10);
    }

    uint32_t grbm_after=R32(0x3260);
    uint32_t scratch_after=R32(0x32D4);
    printf("After poll (500ms): GRBM=0x%08X Scratch=0x%08X\n",grbm_after,scratch_after);
    if(scratch_after!=0x585042) printf("*** SCRATCH CHANGED! Shader executed! (now=0x%08X) ***\n",scratch_after);

    /* Try again with USE_THREAD_DIM */
    printf("\n>>> Triggering with USE_THREAD_DIM | VALID <<<\n");
    W32(0x80E0,USE_THREAD_DIM | DISPATCH_VALID);
    init_after=R32(0x80E0);
    printf("DISPATCH_INITIATOR after = 0x%08X (VALID consumed=%s)\n",
        init_after,(init_after&DISPATCH_VALID)?"NO":"YES");
    Sleep(200);
    printf("GRBM_STATUS = 0x%08X Scratch = 0x%08X\n",R32(0x3260),R32(0x32D4));

    /* Try COMPUTE_SHADER_EN alone (no VALID) */
    printf("\n>>> Triggering COMPUTE_SHADER_EN alone <<<\n");
    W32(0x80E0,COMPUTE_SHADER_EN);
    init_after=R32(0x80E0);
    printf("DISPATCH_INITIATOR after = 0x%08X\n",init_after);
    Sleep(200);
    printf("GRBM_STATUS = 0x%08X\n",R32(0x3260));

    /* ===== PHASE 8: Try RequestActiveWgp (READ-ONLY - query only) ===== */
    printf("\n=== PHASE 8: After-state checks ===\n");
    r=q0(0x1E,0); wgp2=q0_arg();
    printf("QueryActiveWgp = %u\n",wgp2);
    r=q0(0x37,0); printf("Freq = %u\n",q0_arg());
    r=q0(0x3D,0); printf("Features = 0x%08X\n",q0_arg());

    /* ===== RESTORE ===== */
    printf("\n=== Restoring ===\n");
    W32(0x9C1C,savedCcCfg);
    W32(0x5C3C,savedSpiPgm);
    W32(0x34FC,savedSpiMask);
    q2(5,disable_mask,0); /* re-enable GFXOFF+CG+PG */
    Sleep(100);
    q0(0x3A,0); q0(0x3C,0);
    r=q0(0x37,0); printf("Final Freq=%u\n",q0_arg());
    r=q0(0x3D,0); printf("Final Feat=0x%08X\n",q0_arg());
    r=q0(0x1E,0); printf("Final Wgp=%u\n",q0_arg());

    CloseHandle(h);
    printf("\n=== Done ===\n");
    return 0;
}
