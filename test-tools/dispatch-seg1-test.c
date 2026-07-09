#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE h;

static BOOL W32(uint32_t o, uint32_t v) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=v; return DeviceIoControl(h,IOCTL_AMDBC250_WRITE_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL); }
static uint32_t R32(uint32_t o) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=0; if(DeviceIoControl(h,IOCTL_AMDBC250_READ_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL)) return r.Value; return 0xFFFFFFFF; }

#define COMPUTE_SHADER_EN  0x00000001
#define DISPATCH_VALID     0x00000002
#define USE_THREAD_DIM     0x00008000

/* Minimal shader: s_mov_b32 s0, 0x12345678; s_store_dword s0, SCRATCH_REG0; s_endpgm */
/* Using GCN/RDNA encoding */
static const uint32_t test_shader[] = {
    0xBE800000,  /* s_mov_b32 s0, 0x12345678 (actually need 2 dwords) */
    0x00000000,
    0xE8000000,  /* s_store_dword s0, [scratch_offset] - simplified */
    0xBF810000,  /* s_endpgm */
};

int main(){
    setvbuf(stdout,NULL,_IONBF,0);
    h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE){printf("FAIL gle=%lu\n",GetLastError());return 1;}
    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br=0;
    ZeroMemory(&ih,sizeof(ih)); ih.MmioPhysicalBase=0xFE800000ULL; ih.MmioSize=0x80000; ih.Flags=AMDBC250_INIT_FLAG_NBIO_MAP;
    if(!DeviceIoControl(h,IOCTL_AMDBC250_INIT_HARDWARE,&ih,sizeof(ih),&ih,sizeof(ih),&br,NULL)){printf("INIT fail\n");return 1;}

    printf("=== Compute Dispatch Test (SEG1 offsets) ===\n\n");

    /* First, check current SEG1 COMPUTE register state */
    printf("--- Initial SEG1 COMPUTE Registers ---\n");
    printf("DISPATCH_INITIATOR(0x120E0): 0x%08X\n", R32(0x120E0));
    printf("PGM_LO(0x12110): 0x%08X\n", R32(0x12110));
    printf("PGM_HI(0x12114): 0x%08X\n", R32(0x12114));
    printf("PGM_RSRC1(0x12128): 0x%08X\n", R32(0x12128));
    printf("PGM_RSRC2(0x1212C): 0x%08X\n", R32(0x12128));
    printf("NUM_THREAD_X(0x120FC): 0x%08X\n", R32(0x120FC));
    printf("NUM_THREAD_Y(0x12100): 0x%08X\n", R32(0x12100));
    printf("NUM_THREAD_Z(0x12104): 0x%08X\n", R32(0x12104));
    printf("DIM_X(0x120E4): 0x%08X\n", R32(0x120E4));
    printf("DIM_Y(0x120E8): 0x%08X\n", R32(0x120E8));
    printf("DIM_Z(0x120EC): 0x%08X\n", R32(0x120EC));
    printf("START_X(0x120F0): 0x%08X\n", R32(0x120F0));
    printf("START_Y(0x120F4): 0x%08X\n", R32(0x120F4));
    printf("START_Z(0x120F8): 0x%08X\n", R32(0x120F8));
    printf("USER_DATA_0(0x121E0): 0x%08X\n", R32(0x121E0));

    /* Check SCRATCH before */
    uint32_t scratch_before = R32(0x32D4);
    printf("\nSCRATCH(0x32D4) before: 0x%08X\n", scratch_before);
    W32(0x32D4, 0xDEADBEEF);
    printf("SCRATCH after write marker: 0x%08X\n", R32(0x32D4));

    /* Setup compute dispatch at SEG1 addresses */
    printf("\n--- Setting up dispatch at SEG1 ---\n");
    
    /* We need a valid shader address. For now test with SCRATCH as target. */
    /* But SCRATCH is at 0x32D4 (MMIO), not GPU memory. Shader can't write MMIO directly. */
    /* Let's test with USER_DATA and see if we can trigger anything. */
    
    /* Set DIM to 1x1x1 */
    W32(0x120E4, 1);  printf("DIM_X=1 -> 0x%08X\n", R32(0x120E4));
    W32(0x120E8, 1);  printf("DIM_Y=1 -> 0x%08X\n", R32(0x120E8));
    W32(0x120EC, 1);  printf("DIM_Z=1 -> 0x%08X\n", R32(0x120EC));
    
    /* Set START to 0 */
    W32(0x120F0, 0);  printf("START_X=0 -> 0x%08X\n", R32(0x120F0));
    W32(0x120F4, 0);  printf("START_Y=0 -> 0x%08X\n", R32(0x120F4));
    W32(0x120F8, 0);  printf("START_Z=0 -> 0x%08X\n", R32(0x120F8));
    
    /* Set thread dimensions (32 threads = 1 wavefront) */
    W32(0x120FC, 32); printf("NUM_THREAD_X=32 -> 0x%08X\n", R32(0x120FC));
    W32(0x12100, 1);  printf("NUM_THREAD_Y=1 -> 0x%08X\n", R32(0x12100));
    W32(0x12104, 1);  printf("NUM_THREAD_Z=1 -> 0x%08X\n", R32(0x12104));
    
    /* PGM_LO - need a valid GPU address. For test, use VRAM address if we have it mapped. */
    /* BAR2 (VRAM) is at 0xC0000000 physical, but we don't know GPU virtual address. */
    /* Let's test by setting PGM_LO to a known value and see if DISPATCH triggers */
    /* We'll use 0x10000 as a placeholder */
    W32(0x12110, 0x00010000); printf("PGM_LO=0x00010000 -> 0x%08X\n", R32(0x12110));
    W32(0x12114, 0);        printf("PGM_HI=0 -> 0x%08X\n", R32(0x12114));
    
    /* PGM_RSRC1 - need valid RDNA2 shader config. Copy from defaults? */
    /* Default PGM_RSRC1 was 0x00FFF000. Let's try using it. */
    W32(0x12128, 0x00FFF000); printf("PGM_RSRC1=0x00FFF000 -> 0x%08X\n", R32(0x12128));
    W32(0x1212C, 0x00FFF000); printf("PGM_RSRC2=0x00FFF000 -> 0x%08X\n", R32(0x1212C));
    
    /* Set GRBM_GFX_INDEX to broadcast (all SE/SH/CU) */
    W32(0x34D0, 0xE0000000);
    printf("GRBM_GFX_INDEX=0xE0000000\n");
    
    /* Check GRBM_STATUS before */
    uint32_t grbm_before = R32(0x3260);
    printf("\nGRBM_STATUS(0x3260) before: 0x%08X\n", grbm_before);
    
    /* Trigger dispatch at SEG1 DISPATCH_INITIATOR */
    printf("\n>>> Triggering DISPATCH at 0x120E0 (COMPUTE_SHADER_EN | VALID) <<<\n");
    uint32_t dispatch_val = COMPUTE_SHADER_EN | DISPATCH_VALID;
    W32(0x120E0, dispatch_val);
    uint32_t init_after = R32(0x120E0);
    printf("DISPATCH_INITIATOR after = 0x%08X (VALID consumed=%s)\n",
        init_after, (init_after&DISPATCH_VALID)?"NO":"YES");
    
    /* Poll for activity */
    printf("Polling for 2 seconds...\n");
    for(int i=0;i<200;i++){
        uint32_t gs=R32(0x3260);
        uint32_t sc=R32(0x32D4);
        uint32_t di=R32(0x120E0);
        if(gs!=grbm_before||sc!=0xDEADBEEF||di!=init_after){
            printf("  t=%dms: GRBM=0x%08X Scratch=0x%08X DI=0x%08X\n",i*10,gs,sc,di);
        }
        if(sc!=0xDEADBEEF){printf("*** SCRATCH CHANGED! ***\n");break;}
        if(gs!=grbm_before){printf("  GRBM status changed at t=%dms: 0x%08X\n",i*10,gs);}
        Sleep(10);
    }
    
    uint32_t grbm_after=R32(0x3260);
    uint32_t scratch_after=R32(0x32D4);
    printf("After poll: GRBM=0x%08X Scratch=0x%08X\n",grbm_after,scratch_after);
    
    /* Also try at 0x80E0 for comparison */
    printf("\n>>> Triggering at 0x80E0 (OLD ADDRESS) for comparison <<<\n");
    W32(0x80E0, dispatch_val);
    init_after = R32(0x80E0);
    printf("DISPATCH_INITIATOR(0x80E0) after = 0x%08X (VALID consumed=%s)\n",
        init_after, (init_after&DISPATCH_VALID)?"NO":"YES");
    Sleep(200);
    printf("GRBM=0x%08X Scratch=0x%08X\n",R32(0x3260),R32(0x32D4));

    /* Try with USE_THREAD_DIM at SEG1 */
    printf("\n>>> Triggering at 0x120E0 with USE_THREAD_DIM | VALID <<<\n");
    W32(0x120E0, USE_THREAD_DIM | DISPATCH_VALID);
    init_after = R32(0x120E0);
    printf("DISPATCH_INITIATOR after = 0x%08X (VALID consumed=%s)\n",
        init_after, (init_after&DISPATCH_VALID)?"NO":"YES");
    Sleep(200);
    printf("GRBM=0x%08X Scratch=0x%08X\n",R32(0x3260),R32(0x32D4));

    CloseHandle(h);
    printf("\nDone.\n");
    return 0;
}