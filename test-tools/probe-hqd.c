#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE h;
static BOOL W32(uint32_t o, uint32_t v) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=v; return DeviceIoControl(h,IOCTL_AMDBC250_WRITE_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL); }
static uint32_t R32(uint32_t o) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=0; if(DeviceIoControl(h,IOCTL_AMDBC250_READ_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL)) return r.Value; return 0xFFFFFFFF; }

int main(){
    setvbuf(stdout,NULL,_IONBF,0);
    h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE){printf("FAIL gle=%lu\n",GetLastError());return 1;}
    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br=0;
    ZeroMemory(&ih,sizeof(ih)); ih.MmioPhysicalBase=0xFE800000ULL; ih.MmioSize=0x80000; ih.Flags=AMDBC250_INIT_FLAG_NBIO_MAP;
    if(!DeviceIoControl(h,IOCTL_AMDBC250_INIT_HARDWARE,&ih,sizeof(ih),&ih,sizeof(ih),&br,NULL)){printf("INIT fail\n");return 1;}

    printf("=== CP_HQD / Ring Registers at SEG1 (0xB260 + mm*4) ===\n\n");
    
    /* CP_HQD registers from Linux gc_10_1_0_offset.h */
    struct { uint32_t mm; const char* name; } hqd_regs[] = {
        {0x1FA9, "CP_MQD_BASE_ADDR"},
        {0x1FAB, "CP_HQD_ACTIVE"},
        {0x1FAC, "CP_HQD_VMID"},
        {0x1FB1, "CP_HQD_PQ_BASE"},
        {0x1FBA, "CP_HQD_PQ_CONTROL"},
        {0x1FDF, "CP_HQD_PQ_WPTR_LO"},
    };
    
    for(int i=0;i<sizeof(hqd_regs)/sizeof(hqd_regs[0]);i++){
        uint32_t addr = 0xB260 + hqd_regs[i].mm * 4;
        uint32_t rv = R32(addr);
        int w = 0;
        if(rv != 0xFFFFFFFF){
            uint32_t orig = rv;
            W32(addr, 0xFEEDFACE);
            uint32_t r2 = R32(addr);
            W32(addr, orig);
            if(r2 == 0xFEEDFACE) w = 1;
            else if(r2 == orig) w = 0;
            else w = 2;
        }
        printf("  mm=0x%04X -> 0x%05X (%s): read=0x%08X writable=%s\n",
            hqd_regs[i].mm, addr, hqd_regs[i].name, rv,
            rv==0xFFFFFFFF?"DEAD":w==1?"YES":w==0?"NO":"PARTIAL");
    }

    /* CP Ring registers (GFX ring) */
    printf("\n=== CP Ring Registers (search around 0xDA60 and SEG1 equivalent) ===\n");
    /* Old mapping: RB0_BASE=0xDA60. SEG1: 0xB260 + (0xDA60-0x1260) = 0xB260 + 0xC800 = 0x17A60 */
    /* Actually: 0xDA60 is BAR5 offset. GC_BASE=0x1260. So mm = (0xDA60-0x1260)/4 = 0xC800/4 = 0x3200 */
    /* SEG1 = 0xB260 + 0x3200*4 = 0xB260 + 0xC800 = 0x17A60 */
    
    uint32_t ring_offsets[] = {0xDA60, 0xDA64, 0xDA68, 0xDA6C, 0xDA70, 0xDA74, 0xDA78, 0xDA7C};
    const char* ring_names[] = {"RB0_BASE_LO", "RB0_BASE_HI", "RB0_CNTL", "RB0_RPTR", "RB0_RPTR_HI", "RB0_WPTR", "RB0_WPTR_HI", "RB0_DOORBELL"};
    
    printf("\nOld mapping (0xDA60+):\n");
    for(int i=0;i<8;i++){
        uint32_t rv = R32(ring_offsets[i]);
        printf("  0x%04X (%s): 0x%08X\n", ring_offsets[i], ring_names[i], rv);
    }
    
    printf("\nSEG1 mapping (0x17A60+):\n");
    for(int i=0;i<8;i++){
        uint32_t seg1 = 0x17A60 + i*4;
        uint32_t rv = R32(seg1);
        printf("  0x%05X (%s): 0x%08X\n", seg1, ring_names[i], rv);
    }

    /* Also check KIQ registers */
    printf("\n=== KIQ Registers (search) ===\n");
    /* KIQ_BASE mm=0x2C04? KIQ_SIZE mm=0x2C05? Let me check Linux */
    /* From gc_10_1_0_offset.h: mmCP_MEC_KIQ_BASE = 0x1E9D, mmCP_MEC_KIQ_SIZE = 0x1E9E */
    uint32_t kiq_base = 0xB260 + 0x1E9D * 4;  // 0xB260 + 0x7A74 = 0x12CD4
    uint32_t kiq_size = 0xB260 + 0x1E9E * 4;  // 0xB260 + 0x7A78 = 0x12CD8
    printf("  KIQ_BASE (mm=0x1E9D -> 0x%05X): 0x%08X\n", kiq_base, R32(kiq_base));
    printf("  KIQ_SIZE (mm=0x1E9E -> 0x%05X): 0x%08X\n", kiq_size, R32(kiq_size));
    
    /* Also check old KIQ at 0x35E0, 0x35E4 */
    printf("  Old KIQ_BASE (0x35E0): 0x%08X\n", R32(0x35E0));
    printf("  Old KIQ_SIZE (0x35E4): 0x%08X\n", R32(0x35E4));

    CloseHandle(h);
    printf("\nDone.\n");
    return 0;
}