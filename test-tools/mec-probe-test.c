#include <windows.h>
#include <stdio.h>

#define IOCTL_INIT_HW            0x80000B80
#define IOCTL_READ_REG           0x80000B88
#define IOCTL_WRITE_REG          0x80000B8C
#define IOCTL_READ_PHYSICAL_MEM  0x80000C14
#define IOCTL_WRITE_PHYSICAL_MEM 0x80000C10

typedef struct { UINT32 Off; UINT32 Val; } REG_IO;

static HANDLE gH = INVALID_HANDLE_VALUE;

static UINT32 R(UINT32 off) {
    REG_IO in={off,0}, out={0}; DWORD br=0;
    DeviceIoControl(gH, IOCTL_READ_REG, &in, sizeof(in), &out, sizeof(out), &br, NULL);
    return out.Val;
}
static void W(UINT32 off, UINT32 val) {
    REG_IO in={off,val}, out={0}; DWORD br=0;
    DeviceIoControl(gH, IOCTL_WRITE_REG, &in, sizeof(in), &out, sizeof(out), &br, NULL);
}

typedef struct { UINT64 PhysAddr; UINT32 Size; UINT8 Data[256]; } PMEM_IO;

static UINT32 ReadPhys(UINT64 pa) {
    PMEM_IO io = {0}; DWORD br = 0;
    io.PhysAddr = pa; io.Size = 4;
    DeviceIoControl(gH, IOCTL_READ_PHYSICAL_MEM, NULL, 0, &io, sizeof(io), &br, NULL);
    return *(UINT32*)io.Data;
}
static void WritePhys(UINT64 pa, UINT32 val) {
    PMEM_IO io = {0}; DWORD br = 0;
    io.PhysAddr = pa; io.Size = 4;
    *(UINT32*)io.Data = val;
    DeviceIoControl(gH, IOCTL_WRITE_PHYSICAL_MEM, &io, sizeof(io), NULL, 0, &br, NULL);
}

