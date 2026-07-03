#include <windows.h>
#include <stdio.h>

typedef struct { UINT32 Off; UINT32 Val; } REG_IO;
HANDLE gH;
UINT32 R(UINT32 off) {
    REG_IO in={off,0}, out={0}; DWORD br=0;
    DeviceIoControl(gH, 0x80000B88, &in, 8, &out, 8, &br, NULL);
    return out.Val;
}
void W(UINT32 off, UINT32 val) {
    REG_IO in={off,val}, out={0}; DWORD br=0;
    DeviceIoControl(gH, 0x80000B8C, &in, 8, &out, 8, &br, NULL);
}
int main(void) {
    gH = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (gH == INVALID_HANDLE_VALUE) { printf("ERR\n"); return 1; }

    printf("CP_ME_CNTL(0x4A74) = 0x%08X\n", R(0x4A74));
    printf("CP_MEC_CNTL(0x4B14) = 0x%08X\n", R(0x4B14));
    printf("GRBM_STATUS(0x3260) = 0x%08X\n", R(0x3260));
    printf("GRBM_STATUS2(0x326C) = 0x%08X\n", R(0x326C));
    printf("RLC_CP_SCHEDULERS(0xECA8) = 0x%08X\n", R(0xECA8));
    printf("GB_ADDR_CONFIG(0x9800) = 0x%08X\n", R(0x9800));
    printf("CC_ARRAY(0x9C1C) = 0x%08X\n", R(0x9C1C));
    printf("CP_HQD_ACTIVE(0x910C) = 0x%08X\n", R(0x910C));
    printf("CP_MQD_BASE(0x9104) = 0x%08X\n", R(0x9104));
    printf("CP_MQD_BASE_HI(0x9108) = 0x%08X\n", R(0x9108));
    printf("COMPUTE_DISPATCH(0x80E0) = 0x%08X\n", R(0x80E0));
    printf("COMPUTE_PGM_LO(0x8110) = 0x%08X\n", R(0x8110));

    W(0x8110, 0x00C01000);
    printf("PGM_LO after write: 0x%08X\n", R(0x8110));

    W(0x80E0, 3);
    Sleep(100);
    printf("GRBM_STATUS after dispatch: 0x%08X\n", R(0x3260));

    printf("Unhalting ME...\n");
    UINT32 meCntl = R(0x4A74);
    printf("  CP_ME_CNTL before: 0x%08X\n", meCntl);
    W(0x4A74, meCntl & ~(1U<<28));
    printf("  CP_ME_CNTL after:  0x%08X\n", R(0x4A74));

    W(0xECA8, 0x000000A0);
    W(0x4B14, R(0x4B14) & ~(1U<<28));
    printf("  CP_MEC_CNTL after unhalt: 0x%08X\n", R(0x4B14));

    W(0x80E0, 3);
    Sleep(200);
    printf("GRBM_STATUS after unhalt+dispatch: 0x%08X\n", R(0x3260));

    CloseHandle(gH);
    return 0;
}
