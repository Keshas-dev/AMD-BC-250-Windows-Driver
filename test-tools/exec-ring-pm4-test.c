#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE g_hDev = INVALID_HANDLE_VALUE;

static uint32_t ReadReg(uint32_t offset) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD ret = 0;
    r.RegisterOffset = offset; r.Value = 0;
    if (DeviceIoControl(g_hDev, IOCTL_AMDBC250_READ_REG, &r, sizeof(r), &r, sizeof(r), &ret, NULL))
        return r.Value;
    return 0xFFFFFFFF;
}

/* GC 10.1.3 register offsets (mm*4 + GC_BASE 0x1260) */
#define CP_STATUS       0x426C   /* mmCP_STATUS=0x0C03 */
#define MEC_INT_STATUS  0x4270   /* near CP_STATUS (estimated) */
#define CP_INT_STATUS   0x43A0   /* mmCP_INT_STATUS (estimated) */
#define VM_FAULT_STATUS 0x2260   /* mmVM_L2_PROTECTION_FAULT_STATUS (estimated) */

int main() {
    g_hDev = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (g_hDev == INVALID_HANDLE_VALUE) {
        printf("FAIL: CreateFile gle=%lu\n", GetLastError()); return 1;
    }
    printf("CreateFile OK\n");

    /* --- Step 0: INIT_HARDWARE (required on Win11 26100 WDM fallback) --- */
    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD ret = 0;
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL;
    ih.MmioSize = 0x80000;
    ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
    BOOL ok = DeviceIoControl(g_hDev, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &ret, NULL);
    printf("INIT_HW: ok=%d gle=%lu\n", ok, GetLastError());
    if (!ok) { CloseHandle(g_hDev); return 1; }

    /* --- Step 1: Enable GCVM page tables (required for MQD-based queue loading) --- */
    DWORD br2 = 0;
    UCHAR ptBuf[256] = {0};
    BOOL ptOk = DeviceIoControl(g_hDev, 0x8000098C, NULL, 0, ptBuf, sizeof(ptBuf), &br2, NULL);
    if (ptOk) {
        ULONG* pt = (ULONG*)ptBuf;
        UINT32 result = pt[10];
        printf("GCVM_PT_SETUP: result=0x%08X\n", result);
    } else {
        printf("GCVM_PT_SETUP failed: %lu\n", GetLastError());
    }

    /* --- Step 2: Read baseline --- */
    uint32_t scratch0 = ReadReg(0x32D4);
    printf("SCRATCH_REG0 before: 0x%08X\n", scratch0);

    /* --- Step 2: Build EXECUTE_RING_PM4 with IT_WRITE_DATA --- */
    AMDBC250_IOCTL_EXECUTE_RING_PM4 rp;
    ZeroMemory(&rp, sizeof(rp));
    /* PM4_TYPE3 header (Linux format): (3<<30) | ((count-1)<<16) | (opcode<<8)
       IT_WRITE_DATA op=0x37, count=4 body DWORDs -> count-1=3 */
    rp.Commands[0] = 0xC0033700;   /* PM4_TYPE3_HDR(IT_WRITE_DATA, 4) */
    rp.Commands[1] = 0x00000502;   /* CONTROL: DST_SEL=5(register) | WR_CONFIRM */
    rp.Commands[2] = 0x32D4;       /* ADDR_LO: SCRATCH_REG0 */
    rp.Commands[3] = 0x00000000;   /* ADDR_HI */
    rp.Commands[4] = 0xDEADBEEF;   /* DATA to write */
    rp.CommandCount = 5;           /* header + 4 body DWORDs = 5 total */
    rp.TimeoutMs = 5000;           /* 5 second timeout */

    printf("\n=== EXECUTE_RING_PM4 ===\n");
    ok = DeviceIoControl(g_hDev, IOCTL_AMDBC250_EXECUTE_RING_PM4, &rp, sizeof(rp), &rp, sizeof(rp), &ret, NULL);
    printf("IOCTL: ok=%d gle=%lu\n", ok, GetLastError());
    printf("Result:       %u (0=OK, 1=timeout, 2=error)\n", rp.Result);
    printf("Ring PA:      0x%llX\n", rp.RingPa);
    printf("MQD PA:       0x%llX\n", rp.MqdPa);
    printf("WPTR:         %u -> %u\n", rp.WptrBefore, rp.WptrAfter);
    printf("RPTR:         0x%X (before) -> 0x%X (after)\n", rp.RptrBefore, rp.RptrAfter);
    printf("HQD_ACTIVE:   0x%08X\n", rp.HqdActive);
    printf("PQ_CTRL:      0x%08X (before) -> 0x%08X (after) [wrote 0x000A0101]\n", rp.PqCtrlBefore, rp.PqCtrlAfter);
    printf("PQ_BASE:      0x%08X (readback, wrote 0x%llX>>8)\n", rp.PqBaseReadback, rp.RingPa);
    printf("RingDwords:   0x%08X 0x%08X 0x%08X 0x%08X\n",
        rp.RingDwords[0], rp.RingDwords[1], rp.RingDwords[2], rp.RingDwords[3]);
    printf("SW_Fallback:  %s (SCRATCH=0x%08X after SW exec)\n",
        rp.SwResult == 0 ? "EXECUTED" : "FAILED", rp.ScratchAfter);
    printf("SMU:          features=0x%08X gfxFreq=%u MHz\n",
        rp.SmuFeaturesMask, rp.SmuGfxFreqMhz);
    printf("MQD_LOAD:     PGM_LO=0x%08X after ACTIVE=1 (expect 0x%llX>>8)\n", rp.MqdLoadPgmLo, (rp.RingPa + 1024));
    printf("DISPATCH:     result=%u (0=no,1=regs set,2=triggered,3=activity)\n", rp.DispatchResult);
    printf("  PGM_LO=0x%08X PGM_HI=0x%08X THR_MGMT=0x%08X\n",
        rp.PgmLoReadback, rp.PgmHiReadback, rp.TmgMaskReadback);
    printf("  GRBM_STATUS: 0x%08X (before) -> 0x%08X (after)\n",
        rp.GrbmStatusBefore, rp.GrbmStatusAfter);

    /* --- Step 3: Read GRBM_STATUS for any errors --- */
    printf("\n=== Post-test registers ===\n");
    printf("SCRATCH_REG0: 0x%08X\n", ReadReg(0x32D4));
    printf("GRBM_STATUS:  0x%08X\n", ReadReg(0x3260));
    printf("GRBM_STATUS2: 0x%08X\n", ReadReg(0x326C));
    /* Minimal CP_HQD readback (no SEG1 probes — those cause white screen) */
    printf("--- CP_HQD minimal readback ---\n");
    printf("  MQD_BASE (0x9104): 0x%08X\n", ReadReg(0x9104));
    printf("  ACTIVE   (0x910C): 0x%08X\n", ReadReg(0x910C));
    printf("  ME_CNTL  (0x4A74): 0x%08X\n", ReadReg(0x4A74));
    printf("  MEC_CNTL (0x4B14): 0x%08X\n", ReadReg(0x4B14));
    /* CP diagnostic registers */
    printf("--- CP diagnostics ---\n");
    printf("  CP_STATUS  (0x426C): 0x%08X\n", ReadReg(0x426C));
    printf("  MEC_INT_ST (0x4270): 0x%08X\n", ReadReg(0x4270));
    printf("  CP_INT_ST  (0x43A0): 0x%08X\n", ReadReg(0x43A0));
    printf("  VM_FAULT   (0x2260): 0x%08X\n", ReadReg(0x2260));

    /* --- Step 4: Check results --- */
    if (rp.SwResult == 0) {
        uint32_t scratchVal = ReadReg(0x32D4);
        printf("\n*** SOFTWARE PM4 EXECUTOR WORKED! SCRATCH=0x%08X ***\n", scratchVal);
        if ((scratchVal & 0x0FFFFFFF) == (0xDEADBEEF & 0x0FFFFFFF))
            printf("*** SCRATCH value MATCHES expected (lower 28 bits) ***\n");
    } else if (rp.Result == 0) {
        printf("\n*** RPTR advanced but SCRATCH unchanged (MEC executing NOPs?) ***\n");
    } else if (rp.Result == 1) {
        printf("\n*** RPTR did NOT advance within timeout. MEC may be halted. ***\n");
    } else {
        printf("\n*** Error from IOCTL ***\n");
    }

    CloseHandle(g_hDev);
    return 0;
}