int main(void) {
    printf("=== BC-250 MEC/COMPUTE PROBE v1 ===\n\n");
    gH = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (gH == INVALID_HANDLE_VALUE) { printf("ERROR: Cannot open device\n"); return 1; }

    UINT8 initBuf[32] = {0};
    *(UINT64*)(initBuf+0)  = 0xFE800000ULL;
    *(UINT32*)(initBuf+8)  = 0x00080000;
    *(UINT32*)(initBuf+12) = 1;
    *(UINT64*)(initBuf+16) = 0xC0000000ULL;
    *(UINT32*)(initBuf+24) = 0x20000000;
    DWORD br = 0;
    DeviceIoControl(gH, IOCTL_INIT_HW, initBuf, sizeof(initBuf), NULL, 0, &br, NULL);

    /* SECTION 1: MEC Halt Status */
    printf("--- MEC Halt Status ---\n");
    /* Corrected CP_MEC_CNTL: mm=0x0e2d, BAR5 = 0x1260 + 0x0e2d*4 = 0x4B14 */
    UINT32 mecCntl = R(0x4B14);
    printf("  CP_MEC_CNTL @ 0x4B14     = 0x%08X  %s%s\n",
        mecCntl,
        (mecCntl & 0x10000000) ? "ME1_HALT!" : "",
        (mecCntl & 0x20000000) ? " ME2_HALT!" : "");

    /* Also check the previously-guessed offsets */
    UINT32 mecTries[] = {0x9934, 0x21B5, 0x0C0E0, 0x1260 + 0x0e2d};
    for (int i = 0; i < 4; i++) {
        UINT32 v = R(mecTries[i]);
        printf("  CP_MEC_CNTL @ 0x%05X   = 0x%08X  %s\n",
            mecTries[i], v,
            (v & 0x30000000) ? "(HALT bits)" : "");
    }

    /* SECTION 2: GRBM selection registers */
    printf("\n--- GRBM Select ---\n");
    UINT32 grbmIdx = R(0x34D0);  /* GRBM_GFX_INDEX */
    UINT32 grbmCntl = R(0x2022); /* GRBM_GFX_CNTL */
    printf("  GRBM_GFX_INDEX @ 0x34D0 = 0x%08X\n", grbmIdx);
    printf("  GRBM_GFX_CNTL  @ 0x2022 = 0x%08X\n", grbmCntl);

    /* SECTION 3: COMPUTE registers with GRBM_GFX_CNTL select (ME=1) */
    printf("\n--- COMPUTE PIPE STATE (direct) ---\n");
    UINT32 compRegs[] = {0xDC60, 0xDC64, 0xDC68, 0xDC6C, 0xDC70, 0xDC74, 0xDC78, 0xDC7C, 0xDC80};
    const char* compNames[] = {"DIR", "START", "RSRC1", "RSRC2", "PGM_LO", "PGM_HI", "LIMITS", "THR_MGMT", "MISC"};
    for (int i = 0; i < 9; i++) {
        UINT32 v = R(compRegs[i]);
        printf("  COMPUTE_%-10s @ 0x%04X = 0x%08X\n", compNames[i], compRegs[i], v);
    }

    /* SECTION 4: COMPUTE registers WITH GRBM_GFX_CNTL ME=1 select */
    printf("\n--- COMPUTE PIPE STATE (GRBM_GFX_CNTL ME=1) ---\n");
    /* Save original, write ME=1 via GRBM_GFX_CNTL */
    UINT32 origCntl = grbmCntl;
    W(0x2022, 1 << 16);  /* ME=1 */
    Sleep(10);
    for (int i = 0; i < 9; i++) {
        UINT32 v = R(compRegs[i]);
        printf("  COMPUTE_%-10s @ 0x%04X = 0x%08X\n", compNames[i], compRegs[i], v);
    }
    W(0x2022, origCntl);  /* restore */
    Sleep(10);

    /* SECTION 5: COMPUTE registers WITH GRBM_GFX_INDEX ME=1 select */
    printf("\n--- COMPUTE PIPE STATE (GRBM_GFX_INDEX ME=1) ---\n");
    UINT32 origIdx = grbmIdx;
    W(0x34D0, 1 << 16);  /* ME=1 via GRBM_GFX_INDEX */
    Sleep(10);
    for (int i = 0; i < 9; i++) {
        UINT32 v = R(compRegs[i]);
        printf("  COMPUTE_%-10s @ 0x%04X = 0x%08X\n", compNames[i], compRegs[i], v);
    }
    W(0x34D0, origIdx);  /* restore broadcast */

    /* SECTION 6: SPI/CU power gating */
    printf("\n--- CU Power Gating ---\n");
    UINT32 spiOld = R(0x34FC);  /* old SPI offset */
    UINT32 spiNew = R(0x5C3C);  /* corrected: mmSPI_PG_ENABLE_STATIC_WGP_MASK=0x1277*4+GC_BASE */
    UINT32 ccOld  = R(0x3264);  /* old CC offset */
    UINT32 ccNew  = R(0x9C1C);  /* corrected: mmCC_GC_SHADER_ARRAY_CONFIG=0x226F*4+GC_BASE */
    UINT32 rlcPg  = R(0x3D64);  /* RLC_PG_ALWAYS_ON_WGP_MASK */
    printf("  SPI_PG_STATIC  @ 0x34FC (old) = 0x%08X\n", spiOld);
    printf("  SPI_PG_STATIC  @ 0x5C3C (new) = 0x%08X\n", spiNew);
    printf("  CC_ARRAY_CFG  @ 0x3264 (old) = 0x%08X\n", ccOld);
    printf("  CC_ARRAY_CFG  @ 0x9C1C (new) = 0x%08X\n", ccNew);
    printf("  RLC_PG_ALWAYS @ 0x3D64       = 0x%08X\n", rlcPg);

    /* SECTION 7: CP_HQD registers check */
    printf("\n--- CP_HQD Queue Check ---\n");
    /* BAR5 = GC_BASE(0x1260) + 0xC800 + offsets */
    UINT32 hqdActive = R(0xDAC0);
    UINT32 hqdVmid   = R(0xDAC4);
    UINT32 hqdPersist = R(0xDAC8);
    UINT32 hqdPQBase = R(0xDAD8);
    UINT32 hqdPQCtrl = R(0xDAFC);
    UINT32 hqdWPtrLo = R(0xDB90);
    UINT32 mqdBase   = R(0xDAB8);
    printf("  CP_HQD_ACTIVE       @ 0xDAC0 = 0x%08X\n", hqdActive);
    printf("  CP_HQD_VMID         @ 0xDAC4 = 0x%08X\n", hqdVmid);
    printf("  CP_HQD_PERSISTENT   @ 0xDAC8 = 0x%08X\n", hqdPersist);
    printf("  CP_HQD_PQ_BASE_LO   @ 0xDAD8 = 0x%08X\n", hqdPQBase);
    printf("  CP_HQD_PQ_CONTROL   @ 0xDAFC = 0x%08X\n", hqdPQCtrl);
    printf("  CP_HQD_PQ_WPTR_LO   @ 0xDB90 = 0x%08X\n", hqdWPtrLo);
    printf("  CP_MQD_BASE_ADDR    @ 0xDAB8 = 0x%08X\n", mqdBase);

    /* SECTION 8: GCVM status */
    printf("\n--- GCVM ---\n");
    printf("  GCVM_CONTEXT0_CNTL   @ 0xB460 = 0x%08X\n", R(0xB460));
    printf("  GCVM_PT_BASE_LO      @ 0x6C8C = 0x%08X\n", R(0x6C8C));
    printf("  GCVM_PT_BASE_HI      @ 0x6C90 = 0x%08X\n", R(0x6C90));
    printf("  GCVM_L2_CNTL         @ 0xB360 = 0x%08X\n", R(0xB360));

    /* SECTION 9: KIQ registers */
    printf("\n--- KIQ ---\n");
    printf("  KIQ_BASE_LO  @ 0xE060 = 0x%08X\n", R(0xE060));
    printf("  KIQ_CNTL     @ 0xE068 = 0x%08X\n", R(0xE068));
    printf("  KIQ_ACTIVE   @ 0xE080 = 0x%08X\n", R(0xE080));
    printf("  KIQ_RPTR     @ 0xE06C = 0x%08X\n", R(0xE06C));
    printf("  KIQ_WPTR     @ 0xE078 = 0x%08X\n", R(0xE078));

    /* SECTION 10: Try writing test value to COMPUTE registers via GRBM_GFX_CNTL */
    printf("\n--- COMPUTE REG WRITE TEST ---\n");
    printf("  (Will try to write DEBUG value to RSRC1 via GRBM_GFX_CNTL ME=1)\n");
    /* Save current state */
    W(0x2022, origCntl);  /* restore GRBM_GFX_CNTL first */
    Sleep(10);
    UINT32 before = R(0xDC68);
    /* Select ME=1 via GRBM_GFX_CNTL then write RSRC1 */
    W(0x2022, 1 << 16);  /* ME=1 via GRBM_GFX_CNTL */
    Sleep(10);
    UINT32 rsrc1Save = R(0xDC68);
    W(0xDC68, 0xDEAD0000);  /* Write test pattern to lower 16 bits */
    Sleep(10);
    UINT32 after = R(0xDC68);
    W(0xDC68, rsrc1Save & 0xFFFF);  /* Restore */
    W(0x2022, origCntl);
    printf("  RSRC1 before: 0x%08X -> after: 0x%08X (wrote 0xDEAD0000) %s\n",
        before, after, (after != before) ? "CHANGED!" : "(unchanged)");

    /* SECTION 11: Try writing test value to COMPUTE registers via GRBM_GFX_INDEX */
    UINT32 before2 = R(0xDC68);
    W(0x34D0, 1 << 16);  /* ME=1 via GRBM_GFX_INDEX */
    Sleep(10);
    UINT32 rsrc1Save2 = R(0xDC68);
    W(0xDC68, 0x0000DEAD);
    Sleep(10);
    UINT32 after2 = R(0xDC68);
    W(0xDC68, rsrc1Save2 & 0xFFFF);
    W(0x34D0, origIdx);
    printf("  Via INDEX:   0x%08X -> after: 0x%08X (wrote 0x0000DEAD) %s\n",
        before2, after2, (after2 != before2) ? "CHANGED!" : "(unchanged)");

    CloseHandle(gH);
    printf("\n=== Probe Complete ===\n");
    return 0;
}
