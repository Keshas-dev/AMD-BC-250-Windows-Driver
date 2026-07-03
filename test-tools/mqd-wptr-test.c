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
#define FENCE_ADDR 0xC0600000ULL

static UINT8 fwBuf[512*1024]; static UINT32 fwSize = 0; static UINT64 ringPa = 0;
static int LoadFirmware(const char* path) {
    HANDLE f = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    if(f==INVALID_HANDLE_VALUE) return 0; DWORD r=0;
    ReadFile(f, fwBuf, sizeof(fwBuf), &r, NULL); fwSize=r; CloseHandle(f);
    return (fwSize>44)?1:0;
}
static int LoadCpFw(int fwType, const char* label) {
    UINT8* ioBuf = (UINT8*)malloc(sizeof(UINT32)*4+fwSize);
    ((UINT32*)ioBuf)[0]=fwType; ((UINT32*)ioBuf)[1]=fwSize;
    ((UINT32*)ioBuf)[2]=0; ((UINT32*)ioBuf)[3]=0;
    memcpy(ioBuf+16, fwBuf, fwSize);
    UINT8 outBuf[32]={0}; DWORD ob=0;
    BOOL ok = DeviceIoControl(gH, 0x80000BD4, ioBuf, 16+fwSize, outBuf, 16, &ob, NULL);
    UINT32* o=(UINT32*)outBuf; printf("  %s: %s Result=0x%08X\n", label, ok?"OK":"FAIL", o[2]);
    free(ioBuf); return ok;
}

