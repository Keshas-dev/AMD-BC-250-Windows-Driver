/* kiq-gpu-direct.c — Program KIQ ring + submit PM4 via GPU driver proxy directly
 * This bypasses PSP driver's broken proxy and uses GPU driver IOCTL 0x900/0x901
 * which we know works (safe-test proves it). */
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

static void DumpAll(void) {
    struct { unsigned o; const char *n; } r[] = {
        {0x4A74,"ME_CNTL"},   {0x3260,"GRBM"},     {0x3264,"CC_CFG"},
        {0x32D4,"SCRATCH0"},  {0x32D8,"SCRATCH1"},
        {0x34D0,"GFX_INDEX"}, {0xECA8,"RLC_SCHED"},
        {0xE060,"KIQ_BASE"},  {0xE064,"KIQ_BASE_HI"},
        {0xE068,"KIQ_CNTL"},  {0xE06C,"KIQ_RPTR"}, {0xE078,"KIQ_WPTR"},
        {0xDAC0,"HQD_ACT"},   {0xDAC4,"HQD_VMD"},  {0xDAC8,"HQD_PSTATE"},
        {0xDAD8,"PQ_BASE"},   {0xDADC,"PQ_BASE_HI"},
        {0xDAE0,"PQ_RPTR"},   {0xDAF4,"PQ_DBELL"},
        {0xDAFC,"PQ_CNTL"},   {0xDB00,"PQ_WPTR_CNTL"},
        {0xDB90,"PQ_WPTR_LO"},{0xDB94,"PQ_WPTR_HI"},
    };
    for (int i = 0; i < sizeof(r)/sizeof(r[0]); i++)
        Log("  %s=0x%08X", r[i].n, ReadReg(r[i].o));
    Log("\n");
}

/* Ring buffer in physical memory */
#define RING_SIZE 0x10000  /* 64KB = 16K DWORDs */
static PVOID g_RingVa = NULL;
static PHYSICAL_ADDRESS g_RingPa;

