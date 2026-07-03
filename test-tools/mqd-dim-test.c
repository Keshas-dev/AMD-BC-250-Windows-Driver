#include <windows.h>
#include <stdio.h>
#pragma pack(push, 1)
typedef struct { UINT32 Off; UINT32 Val; } REG_IO;
#pragma pack(pop)
static HANDLE gH;

static UINT32 R(UINT32 off) {
    REG_IO in={off,0}, out={0}; DWORD br=0;
    DeviceIoControl(gH, 0x80000B88, &in, 8, &out, 8, &br, NULL);
    return out.Val;
}
static void W(UINT32 off, UINT32 val) {
    REG_IO in={off,val}, out={0}; DWORD br=0;
    DeviceIoControl(gH, 0x80000B8C, &in, 8, &out, 8, &br, NULL);
}
static void WritePhys(UINT64 pa, const void* data, ULONG size) {
    UCHAR buf[4096+12];
    ((UINT32*)buf)[0]=(UINT32)(pa&0xFFFFFFFF); ((UINT32*)buf)[1]=(UINT32)(pa>>32); ((UINT32*)buf)[2]=size;
    memcpy(buf+12,data,size); DWORD br=0;
    DeviceIoControl(gH, 0x80000C10, buf, 12+size, NULL, 0, &br, NULL);
}
static ULONG ReadPhys(UINT64 pa, void* data, ULONG size) {
    UCHAR inb[24], outb[4096];
    ((UINT32*)inb)[0]=(UINT32)(pa&0xFFFFFFFF); ((UINT32*)inb)[1]=(UINT32)(pa>>32); ((UINT32*)inb)[2]=size;
    DWORD br=0; DeviceIoControl(gH, 0x80000C14, inb, 12, outb, sizeof(outb), &br, NULL);
    if(br>0) memcpy(data, outb, min(br,size)); return br;
}
#define SHADER_ADDR 0xC0100000ULL
#define FENCE_ADDR  0xC0600000ULL

int main(void) {
    gH = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (gH == INVALID_HANDLE_VALUE) { printf("ERR: open device\n"); return 1; }
    printf("Device opened\n");

    UINT8 initBuf[32] = {0};
    *(UINT64*)(initBuf+0)=0xFE800000; *(UINT32*)(initBuf+8)=0x80000;
    *(UINT32*)(initBuf+12)=1; *(UINT64*)(initBuf+16)=0xC0000000; *(UINT32*)(initBuf+24)=0x20000000;
    DWORD br=0; DeviceIoControl(gH, 0x80000B80, initBuf, 32, NULL, 0, &br, NULL);

    /* Write shader + fence */
    UINT32 shader[4] = {0xBF9F0000};
    WritePhys(SHADER_ADDR, shader, 16);
    UINT64 zero = 0; WritePhys(FENCE_ADDR, &zero, 8);

    /* GCVM */
    UCHAR ptBuf[256] = {0};
    DeviceIoControl(gH, 0x8000098C, NULL, 0, ptBuf, sizeof(ptBuf), &br, NULL);
    UINT32* pt = (UINT32*)ptBuf;
    printf("GCVM: RingPA=0x%08X_%08X Phys[0]=0x%08X\n", pt[2], pt[1], pt[10]);
    UINT64 ringPa = ((UINT64)pt[2]<<32)|pt[1];
    UINT32 cntl=R(0xB460);
    if(!(cntl&1)){ W(0xB460, cntl|1); W(0x6C10,1); W(0x6C0C,1); Sleep(10); }

    /* Write MQD at ringPa */
    UINT32 mqd[256] = {0};
    mqd[1] = (UINT32)(SHADER_ADDR >> 8);  /* PGM_LO */
    mqd[2] = 2;           /* DIM_X = 2 (test if readable) */
    mqd[3] = 3;           /* DIM_Y = 3 */
    mqd[4] = 4;           /* DIM_Z = 4 */
    mqd[5] = 5;           /* START_X = 5 */
    mqd[6] = 6;           /* START_Y = 6 */
    mqd[7] = 7;           /* START_Z = 7 */
    mqd[8] = 16;          /* NUM_THREAD_X */
    mqd[9] = 1;           /* NUM_THREAD_Y */
    mqd[10] = 1;          /* NUM_THREAD_Z */
    mqd[11] = (UINT32)(SHADER_ADDR >> 8);  /* PGM_LO */
    mqd[12] = 0;                            /* PGM_HI */
    mqd[13] = (2<<0)|(1<<6);                /* RSRC1 */
    mqd[14] = (63<<0);                      /* RSRC2 */
    WritePhys(ringPa, mqd, 1024);

    printf("\n=== BEFORE MQD LOAD ===\n");
    printf("DIM_X(0x80E4)=0x%08X DIM_Y=0x%08X DIM_Z=0x%08X\n", R(0x80E4), R(0x80E8), R(0x80EC));
    printf("PGM_LO(0x8110)=0x%08X\n", R(0x8110));
    printf("START(0x80F0)=0x%08X 0x%08X 0x%08X\n", R(0x80F0), R(0x80F4), R(0x80F8));
    printf("NUM_THREAD(0x80FC)=0x%08X 0x%08X 0x%08X\n", R(0x80FC), R(0x8100), R(0x8104));

    /* Load MQD */
    W(0x9104, (UINT32)(ringPa & 0xFFFFFFFF));
    W(0x9108, (UINT32)(ringPa >> 32));
    W(0x910C, 1);
    Sleep(50);

    printf("\n=== AFTER MQD LOAD (CP_HQD_ACTIVE=1) ===\n");
    printf("DIM_X(0x80E4)=0x%08X DIM_Y=0x%08X DIM_Z=0x%08X\n", R(0x80E4), R(0x80E8), R(0x80EC));
    printf("PGM_LO(0x8110)=0x%08X\n", R(0x8110));
    printf("START(0x80F0)=0x%08X 0x%08X 0x%08X\n", R(0x80F0), R(0x80F4), R(0x80F8));
    printf("NUM_THREAD(0x80FC)=0x%08X 0x%08X 0x%08X\n", R(0x80FC), R(0x8100), R(0x8104));

    printf("\n=== DIM_X WRITE TEST (0x80E4) ===\n");
    W(0x80E4, 0x42);
    printf("After write 0x42: 0x%08X\n", R(0x80E4));

    printf("\n=== TRIGGER DISPATCH ===\n");
    W(0x80E0, 0xFFFFFFFF);
    printf("DISPATCH_INITIATOR=0x%08X GRBM=0x%08X\n", R(0x80E0), R(0x3260));

    /* Poll for activity */
    for(int i=0; i<20; i++) {
        UINT32 grbm=R(0x3260);
        UINT32 fence=0; ReadPhys(FENCE_ADDR, &fence, 4);
        printf("  [%d] GRBM=0x%08X FENCE=0x%08X SCRATCH=0x%08X\n", i, grbm, fence, R(0x32D4));
        if(grbm!=0||fence!=0) break;
        Sleep(100);
    }

    W(0x910C, 0);
    printf("Done\n");
    CloseHandle(gH);
    return 0;
}