int main(void) {
    gH = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if(gH==INVALID_HANDLE_VALUE) { printf("ERR: open device\n"); return 1; }
    printf("Device opened\n");
    UINT8 initBuf[32]={0};
    *(UINT64*)(initBuf+0)=0xFE800000;*(UINT32*)(initBuf+8)=0x80000;
    *(UINT32*)(initBuf+12)=1;*(UINT64*)(initBuf+16)=0xC0000000;*(UINT32*)(initBuf+24)=0x20000000;
    DWORD br=0; DeviceIoControl(gH, 0x80000B80, initBuf, 32, NULL, 0, &br, NULL);
    UINT64 zero=0; WritePhys(FENCE_ADDR, &zero, 8);

    printf("\n[1] Load RLC\n");
    if(!LoadFirmware("..\\firmware\\cyan_skillfish2_rlc.bin")) return 1;
    if(!LoadCpFw(6, "RLC")) return 1;

    printf("\n[2] Load MEC (patched)\n");
    Sleep(10);
    if(!LoadFirmware("..\\firmware\\cyan_skillfish2_mec_patched.bin")) return 1;
    if(!LoadCpFw(4, "MEC")) return 1;
    printf("  MEC_ME1_CNTL=0x%08X CP_MEC_CNTL=0x%08X\n", R(0x7A00), R(0x4B14));

    /* GCVM */
    printf("\n[3] GCVM\n");
    UCHAR ptBuf[256]={0};
    DeviceIoControl(gH, 0x8000098C, NULL, 0, ptBuf, sizeof(ptBuf), &br, NULL);
    UINT32* pt=(UINT32*)ptBuf;
    if(pt[9]!=0xCAFEBABE) { printf("  GCVM FAIL\n"); return 1; }
    ringPa = ((UINT64)pt[2]<<32)|pt[1];
    printf("  RingPA=0x%016llX\n", ringPa);

    /* Write MQD: Linux v10_compute_mqd layout, Region2 at DW64 */
    /* MQD and ring in SAME page so both covered by GCVM page table */
    UINT64 mqdPa = ringPa;      /* MQD at offset 0 (bytes 0-1023) */
    UINT64 rbPa = ringPa + 0x800; /* Ring at offset 0x800 (bytes 2048+) */
    UINT32 rbOffset = 0x800;
    UINT32 mqd[256] = {0};
    /* Region 1 (DW0-63): compute state */
    mqd[11] = 0;                            /* PGM_LO */
    mqd[13] = (2<<0)|(1<<6);                /* RSRC1 */
    mqd[14] = (63<<0);                      /* RSRC2 */
    /* Region 2 (DW64+): ring config */
    mqd[64] = (UINT32)(rbPa & 0xFFFFFFFF);  /* CP_HQD_PQ_BASE_LO */
    mqd[65] = (UINT32)(rbPa >> 32);         /* CP_HQD_PQ_BASE_HI */
    mqd[66] = 0x0000000F;                   /* CP_HQD_PQ_CONTROL: ring=16 dwords */
    /* WPTR set to 0 initially — will update after writing ring data */
    mqd[67] = 0;                             /* CP_HQD_PQ_WPTR_LO (dwords) */
    mqd[68] = 0;                             /* CP_HQD_PQ_WPTR_HI */
    mqd[69] = 0;                             /* CP_HQD_PQ_RPTR */
    mqd[70] = 0;                             /* CP_HQD_PQ_WPTR_POLL_ADDR */
    mqd[72] = 0;                             /* CP_HQD_PQ_DOORBELL_CONTROL */

    /* First write MQD with WPTR=0 */
    WritePhys(mqdPa, mqd, 1024);

    /* Write ring buffer with IT_WRITE_DATA to fence */
    UINT32 ring[64] = {0};
    int dw = 0;
    ring[dw++] = 0xC0390003;  /* IT_WRITE_DATA: count=3 */
    ring[dw++] = 0x00000002;  /* WR_CONFIRM=1, ONE_ADDR=1 */
    ring[dw++] = (UINT32)(FENCE_ADDR & 0xFFFFFFFF);
    ring[dw++] = (UINT32)(FENCE_ADDR >> 32);
    ring[dw++] = 0x0000CAFE;  /* data */
    ring[dw++] = 0xC0001000;  /* NOP */
    UINT32 wptrdw = dw;       /* WPTR in dwords */
    UINT32 wptrBytes = dw * 4;

    WritePhys(rbPa, ring, wptrBytes);

    /* NOW update MQD with WPTR in dwords */
    WritePhys(mqdPa + 67*4, &wptrdw, 4);

    printf("\n[4] MQD + ring written\n");
    printf("  MQD at 0x%016llX, Ring at 0x%016llX\n", mqdPa, rbPa);
    printf("  Ring: %d dwords, MQD[67](WPTR)=%d\n", dw, wptrdw);

    /* Verify ring and MQD */
    UINT32 v[8]; ReadPhys(rbPa, v, 32);
    printf("  Ring[0..7]: 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X\n",
        v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7]);
    UINT32 mqdw; ReadPhys(mqdPa+67*4, &mqdw, 4);
    printf("  MQD[67](WPTR)=%d\n", mqdw);

    /* Setup KIQ */
    printf("\n[5] KIQ + CP_HQD setup\n");
    W(0xE060, (UINT32)(mqdPa & 0xFFFFFFFF));
    W(0xE064, (UINT32)(mqdPa >> 32));
    W(0xE078, 0);
    W(0xE07C, 0);
    W(0xE080, 1);
    W(0xECA8, 0xA0);
    W(0x9104, (UINT32)(mqdPa & 0xFFFFFFFF));
    W(0x910C, 1);
    Sleep(50);
    printf("  KIQ_ACTIVE=0x%08X CP_HQD_ACTIVE=0x%08X\n", R(0xE080), R(0x910C));
    printf("  KIQ: BASE=0x%08X_%08X SIZE=0x%08X RPTR=0x%08X MQD_WPTR=%d\n",
        R(0xE064), R(0xE060), R(0xE068), R(0xE06C), mqdw);

    /* Trigger ring processing: write KIQ_WPTR (bytes) */
    printf("\n[6] Trigger (KIQ_WPTR=%d)\n", wptrBytes);
    UINT32 fence = 0; ReadPhys(FENCE_ADDR, &fence, 4);
    printf("  Before: GRBM=0x%08X FENCE=0x%04X\n", R(0x3260), fence);

    W(0xE078, wptrBytes);
    Sleep(200);

    fence = 0; ReadPhys(FENCE_ADDR, &fence, 4);
    printf("  After:  RPTR=0x%08X WPTR=0x%08X FENCE=0x%04X GRBM=0x%08X\n",
        R(0xE06C), R(0xE078), fence, R(0x3260));

    /* Poll */
    printf("Polling 3s...\n");
    for(int i=0; i<30; i++) {
        UINT32 rptr=R(0xE06C); ReadPhys(FENCE_ADDR, &fence, 4);
        if(rptr!=0||fence==0xCAFE)
            { printf("  [%d] RPTR=0x%08X FENCE=0x%04X ***\n", i, rptr, fence); break; }
        Sleep(100);
    }
    ReadPhys(FENCE_ADDR, &fence, 4);
    printf("FINAL: RPTR=0x%08X FENCE=0x%04X GRBM=0x%08X\n",
        R(0xE06C), fence, R(0x3260));

    W(0x910C,0); W(0xE080,0);
    printf("Done\n");
    CloseHandle(gH);
    return 0;
}
