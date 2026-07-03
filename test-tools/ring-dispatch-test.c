#include <windows.h>
#include <stdio.h>
#include <memory.h>

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
static void WritePhys(UINT64 pa, const void* data, ULONG size) {
    UCHAR buf[4096 + 12];
    ((PULONG)buf)[0] = (ULONG)(pa & 0xFFFFFFFF);
    ((PULONG)buf)[1] = (ULONG)(pa >> 32);
    ((PULONG)buf)[2] = size;
    memcpy(buf + 12, data, size);
    DWORD br = 0;
    DeviceIoControl(gH, IOCTL_WRITE_PHYSICAL_MEM, buf, 12 + size, NULL, 0, &br, NULL);
}

static ULONG ReadPhys(UINT64 pa, void* data, ULONG size) {
    UCHAR inbuf[24], outbuf[4096];
    ((PULONG)inbuf)[0] = (ULONG)(pa & 0xFFFFFFFF);
    ((PULONG)inbuf)[1] = (ULONG)(pa >> 32);
    ((PULONG)inbuf)[2] = size;
    DWORD br = 0;
    DeviceIoControl(gH, IOCTL_READ_PHYSICAL_MEM, inbuf, 12, outbuf, sizeof(outbuf), &br, NULL);
    if (br > 0) memcpy(data, outbuf, min(br, size));
    return br;
}

/* CORRECT register addresses */
#define GC_BASE                 0x1260
#define COMPUTE_DISPATCH_INITIATOR  0x80E0  /* mm=0x1ba0 */
#define COMPUTE_PGM_LO              0x8110  /* mm=0x1bac */
#define COMPUTE_PGM_HI              0x8114  /* mm=0x1bad */
#define CP_MQD_BASE_ADDR       0x9104  /* mm=0x1fa9 */
#define CP_MQD_BASE_ADDR_HI    0x9108  /* mm=0x1faa */
#define CP_HQD_ACTIVE          0x910C  /* mm=0x1fab */
#define CP_HQD_VMID            0x9110  /* mm=0x1fac */
#define CP_HQD_PQ_BASE         0x9124  /* mm=0x1fb1 */
#define CP_HQD_PQ_BASE_HI      0x9128  /* mm=0x1fb2 */
#define CP_HQD_PQ_RPTR         0x912C  /* mm=0x1fb3 */
#define CP_HQD_PQ_CONTROL      0x9148  /* mm=0x1fba */
#define CP_HQD_PQ_WPTR_LO      0x91DC  /* mm=0x1fdf */
#define CP_HQD_PQ_WPTR_HI      0x91E0  /* mm=0x1fe0 */
#define GRBM_STATUS            0x3260
#define GRBM_GFX_INDEX         0x34D0
#define GCVM_CONTEXT0_CNTL     0xB460
#define SPI_PG_MASK            0x34FC
#define CC_ARRAY_CONFIG        0x9C1C
#define RLC_CP_SCHEDULERS      0xECA8

/* VRAM addresses */
#define SHADER_ADDR   0xC0100000ULL
#define MQD_ADDR      0xC0300000ULL
#define RING_ADDR     0xC0400000ULL
#define FENCE_ADDR    0xC0500000ULL

/* MQD Region 2 offsets (from DW128) */
#define MQD_DW_CP_HQD_ACTIVE         130  /* 128 + (0x1fab - 0x1fa9) */
#define MQD_DW_CP_HQD_VMID           131
#define MQD_DW_CP_HQD_PERSISTENT     132
#define MQD_DW_CP_HQD_PQ_BASE        136  /* 128 + (0x1fb1 - 0x1fa9) */
#define MQD_DW_CP_HQD_PQ_BASE_HI     137
#define MQD_DW_CP_HQD_PQ_RPTR        138
#define MQD_DW_CP_HQD_PQ_DOORBELL    143  /* 128 + (0x1fb8 - 0x1fa9) */
#define MQD_DW_CP_HQD_PQ_CONTROL     145  /* 128 + (0x1fba - 0x1fa9) */
#define MQD_DW_CP_HQD_PQ_WPTR_LO     182  /* 128 + (0x1fdf - 0x1fa9) */
#define MQD_DW_CP_HQD_PQ_WPTR_HI     183

