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
int main(void) {
    gH = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL);
    if (gH == INVALID_HANDLE_VALUE) { printf("FAIL: open\n"); return 1; }

    /* Init */
    UINT8 initBuf[32] = {0};
    *(UINT64*)(initBuf+0)=0xFE800000; *(UINT32*)(initBuf+8)=0x80000;
    *(UINT32*)(initBuf+12)=1; *(UINT64*)(initBuf+16)=0xC0000000;
    *(UINT32*)(initBuf+24)=0x20000000;
    DWORD br=0; DeviceIoControl(gH, 0x80000B80, initBuf, 32, NULL, 0, &br, NULL);

    /* Check ring registers */
    printf("=== GFX Ring Registers (CP_RING0) ===\n");
    printf("CP_RING0_BASE_LO  = 0x%08X (0xDA60)\n", R(0xDA60));
    printf("CP_RING0_BASE_HI  = 0x%08X (0xDA64)\n", R(0xDA64));
    printf("CP_RING0_CNTL     = 0x%08X (0xDA68)\n", R(0xDA68));
    printf("CP_RING0_RPTR     = 0x%08X (0xDA6C)\n", R(0xDA6C));
    printf("CP_RING0_WPTR     = 0x%08X (0xDA78)\n", R(0xDA78));

    printf("\n=== KIQ Registers ===\n");
    printf("KIQ_BASE_LO  = 0x%08X (0xE060)\n", R(0xE060));
    printf("KIQ_CNTL     = 0x%08X (0xE068 = SIZE)\n", R(0xE068));
    printf("KIQ_RPTR     = 0x%08X (0xE06C)\n", R(0xE06C));
    printf("KIQ_WPTR     = 0x%08X (0xE078)\n", R(0xE078));

    printf("\n=== COMPUTE Registers ===\n");
    printf("DISPATCH_INITIATOR = 0x%08X (0x80E0)\n", R(0x80E0));
    printf("DIM_X = 0x%08X (0x80E4)\n", R(0x80E4));
    printf("PGM_LO = 0x%08X (0x8110)\n", R(0x8110));
    printf("PGM_HI = 0x%08X (0x8114)\n", R(0x8114));

    printf("\n=== GRBM ===\n");
    printf("GRBM_STATUS   = 0x%08X (0x3260)\n", R(0x3260));
    printf("GRBM_STATUS2  = 0x%08X (0x3264)\n", R(0x3264));
    printf("ME_CNTL       = 0x%08X\n", R(0x7A00));
    printf("CP_MEC_CNTL   = 0x%08X (0x4B14)\n", R(0x4B14));

    printf("\n=== PSP Status ===\n");
    printf("C2PMSG_35 = 0x%08X\n", R(0x1056C));
    printf("C2PMSG_81 = 0x%08X\n", R(0x105AC));

    printf("\n=== RLC ===\n");
    printf("RLC_CP_SCHEDULERS = 0x%08X (0xECA8)\n", R(0xECA8));
    printf("RLC_STAT          = 0x%08X\n", R(0x3A00));

    CloseHandle(gH);
    printf("\nDone\n");
    return 0;
}
