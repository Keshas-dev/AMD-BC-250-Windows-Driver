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
    UCHAR buf[4096 + 12];
    ((UINT32*)buf)[0] = (UINT32)(pa & 0xFFFFFFFF);
    ((UINT32*)buf)[1] = (UINT32)(pa >> 32);
    ((UINT32*)buf)[2] = size;
    memcpy(buf + 12, data, size);
    DWORD br = 0;
    DeviceIoControl(gH, 0x80000C10, buf, 12 + size, NULL, 0, &br, NULL);
}
static ULONG ReadPhys(UINT64 pa, void* data, ULONG size) {
    UCHAR inbuf[24], outbuf[4096];
    ((UINT32*)inbuf)[0] = (UINT32)(pa & 0xFFFFFFFF);
    ((UINT32*)inbuf)[1] = (UINT32)(pa >> 32);
    ((UINT32*)inbuf)[2] = size;
    DWORD br = 0;
    DeviceIoControl(gH, 0x80000C14, inbuf, 12, outbuf, sizeof(outbuf), &br, NULL);
    if (br > 0) memcpy(data, outbuf, min(br, size));
    return br;
}

#define FENCE_ADDR 0xC0600000ULL
#define VRAM_MQD   0xC0100000ULL  /* MQD in VRAM instead of system RAM */

int main(void) {
    gH = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL);
    if (gH == INVALID_HANDLE_VALUE) { printf("FAIL: open\n"); return 1; }

    UINT8 initBuf[32] = {0};
    *(UINT64*)(initBuf+0)=0xFE800000; *(UINT32*)(initBuf+8)=0x80000;
    *(UINT32*)(initBuf+12)=1; *(UINT64*)(initBuf+16)=0xC0000000;
    *(UINT32*)(initBuf+24)=0x20000000;
    DWORD br=0; DeviceIoControl(gH, 0x80000B80, initBuf, 32, NULL, 0, &br, NULL);

    /* GCVM setup for page tables */
    UINT8 ptBuf[256] = {0};
    if (!DeviceIoControl(gH, 0x8000098C, NULL, 0, ptBuf, sizeof(ptBuf), &br, NULL)) {
        printf("GCVM FAILED\n"); CloseHandle(gH); return 1;
    }
    UINT32* pt = (UINT32*)ptBuf;
    if (pt[9] != 0xCAFEBABE) { printf("Bad GCVM\n"); CloseHandle(gH); return 1; }
    printf("GCVM setup OK, page tables at 0x%08X_%08X\n", pt[2], pt[1]);

    /* Enable GCVM */
    UINT32 cntl = R(0xB460);
    if (!(cntl & 1)) { W(0xB460, cntl|1); W(0x6C10,1); W(0x6C0C,1); Sleep(10); }

    UINT64 mqdPa = VRAM_MQD;
    UINT64 rbPa = VRAM_MQD + 0x1000;
    printf("MQD at VRAM 0x%llX, Ring at VRAM 0x%llX\n", mqdPa, rbPa);

    /* Write MQD to VRAM - use identity mapping (0xC0000000 -> 0xC0000000) */
    UINT32 mqd[256] = {0};
    mqd[0] = 0;                          /* HEADER */
    mqd[11] = 0xCAFEBABE;               /* PGM_LO = test magic */
    mqd[13] = (2<<0)|(1<<6);             /* RSRC1 */
    mqd[14] = (63<<0);                   /* RSRC2 */
    mqd[64] = (UINT32)(rbPa & 0xFFFFFFFF);   /* PQ_BASE_LO */
    mqd[65] = (UINT32)(rbPa >> 32);           /* PQ_BASE_HI */
    mqd[66] = 0x0000000F;                     /* PQ_CONTROL: 16 dwords */
    WritePhys(mqdPa, mqd, 1024);

    /* Verify MQD write */
    UINT32 v[12]; ReadPhys(mqdPa, v, 48);
    printf("MQD verify: [0]=0x%08X [11]=0x%08X [13]=0x%08X [14]=0x%08X\n",
        v[0], v[11], v[13], v[14]);
    printf("PGM_LO slot=%s\n", (v[11]==0xCAFEBABE)?"MATCH!":"MISMATCH");

    /* Write ring buffer (PM4: IT_WRITE_DATA to fence) */
    UINT32 zero = 0; WritePhys(FENCE_ADDR, &zero, 8);
    UINT32 ring[64] = {0};
    int dw = 0;
    ring[dw++] = 0xC0390003;  /* IT_WRITE_DATA, count=3 */
    ring[dw++] = 0x00000002;  /* WR_CONFIRM=1, ONE_ADDR=1 */
    ring[dw++] = (UINT32)(FENCE_ADDR & 0xFFFFFFFF);
    ring[dw++] = (UINT32)(FENCE_ADDR >> 32);
    ring[dw++] = 0x0000CAFE;
    ring[dw++] = 0xC0001000;  /* NOP */
    UINT32 ringBytes = dw * 4;
    WritePhys(rbPa, ring, ringBytes);
    ReadPhys(rbPa, v, 24);
    printf("Ring verify: 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X\n",
        v[0], v[1], v[2], v[3], v[4], v[5]);

    /* Activate MQD via CP_HQD */
    printf("\n--- Setup KIQ + CP_HQD ---\n");
    printf("Before: PGM_LO=0x%08X\n", R(0x8110));

    W(0x9104, (UINT32)(mqdPa & 0xFFFFFFFF));  /* CP_MQD_BASE_ADDR */
    W(0x910C, 1);                               /* CP_HQD_ACTIVE */
    Sleep(50);
    printf("After CP_HQD_ACTIVE: PGM_LO=0x%08X (expected 0xCAFEBABE)\n", R(0x8110));

    /* Also try KIQ */
    W(0xE060, (UINT32)(mqdPa & 0xFFFFFFFF));
    W(0xE064, (UINT32)(mqdPa >> 32));
    W(0xE078, 0);
    W(0xE07C, 0);
    W(0xE080, 1);
    W(0xECA8, 0xA0);

    printf("KIQ: SIZE=0x%08X RPTR=0x%08X WPTR=0x%08X\n",
        R(0xE068), R(0xE06C), R(0xE078));

    /* Trigger */
    UINT32 fence = 0;
    ReadPhys(FENCE_ADDR, &fence, 4);
    printf("Before: FWPTR=0x%08X FENCE=0x%04X\n", R(0xE078), fence);

    W(0xE078, ringBytes);
    Sleep(200);

    ReadPhys(FENCE_ADDR, &fence, 4);
    printf("After:  RPTR=0x%08X WPTR=0x%08X FENCE=0x%04X\n",
        R(0xE06C), R(0xE078), fence);

    printf("Poll 5s...\n");
    for (int i=0;i<50;i++) {
        UINT32 rptr=R(0xE06C); ReadPhys(FENCE_ADDR,&fence,4);
        if (rptr!=0 || fence==0xCAFE) {
            printf("[%d] RPTR=0x%08X FENCE=0x%04X *** ACTIVITY! ***\n",i,rptr,fence); break;
        }
        Sleep(100);
    }
    printf("Final: RPTR=0x%08X FENCE=0x%04X GRBM=0x%08X\n", R(0xE06C), fence, R(0x3260));

    W(0x910C,0); W(0xE080,0);
    CloseHandle(gH);
    printf("Done\n");
    return 0;
}
