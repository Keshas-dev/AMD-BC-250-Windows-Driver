#include <windows.h>
#include <stdio.h>
#include <memory.h>

typedef struct { UINT32 Off; UINT32 Val; } REG_IO;
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
    ((PULONG)buf)[0] = (ULONG)(pa & 0xFFFFFFFF);
    ((PULONG)buf)[1] = (ULONG)(pa >> 32);
    ((PULONG)buf)[2] = size;
    memcpy(buf + 12, data, size);
    DWORD br = 0;
    DeviceIoControl(gH, 0x80000C10, buf, 12 + size, NULL, 0, &br, NULL);
}
static ULONG ReadPhys(UINT64 pa, void* data, ULONG size) {
    UCHAR inbuf[24], outbuf[4096];
    ((PULONG)inbuf)[0] = (ULONG)(pa & 0xFFFFFFFF);
    ((PULONG)inbuf)[1] = (ULONG)(pa >> 32);
    ((PULONG)inbuf)[2] = size;
    DWORD br = 0;
    DeviceIoControl(gH, 0x80000C14, inbuf, 12, outbuf, sizeof(outbuf), &br, NULL);
    if (br > 0) memcpy(data, outbuf, min(br, size));
    return br;
}

#define SHADER_ADDR  0xC0100000ULL
#define WRITE_ADDR   0xC0600000ULL

int main(void) {
    gH = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (gH == INVALID_HANDLE_VALUE) { printf("ERR\n"); return 1; }

    UINT8 initBuf[32] = {0};
    *(UINT64*)(initBuf+0)  = 0xFE800000ULL;
    *(UINT32*)(initBuf+8)  = 0x00080000;
    *(UINT32*)(initBuf+12) = 1;
    *(UINT64*)(initBuf+16) = 0xC0000000ULL;
    *(UINT32*)(initBuf+24) = 0x20000000;
    DWORD br = 0;
    DeviceIoControl(gH, 0x80000B80, initBuf, 32, NULL, 0, &br, NULL);

    /* Write shader with memory write to detect execution */
    UINT32 shader[16] = {0};
    /* RDNA GFX10: s_store_dword s0, s[2:3] ; store s0 to memory at s[2:3]
     * But we need SGPRs set up first.
     * Simpler: just try s_endpgm with SDMA-like output check.
     * 
     * Actually let's use a shader that writes global memory:
     * We need to set SGPR0 = data, SGPR[2:3] = write address
     * But SGPRs are loaded from USER_DATA in MQD.
     * 
     * For minimal test, use flat_store_dword with VGPRs:
     * v_mov_b32 v0, 0x12345678
     * v_mov_b32 v1, 0xC0600000 & 0xFFFFFFFF
     * v_mov_b32 v2, 0xC0600000 >> 32 = 0
     * flat_store_dword v[1:2], v0
     * s_endpgm
     * 
     * RDNA encoding:
     * v_mov_b32 v0, imm32: 0x7E000200 | imm32 (VOP1 with VGPR dest, SGPR/imm src)
     *   Actually: v_mov_b32 v0, imm32 uses DWORD encoding:
     *   0xB08003F8 + (v0 << 0)  -- wait need to check encoding
     *   
     * Simpler: just 3x s_endpgm and check GRBM_STATUS2
     */
    
    /* Alternative: check if any status changes after dispatch.
     * Try the CP_STATUS or CP_STAT register */
    
    /* Just write a known pattern and s_endpgm */
    shader[0] = 0xBF9F0000;  /* s_endpgm */
    shader[1] = 0xBF9F0000;  /* s_endpgm (extra) */
    WritePhys(SHADER_ADDR, shader, 64);

    /* Init fence location */
    UINT64 zero = 0;
    WritePhys(WRITE_ADDR, &zero, 8);
    printf("Fence at 0x%08X initialized to 0\n", (UINT32)WRITE_ADDR);

    /* Configure */
    W(0xB460, R(0xB460) & ~1);  /* GCVM OFF */
    W(0x34FC, 0xFFFFFFFF);      /* SPI max */
    W(0x9C1C, 0x1F000000);      /* preserve existing */

    /* Unhalt ME */
    UINT32 meCntl = R(0x4A74);
    printf("CP_ME_CNTL=0x%08X (ME_HALT=%d)\n", meCntl, (meCntl>>28)&1);
    W(0x4A74, meCntl & ~(1<<28));
    printf("  → unhalted\n");

    /* Read various status registers */
    printf("\n--- STATUS CHECK ---\n");
    printf("GRBM_STATUS(0x3260)=0x%08X\n", R(0x3260));
    for (UINT32 off = 0x3268; off <= 0x3278; off += 4) {
        printf("  0x%04X = 0x%08X\n", off, R(off));
    }
    printf("SDMA0_STATUS(0xE000+)=0x%08X\n", R(0xE018));  /* SDMA0_CNTL */
    printf("CP_STAT(0x3260*) no additional\n");

    /* Set PGM via MMIO */
    W(0x8110, (UINT32)(SHADER_ADDR >> 8));  /* PGM_LO = 0x00C01000 */
    W(0x8114, 0);                           /* PGM_HI = 0 */

    /* DISPATCH with different initiator values */
    printf("\n--- DISPATCH TESTS ---\n");
    UINT32 initVals[] = {0x0003, 0x8003, 0x0001, 0x8001, 0x0083, 0x8083};
    for (int i = 0; i < 6; i++) {
        W(0x80E0, initVals[i]);
        UINT32 initR = R(0x80E0);
        Sleep(50);
        UINT32 grbm = R(0x3260);
        UINT32 fenceHi = 0, fenceLo = 0;
        ReadPhys(WRITE_ADDR, &fenceLo, 4);
        if (sizeof(UINT64) > 4) ReadPhys(WRITE_ADDR+4, &fenceHi, 4);
        printf("  INIT=0x%04X → [0x80E0]=0x%08X  GRBM=0x%08X  FENCE=%llu\n",
            initVals[i], initR, grbm, (UINT64)((ULONG64)fenceLo | ((ULONG64)fenceHi << 32)));
    }

    /* Try CP_HQD_ACTIVE with different MQD */
    printf("\n--- HQD ACTIVATE DIRECT (no MQD) ---\n");
    W(0x910C, 1);  /* CP_HQD_ACTIVE = 1 */
    Sleep(10);
    printf("  CP_HQD_ACTIVE = 0x%08X\n", R(0x910C));
    
    /* Try method from Linux: write PGM via registers only, then dispatch */
    printf("\n--- DIRECT PGM + DISPATCH with ME=1 ---\n");
    W(0x34D0, 0x00010000);  /* GRBM select ME=1 */
    W(0xECA8, 0x000000A0);  /* RLC enable */
    W(0x8110, (UINT32)(SHADER_ADDR >> 8));
    W(0x8114, 0);
    W(0x80E0, 0x0003);
    UINT32 initAfter = R(0x80E0);
    W(0x34D0, 0xE0000000);  /* broadcast */
    Sleep(200);
    printf("  INIT=0x%08X GRBM=0x%08X FENCE=%llu\n",
        initAfter, R(0x3260), (ULONG64)R(WRITE_ADDR));

    /* Check old 0xDC60 */
    printf("\n--- OLD REGISTER 0xDC60 behavior ---\n");
    printf("  0xDC60 = 0x%08X\n", R(0xDC60));
    for (int i = 0; i < 3; i++) {
        W(0xDC60, 0xDEADBEEF);
        Sleep(10);
        printf("  after write #%d: 0x%08X\n", i+1, R(0xDC60));
    }

    CloseHandle(gH);
    return 0;
}
