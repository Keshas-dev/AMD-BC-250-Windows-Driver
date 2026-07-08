/* dcn-probe.c — DCN 2.1 display engine probe. Tests if DCN block is alive on BC-250 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <memory.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE h;
static BOOL W32(uint32_t o, uint32_t v) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=v; return DeviceIoControl(h,IOCTL_AMDBC250_WRITE_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL); }
static uint32_t R32(uint32_t o) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=0; if(DeviceIoControl(h,IOCTL_AMDBC250_READ_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL)) return r.Value; return 0xFFFFFFFF; }

static void probe_range(const char* name, uint32_t start, uint32_t end, int step) {
    printf("--- %s [0x%04X-0x%04X] ---\n",name,start,end);
    int non_zero=0;
    for(uint32_t off=start; off<=end; off+=step){
        uint32_t v=R32(off);
        if(v!=0xFFFFFFFF) {
            printf("  0x%04X = 0x%08X%s\n",off,v,v?" <-- non-zero":"");
            non_zero++;
        }
    }
    if(!non_zero) printf("  (all dead 0xFFFFFFFF)\n");
}

int main(){
    setvbuf(stdout,NULL,_IONBF,0);
    h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE){printf("FAIL gle=%lu\n",GetLastError());return 1;}
    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br=0;
    ZeroMemory(&ih,sizeof(ih)); ih.MmioPhysicalBase=0xFE800000ULL; ih.MmioSize=0x80000; ih.Flags=AMDBC250_INIT_FLAG_NBIO_MAP;
    if(!DeviceIoControl(h,IOCTL_AMDBC250_INIT_HARDWARE,&ih,sizeof(ih),&ih,sizeof(ih),&br,NULL)){printf("INIT fail\n");return 1;}

    printf("=== DCN 2.1 Display Engine Probe ===\n\n");

    /* 1. Probe HUBPREQ0 (display surface / flippipe) registers */
    printf("=== PHASE 1: HUBPREQ0 (Flip Pipe) ===\n\n");
    probe_range("HUBPREQ0_DCSURF",0x5080,0x50FF,4);

    /* 2. Probe OTG0 (CRTC / timing generator) registers */
    printf("\n=== PHASE 2: OTG0 (CRTC) ===\n");
    printf("WARNING: Some OTG registers may be in freeze zone (0x3400-0x8100)!\n\n");
    probe_range("OTG0",0x6000,0x60FF,4);

    /* Read OTG0_CRTC_STATUS specifically */
    uint32_t otg_status=R32(0x6028);
    uint32_t otg_h_total=R32(0x6014);
    uint32_t otg_v_total=R32(0x6010);
    uint32_t otg_control=R32(0x6000);
    printf("  OTG0_CONTROL(0x6000) = 0x%08X%s\n",otg_control,(otg_control&1)?" ** ENABLED **":" (disabled)");
    printf("  OTG0_V_TOTAL(0x6010) = 0x%08X (%u lines)\n",otg_v_total,otg_v_total);
    printf("  OTG0_H_TOTAL(0x6014) = 0x%08X (%u pixels)\n",otg_h_total,otg_h_total);
    printf("  OTG0_CRTC_STATUS(0x6028) = 0x%08X\n",otg_status);
    if(otg_v_total>0 && otg_v_total<10000) printf("  ** OTG0 ACTIVE: %ux%u **\n",otg_h_total,otg_v_total);
    else if(otg_v_total==0) printf("  ** OTG0 INACTIVE (no timing) **\n");

    /* 3. Probe DMCUB registers */
    printf("\n=== PHASE 3: DMCUB (Display MCU) ===\n\n");
    probe_range("DMCUB",0x7000,0x701F,4);

    /* 4. Check HUBPREQ0 current surface address */
    printf("\n=== PHASE 4: Current Display Surface ===\n");
    uint32_t surf_lo=R32(0x5080);
    uint32_t surf_hi=R32(0x5084);
    uint64_t surf_addr=((uint64_t)surf_hi<<32)|surf_lo;
    printf("  HUBPREQ0_PRIMARY_SURFACE(0x5080) = 0x%08X_%08X\n",surf_hi,surf_lo);
    printf("  Decoded address: 0x%016llX\n",surf_addr);
    if(surf_addr) printf("  ** BIOS framebuffer at 0x%llX **\n",surf_addr);

    uint32_t pitch=R32(0x508C);
    uint32_t dim=R32(0x5090);
    uint32_t flip_ctrl=R32(0x5088);
    printf("  SURFACE_PITCH(0x508C) = 0x%08X (pitch=%u bytes)\n",pitch,pitch);
    printf("  SURFACE_DIM(0x5090) = 0x%08X (w=%u h=%u)\n",dim,dim>>16,dim&0xFFFF);
    printf("  FLIP_CONTROL(0x5088) = 0x%08X\n",flip_ctrl);

    /* 5. Try to read the framebuffer content to see if display is active */
    printf("\n=== PHASE 5: Framebuffer readback ===\n");
    if(surf_addr && (surf_addr>>32)==0){
        uint32_t fb_word=R32((uint32_t)surf_addr);
        printf("  Pixel at FB+0 = 0x%08X\n",fb_word);
        fb_word=R32((uint32_t)(surf_addr+0x1000));
        printf("  Pixel at FB+0x1000 = 0x%08X\n",fb_word);
    } else {
        /* Try reading at typical BIOS framebuffer locations */
        uint32_t try_addrs[]={0x0,0x100000,0x200000,0x400000,0x800000,0x1000000,0x2000000,0x4000000};
        printf("  Trying direct BAR5 reads at possible framebuffer addresses:\n");
        for(int i=0;i<sizeof(try_addrs)/sizeof(try_addrs[0]);i++){
            uint32_t v=R32(try_addrs[i]);
            if(v!=0xFFFFFFFF && v!=0) printf("    0x%08X = 0x%08X\n",try_addrs[i],v);
        }
    }

    /* 6. Probe additional display pipe registers (HUBP0) */
    printf("\n=== PHASE 6: HUBP0 (Display Pipe 0) ===\n\n");
    probe_range("HUBP0",0x5100,0x51FF,4);
    probe_range("HUBP0_ext",0x5200,0x52FF,4);

    /* 7. Probe OTG1 (pipe 2) for comparison */
    printf("\n=== PHASE 7: OTG1 (Pipe 2) ===\n\n");
    uint32_t otg1_ctrl=R32(0x6100);
    uint32_t otg1_vtotal=R32(0x6110);
    uint32_t otg1_htotal=R32(0x6114);
    printf("  OTG1_CONTROL(0x6100) = 0x%08X%s\n",otg1_ctrl,(otg1_ctrl&1)?" ** ENABLED **":"");
    printf("  OTG1_V_TOTAL(0x6110) = 0x%08X (%u lines)\n",otg1_vtotal,otg1_vtotal);
    printf("  OTG1_H_TOTAL(0x6114) = 0x%08X (%u pixels)\n",otg1_htotal,otg1_htotal);

    /* 8. Probe DCIO (Display I/O) - physical display outputs */
    printf("\n=== PHASE 8: DCIO / Display Outputs ===\n\n");
    probe_range("DCIO",0x5300,0x53FF,4);
    probe_range("HPD (Hot Plug)",0x5400,0x54FF,4);

    /* 9. Try writing to HUBPREQ0 flip control */
    printf("\n=== PHASE 9: Flip Control Write Test ===\n");
    printf("  Writing 0x00000001 to FLIP_CONTROL(0x5088)...\n");
    uint32_t flip_before=R32(0x5088);
    W32(0x5088,0x00000001);
    uint32_t flip_after=R32(0x5088);
    printf("  FLIP_CONTROL: 0x%08X -> 0x%08X (writable=%s)\n",flip_before,flip_after,flip_before!=flip_after?"YES":"NO");

    /* 10. Try flipping to a test framebuffer */
    printf("\n=== PHASE 10: Test Framebuffer Flip ===\n");
    /* Read current surface first */
    uint32_t old_surf_lo=R32(0x5080);
    uint32_t old_surf_hi=R32(0x5084);
    printf("  Old surface: 0x%08X_%08X\n",old_surf_hi,old_surf_lo);

    /* We need VRAM memory for a framebuffer. Try using physical addresses
     * in BAR4 range. For now, just check if ANY address survives a flip write */
    printf("  Testing flip to address 0x00000000 (should be harmless)...\n");
    W32(0x5080,0);
    W32(0x5084,0);
    uint32_t test_surf_lo=R32(0x5080);
    uint32_t test_surf_hi=R32(0x5084);
    printf("  Surface after write: 0x%08X_%08X (writable=%s)\n",
        test_surf_hi,test_surf_lo,
        (test_surf_lo==0 && test_surf_hi==0)?"YES":"NO");

    /* Restore original */
    W32(0x5080,old_surf_lo);
    W32(0x5084,old_surf_hi);
    uint32_t restored_surf=R32(0x5080);
    printf("  Surface restored: 0x%08X (restore=%s)\n",restored_surf,
        restored_surf==old_surf_lo?"OK":"FAIL");

    CloseHandle(h);
    printf("\n=== DCN Probe Complete ===\n");
    return 0;
}