int main(void) {
    g_log = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\kiq-gpu-direct.log", "w");
    Log("=== KIQ GPU Direct Test ===\n\n");

    /* Open GPU driver */
    g_hGpu = CreateFileW(L"\\\\.\\AMDBC250DreamV43",
        GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (g_hGpu == INVALID_HANDLE_VALUE) {
        Log("ERROR: Cannot open GPU driver\n");
        if (g_log) fclose(g_log);
        return 1;
    }
    Log("GPU driver opened\n");

    /* INIT_HARDWARE — required to map BAR5 */
    UCHAR ii[32]={0}; DWORD br=0;
    *(unsigned __int64*)(ii+0)=0xFE800000ULL;
    *(unsigned*)(ii+8)=0x00080000;
    *(unsigned*)(ii+12)=1; /* NBIO_MAP */
    *(unsigned __int64*)(ii+16)=0xC0000000ULL;
    *(unsigned*)(ii+24)=0x10000000;
    BOOL ok = DeviceIoControl(g_hGpu, 0x80000B80, ii, sizeof(ii), NULL, 0, &br, NULL);
    Log("INIT_HARDWARE: %s (err=%lu)\n", ok?"OK":"FAIL", GetLastError());

    /* Allocate ring buffer — must be below 4GB for GPU DMA */
    g_RingVa = VirtualAlloc(NULL, RING_SIZE, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
    if (!g_RingVa) { Log("Ring alloc failed\n"); return 1; }
    RtlZeroMemory(g_RingVa, RING_SIZE);
    /* Physical address would be needed for ring base, but can't get from user mode.
     * Use SEND_PM4 IOCTL instead which handles KIQ ring internally. */

    /* We need physical address — use VirtualLock + IOCTL approach */
    /* Actually, from user mode we can't get physical address directly.
     * Instead, let's use the GPU driver's existing proxy which does MMIO.
     * The ring buffer PA must be set via a different approach.
     *
     * ALTERNATIVE: Use KIQ_SUBMIT through PSP driver for WPTR updates,
     * but program GPU registers directly through GPU driver proxy.
     *
     * SIMPLEST: Just test if GPU proxy writes work for KIQ registers.
     */

    Log("\n--- Step 0: Baseline ---\n");
    DumpAll();

    /* Step 1: Halt ME + PFP */
    Log("\n--- Step 1: Halt ME+PFP (0x4A74 = 0x50000000) ---\n");
    WriteReg(0x4A74, 0x50000000);  /* ME_HALT | PFP_HALT */
    Sleep(10);
    Log("  ME_CNTL=0x%08X\n", ReadReg(0x4A74));

    /* Step 2: Select KIQ engine via GRBM_GFX_INDEX */
    Log("\n--- Step 2: GRBM_GFX_INDEX = KIQ (0x34D0 = 0x00010000) ---\n");
    WriteReg(0x34D0, 0x00010000);  /* ME=1, QUEUE=0 */
    Sleep(1);
    Log("  GFX_INDEX=0x%08X\n", ReadReg(0x34D0));

    /* Step 3: Deactivate HQD */
    Log("\n--- Step 3: HQD_ACTIVE = 0 (0xDAC0) ---\n");
    WriteReg(0xDAC0, 0);
    Sleep(10);
    Log("  HQD_ACTIVE=0x%08X\n", ReadReg(0xDAC0));

    /* Step 4: Set KIQ_CNTL = log2(ring_dwords) = log2(16384) = 14 */
    Log("\n--- Step 4: KIQ_CNTL = 14 (0xE068) ---\n");
    unsigned kcntl_before = ReadReg(0xE068);
    WriteReg(0xE068, 14);
    Sleep(1);
    unsigned kcntl_after = ReadReg(0xE068);
    Log("  KIQ_CNTL: before=0x%08X after=0x%08X\n", kcntl_before, kcntl_after);

    /* Step 5: Set KIQ_BASE — we need a physical address.
     * Let's test if the register is writable with a known value first. */
    Log("\n--- Step 5: KIQ_BASE test write (0xE060 = 0xDEADBEEF) ---\n");
    WriteReg(0xE060, 0xDEADBEEF);
    Sleep(1);
    Log("  KIQ_BASE=0x%08X\n", ReadReg(0xE060));

    /* Step 6: Set KIQ_RPTR = 0 */
    Log("\n--- Step 6: KIQ_RPTR = 0 (0xE06C) ---\n");
    WriteReg(0xE06C, 0);
    Sleep(1);
    Log("  KIQ_RPTR=0x%08X\n", ReadReg(0xE06C));

    /* Step 7: Set KIQ_WPTR = 0 */
    Log("\n--- Step 7: KIQ_WPTR = 0 (0xE078) ---\n");
    WriteReg(0xE078, 0);
    Sleep(1);
    Log("  KIQ_WPTR=0x%08X\n", ReadReg(0xE078));

    /* Step 8: Set VMID = 0 */
    Log("\n--- Step 8: HQD_VMID = 0 (0xDAC4) ---\n");
    WriteReg(0xDAC4, 0);
    Sleep(1);
    Log("  HQD_VMID=0x%08X\n", ReadReg(0xDAC4));

    /* Step 9: Set persistent state */
    Log("\n--- Step 9: HQD_PSTATE = 0xE001 (0xDAC8) ---\n");
    WriteReg(0xDAC8, 0xE001);
    Sleep(1);
    Log("  HQD_PSTATE=0x%08X\n", ReadReg(0xDAC8));

    /* Step 10: PQ WPTR = 0 */
    Log("\n--- Step 10: PQ_WPTR = 0 (0xDB90) ---\n");
    WriteReg(0xDB90, 0);
    WriteReg(0xDB94, 0);
    Sleep(1);
    Log("  PQ_WPTR_LO=0x%08X PQ_WPTR_HI=0x%08X\n", ReadReg(0xDB90), ReadReg(0xDB94));

    /* Step 11: RPTR report / WPTR poll clear */
    Log("\n--- Step 11: Clear EOP + poll addrs ---\n");
    WriteReg(0xDAE4, 0);  /* RPTR_REPORT_ADDR */
    WriteReg(0xDAE8, 0);
    WriteReg(0xDAEC, 0);  /* WPTR_POLL_ADDR */
    WriteReg(0xDAF0, 0);
    WriteReg(0xDB54, 0x08000000);  /* EOP_CONTROL */

    /* Step 12: WPTR poll control = disable */
    WriteReg(0xDB00, 0);
    WriteReg(0xDAF4, 0);  /* doorbell control */

    /* Step 13: Final dump */
    Log("\n--- Final state ---\n");
    DumpAll();

    /* STOP before HQD_ACTIVE=1 */
    Log("\n=== PAUSE: All KIQ regs programmed (except KIQ_BASE needs real PA) ===\n");
    Log("Next: allocate ring below 4GB, program KIQ_BASE, activate, submit PM4\n");

    /* Cleanup */
    VirtualFree(g_RingVa, 0, MEM_RELEASE);
    CloseHandle(g_hGpu);
    if (g_log) fclose(g_log);
    printf("Done. Check output\\kiq-gpu-direct.log\n");
    return 0;
}
