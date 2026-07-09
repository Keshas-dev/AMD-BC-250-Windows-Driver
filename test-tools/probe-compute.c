#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE h;

static BOOL W32(uint32_t o, uint32_t v) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=v; return DeviceIoControl(h,IOCTL_AMDBC250_WRITE_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL); }
static uint32_t R32(uint32_t o) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=0; if(DeviceIoControl(h,IOCTL_AMDBC250_READ_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL)) return r.Value; return 0xFFFFFFFF; }

static int test_writability(uint32_t addr, uint32_t test_val) {
    uint32_t orig = R32(addr);
    if (orig == 0xFFFFFFFF) return -1;
    W32(addr, test_val);
    uint32_t wv = R32(addr);
    W32(addr, orig);
    if (wv == test_val) return 1;
    if (wv == orig) return 0;
    return 2; /* partial/changed but not exact */
}

typedef struct { uint32_t addr; const char* name; } KeyReg;

int main(){
    setvbuf(stdout,NULL,_IONBF,0);
    h=CreateFileA("\\\\.\\AMDBC250DreamV43",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE){printf("FAIL gle=%lu\n",GetLastError());return 1;}
    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br=0;
    ZeroMemory(&ih,sizeof(ih)); ih.MmioPhysicalBase=0xFE800000ULL; ih.MmioSize=0x80000; ih.Flags=AMDBC250_INIT_FLAG_NBIO_MAP;
    if(!DeviceIoControl(h,IOCTL_AMDBC250_INIT_HARDWARE,&ih,sizeof(ih),&ih,sizeof(ih),&br,NULL)){printf("INIT fail\n");return 1;}

    /* Key COMPUTE registers at SEG1 (BASE_ID=1) mapping: BAR5 = 0xA000 + 0x1260 + mm*4 = 0xB260 + mm*4 */
    KeyReg key_regs[] = {
        {0x120E0, "DISPATCH_INITIATOR"},    /* mm=0x1ba0 */
        {0x120E4, "DIM_X"},                 /* mm=0x1ba1 */
        {0x120E8, "DIM_Y"},                 /* mm=0x1ba2 */
        {0x120EC, "DIM_Z"},                 /* mm=0x1ba3 */
        {0x120F0, "START_X"},               /* mm=0x1ba4 */
        {0x120F4, "START_Y"},               /* mm=0x1ba5 */
        {0x120F8, "START_Z"},               /* mm=0x1ba6 */
        {0x120FC, "NUM_THREAD_X"},          /* mm=0x1ba7 */
        {0x12100, "NUM_THREAD_Y"},          /* mm=0x1ba8 */
        {0x12104, "NUM_THREAD_Z"},          /* mm=0x1ba9 */
        {0x12108, "NUM_INST_X"},            /* mm=0x1baa */
        {0x1210C, "NUM_INST_Y"},            /* mm=0x1bab */
        {0x12110, "PGM_LO"},                /* mm=0x1bac */
        {0x12114, "PGM_HI"},                /* mm=0x1bad */
        {0x12118, "UNK_1baf"},              /* mm=0x1baf */
        {0x1211C, "UNK_1bb0"},              /* mm=0x1bb0 */
        {0x12120, "UNK_1bb1"},              /* mm=0x1bb1 */
        {0x12128, "PGM_RSRC1"},             /* mm=0x1bb2 */
        {0x1212C, "PGM_RSRC2"},             /* mm=0x1bb3 */
        {0x12130, "UNK_1bb4"},              /* mm=0x1bb4 */
        {0x12134, "UNK_1bb5"},              /* mm=0x1bb5 */
        {0x12138, "STATIC_THREAD_MGMT_SE0"},/* mm=0x1bb6 */
        {0x1213C, "UNK_1bb7"},              /* mm=0x1bb7 */
        {0x12140, "TMPRING_SIZE"},          /* mm=0x1bb8 */
        {0x121E0, "USER_DATA_0"},           /* mm=0x1be0 */
    };

    printf("=== SEG1 COMPUTE Register Writability Test ===\n\n");
    for(int i=0;i<sizeof(key_regs)/sizeof(key_regs[0]);i++){
        uint32_t rv=R32(key_regs[i].addr);
        int w = test_writability(key_regs[i].addr, 0xFEEDFACE);
        const char* ws = (w==1)?"YES":(w==0)?"PARTIAL":(w==-1)?"DEAD":"CHANGED";
        printf("  0x%05X (%-25s): read=0x%08X writable=%s\n",
            key_regs[i].addr, key_regs[i].name, rv, ws);
    }

    /* Cross-check: DISPATCH_INITIATOR at 0x80E0 vs 0x120E0 */
    printf("\n=== Cross-check: DISPATCH_INITIATOR at 0x80E0 vs 0x120E0 ===\n");
    uint32_t orig0 = R32(0x80E0);
    uint32_t orig1 = R32(0x120E0);
    printf("  Initial: 0x80E0=0x%08X, 0x120E0=0x%08X\n", orig0, orig1);
    W32(0x80E0, 0xABCDEF01);
    uint32_t after0 = R32(0x80E0);
    uint32_t after1 = R32(0x120E0);
    printf("  After write 0xABCDEF01 to 0x80E0:\n");
    printf("    0x80E0=0x%08X (match=%s), 0x120E0=0x%08X (match=%s)\n",
        after0, (after0==0xABCDEF01)?"YES":"NO",
        after1, (after1==0xABCDEF01)?"YES":"NO");
    W32(0x80E0, orig0);
    W32(0x120E0, orig1);

    /* Now test opposite direction */
    printf("  After write 0x12345678 to 0x120E0:\n");
    W32(0x120E0, 0x12345678);
    after0 = R32(0x80E0);
    after1 = R32(0x120E0);
    printf("    0x80E0=0x%08X (match=%s), 0x120E0=0x%08X (match=%s)\n",
        after0, (after0==0x12345678)?"YES":"NO",
        after1, (after1==0x12345678)?"YES":"NO");
    W32(0x80E0, orig0);
    W32(0x120E0, orig1);

    /* GRBM_STATUS at canonical locations */
    printf("\n=== GRBM_STATUS (canonical) ===\n");
    uint32_t grbm_addr[] = {0x3260, 0x3264, 0x3268, 0x326C, 0x3270, 0x3274, 0x34D0, 0x34D4, 0x34D8, 0x34DC};
    for(int i=0;i<sizeof(grbm_addr)/sizeof(grbm_addr[0]);i++){
        uint32_t v=R32(grbm_addr[i]);
        printf("  0x%04X = 0x%08X\n", grbm_addr[i], v);
    }

    CloseHandle(h);
    printf("\nDone.\n");
    return 0;
}