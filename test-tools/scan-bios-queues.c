/* scan-bios-queues.c — Scan all 16 HQD queues to find BIOS-configured ones
 * and try submitting PM4 through an existing BIOS queue.
 *
 * Key insight: BIOS already set up compute queues 4-15 (HQD_ACTIVE=0xFFF0).
 * Those queues have valid GPU VM mappings configured by BIOS/firmware.
 * If we can find their ring buffer addresses and write PM4 to them,
 * we bypass the dead GCVM problem entirely. */
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

static FILE *g_log = NULL;
static void Log(const char *fmt, ...) {
    va_list a; va_start(a, fmt); vfprintf(stdout, fmt, a); va_end(a);
    if (g_log) { va_list b; va_start(b, fmt); vfprintf(g_log, fmt, b); va_end(b); }
}

static HANDLE g_hGpu = INVALID_HANDLE_VALUE;

static unsigned ReadReg(unsigned off) {
    unsigned ra[2] = {off, 0xDEADBEEF}; DWORD br = 0;
    DeviceIoControl(g_hGpu, 0x80000B88, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
    return ra[1];
}

static BOOL WriteReg(unsigned off, unsigned val) {
    unsigned ra[2] = {off, val}; DWORD br = 0;
    return DeviceIoControl(g_hGpu, 0x80000B8C, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
}

/* Select engine via GRBM_GFX_INDEX */
static void SelectEngine(unsigned me, unsigned queue) {
    unsigned idx = (me << 16) | queue;
    WriteReg(0x34D0, idx);
    /* Also write broadcast disabled index for ME select */
    unsigned idx_full = 0x00000000 | (me << 16) | queue;
    WriteReg(0x34D0, idx_full);
    Sleep(1);
}

/* HQD register offsets (selected via GRBM_GFX_INDEX) */
#define HQD_ACTIVE      0xDAC0
#define HQD_VMID        0xDAC4
#define HQD_PSTATE      0xDAC8
#define HQD_PQ_BASE     0xDAD8
#define HQD_PQ_BASE_HI  0xDADC
#define HQD_PQ_RPTR     0xDAE0
#define HQD_PQ_CNTL     0xDAFC
#define HQD_PQ_WP_CNTL  0xDB00
#define HQD_PQ_DOORBELL 0xDAF4
#define HQD_EOP_CNTL    0xDB54
#define HQD_PQ_WPTR_LO  0xDB90
#define HQD_PQ_WPTR_HI  0xDB94
#define KIQ_BASE_LO     0xE060
#define KIQ_BASE_HI     0xE064
#define KIQ_CNTL        0xE068
#define KIQ_RPTR        0xE06C
#define KIQ_WPTR        0xE078
#define ME_CNTL         0x4A74
#define GRBM_INDEX      0x34D0
#define RLC_SCHED       0xECA8

/* Read all HQD regs for selected queue */
static void DumpQueue(int qnum) {
    unsigned active = ReadReg(HQD_ACTIVE);
    unsigned vmid = ReadReg(HQD_VMID);
    unsigned pqbase = ReadReg(HQD_PQ_BASE);
    unsigned pqbase_hi = ReadReg(HQD_PQ_BASE_HI);
    unsigned rptr = ReadReg(HQD_PQ_RPTR);
    unsigned wptr_lo = ReadReg(HQD_PQ_WPTR_LO);
    unsigned wptr_hi = ReadReg(HQD_PQ_WPTR_HI);
    unsigned pqcntl = ReadReg(HQD_PQ_CNTL);
    unsigned dbell = ReadReg(HQD_PQ_DOORBELL);
    unsigned eopcntl = ReadReg(HQD_EOP_CNTL);
    unsigned pstate = ReadReg(HQD_PSTATE);
    unsigned wpoll = ReadReg(HQD_PQ_WP_CNTL);

    Log("  Q%2d: ACT=0x%08X VMID=0x%08X PQ=0x%08X%08X RPTR=0x%08X WPTR=0x%08X%08X\n",
        qnum, active, vmid, pqbase_hi, pqbase, rptr, wptr_hi, wptr_lo);
    Log("        PCNTL=0x%08X DBELL=0x%08X EOP=0x%08X PST=0x%08X WPOLL=0x%08X\n",
        pqcntl, dbell, eopcntl, pstate, wpoll);
}

int main(void) {
    g_log = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\scan-bios-queues.log", "w");
    Log("=== BIOS Queue Scanner ===\n\n");

    g_hGpu = CreateFileW(L"\\\\.\\AMDBC250DreamV43",
        GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (g_hGpu == INVALID_HANDLE_VALUE) {
        Log("ERROR: Cannot open GPU driver\n");
        if (g_log) fclose(g_log);
        return 1;
    }

    /* INIT_HARDWARE */
    UCHAR ii[32]={0}; DWORD br=0;
    *(unsigned __int64*)(ii+0)=0xFE800000ULL;
    *(unsigned*)(ii+8)=0x00080000;
    *(unsigned*)(ii+12)=1;
    *(unsigned __int64*)(ii+16)=0xC0000000ULL;
    *(unsigned*)(ii+24)=0x10000000;
    DeviceIoControl(g_hGpu, 0x80000B80, ii, sizeof(ii), NULL, 0, &br, NULL);
    Log("INIT_HARDWARE done\n\n");

    /* Step 1: Scan all 16 queues (ME=0 for each) */
    Log("--- Step 1: Scan all 16 queues (ME=0) ---\n");
    for (int q = 0; q < 16; q++) {
        SelectEngine(0, q);
        DumpQueue(q);
    }

    /* Step 2: Also scan with ME=1 (kernel queues) */
    Log("\n--- Step 2: Scan queues with ME=1 ---\n");
    for (int q = 0; q < 4; q++) {
        SelectEngine(1, q);
        unsigned active = ReadReg(HQD_ACTIVE);
        unsigned vmid = ReadReg(HQD_VMID);
        unsigned pqbase = ReadReg(HQD_PQ_BASE);
        unsigned pqbase_hi = ReadReg(HQD_PQ_BASE_HI);
        Log("  ME1_Q%d: ACT=0x%08X VMID=0x%08X PQ=0x%08X%08X\n",
            q, active, vmid, pqbase_hi, pqbase);
    }

    /* Step 3: Restore broadcast mode */
    SelectEngine(0, 0);
    WriteReg(0x34D0, 0xE0000000);

    /* Step 4: Read KIQ and CP fence/doorbell registers */
    Log("\n--- Step 3: KIQ + CP fence registers ---\n");
    Log("  KIQ_BASE=0x%08X%08X KIQ_CNTL=0x%08X\n",
        ReadReg(KIQ_BASE_HI), ReadReg(KIQ_BASE_LO), ReadReg(KIQ_CNTL));
    Log("  KIQ_RPTR=0x%08X KIQ_WPTR=0x%08X\n",
        ReadReg(KIQ_RPTR), ReadReg(KIQ_WPTR));
    Log("  ME_CNTL=0x%08X\n", ReadReg(ME_CNTL));
    Log("  RLC_SCHED=0x%08X\n", ReadReg(RLC_SCHED));

    /* Step 5: Read CP fence/doorbell registers (GC_BASE shifted) */
    Log("\n--- Step 4: CP fence/doorbell (0x3AD8-0x3AEC) ---\n");
    for (unsigned off = 0x3AD8; off <= 0x3AEC; off += 4) {
        unsigned v = ReadReg(off);
        if (v != 0 && v != 0xFFFFFFFF)
            Log("  [0x%04X]=0x%08X", off, v);
    }
    Log("\n");

    /* Step 6: Key question — which queues have valid PQ_BASE? */
    Log("\n--- Step 5: Summary ---\n");
    int active_count = 0;
    for (int q = 0; q < 16; q++) {
        SelectEngine(0, q);
        unsigned active = ReadReg(HQD_ACTIVE);
        if (active & 1) active_count++;
    }
    Log("  Active queues (ME=0): %d\n", active_count);

    /* Restore broadcast */
    SelectEngine(0, 0);
    WriteReg(0x34D0, 0xE0000000);

    CloseHandle(g_hGpu);
    Log("\n=== Done ===\n");
    if (g_log) fclose(g_log);
    printf("Done. Check output\\scan-bios-queues.log\n");
    return 0;
}
