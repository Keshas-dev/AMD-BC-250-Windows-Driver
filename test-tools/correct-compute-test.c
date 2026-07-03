#include <windows.h>
#include <stdio.h>
#include <memory.h>

#define IOCTL_INIT_HW            0x80000B80
#define IOCTL_READ_REG           0x80000B88
#define IOCTL_WRITE_REG          0x80000B8C
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
static void WritePhys(UINT64 pa, const void* data, ULONG size) {
    UCHAR buf[4096 + 12];
    ((PULONG)buf)[0] = (ULONG)(pa & 0xFFFFFFFF);
    ((PULONG)buf)[1] = (ULONG)(pa >> 32);
    ((PULONG)buf)[2] = size;
    memcpy(buf + 12, data, size);
    DWORD br = 0;
    DeviceIoControl(gH, IOCTL_WRITE_PHYSICAL_MEM, buf, 12 + size, NULL, 0, &br, NULL);
}

/*
 * CORRECT COMPUTE REGISTER ADDRESSES
 * Formula: BAR5 = GC_BASE(0x1260) + mmDWORD * 4
 * All COMPUTE registers have BASE_IDX=0 (not SEG1!)
 */

/* GRBM */
#define GRBM_STATUS         0x3260
#define GRBM_STATUS2        0x326C
#define GRBM_GFX_INDEX      0x34D0  /* empirically confirmed */
#define GRBM_GFX_CNTL       0x4968  /* mm=0x0dc2, CORRECT, never probed! */

/* GCVM */
#define GCVM_CONTEXT0_CNTL  0xB460

/* SPI PG */
#define SPI_PG_MASK         0x34FC

/* CC config */
#define CC_ARRAY_CONFIG     0x9C1C  /* cc_collective offset */

/* CORRECT COMPUTE register addresses (BASE_IDX=0) */
#define COMPUTE_DISPATCH_INITIATOR  0x80E0  /* mm=0x1ba0 */
#define COMPUTE_DIM_X               0x80E4  /* mm=0x1ba1 */
#define COMPUTE_DIM_Y               0x80E8  /* mm=0x1ba2 */
#define COMPUTE_DIM_Z               0x80EC  /* mm=0x1ba3 */
#define COMPUTE_START_X             0x80F0  /* mm=0x1ba4 */
#define COMPUTE_START_Y             0x80F4  /* mm=0x1ba5 */
#define COMPUTE_START_Z             0x80F8  /* mm=0x1ba6 */
#define COMPUTE_NUM_THREAD_X        0x80FC  /* mm=0x1ba7 */
#define COMPUTE_NUM_THREAD_Y        0x8100  /* mm=0x1ba8 */
#define COMPUTE_NUM_THREAD_Z        0x8104  /* mm=0x1ba9 */
#define COMPUTE_PGM_LO              0x8110  /* mm=0x1bac */
#define COMPUTE_PGM_HI              0x8114  /* mm=0x1bad */
#define COMPUTE_PGM_RSRC1           0x8128  /* mm=0x1bb2 */
#define COMPUTE_PGM_RSRC2           0x812C  /* mm=0x1bb3 */
#define COMPUTE_STATIC_THREAD_MGMT  0x8138  /* mm=0x1bb6, SE0 */
#define COMPUTE_TMPRING_SIZE        0x8140  /* mm=0x1bb8 */
#define COMPUTE_USER_DATA_0         0x81E0  /* mm=0x1be0 */

/* CORRECT CP_HQD registers (also BASE_IDX=0) */
#define CP_MQD_BASE_ADDR     0x9104  /* mm=0x1fa9 */
#define CP_MQD_BASE_ADDR_HI  0x9108  /* mm=0x1faa */
#define CP_HQD_ACTIVE        0x910C  /* mm=0x1fab */
#define CP_HQD_VMID          0x9110  /* mm=0x1fac */
#define CP_HQD_PQ_BASE       0x9124  /* mm=0x1fb1 */
#define CP_HQD_PQ_BASE_HI    0x9128  /* mm=0x1fb2 */
#define CP_HQD_PQ_RPTR       0x912C  /* mm=0x1fb3 */
#define CP_HQD_PQ_WPTR_LO    0x91DC  /* mm=0x1fdf */
#define CP_HQD_PQ_CONTROL    0x9148  /* mm=0x1fba */