/* CP_HQD_PQ_CONTROL encoding */
#define PQ_CONTROL_QUEUE_SIZE_1024   0x00040000  /* bits[15:8] = 0x04 → 1024 DWORDs */
#define PQ_CONTROL_UNORD             0x00000000  /* unordered mode */
#define PQ_CONTROL_RPTR_BLOCK_1      0x00000100  /* RPTR block size = 1 */
#define PQ_CONTROL_RPTR_BLOCK_64     0x00000700  /* RPTR block size = 64 */

int main(void) {
    printf("=== RING BUFFER + MQD DISPATCH TEST ===\n\n");
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

    /* ===== CONFIG: Enable WGPs, disable GCVM ===== */
    UINT32 savedGcvm = R(GCVM_CONTEXT0_CNTL);
    UINT32 savedSpi = R(SPI_PG_MASK);
    W(GCVM_CONTEXT0_CNTL, savedGcvm & ~1);
    W(SPI_PG_MASK, 0xFFFFFFFF);
    W(CC_ARRAY_CONFIG, 0xFFE00000);
    printf("Config: GCVM=OFF, SPI_PG=MAX\n");

    /* ===== STEP 1: Write shader to VRAM ===== */
    UINT32 shader[16] = {0};
    shader[0] = 0xBF9F0000;  /* s_endpgm */
    shader[1] = 0xBF9F0000;  /* s_endpgm (redundant) */
    WritePhys(SHADER_ADDR, shader, sizeof(shader));
    printf("1. Shader at 0x%08X\n", (UINT32)SHADER_ADDR);

    /* ===== STEP 2: Write ring buffer with PM4 DISPATCH_DIRECT ===== */
    UINT32 ring[1024] = {0};
    UINT32 wptr = 0;
    /* PM4 IT_DISPATCH_DIRECT: HEADER + DIM_X + DIM_Y + DIM_Z */
    ring[wptr++] = 0xC0031500;  /* type3(0x15<<8 | count=4) */
    ring[wptr++] = 1;  /* DIM_X */
    ring[wptr++] = 1;  /* DIM_Y */
    ring[wptr++] = 1;  /* DIM_Z */
    /* PM4 IT_EVENT_WRITE_EOP: signal fence */
    ring[wptr++] = 0xC0044700;  /* type3(0x47<<8 | count=5) */
    ring[wptr++] = 0x0000003A;  /* EVENT_TYPE=BOTTOM_OF_PIPE(0x3A) << 8 */
    ring[wptr++] = 0x00000000;  /* ADDR_LO */
    ring[wptr++] = (UINT32)(FENCE_ADDR & 0xFFFFFFFF);  /* ADDR_LO */
    ring[wptr++] = 1;           /* DATA = 1 (increment fence) */
    WritePhys(RING_ADDR, ring, sizeof(ring));
    printf("2. Ring buffer at 0x%08X, WPTR=%d\n", (UINT32)RING_ADDR, wptr);

    /* ===== STEP 3: Write MQD with Region 1 + Region 2 ===== */
    UINT32 mqd[512] = {0};
    /* Region 1: COMPUTE PIPE STATE */
    mqd[0]  = 0xC0310800;  /* header */
    mqd[1]  = 0x00000001;   /* DISPATCH_INITIATOR: COMPUTE_SHADER_EN, NO VALID */
    mqd[2]  = 1; mqd[3] = 1; mqd[4] = 1;  /* DIM_X/Y/Z */
    mqd[5]  = 0; mqd[6] = 0; mqd[7] = 0;  /* START */
    mqd[8]  = 32; mqd[9] = 1; mqd[10] = 1;/* NUM_THREAD */
    mqd[11] = 0;  /* pipeline_stat_enable */
    mqd[14] = (UINT32)(SHADER_ADDR >> 8);  /* PGM_LO */
    mqd[15] = (UINT32)(SHADER_ADDR >> 40); /* PGM_HI */
    mqd[20] = 0x00000000;  /* PGM_RSRC1: min registers */
    mqd[21] = 0x00000000;  /* PGM_RSRC2 */
    mqd[22] = 0;  /* VMID */
    mqd[24] = 0xFFFFFFFF;  /* STATIC_THREAD_MGMT_SE0 */
    mqd[25] = 0xFFFFFFFF;  /* STATIC_THREAD_MGMT_SE1 */

    /* Region 2: CP_HQD state */
    mqd[MQD_DW_CP_HQD_ACTIVE] = 1;  /* HQD_ACTIVE in MQD */
    mqd[MQD_DW_CP_HQD_VMID] = 0;
    mqd[MQD_DW_CP_HQD_PERSISTENT] = 0x00000001;  /* ORD=1 */
    mqd[MQD_DW_CP_HQD_PQ_BASE] = (UINT32)(RING_ADDR >> 8);
    mqd[MQD_DW_CP_HQD_PQ_BASE_HI] = (UINT32)(RING_ADDR >> 40);
    mqd[MQD_DW_CP_HQD_PQ_CONTROL] = PQ_CONTROL_QUEUE_SIZE_1024;
    mqd[MQD_DW_CP_HQD_PQ_WPTR_LO] = wptr;  /* initial WPTR */
    mqd[MQD_DW_CP_HQD_PQ_DOORBELL] = 0;  /* no doorbell */

    WritePhys(MQD_ADDR, mqd, sizeof(mqd));
    printf("3. MQD at 0x%08X\n", (UINT32)MQD_ADDR);

    /* ===== STEP 4: Save state ===== */
    UINT32 oldMqdBase = R(CP_MQD_BASE_ADDR);
    UINT32 oldMqdHi = R(CP_MQD_BASE_ADDR_HI);
    UINT32 oldActive = R(CP_HQD_ACTIVE);
    UINT32 oldPqBase = R(CP_HQD_PQ_BASE);
    UINT32 oldPqCtrl = R(CP_HQD_PQ_CONTROL);
    UINT32 oldWptr = R(CP_HQD_PQ_WPTR_LO);
    UINT32 oldVmid = R(CP_HQD_VMID);
    UINT32 oldRlc = R(RLC_CP_SCHEDULERS);
    UINT32 oldIdx = R(GRBM_GFX_INDEX);

    /* Init fence to 0 */
    UINT64 zero = 0;
    WritePhys(FENCE_ADDR, &zero, 8);
    printf("   Fence initialized to 0 at 0x%08X\n", (UINT32)FENCE_ADDR);

    /* Set MQD base and verify write-back */
    W(CP_MQD_BASE_ADDR, (UINT32)(MQD_ADDR >> 8));
    W(CP_MQD_BASE_ADDR_HI, (UINT32)(MQD_ADDR >> 40));
    UINT32 mqdBaseCheck = R(CP_MQD_BASE_ADDR);
    UINT32 mqdHiCheck = R(CP_MQD_BASE_ADDR_HI);
    printf("4. MQD_BASE wrote 0x%08X, reads back 0x%08X (HI: 0x%08X -> 0x%08X)\n",
        (UINT32)(MQD_ADDR >> 8), mqdBaseCheck,
        (UINT32)(MQD_ADDR >> 40), mqdHiCheck);

    /* ===== STEP 5: Activate HQD ===== */
    W(CP_HQD_ACTIVE, 0);  /* deactivate first */
    Sleep(10);
    W(CP_HQD_ACTIVE, 1);
    Sleep(10);
    UINT32 act = R(CP_HQD_ACTIVE);
    printf("5. CP_HQD_ACTIVE = 0x%08X (ACK=%s)\n", act, (act & 1) ? "YES" : "NO");

    /* Verify MQD loaded correctly */
    UINT32 pgmLo = R(COMPUTE_PGM_LO);
    UINT32 pgmHi = R(COMPUTE_PGM_HI);
    printf("   PGM_LO=0x%08X PGM_HI=0x%08X (shader addr=0x%08X)\n", pgmLo, pgmHi, (UINT32)SHADER_ADDR);
    printf("   PQ_BASE=0x%08X PQ_CTRL=0x%08X\n", R(CP_HQD_PQ_BASE), R(CP_HQD_PQ_CONTROL));
    printf("   WPTR=%d RPTR=%d\n", R(CP_HQD_PQ_WPTR_LO), R(CP_HQD_PQ_RPTR));

    /* ===== STEP 6: Trigger dispatch via WPTR increment ===== */
    printf("\n--- Attempt 1: Increment WPTR to trigger MEC ---\n");
    W(CP_HQD_PQ_WPTR_LO, wptr);  /* WPTR = number of DWORDs in ring */
    Sleep(100);
    UINT32 grbm1 = R(GRBM_STATUS);
    UINT32 rptr1 = R(CP_HQD_PQ_RPTR);
    UINT32 wptr1 = R(CP_HQD_PQ_WPTR_LO);
    printf("   GRBM_STATUS=0x%08X RPTR=%d WPTR=%d\n", grbm1, rptr1, wptr1);

    /* ===== STEP 7: Direct DISPATCH_VALID after MQD ===== */
    printf("\n--- Attempt 2: VALID after MQD ---\n");
    W(COMPUTE_DISPATCH_INITIATOR, 0x0003);  /* SHADER_EN | VALID */
    UINT32 init = R(COMPUTE_DISPATCH_INITIATOR);
    printf("   DISPATCH_INITIATOR=0x%08X (consumed=%s)\n", init, (init & 2) ? "NO" : "YES");
    Sleep(200);
    UINT32 grbm2 = R(GRBM_STATUS);
    printf("   GRBM_STATUS=0x%08X RPTR=%d\n", grbm2, R(CP_HQD_PQ_RPTR));

    /* ===== STEP 8: Try with ME=1 + RLC ===== */
    printf("\n--- Attempt 3: ME=1 + RLC + MQD + WPTR ---\n");
    W(GRBM_GFX_INDEX, 0x00010000);  /* ME=1 */
    W(RLC_CP_SCHEDULERS, 0x000000A0);  /* enable queue for ME=1 */
    W(CP_HQD_ACTIVE, 1);
    Sleep(10);
    W(CP_HQD_PQ_WPTR_LO, wptr);  /* retrigger WPTR */
    Sleep(200);
    W(GRBM_GFX_INDEX, 0xE0000000);  /* broadcast */
    UINT32 grbm3 = R(GRBM_STATUS);
    UINT32 rptr3 = R(CP_HQD_PQ_RPTR);
    printf("   GRBM_STATUS=0x%08X RPTR=%d\n", grbm3, rptr3);

    /* ===== STEP 9: Check fence at FENCE_ADDR ===== */
    printf("\n--- Checking fence value ---\n");
    UINT64 fenceVal = 0;
    ReadPhys(FENCE_ADDR, &fenceVal, 8);
    printf("   Fence at 0x%08X = 0x%016llX\n", (UINT32)FENCE_ADDR, fenceVal);
    if (fenceVal != 0) printf("   >>> SHADER EXECUTED! Fence incremented!\n");

    /* ===== RESTORE ===== */
    printf("\nRestoring...\n");
    W(CP_HQD_ACTIVE, 0);
    W(CP_MQD_BASE_ADDR, oldMqdBase);
    W(CP_MQD_BASE_ADDR_HI, oldMqdHi);
    W(CP_HQD_VMID, oldVmid);
    W(CP_HQD_PQ_BASE, oldPqBase);
    W(CP_HQD_PQ_CONTROL, oldPqCtrl);
    W(CP_HQD_PQ_WPTR_LO, oldWptr);
    W(RLC_CP_SCHEDULERS, oldRlc);
    W(SPI_PG_MASK, savedSpi);
    W(GCVM_CONTEXT0_CNTL, savedGcvm);
    W(GRBM_GFX_INDEX, oldIdx);
    W(COMPUTE_DISPATCH_INITIATOR, 0);

    CloseHandle(gH);
    printf("\n=== Done ===\n");
    return 0;
}
