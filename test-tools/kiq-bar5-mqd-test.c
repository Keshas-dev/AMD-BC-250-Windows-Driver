#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>

#define AMDBC250_DEVICE_PATH L"\\\\.\\AMDBC250DreamV43"
#define IOCTL_AMDBC250_READ_REG   ((ULONG)0x80000B88)
#define IOCTL_AMDBC250_WRITE_REG  ((ULONG)0x80000B8C)
#define IOCTL_AMDBC250_INIT_HARDWARE ((ULONG)0x80000B80)

typedef struct { ULONG Offset, Value, Status; } REG_IOCTL;
typedef struct { ULONG64 MmioPhysicalBase; ULONG MmioSize, Flags; ULONG64 FbPhysicalBase; ULONG FbSize; } INIT_HW;

static HANDLE g_h;
static int OpenGpu(void) {
    g_h = CreateFileW(AMDBC250_DEVICE_PATH, GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    return g_h != INVALID_HANDLE_VALUE ? 0 : -1;
}
static ULONG ReadReg(ULONG offset) {
    REG_IOCTL req = {offset,0,0}; DWORD ret;
    DeviceIoControl(g_h, IOCTL_AMDBC250_READ_REG, &req,sizeof(req), &req,sizeof(req), &ret, NULL);
    return req.Value;
}
static int WriteReg(ULONG offset, ULONG value) {
    REG_IOCTL req = {offset,value,0}; DWORD ret;
    return DeviceIoControl(g_h, IOCTL_AMDBC250_WRITE_REG, &req,sizeof(req), &req,sizeof(req), &ret, NULL) ? 0 : -1;
}
static int InitHw(void) {
    INIT_HW ih = {0}; ih.MmioPhysicalBase = 0xFE800000ULL; ih.MmioSize = 0x80000; ih.Flags = 1; DWORD ret;
    return DeviceIoControl(g_h, IOCTL_AMDBC250_INIT_HARDWARE, &ih,sizeof(ih), &ih,sizeof(ih), &ret, NULL) ? 0 : -1;
}

/* Offsets */
#define GRBM_GFX_INDEX   0x34D0
#define CP_MQD_BASE_ADDR  0x9104
#define CP_MQD_BASE_ADDR_HI 0x9108
#define CP_HQD_ACTIVE     0x910C
#define CP_HQD_PQ_BASE_LO 0x9124
#define CP_HQD_PQ_BASE_HI 0x9128
#define CP_HQD_PQ_CONTROL 0x9148
#define CP_HQD_PQ_WPTR_LO 0x91DC
#define CP_HQD_PQ_RPTR    0x91C4
#define CP_HQD_VMID       0x9110
#define CP_HQD_PERSISTENT_STATE 0x9114
#define CP_MQD_CONTROL    0x9120  /* write 1 to set PRIV_STATE */

/* Use BAR5 area at 0x7F000 (508KB) for MQD */
#define MQD_BAR5_OFFSET   0x7F000
#define MQD_PHYS_ADDR     0xFE87F000ULL  /* BAR5(0xFE800000) + 0x7F000 */
#define RING_BAR5_OFFSET  0x7F400  /* 1KB after MQD */
#define RING_PHYS_ADDR    0xFE87F400ULL

int main(void) {
    int i;
    if (OpenGpu() != 0) { printf("Cannot open GPU\n"); return 1; }
    if (InitHw() != 0) { printf("InitHw err=%lu\n", GetLastError()); return 1; }
    printf("=== KIQ via BAR5 MQD ===\n\n");

    /* Step 1: Probe BAR5 area for MQD */
    printf("--- Step 1: Probe BAR5 area at 0x%X ---\n", MQD_BAR5_OFFSET);
    ULONG probe = ReadReg(MQD_BAR5_OFFSET);
    printf("BAR5[0x%X] = 0x%08X\n", MQD_BAR5_OFFSET, probe);
    if (probe == 0xFFFFFFFF) { printf("AREA DEAD - cannot use!\n"); return 1; }

    /* Step 2: Write MQD structure to BAR5 */
    printf("\n--- Step 2: Write MQD to BAR5[0x%X] ---\n", MQD_BAR5_OFFSET);

    /* MQD structure (v10_compute_mqd style - simplified) */
    ULONG mqd[256];  /* 256 DWORDs = 1KB MQD */
    memset(mqd, 0, sizeof(mqd));

    /* Fill key MQD fields (offsets based on gc_10_1_0_offset.h + v10_structs.h):
       DWORD offsets within the MQD: */
    #define MQD_OFF_CP_HQD_PQ_BASE_LO   136  /* ringPa >> 8 */
    #define MQD_OFF_CP_HQD_PQ_BASE_HI   137
    #define MQD_OFF_CP_HQD_PQ_CONTROL   145  /* 0x000A0101: QUEUE_SIZE=7, RPTR_BLOCK=5 */
    #define MQD_OFF_CP_MQD_BASE_ADDR_LO 128  /* MQD PA (own addr) */
    #define MQD_OFF_CP_MQD_BASE_ADDR_HI 129
    #define MQD_OFF_CP_HQD_ACTIVE       130
    #define MQD_OFF_CP_HQD_VMID         131
    #define MQD_OFF_CP_HQD_PERSISTENT_STATE 132
    #define MQD_OFF_CP_HQD_PQ_DOORBELL_CONTROL 143
    #define MQD_OFF_CP_HQD_PQ_WPTR_LO   ((136+42+2*3)/4) /* approximated */
    #define MQD_OFF_QUEUE_TYPE          170

    /* These offsets come from v10_structs.h MQD layout */
    mqd[128] = (ULONG)(MQD_PHYS_ADDR & 0xFFFFFFFF);       /* MQD_BASE_ADDR_LO */
    mqd[129] = (ULONG)(MQD_PHYS_ADDR >> 32);               /* MQD_BASE_ADDR_HI */
    mqd[130] = 1;                                          /* HQD_ACTIVE */
    mqd[131] = 0;                                          /* VMID */
    mqd[132] = 0x8000014C;                                 /* PERSISTENT_STATE */
    mqd[133] = 1;                                          /* PIPE_PRIORITY */
    mqd[134] = 15;                                         /* QUEUE_PRIORITY */
    mqd[135] = 0x00010011;                                 /* QUANTUM */
    mqd[136] = (ULONG)(RING_PHYS_ADDR >> 8);               /* PQ_BASE_LO (ring >> 8) */
    mqd[137] = (ULONG)(RING_PHYS_ADDR >> 40);              /* PQ_BASE_HI */
    mqd[138] = 0;                                          /* PQ_RPTR */
    mqd[139] = 0;                                          /* RPTR_REPORT_ADDR_LO */
    mqd[140] = 0;                                          /* RPTR_REPORT_ADDR_HI */
    mqd[141] = 0;                                          /* WPTR_POLL_ADDR_LO */
    mqd[142] = 0;                                          /* WPTR_POLL_ADDR_HI */
    mqd[143] = 0;                                          /* DOORBELL_CONTROL */
    mqd[145] = 0x000A0101;                                 /* PQ_CONTROL (size=7=2^8=256 dwords) */
    mqd[146] = 0;                                          /* IB_BASE_ADDR_LO */
    mqd[147] = 0;                                          /* IB_BASE_ADDR_HI */
    mqd[148] = 0;                                          /* IB_RPTR */
    mqd[149] = 0x00030003;                                 /* IB_CONTROL */
    mqd[150] = 0;                                          /* IQ_TIMER */
    mqd[164] = 1 << 14;                                    /* HQ_SCHEDULER0 */
    mqd[166] = 1;                                          /* MQD_CONTROL (PRIV_STATE) */
    mqd[170] = 0;                                          /* QUEUE_TYPE (KIQ=0) */

    /* Write MQD to BAR5 */
    for (i = 0; i < 256; i++) {
        WriteReg(MQD_BAR5_OFFSET + i * 4, mqd[i]);
    }
    printf("MQD written (256 DWORDs)\n");

    /* Verify first few DWORDs */
    printf("MQD[0]=0x%08X MQD[128]=0x%08X MQD[136]=0x%08X\n",
        ReadReg(MQD_BAR5_OFFSET),
        ReadReg(MQD_BAR5_OFFSET + 128*4),
        ReadReg(MQD_BAR5_OFFSET + 136*4));

    /* Step 3: Write ring data (test NOPs) */
    printf("\n--- Step 3: Write ring buffer to BAR5[0x%X] ---\n", RING_BAR5_OFFSET);
    ULONG ringData[32];
    for (i = 0; i < 32; i++) {
        ringData[i] = 0xC0001000; /* type-3 NOP with 1 DWORD */
    }
    /* Put one real command: WRITE_DATA to SCRATCH */
    ringData[0] = 0xC0043900; /* type3, WRITE_DATA, 1 DWORD */
    ringData[1] = 0x00000000; /* CONTROL: engine_sel=0, dst_sel=0 (MEM) */
    ringData[2] = 0x00000000; /* DST_ADDR_LO (CLEAR: set to 0 to avoid DMA) */
    ringData[3] = 0x00000000; /* DST_ADDR_HI */
    ringData[4] = 0x00000000; /* DATA: write 0 to check */

    for (i = 0; i < 32; i++) {
        WriteReg(RING_BAR5_OFFSET + i * 4, ringData[i]);
    }
    printf("Ring written (32 DWORDs)\n");

    /* Step 4: Select KIQ via GRBM_GFX_INDEX */
    printf("\n--- Step 4: Select KIQ (ME=1,PIPE=0,Q=0) ---\n");
    WriteReg(GRBM_GFX_INDEX, 0x00010000);
    printf("GRBM_GFX_INDEX = 0x%08X\n", ReadReg(GRBM_GFX_INDEX));

    /* Step 5: Check CP_HQD state before activation */
    printf("\n--- Step 5: CP_HQD before activation ---\n");
    printf("CP_MQD_BASE_ADDR  (0x9104)=0x%08X\n", ReadReg(CP_MQD_BASE_ADDR));
    printf("CP_MQD_BASE_ADDR_HI(0x9108)=0x%08X\n", ReadReg(CP_MQD_BASE_ADDR_HI));
    printf("CP_HQD_ACTIVE     (0x910C)=0x%08X\n", ReadReg(CP_HQD_ACTIVE));
    printf("CP_HQD_PQ_BASE_LO (0x9124)=0x%08X\n", ReadReg(CP_HQD_PQ_BASE_LO));
    printf("CP_HQD_PQ_CONTROL (0x9148)=0x%08X\n", ReadReg(CP_HQD_PQ_CONTROL));
    printf("CP_HQD_PQ_WPTR_LO (0x91DC)=0x%08X\n", ReadReg(CP_HQD_PQ_WPTR_LO));
    printf("CP_HQD_PQ_RPTR    (0x91C4)=0x%08X\n", ReadReg(CP_HQD_PQ_RPTR));

    /* Step 6: Write MQD base addr */
    printf("\n--- Step 6: Write CP_MQD_BASE_ADDR = 0x%llX ---\n", MQD_PHYS_ADDR);
    WriteReg(CP_MQD_BASE_ADDR, (ULONG)(MQD_PHYS_ADDR & 0xFFFFFFFF));
    WriteReg(CP_MQD_BASE_ADDR_HI, (ULONG)(MQD_PHYS_ADDR >> 32));
    /* Set MQD control (PRIV_STATE=1) - bit field at a different register */
    WriteReg(CP_MQD_CONTROL, 1);
    printf("CP_MQD_BASE_ADDR   = 0x%08X\n", ReadReg(CP_MQD_BASE_ADDR));
    printf("CP_MQD_BASE_ADDR_HI= 0x%08X\n", ReadReg(CP_MQD_BASE_ADDR_HI));
    printf("CP_MQD_CONTROL     = 0x%08X\n", ReadReg(CP_MQD_CONTROL));

    /* Step 7: Write PQ_BASE register directly */
    printf("\n--- Step 7: Write CP_HQD_PQ_BASE_LO = 0x%llX ---\n", RING_PHYS_ADDR >> 8);
    WriteReg(CP_HQD_PQ_BASE_LO, (ULONG)((RING_PHYS_ADDR >> 8) & 0xFFFFFFFF));
    WriteReg(CP_HQD_PQ_BASE_HI, (ULONG)((RING_PHYS_ADDR >> 8) >> 32));
    printf("CP_HQD_PQ_BASE_LO  = 0x%08X (after write)\n", ReadReg(CP_HQD_PQ_BASE_LO));
    printf("CP_HQD_PQ_BASE_HI  = 0x%08X (after write)\n", ReadReg(CP_HQD_PQ_BASE_HI));

    /* Step 8: Activate HQD */
    printf("\n--- Step 8: CP_HQD_ACTIVE = 1 ---\n");
    WriteReg(CP_HQD_ACTIVE, 1);
    printf("CP_HQD_ACTIVE = 0x%08X\n", ReadReg(CP_HQD_ACTIVE));

    /* Wait for MQD load */
    Sleep(100);

    /* Step 9: Check if MQD was loaded into registers */
    printf("\n--- Step 9: After MQD load (100ms wait) ---\n");
    printf("CP_HQD_PQ_BASE_LO (0x9124)=0x%08X\n", ReadReg(CP_HQD_PQ_BASE_LO));
    printf("CP_HQD_PQ_CONTROL (0x9148)=0x%08X\n", ReadReg(CP_HQD_PQ_CONTROL));
    printf("CP_HQD_PQ_RPTR    (0x91C4)=0x%08X\n", ReadReg(CP_HQD_PQ_RPTR));
    printf("CP_HQD_VMID       (0x9110)=0x%08X\n", ReadReg(CP_HQD_VMID));
    printf("CP_HQD_PERSISTENT (0x9114)=0x%08X\n", ReadReg(CP_HQD_PERSISTENT_STATE));

    /* Step 10: Kick WPTR */
    printf("\n--- Step 10: Kick WPTR = 128 (32 DWORDs) ---\n");
    WriteReg(CP_HQD_PQ_WPTR_LO, 128);
    printf("CP_HQD_PQ_WPTR_LO = 0x%08X\n", ReadReg(CP_HQD_PQ_WPTR_LO));

    /* Poll for RPTR advance */
    printf("\n--- Step 11: Polling RPTR (5 seconds) ---\n");
    for (i = 0; i < 50; i++) {
        ULONG rptr = ReadReg(CP_HQD_PQ_RPTR);
        ULONG active = ReadReg(CP_HQD_ACTIVE);
        printf("  [%d] RPTR=0x%08X ACTIVE=0x%08X", i, rptr, active);
        if (rptr > 0) {
            printf(" RPTR ADVANCED! (delta=%u)", rptr);
            break;
        }
        printf("\n");
        Sleep(100);
    }
    printf("\n");

    /* Step 12: Read back some registers */
    printf("\n--- Step 12: Final state ---\n");
    ULONG grbm = ReadReg(GRBM_GFX_INDEX);
    printf("GRBM_GFX_INDEX = 0x%08X\n", grbm);
    printf("CP_HQD_PQ_BASE_LO = 0x%08X\n", ReadReg(CP_HQD_PQ_BASE_LO));
    printf("CP_HQD_PQ_CONTROL = 0x%08X\n", ReadReg(CP_HQD_PQ_CONTROL));
    printf("CP_HQD_ACTIVE     = 0x%08X\n", ReadReg(CP_HQD_ACTIVE));
    printf("CP_HQD_PQ_RPTR    = 0x%08X\n", ReadReg(CP_HQD_PQ_RPTR));
    printf("CP_HQD_PQ_WPTR_LO = 0x%08X\n", ReadReg(CP_HQD_PQ_WPTR_LO));

    /* Restore broadcast */
    WriteReg(GRBM_GFX_INDEX, 0xE4000000);
    CloseHandle(g_h);
    return 0;
}
