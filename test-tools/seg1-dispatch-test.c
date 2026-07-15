#include <windows.h>
#include <stdio.h>
#include <memory.h>

#define IOCTL_INIT_HW            0x80000B80
#define IOCTL_READ_REG           0x80000B88
#define IOCTL_WRITE_REG          0x80000B8C
#define IOCTL_WRITE_PHYSICAL_MEM 0x80000C10

typedef struct { UINT32 Off; UINT32 Val; } REG_IO;
static HANDLE gH = INVALID_HANDLE_VALUE;
static UINT32 R(UINT32 off){ REG_IO in={off,0},out={0}; DWORD br=0; DeviceIoControl(gH,IOCTL_READ_REG,&in,sizeof(in),&out,sizeof(out),&br,NULL); return out.Val; }
static void W(UINT32 off,UINT32 val){ REG_IO in={off,val},out={0}; DWORD br=0; DeviceIoControl(gH,IOCTL_WRITE_REG,&in,sizeof(in),&out,sizeof(out),&br,NULL); }
static void WritePhys(UINT64 pa,const void* data,ULONG size){ UCHAR buf[4096+12]; ((PULONG)buf)[0]=(ULONG)(pa&0xFFFFFFFF); ((PULONG)buf)[1]=(ULONG)(pa>>32); ((PULONG)buf)[2]=size; memcpy(buf+12,data,size); DWORD br=0; DeviceIoControl(gH,IOCTL_WRITE_PHYSICAL_MEM,buf,12+size,NULL,0,&br,NULL); }

/* SEG1 (broadcast / per-SE) compute view = SEG0 (BASE_IDX=0) + 0xA000 */
#define S1(base) ((base)+0xA000)
#define GRBM_STATUS              0x3260
#define GCVM_CONTEXT0_CNTL       0xB460
#define COMPUTE_DISPATCH_INITIATOR  S1(0x80E0)
#define COMPUTE_DIM_X               S1(0x80E4)
#define COMPUTE_DIM_Y               S1(0x80E8)
#define COMPUTE_DIM_Z               S1(0x80EC)
#define COMPUTE_NUM_THREAD_X        S1(0x80FC)
#define COMPUTE_PGM_LO              S1(0x8110)
#define COMPUTE_PGM_HI              S1(0x8114)
#define COMPUTE_PGM_RSRC1           S1(0x8128)
#define COMPUTE_PGM_RSRC2           S1(0x812C)
#define SHADER_ADDR  0xC0100000ULL

int main(void){
    printf("=== SEG1 (0x12xxx) DISPATCH EXPERIMENT ===\n");
    gH = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (gH == INVALID_HANDLE_VALUE) { printf("can't open device\n"); return 1; }

    UINT8 initBuf[32] = {0};
    *(UINT64*)(initBuf+0)  = 0xFE800000ULL;
    *(UINT32*)(initBuf+8)  = 0x00080000;
    *(UINT32*)(initBuf+12) = 1;            /* NBIO_MAP only */
    *(UINT64*)(initBuf+16) = 0xC0000000ULL;
    *(UINT32*)(initBuf+24) = 0x20000000;
    DWORD br = 0;
    DeviceIoControl(gH, IOCTL_INIT_HW, initBuf, sizeof(initBuf), NULL, 0, &br, NULL);

    /* minimal shader: s_endpgm */
    UINT32 shader[16] = {0};
    shader[0] = 0xBF9F0000;
    WritePhys(SHADER_ADDR, shader, sizeof(shader));

    UINT32 savedGcvm = R(GCVM_CONTEXT0_CNTL);
    W(GCVM_CONTEXT0_CNTL, savedGcvm & ~1);  /* disable translation: PGM_LO = physical */

    UINT32 grbm0 = R(GRBM_STATUS);
    printf("GRBM before       = 0x%08X\n", grbm0);
    printf("SEG1 PGM_RSRC1/2   = 0x%08X / 0x%08X\n", R(COMPUTE_PGM_RSRC1), R(COMPUTE_PGM_RSRC2));

    W(COMPUTE_DIM_X, 1); W(COMPUTE_DIM_Y, 1); W(COMPUTE_DIM_Z, 1);
    W(COMPUTE_NUM_THREAD_X, 32);
    W(COMPUTE_PGM_LO, (UINT32)(SHADER_ADDR >> 8));
    W(COMPUTE_PGM_HI, (UINT32)(SHADER_ADDR >> 40));
    W(COMPUTE_PGM_RSRC1, 0);
    W(COMPUTE_PGM_RSRC2, 0);
    printf("SEG1 PGM_LO/HI     = 0x%08X / 0x%08X\n", R(COMPUTE_PGM_LO), R(COMPUTE_PGM_HI));

    printf("Trigger DISPATCH_INITIATOR(0x%X) = 0x8003\n", COMPUTE_DISPATCH_INITIATOR);
    W(COMPUTE_DISPATCH_INITIATOR, 0x8003);
    UINT32 initAfter = R(COMPUTE_DISPATCH_INITIATOR);
    printf("DISPATCH_INIT after= 0x%08X (VALID %s)\n", initAfter, (initAfter & 2) ? "STILL SET" : "consumed");

    UINT32 maxBusy = 0, anyChange = 0, base = grbm0;
    for (int i = 0; i < 500; i++) {
        UINT32 g = R(GRBM_STATUS);
        UINT32 busy = g & 0xFFFFF000;
        if (busy > maxBusy) maxBusy = busy;
        if (g != base) anyChange = 1;
        Sleep(1);
    }
    UINT32 grbmEnd = R(GRBM_STATUS);
    printf("GRBM after poll   = 0x%08X\n", grbmEnd);
    printf("max busy bits     = 0x%08X\n", maxBusy);
    printf("GRBM changed?     = %s\n", anyChange ? "YES" : "NO");

    W(GCVM_CONTEXT0_CNTL, savedGcvm);
    printf("Done.\n");
    return 0;
}