/* WRONG addresses from old hw.h (for comparison) */
#define OLD_COMPUTE_DISPATCH 0xDC60
#define OLD_COMPUTE_START    0xDC64
#define OLD_COMPUTE_PGM_LO   0xDC70
#define OLD_CP_HQD_ACTIVE    0xDAC0
#define OLD_CP_MQD_BASE      0xDAB8
#define OLD_GRBM_GFX_CNTL    0x2022  /* WRONG - should be 0x4968 */

#define SHADER_ADDR          0xC0100000ULL
#define SHADER_SIZE          64  /* 16 DWORDs */

int main(void) {
    printf("=== CORRECT COMPUTE ADDRESS PROBE + DISPATCH ===\n\n");
    gH = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (gH == INVALID_HANDLE_VALUE) { printf("ERROR: can't open device\n"); return 1; }

    UINT8 initBuf[32] = {0};
    *(UINT64*)(initBuf+0)  = 0xFE800000ULL;
    *(UINT32*)(initBuf+8)  = 0x00080000;
    *(UINT32*)(initBuf+12) = 1;
    *(UINT64*)(initBuf+16) = 0xC0000000ULL;
    *(UINT32*)(initBuf+24) = 0x20000000;
    DWORD br = 0;
    DeviceIoControl(gH, IOCTL_INIT_HW, initBuf, sizeof(initBuf), NULL, 0, &br, NULL);

    /* ========== PHASE 1: PROBE REGISTERS ========== */
    printf("\n--- PHASE 1: Register Probe ---\n");

    /* 1a: CORRECT COMPUTE registers (0x80E0-0x8140) */
    printf("\n[1a] CORRECT COMPUTE block (0x80E0-0x8140):\n");
    for (UINT32 off = 0x80E0; off <= 0x8148; off += 4) {
        UINT32 v = R(off);
        printf("  0x%04X = 0x%08X", off, v);
        /* Try write-back test */
        if (off >= 0x80E4 && off <= 0x8140 && off != 0x80E0 && off != 0x80E4) {
            UINT32 w = 0xDEADBEEF;
            W(off, w);
            UINT32 r = R(off);
            if (r == w) printf(" <-- WRITABLE");
            else printf(" <-- read-only (now=0x%08X)", r);
            /* Restore if changed to something meaningful */
            W(off, 0);
            r = R(off);
            if (r != 0) printf(" [restored=0x%08X]", r);
        }
        printf("\n");
    }

    /* Skip write test for DISPATCH_INITIATOR and DIM_X */
    /* Test DISPATCH_INITIATOR specially: VALID is W1C */
    {
        UINT32 v = R(COMPUTE_DISPATCH_INITIATOR);
        printf("\n  DISPATCH_INITIATOR(0x80E0) initial = 0x%08X\n", v);
        /* Write with VALID to test if consumed */
        W(COMPUTE_DISPATCH_INITIATOR, 0x8003);  /* USE_THREAD_DIM | VALID | COMPUTE_SHADER_EN */
        UINT32 v2 = R(COMPUTE_DISPATCH_INITIATOR);
        printf("  After VALID write: 0x%08X (VALID consumed? %s)\n", v2, (v2 & 2) ? "NO" : "YES");
    }

    /* 1b: CORRECT CP_HQD registers (0x9104-0x91F0) */
    printf("\n[1b] CORRECT CP_HQD block (0x9104-0x91F0):\n");
    UINT32 hqdRegs[] = {
        CP_MQD_BASE_ADDR, 0x9108, CP_HQD_ACTIVE, 0x9110, 0x9114, 0x9118, 0x911C,
        0x9120, CP_HQD_PQ_BASE, CP_HQD_PQ_BASE_HI, CP_HQD_PQ_RPTR, 
        0x9130, 0x9134, 0x9138, 0x913C, 0x9140, 0x9144, CP_HQD_PQ_CONTROL,
        0x914C, 0x9150, 0x9154, 0x9158, 0x915C,
        0x91DC, 0x91E0, 0x91E4, 0x91E8, 0x91EC, 0x91F0
    };
    for (int i = 0; i < sizeof(hqdRegs)/sizeof(hqdRegs[0]); i++) {
        UINT32 off = hqdRegs[i];
        UINT32 v = R(off);
        printf("  0x%04X = 0x%08X\n", off, v);
    }

    /* 1c: OLD addresses for comparison */
    printf("\n[1c] OLD (wrong) addresses:\n");
    printf("  OLD GRBM_GFX_CNTL(0x2022) = 0x%08X\n", R(0x2022));
    printf("  NEW GRBM_GFX_CNTL(0x4968) = 0x%08X\n", R(GRBM_GFX_CNTL));
    printf("  OLD COMPUTE_DISPATCH(0xDC60) = 0x%08X\n", R(OLD_COMPUTE_DISPATCH));
    printf("  OLD COMPUTE_START(0xDC64) = 0x%08X\n", R(OLD_COMPUTE_START));
    printf("  OLD PGM_LO(0xDC70) = 0x%08X\n", R(OLD_COMPUTE_PGM_LO));
    printf("  OLD CP_HQD_ACTIVE(0xDAC0) = 0x%08X\n", R(OLD_CP_HQD_ACTIVE));
    printf("  OLD CP_MQD_BASE(0xDAB8) = 0x%08X\n", R(OLD_CP_MQD_BASE));
    printf("  NEW CP_MQD_BASE(0x9104) = 0x%08X\n", R(CP_MQD_BASE_ADDR));
    printf("  NEW CP_HQD_ACTIVE(0x910C) = 0x%08X\n", R(CP_HQD_ACTIVE));

    /* 1d: Full COMPUTE extended scan 0x8200-0x8300 */
    printf("\n[1d] Extended COMPUTE scan [0x8200-0x8300]:\n");
    int found = 0;
    for (UINT32 off = 0x8200; off <= 0x82FF; off += 4) {
        UINT32 v = R(off);
        if (v != 0) { printf("  0x%04X = 0x%08X\n", off, v); found++; }
    }
    if (!found) printf("  All zero\n");

    /* 1e: GRBM_GFX_CNTL at CORRECT address */
    printf("\n[1e] GRBM_GFX_CNTL (CORRECT 0x4968):\n");
    UINT32 gfxCntl = R(GRBM_GFX_CNTL);
    printf("  = 0x%08X\n", gfxCntl);
    /* Write KIQ select (ME=1) */
    W(GRBM_GFX_CNTL, 0x00010000);
    printf("  After ME=1 write: 0x%08X\n", R(GRBM_GFX_CNTL));
    W(GRBM_GFX_CNTL, 0);  /* restore */
    W(GRBM_GFX_INDEX, 0xE0000000);  /* restore GRBM_GFX_INDEX to broadcast */

    /* ========== PHASE 2: SIMPLE DISPATCH ========== */
    printf("\n--- PHASE 2: Simple Dispatch Test ---\n");

    /* Save registers */
    UINT32 savedGfxIdx = R(GRBM_GFX_INDEX);
    UINT32 savedGcvm = R(GCVM_CONTEXT0_CNTL);
    UINT32 savedSpi = R(SPI_PG_MASK);
    UINT32 savedCcCfg = R(CC_ARRAY_CONFIG);

    /* Save ALL compute registers */
    UINT32 savedCompute[10];
    savedCompute[0] = R(COMPUTE_DIM_X);
    savedCompute[1] = R(COMPUTE_DIM_Y);
    savedCompute[2] = R(COMPUTE_DIM_Z);
    savedCompute[3] = R(COMPUTE_NUM_THREAD_X);
    savedCompute[4] = R(COMPUTE_NUM_THREAD_Y);
    savedCompute[5] = R(COMPUTE_NUM_THREAD_Z);
    savedCompute[6] = R(COMPUTE_PGM_LO);
    savedCompute[7] = R(COMPUTE_PGM_HI);
    savedCompute[8] = R(COMPUTE_PGM_RSRC1);
    savedCompute[9] = R(COMPUTE_PGM_RSRC2);

    /* Enable WGPs, disable GCVM */
    W(GCVM_CONTEXT0_CNTL, savedGcvm & ~1);  /* clear TRANSLATION_ENABLE */
    W(SPI_PG_MASK, 0xFFFFFFFF);  /* max WGPs */
    W(CC_ARRAY_CONFIG, 0xFFE00000);
    printf("  GCVM=OFF, SPI_PG=MAX, CC_ARRAY=0xFFE00000\n");

    /* Write shader to VRAM */
    UINT32 shader[16] = {0};
    shader[0] = 0xBF9F0000;  /* s_endpgm */
    WritePhys(SHADER_ADDR, shader, sizeof(shader));
    printf("  Shader written to 0x%08X: [0]=0x%08X\n", (UINT32)SHADER_ADDR, shader[0]);

    /* Get initial GRBM status */
    UINT32 grbm0 = R(GRBM_STATUS);
    printf("  GRBM_STATUS before = 0x%08X\n", grbm0);

    /* Write compute registers */
    W(COMPUTE_DIM_X, 1);
    W(COMPUTE_DIM_Y, 1);
    W(COMPUTE_DIM_Z, 1);
    W(COMPUTE_NUM_THREAD_X, 32);
    W(COMPUTE_NUM_THREAD_Y, 1);
    W(COMPUTE_NUM_THREAD_Z, 1);
    W(COMPUTE_PGM_LO, (UINT32)(SHADER_ADDR >> 8));  /* 0x00C01000 */
    W(COMPUTE_PGM_HI, (UINT32)(SHADER_ADDR >> 40)); /* 0x00000000 */
    W(COMPUTE_PGM_RSRC1, 0x00000000);  /* minimal VGPRs */
    W(COMPUTE_PGM_RSRC2, 0x00000000);  /* no LDS */

    printf("  Compute registers written:\n");
    printf("    PGM_LO=0x%08X PGM_HI=0x%08X\n", R(COMPUTE_PGM_LO), R(COMPUTE_PGM_HI));
    printf("    DIM=%d,%d,%d THREADS=%d,%d,%d\n",
        R(COMPUTE_DIM_X), R(COMPUTE_DIM_Y), R(COMPUTE_DIM_Z),
        R(COMPUTE_NUM_THREAD_X), R(COMPUTE_NUM_THREAD_Y), R(COMPUTE_NUM_THREAD_Z));

    /* Verify PGM_LO reads back correctly */
    UINT32 pgmLo = R(COMPUTE_PGM_LO);
    UINT32 pgmHi = R(COMPUTE_PGM_HI);
    UINT64 pgmAddr = ((UINT64)pgmHi << 40) | ((UINT64)pgmLo << 8);
    printf("    Decoded PGM addr: 0x%016llX\n", pgmAddr);

    /* DISPATCH! Write DISPATCH_INITIATOR with VALID */
    printf("  Triggering DISPATCH_INITIATOR (0x80E0) with 0x8003...\n");
    W(COMPUTE_DISPATCH_INITIATOR, 0x8003);
    /* USE_THREAD_DIM(bit15) | VALID(bit1) | COMPUTE_SHADER_EN(bit0) */

    /* Check register after VALID */
    UINT32 initAfter = R(COMPUTE_DISPATCH_INITIATOR);
    printf("  DISPATCH_INITIATOR after = 0x%08X (VALID=%s)\n",
        initAfter, (initAfter & 2) ? "STILL SET" : "consumed");

    /* Check GRBM status for compute/busy bits */
    Sleep(100);  /* 100ms for shader to execute */
    UINT32 grbm1 = R(GRBM_STATUS);
    printf("  GRBM_STATUS after (100ms) = 0x%08X\n", grbm1);

    /* Check if any engine became busy */
    UINT32 diff = grbm0 ^ grbm1;
    UINT32 busyBits = grbm1 & 0xFFFFF000;  /* busy bits in high part */
    printf("  Busy bits: 0x%08X (non-zero = some engine active)\n", busyBits);

    /* Try two-phase: COMPUTE_SHADER_EN first, then VALID */
    printf("\n  Attempting two-phase dispatch...\n");
    W(COMPUTE_DISPATCH_INITIATOR, 0x0001);  /* COMPUTE_SHADER_EN only */
    UINT32 phase1 = R(COMPUTE_DISPATCH_INITIATOR);
    printf("    Phase1 (SHADER_EN): 0x%08X\n", phase1);

    W(COMPUTE_DISPATCH_INITIATOR, 0x0003);  /* VALID | SHADER_EN */
    UINT32 phase2 = R(COMPUTE_DISPATCH_INITIATOR);
    printf("    Phase2 (VALID): 0x%08X (consumed=%s)\n",
        phase2, (phase2 & 2) ? "NO" : "YES");

    Sleep(100);
    UINT32 grbm2 = R(GRBM_STATUS);
    printf("  GRBM_STATUS after phase2 = 0x%08X\n", grbm2);

    /* ========== PHASE 3: MQD ACTIVATION + DISPATCH VALID ========== */
    printf("\n--- PHASE 3: MQD Activation + DISPATCH VALID ---\n");

    /* Write MQD to VRAM with correct v10_compute_mqd layout */
    UINT32 mqd[512] = {0};
    mqd[0]  = 0xC0310800;  /* header */
    mqd[1]  = 0x00000001;   /* COMPUTE_SHADER_EN=1, VALID=0 (will trigger after MQD load) */
    mqd[2]  = 1; mqd[3] = 1; mqd[4] = 1;   /* DIM_X/Y/Z */
    mqd[5]  = 0; mqd[6] = 0; mqd[7] = 0;   /* START_X/Y/Z = 0 */
    mqd[8]  = 32; mqd[9] = 1; mqd[10] = 1; /* NUM_THREAD_X/Y/Z */
    mqd[11] = 1;  /* pipeline_stat_enable */
    mqd[12] = 0;  /* loops */
    mqd[13] = 0;  /* pad */
    /* DW14/DW15: PGM_LO/HI (v10_compute_mqd offset) */
    mqd[14] = (UINT32)(SHADER_ADDR >> 8);   /* PGM_LO */
    mqd[15] = (UINT32)(SHADER_ADDR >> 40);  /* PGM_HI */
    /* DW16/DW17: TBA_LO/HI (exception handler base addr) - leave 0 */
    /* DW18/DW19: TMA_LO/HI - leave 0 */
    /* DW20/DW21: PGM_RSRC1/RSRC2 */
    mqd[20] = 0x00000000;  /* PGM_RSRC1: min VGPRS+SGPRS */
    /* bits[5:0]=USER_DATA_SIZE(0), bits[16:21]=VGPRS(0=4VGPRs min), bits[26:31]=SGPRS(0=8SGPRs min) */
    mqd[21] = 0x00000000;  /* PGM_RSRC2: no LDS */
    mqd[22] = 0;  /* VMID */
    mqd[23] = 0x000000FF;  /* resource_limits */
    mqd[24] = 0xFFFFFFFF;  /* static_thread_mgmt_se0 */
    mqd[25] = 0xFFFFFFFF;  /* static_thread_mgmt_se1 */
    /* DW26-DW33: reserved */
    /* DW34-DW35: tcp_ctx_config_raw */
    /* DW36: reserved2 */
    /* DW37-DW52: user_data[16] */
    /* Region 2 starts at DW128 - CP_HQD state */
    /* For now, leave Region 2 as zeros (hardware will use defaults + MMIO values for HQD) */
    UINT64 mqdAddr = 0xC0300000ULL;
    WritePhys(mqdAddr, mqd, sizeof(mqd));
    printf("  MQD written to 0x%016llX\n", mqdAddr);

    /* Save and set MQD_BASE */
    UINT32 oldMqd = R(CP_MQD_BASE_ADDR);
    UINT32 oldMqdHi = R(CP_MQD_BASE_ADDR_HI);
    UINT32 oldHqdActive = R(CP_HQD_ACTIVE);
    printf("  CP_MQD_BASE(0x9104) before = 0x%08X (HI=0x%08X)\n", oldMqd, oldMqdHi);
    printf("  CP_HQD_ACTIVE(0x910C) before = 0x%08X\n", oldHqdActive);

    W(CP_MQD_BASE_ADDR, (UINT32)(mqdAddr >> 8));
    W(CP_MQD_BASE_ADDR_HI, (UINT32)(mqdAddr >> 40));
    printf("  MQD_BASE set to 0x%016llX\n", mqdAddr);

    /* Load MQD into internal registers via HQD_ACTIVE */
    W(CP_HQD_ACTIVE, 1);
    Sleep(10);
    UINT32 hqdAfter = R(CP_HQD_ACTIVE);
    printf("  CP_HQD_ACTIVE after = 0x%08X (ACK=%s)\n",
        hqdAfter, (hqdAfter & 1) ? "YES" : "NO");

    /* Check GRBM status after MQD activation */
    UINT32 grbm3 = R(GRBM_STATUS);
    printf("  GRBM_STATUS after MQD load = 0x%08X\n", grbm3);

    /* Read PGM_LO to verify MQD was loaded */
    UINT32 pgmLoNew = R(COMPUTE_PGM_LO);
    UINT32 pgmHiNew = R(COMPUTE_PGM_HI);
    printf("  PGM_LO after MQD = 0x%08X (expected 0x%08X)\n",
        pgmLoNew, (UINT32)(SHADER_ADDR >> 8));

    /* NOW trigger DISPATCH VALID (this was missed in Phase 2!) */
    printf("\n  >>> Triggering DISPATCH VALID after MQD load...\n");
    W(COMPUTE_DISPATCH_INITIATOR, 0x0003);  /* VALID | COMPUTE_SHADER_EN */
    UINT32 initMqd = R(COMPUTE_DISPATCH_INITIATOR);
    printf("  DISPATCH_INITIATOR after = 0x%08X (VALID consumed=%s)\n",
        initMqd, (initMqd & 2) ? "NO" : "YES");

    Sleep(200);
    UINT32 grbm4 = R(GRBM_STATUS);
    printf("  GRBM_STATUS after MQD+VALID (200ms) = 0x%08X\n", grbm4);

    /* Phase 3b: MQD + GRBM select ME=1 + VALID */
    printf("\n--- PHASE 3b: MQD + GRBM ME=1 select + VALID ---\n");
    W(GRBM_GFX_INDEX, 0x00010000);  /* ME=1, PIPE=0, QUEUE=0 */
    W(COMPUTE_DISPATCH_INITIATOR, 0x0003);  /* VALID | SHADER_EN */
    UINT32 initME1 = R(COMPUTE_DISPATCH_INITIATOR);
    printf("  DISPATCH_INITIATOR with ME=1: 0x%08X (VALID=%s)\n",
        initME1, (initME1 & 2) ? "STILL SET" : "consumed");
    W(GRBM_GFX_INDEX, 0xE0000000);  /* restore broadcast */
    Sleep(200);
    UINT32 grbm3b = R(GRBM_STATUS);
    printf("  GRBM_STATUS = 0x%08X\n", grbm3b);

    /* Phase 3c: RLC_CP_SCHEDULERS enable */
    printf("\n--- PHASE 3c: MQD + RLC_CP_SCHEDULERS enable ---\n");
    UINT32 oldRlc = R(0xECA8);
    W(0xECA8, 0x000000A0);  /* enable | ME=1, PIPE=0, QUEUE=0 */
    printf("  RLC_CP_SCHEDULERS(0xECA8) wrote 0xA0, reads 0x%08X\n", R(0xECA8));
    W(COMPUTE_DISPATCH_INITIATOR, 0x0003);  /* VALID | SHADER_EN */
    Sleep(200);
    UINT32 grbm3c = R(GRBM_STATUS);
    printf("  GRBM_STATUS = 0x%08X\n", grbm3c);
    W(0xECA8, oldRlc);

    /* Phase 3d: MQD with complete Region 2 (CP_HQD state) */
    printf("\n--- PHASE 3d: Full MQD + PQ setup + VALID ---\n");
    /* Save HQD state */
    UINT32 oldPqBase = R(CP_HQD_PQ_BASE);
    UINT32 oldPqCtrl = R(CP_HQD_PQ_CONTROL);
    UINT32 oldWptr = R(CP_HQD_PQ_WPTR_LO);
    UINT32 oldVmid = R(CP_HQD_VMID);

    /* Write full MQD with Region 2 */
    mqd[128+0] = 0;               /* reserved */
    mqd[128+1] = 1;               /* CP_HQD_ACTIVE = 1 */
    mqd[128+2] = 0;               /* CP_HQD_VMID = 0 */
    mqd[128+3] = 0x00000001;      /* PERSISTENT_STATE: ORD=1 */
    mqd[128+4] = 0;               /* PIPE_PRIORITY */
    mqd[128+5] = 0;               /* QUEUE_PRIORITY */
    mqd[128+8] = (UINT32)(mqdAddr >> 8);  /* MQD_BASE_LO */
    mqd[128+9] = (UINT32)(mqdAddr >> 40); /* MQD_BASE_HI */
    WritePhys(mqdAddr, mqd, sizeof(mqd));
    printf("  Full MQD (with Region 2) re-written\n");

    W(CP_MQD_BASE_ADDR, (UINT32)(mqdAddr >> 8));
    W(CP_MQD_BASE_ADDR_HI, (UINT32)(mqdAddr >> 40));
    W(CP_HQD_VMID, 0);
    W(CP_HQD_PQ_CONTROL, 0x00000000);  /* disabled */
    W(CP_HQD_ACTIVE, 1);
    Sleep(10);
    printf("  CP_HQD_ACTIVE after full MQD = 0x%08X\n", R(CP_HQD_ACTIVE));

    W(COMPUTE_DISPATCH_INITIATOR, 0x0003);
    Sleep(200);
    UINT32 grbm3d = R(GRBM_STATUS);
    printf("  GRBM_STATUS after full MQD+VALID = 0x%08X\n", grbm3d);

    /* Phase 3e: ME=1 select + RLC scheduler + MQD */
    printf("\n--- PHASE 3e: ME=1 + RLC + MQD + VALID ---\n");
    W(GRBM_GFX_INDEX, 0x00010000);  /* ME=1 */
    W(0xECA8, 0x000000A0);  /* RLC enable */
    W(CP_HQD_ACTIVE, 1);
    Sleep(10);
    W(COMPUTE_DISPATCH_INITIATOR, 0x0003);
    W(GRBM_GFX_INDEX, 0xE0000000);  /* restore broadcast */
    Sleep(200);
    UINT32 grbm3e = R(GRBM_STATUS);
    printf("  GRBM_STATUS = 0x%08X\n", grbm3e);
    W(0xECA8, oldRlc);

    /* Restore HQD state */
    W(CP_HQD_VMID, oldVmid);
    W(CP_HQD_PQ_BASE, oldPqBase);
    W(CP_HQD_PQ_CONTROL, oldPqCtrl);
    W(CP_HQD_PQ_WPTR_LO, oldWptr);

    /* Phase 3f: MQD with VALID=1 INSIDE MQD (not written separately) */
    printf("\n--- PHASE 3f: MQD with VALID=1 INSIDE + CP_HQD_ACTIVE load ---\n");
    W(GRBM_GFX_INDEX, 0xE0000000);  /* broadcast */
    W(0xECA8, 0x000000A0);  /* RLC enable: ME=1, PIPE=0, QUEUE=0 */
    W(CP_HQD_ACTIVE, 0);    /* deactivate first */
    Sleep(10);

    /* Write MQD with VALID=1 in dispatch_initiator */
    mqd[1] = 0x00000003;  /* COMPUTE_SHADER_EN | VALID (BOTH, will fire on load) */
    WritePhys(mqdAddr, mqd, sizeof(mqd));
    printf("  MQD with VALID=1 written to 0x%016llX\n", mqdAddr);

    W(CP_MQD_BASE_ADDR, (UINT32)(mqdAddr >> 8));
    W(CP_MQD_BASE_ADDR_HI, (UINT32)(mqdAddr >> 40));
    W(CP_HQD_ACTIVE, 1);
    Sleep(50);
    UINT32 actF = R(CP_HQD_ACTIVE);
    printf("  CP_HQD_ACTIVE after = 0x%08X (ACK=%s)\n", actF, (actF & 1) ? "YES" : "NO");
    UINT32 grbm3f = R(GRBM_STATUS);
    printf("  GRBM_STATUS = 0x%08X\n", grbm3f);

    /* Phase 3g: MQD with VALID=1 + GRBM ME=1 select */
    printf("\n--- PHASE 3g: MQD VALID=1 + GRBM ME=1 ---\n");
    W(GRBM_GFX_INDEX, 0x00010000);  /* ME=1 */
    W(CP_HQD_ACTIVE, 0);
    Sleep(10);
    W(CP_HQD_ACTIVE, 1);
    Sleep(50);
    W(GRBM_GFX_INDEX, 0xE0000000);  /* broadcast */
    UINT32 grbm3g = R(GRBM_STATUS);
    printf("  GRBM_STATUS = 0x%08X\n", grbm3g);

    /* Phase 3h: Try writing 0xFFFFFFFF to DISPATCH_INITIATOR to see if it stores anything */
    printf("\n--- PHASE 3h: DISPATCH_INITIATOR all-bits test ---\n");
    UINT32 initBefore3h = R(COMPUTE_DISPATCH_INITIATOR);
    printf("  Before: 0x%08X\n", initBefore3h);
    W(COMPUTE_DISPATCH_INITIATOR, 0xFFFFFFFF);
    UINT32 initAfter3h = R(COMPUTE_DISPATCH_INITIATOR);
    printf("  After write 0xFFFFFFFF: 0x%08X (all-bits W1C=%s)\n",
        initAfter3h, (initAfter3h == 0) ? "YES, self-clears" : "NO, some bits survived");

    /* ========== PHASE 4: TRY OLD DISPATCH (for comparison) ========== */
    printf("\n--- PHASE 4: OLD Dispatch Path Comparison ---\n");
    UINT32 oldVal = R(OLD_COMPUTE_DISPATCH);
    printf("  OLD DISPATCH(0xDC60) reads = 0x%08X\n", oldVal);
    W(OLD_COMPUTE_DISPATCH, 0x00030000);
    printf("  OLD DISPATCH after 0x00030000 write = 0x%08X\n", R(OLD_COMPUTE_DISPATCH));
    Sleep(100);
    printf("  GRBM_STATUS = 0x%08X\n", R(GRBM_STATUS));

    /* ========== PHASE 5: GRBM_GFX_INDEX vs GRBM_GFX_CNTL ========== */
    printf("\n--- PHASE 5: GRBM Select Register Comparison ---\n");
    UINT32 idx = R(GRBM_GFX_INDEX);
    UINT32 cntl = R(0x4968);
    printf("  GRBM_GFX_INDEX(0x34D0) = 0x%08X\n", idx);
    printf("  GRBM_GFX_CNTL(0x4968) = 0x%08X\n", cntl);
    printf("  GRBM_GFX_CNTL(0x2022, old) = 0x%08X\n", R(0x2022));

    /* ========== RESTORE ========== */
    printf("\n--- Restoring registers ---\n");
    W(COMPUTE_DIM_X, savedCompute[0]);
    W(COMPUTE_DIM_Y, savedCompute[1]);
    W(COMPUTE_DIM_Z, savedCompute[2]);
    W(COMPUTE_NUM_THREAD_X, savedCompute[3]);
    W(COMPUTE_NUM_THREAD_Y, savedCompute[4]);
    W(COMPUTE_NUM_THREAD_Z, savedCompute[5]);
    W(COMPUTE_PGM_LO, savedCompute[6]);
    W(COMPUTE_PGM_HI, savedCompute[7]);
    W(COMPUTE_PGM_RSRC1, savedCompute[8]);
    W(COMPUTE_PGM_RSRC2, savedCompute[9]);
    W(CC_ARRAY_CONFIG, savedCcCfg);
    W(SPI_PG_MASK, savedSpi);
    W(GCVM_CONTEXT0_CNTL, savedGcvm);
    W(CP_MQD_BASE_ADDR, oldMqd);
    W(CP_MQD_BASE_ADDR_HI, oldMqdHi);
    W(CP_HQD_ACTIVE, oldHqdActive);
    W(COMPUTE_DISPATCH_INITIATOR, 0);
    W(GRBM_GFX_INDEX, savedGfxIdx);

    CloseHandle(gH);
    printf("\n=== Done ===\n");
    return 0;
}
