/* probe-vm-path.c — Investigate how GPU accesses memory without GCVM
 * Scan MMHUB at correct offsets, check HDP MC_VM, test identity mapping */
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

static void SelectEngine(unsigned me, unsigned queue) {
    WriteReg(0x34D0, (me << 16) | queue);
    Sleep(1);
}

int main(void) {
    g_log = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\probe-vm-path.log", "w");
    Log("=== VM Path Investigation ===\n\n");

    g_hGpu = CreateFileW(L"\\\\.\\AMDBC250DreamV43",
        GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (g_hGpu == INVALID_HANDLE_VALUE) { Log("Open failed\n"); return 1; }

    /* INIT_HARDWARE */
    UCHAR ii[32]={0}; DWORD br=0;
    *(unsigned __int64*)(ii+0)=0xFE800000ULL;
    *(unsigned*)(ii+8)=0x00080000;
    *(unsigned*)(ii+12)=1;
    *(unsigned __int64*)(ii+16)=0xC0000000ULL;
    *(unsigned*)(ii+24)=0x10000000;
    DeviceIoControl(g_hGpu, 0x80000B80, ii, sizeof(ii), NULL, 0, &br, NULL);
    Log("INIT_HARDWARE done\n\n");

    /* === 1. HDP MC_VM registers (0x0500-0x05FF) === */
    Log("--- 1. HDP MC_VM Registers (0x0500-0x05FF) ---\n");
    Log("  [offset] value   [offset] value   [offset] value\n");
    for (unsigned off = 0x0500; off <= 0x05FF; off += 4) {
        unsigned v = ReadReg(off);
        if (v != 0 && v != 0xFFFFFFFF) {
            Log("  [0x%04X]=0x%08X", off, v);
            if ((off - 0x0500) % 12 == 8) Log("\n");
        }
    }
    Log("\n\n");

    /* === 2. MMHUB at Linux offsets (0x1A000 base) === */
    Log("--- 2. MMHUB Registers (BAR5+0x1A000 + offset) ---\n");
    Log("  Linux DWORD offsets -> byte offsets from MMHUB base:\n");
    Log("  VM_INVALIDATE=0x1478, VM_CONTEXT0_CNTL=0x1534\n");
    Log("  VM_CONTEXT0_PT_BASE_LO=0x153C, VM_CONTEXT0_PT_BASE_HI=0x1540\n\n");

    /* Read MMHUB at key VM offsets */
    struct { unsigned off; const char *name; } mmhub[] = {
        {0x1478, "VM_INVALIDATE"},
        {0x1534, "VM_CONTEXT0_CNTL"},
        {0x1538, "VM_CONTEXT0_CNTL2"},  /* guess */
        {0x153C, "VM_PT_BASE_LO"},
        {0x1540, "VM_PT_BASE_HI"},
        {0x1544, "VM_PT_BASE2_LO"},
        {0x1548, "VM_PT_BASE2_HI"},
        {0x1510, "VM_L2_CNTL"},
        {0x1514, "VM_L2_CNTL2"},
        {0x1518, "VM_L2_CNTL3"},
        {0x151C, "VM_L2_CNTL4"},
        {0x1520, "VM_L2_CNTL5"},
        {0x1500, "VM_FB_LO32"},
        {0x1504, "VM_FB_HI32"},
        {0x1508, "VM_AGP_LO32"},
        {0x150C, "VM_AGP_HI32"},
    };
    for (int i = 0; i < sizeof(mmhub)/sizeof(mmhub[0]); i++) {
        unsigned v = ReadReg(0x1A000 + mmhub[i].off);
        Log("  MMHUB+%s [0x%05X]=0x%08X\n", mmhub[i].name, 0x1A000 + mmhub[i].off, v);
    }

    /* Scan a wider MMHUB range for any alive registers */
    Log("\n  MMHUB alive registers (0x1A000-0x1BFFF):\n");
    int mmhub_alive = 0;
    for (unsigned off = 0x1A000; off <= 0x1BFFF; off += 4) {
        unsigned v = ReadReg(off);
        if (v != 0 && v != 0xFFFFFFFF) {
            Log("    [0x%05X]=0x%08X", off, v);
            mmhub_alive++;
            if (mmhub_alive % 4 == 0) Log("\n");
        }
    }
    Log("\n  MMHUB alive: %d registers\n\n", mmhub_alive);

    /* === 3. GCVM at correct DWORD*4 offsets === */
    Log("--- 3. GCVM Registers (BAR5 + 0x1260 + DWORD_offset*4) ---\n");
    struct { unsigned dword_off; const char *name; } gcvm[] = {
        {0x2840, "L2_CNTL"},
        {0x2880, "CONTEXT0_CNTL"},
        {0x28EA, "CONTEXT0_PT_BASE_LO"},
        {0x28EB, "CONTEXT0_PT_BASE_HI"},
        {0x28E0, "CONTEXT0_CNTL2"},
        {0x2841, "L2_CNTL2"},
        {0x2842, "L2_CNTL3"},
        {0x2980, "MC_VM_FB_LO32"},
        {0x2981, "MC_VM_FB_HI32"},
        {0x2984, "MC_VM_AGP_LO32"},
        {0x2985, "MC_VM_AGP_HI32"},
        {0x2987, "MX_L1_TLB_CNTL"},
    };
    for (int i = 0; i < sizeof(gcvm)/sizeof(gcvm[0]); i++) {
        unsigned v = ReadReg(0x1260 + gcvm[i].dword_off * 4);
        Log("  GCVM+%s [0x%05X]=0x%08X\n", gcvm[i].name,
            0x1260 + gcvm[i].dword_off * 4, v);
    }

    /* === 4. Check BIOS queue HQD state before we touch it === */
    Log("\n--- 4. BIOS Queue State (ME=0) ---\n");
    SelectEngine(0, 0);
    unsigned hqd_act = ReadReg(0xDAC0);
    unsigned hqd_vmd = ReadReg(0xDAC4);
    unsigned hqd_pstate = ReadReg(0xDAC8);
    unsigned hqd_pqbase = ReadReg(0xDAD8);
    unsigned hqd_pqbhi = ReadReg(0xDADC);
    unsigned hqd_pqcntl = ReadReg(0xDAFC);
    unsigned hqd_dbell = ReadReg(0xDAF4);
    unsigned hqd_eopcntl = ReadReg(0xDB54);
    unsigned hqd_wptrlo = ReadReg(0xDB90);
    unsigned hqd_wptrhi = ReadReg(0xDB94);
    unsigned mecntl = ReadReg(0x4A74);
    unsigned rptr = ReadReg(0xDAE0);

    Log("  ME_CNTL=0x%08X\n", mecntl);
    Log("  HQD_ACTIVE=0x%08X\n", hqd_act);
    Log("  HQD_VMID=0x%08X\n", hqd_vmd);
    Log("  HQD_PSTATE=0x%08X\n", hqd_pstate);
    Log("  HQD_PQ_BASE=0x%08X%08X\n", hqd_pqbhi, hqd_pqbase);
    Log("  HQD_PQ_CNTL=0x%08X\n", hqd_pqcntl);
    Log("  HQD_DBELL=0x%08X\n", hqd_dbell);
    Log("  HQD_EOP_CNTL=0x%08X\n", hqd_eopcntl);
    Log("  HQD_PQ_RPTR=0x%08X\n", rptr);
    Log("  HQD_PQ_WPTR=0x%08X%08X\n", hqd_wptrhi, hqd_wptrlo);

    /* === 5. Try to find BIOS ring buffer === */
    Log("\n--- 5. HQD_PQ_BASE scan across queues ---\n");
    /* Try all 16 ME=0 queues and 4 ME=1 queues */
    for (int q = 0; q < 16; q++) {
        SelectEngine(0, q);
        unsigned base = ReadReg(0xDAD8);
        unsigned base_hi = ReadReg(0xDADC);
        unsigned active = ReadReg(0xDAC0);
        unsigned vmid = ReadReg(0xDAC4);
        if (base != 0 || base_hi != 0 || active != 0) {
            Log("  ME0_Q%d: ACT=0x%08X VMID=0x%08X PQ_BASE=0x%08X%08X\n",
                q, active, vmid, base_hi, base);
        }
    }
    for (int q = 0; q < 4; q++) {
        SelectEngine(1, q);
        unsigned base = ReadReg(0xDAD8);
        unsigned base_hi = ReadReg(0xDADC);
        unsigned active = ReadReg(0xDAC0);
        if (base != 0 || base_hi != 0 || active != 0) {
            Log("  ME1_Q%d: ACT=0x%08X PQ_BASE=0x%08X%08X\n",
                q, active, base_hi, base);
        }
    }

    /* Restore broadcast */
    SelectEngine(0, 0);
    WriteReg(0x34D0, 0xE0000000);

    /* === 6. Critical: Read KIQ registers after BIOS restore === */
    Log("\n--- 6. KIQ Registers ---\n");
    Log("  KIQ_BASE=0x%08X%08X\n", ReadReg(0xE064), ReadReg(0xE060));
    Log("  KIQ_CNTL=0x%08X\n", ReadReg(0xE068));
    Log("  KIQ_RPTR=0x%08X\n", ReadReg(0xE06C));
    Log("  KIQ_WPTR=0x%08X\n", ReadReg(0xE078));

    /* === 7. RLC registers === */
    Log("\n--- 7. RLC Registers ---\n");
    Log("  RLC_CNTL [0xE090]=0x%08X\n", ReadReg(0xE090));
    Log("  RLC_CP_SCHEDULERS [0xECA8]=0x%08X\n", ReadReg(0xECA8));
    Log("  RLC_SERDES_CU_MASTER [0xECA4]=0x%08X\n", ReadReg(0xECA4));

    /* === 8. CP fence/doorbell === */
    Log("\n--- 8. CP Fence/Doorbell (0x3AD8-0x3AEC) ---\n");
    for (unsigned off = 0x3AD8; off <= 0x3AEC; off += 4) {
        Log("  [0x%04X]=0x%08X", off, ReadReg(off));
    }
    Log("\n");

    /* === 9. Test: Write to GCVM registers with correct offset === */
    Log("\n--- 9. GCVM Write Test ---\n");
    unsigned gcvm_off = 0x1260 + 0x2880 * 4;  /* GCVM_CONTEXT0_CNTL */
    unsigned v = ReadReg(gcvm_off);
    Log("  GCVM_CONTEXT0_CNTL [0x%05X] before: 0x%08X\n", gcvm_off, v);
    WriteReg(gcvm_off, 0x00000001);
    unsigned v2 = ReadReg(gcvm_off);
    Log("  GCVM_CONTEXT0_CNTL after write 1: 0x%08X\n", v2);

    /* Also try the "short" offset (0x2880 directly) */
    v = ReadReg(0x2880);
    Log("  GCVM_CONTEXT0_CNTL [0x2880] direct: 0x%08X\n", v);

    CloseHandle(g_hGpu);
    Log("\n=== Done ===\n");
    if (g_log) fclose(g_log);
    printf("Done. Check output\\probe-vm-path.log\n");
    return 0;
}
