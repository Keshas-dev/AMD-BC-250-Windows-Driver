#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* IOCTL codes */
#define IOCTL_READ_REG   0x80000B88
#define IOCTL_WRITE_REG  0x80000B8C
#define IOCTL_INIT_HW    0x80000B80

/* Register offsets from hw.h */
#define REG_GRBM_GFX_INDEX_C1  0x000034D0  /* GC_BASE+0x2270 — has init val 0xBA062100 */
#define REG_GRBM_GFX_INDEX_C2  0x0000C200  /* SEG1+0x2200 — reads 0x00000000 */

/* GRBM_GFX_INDEX bit fields (Linux soc15 layout: MEID_SHIFT=16, PIPEID_SHIFT=8, QUEUEID_SHIFT=0)
 * KIQ select: SE_BROADCAST | INSTANCE_BROADCAST | ME=1, PIPE=0, QUEUE=0
 */
#define REG_GRBM_GFX_INDEX_KIQ_VAL  0x84010000  /* (1<<31) | (1<<26) | (1<<16) */
#define REG_GRBM_GFX_INDEX_GFX_VAL  0x80000000  /* SE_BROADCAST only (ME=0, no queue select) */
#define REG_GRBM_GFX_INDEX_BROADCAST 0xE0000000 /* SE | QUEUE | PIPE broadcast (default) */

#define REG_CP_HQD_PQ_WPTR_POLL_CNTL 0x0000DB00  /* GC_BASE+0xC8A0 */
#define REG_RLC_CP_SCHEDULERS      0x0000ECAA
#define REG_RLC_SCHED_KIQ_VAL      0x000000A0

#define REG_CP_HQD_ACTIVE          0x0000DAC0
#define REG_CP_HQD_VMID            0x0000DAC4
#define REG_CP_HQD_PERSISTENT_STATE 0x0000DAC8
#define REG_CP_HQD_PQ_BASE         0x0000DAD8
#define REG_CP_HQD_PQ_BASE_HI      0x0000DADC
#define REG_CP_HQD_PQ_RPTR         0x0000DAE0
#define REG_CP_HQD_PQ_CONTROL      0x0000DAFC
#define REG_CP_HQD_PQ_WPTR_LO      0x0000DB90
#define REG_CP_HQD_PQ_WPTR_HI      0x0000DB94
#define REG_CP_HQD_EOP_BASE_ADDR   0x0000DB4C
#define REG_CP_HQD_EOP_BASE_ADDR_HI 0x0000DB50
#define REG_CP_HQD_EOP_CONTROL     0x0000DB54
#define REG_CP_HQD_EOP_RPTR        0x0000DB58
#define REG_CP_HQD_EOP_WPTR        0x0000DB5C
#define REG_CP_HQD_PQ_DOORBELL_CONTROL  0x0000DAF4
#define REG_CP_HQD_PQ_RPTR_REPORT_ADDR   0x0000DAE4
#define REG_CP_HQD_PQ_RPTR_REPORT_ADDR_HI 0x0000DAE8
#define REG_CP_HQD_PQ_WPTR_POLL_ADDR     0x0000DAEC
#define REG_CP_HQD_PQ_WPTR_POLL_ADDR_HI  0x0000DAF0
#define REG_CP_HQD_DEQUEUE_REQUEST 0x0000DB18
#define REG_CP_MQD_BASE_ADDR       0x0000DAB8
#define REG_CP_MQD_BASE_ADDR_HI    0x0000DABC
#define REG_CP_HQD_PIPE_PRIORITY   0x0000DACC
#define REG_CP_HQD_QUEUE_PRIORITY  0x0000DAD0
#define REG_CP_HQD_QUANTUM         0x0000DAD4
#define REG_KIQ_BASE_LO  0x0000E060
#define REG_KIQ_BASE_HI  0x0000E064
#define REG_KIQ_RPTR     0x0000E06C
#define REG_KIQ_WPTR     0x0000E078
#define REG_GRBM_STATUS  0x00003260
#define REG_SCRATCH      0x000032D4
#define REG_GFX_RING0_RPTR  0x0000DA6C
#define REG_GFX_RING0_WPTR  0x0000DA78

static HANDLE g_hDev = INVALID_HANDLE_VALUE;

static int open_dev(void)
{
    g_hDev = CreateFileA("\\\\.\\AMDBC250DreamV43",
        GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_hDev == INVALID_HANDLE_VALUE) { printf("FAIL: open err=%lu\n", GetLastError()); return 0; }
    return 1;
}

static int init_hw(void)
{
    unsigned char buf[32] = {0};
    *(unsigned long long*)(buf+0) = 0xFE800000ULL;
    *(unsigned int*)(buf+8) = 0x00080000;
    *(unsigned int*)(buf+12) = 1;
    *(unsigned long long*)(buf+16) = 0xC0000000ULL;
    *(unsigned int*)(buf+24) = 0x10000000;
    DWORD retlen;
    if (!DeviceIoControl(g_hDev, IOCTL_INIT_HW, buf, sizeof(buf), buf, sizeof(buf), &retlen, NULL)) {
        printf("FAIL: INIT_HW err=%lu\n", GetLastError()); return 0;
    }
    printf("INIT_HW OK\n");
    return 1;
}

static unsigned long rd(unsigned int off)
{
    unsigned int ra[2] = { off, 0 };
    DWORD retlen;
    DeviceIoControl(g_hDev, IOCTL_READ_REG, ra, sizeof(ra), ra, sizeof(ra), &retlen, NULL);
    return ra[1];
}

static void wr(unsigned int off, unsigned long val)
{
    unsigned int wa[2] = { off, val };
    DWORD retlen;
    DeviceIoControl(g_hDev, IOCTL_WRITE_REG, wa, sizeof(wa), wa, sizeof(wa), &retlen, NULL);
}

static void rreg(unsigned int off, const char *name)
{
    printf("  [0x%05X] %-32s = 0x%08X\n", off, name, rd(off));
}

static void wreg(unsigned int off, unsigned long val)
{
    wr(off, val);
}

static void wreg_chk(unsigned int off, unsigned long val, const char *name)
{
    wr(off, val);
    unsigned long chk = rd(off);
    printf("  %-32s: write 0x%08X, read=0x%08X %s\n", name, val, chk,
        (chk == val) ? "OK" : (chk == 0 ? "ZERO" : "DIFF"));
}

int main()
{
    if (!open_dev()) return 1;
    if (!init_hw()) { CloseHandle(g_hDev); return 1; }

    /* === Phase 1: Probe GRBM_GFX_INDEX at BOTH candidates === */
    printf("\n=== Phase 1: GRBM_GFX_INDEX Probe ===\n");
    unsigned int grbm_off = 0;
    struct { unsigned int off; const char *name; } cand[] = {
        { REG_GRBM_GFX_INDEX_C1, "GC_BASE+0x2270 (0x34D0)" },
        { REG_GRBM_GFX_INDEX_C2, "SEG1+0x2200 (0xC200)" },
    };
    for (int i = 0; i < 2; i++) {
        unsigned long v1 = rd(cand[i].off);
        /* Read twice to verify stability */
        unsigned long v1b = rd(cand[i].off);
        unsigned long tv = (v1 == 0xDEAD) ? 0x1234 : 0xDEAD;
        wreg(cand[i].off, tv);
        unsigned long v2 = rd(cand[i].off);
        wreg(cand[i].off, v1); /* restore */
        int writable = (v1 != tv && v2 == tv);
        int stable = (v1 == v1b);
        printf("  %-32s: before=0x%08X(%s), write(0x%08X), after=0x%08X %s\n",
            cand[i].name, v1, stable ? "stable" : "UNSTABLE", tv, v2,
            writable ? "*** WRITABLE ***" : "(read-only)");
        if (writable && !grbm_off && v1 != 0) {
            /* Prefer candidate with non-zero initial value (real GRBM config) */
            grbm_off = cand[i].off;
            printf("    -> SELECTED (non-zero initial value = 0x%08X)\n", v1);
        } else if (writable && !grbm_off) {
            grbm_off = cand[i].off;
            printf("    -> SELECTED (fallback, only writable candidate)\n");
        }
    }
    if (!grbm_off) {
        printf("\nFATAL: GRBM_GFX_INDEX NOT FOUND at either candidate.\n");
        printf("HQD registers can only be tested via broadcast.\n");
        grbm_off = REG_GRBM_GFX_INDEX_C1; /* try C1 anyway */
    }
    printf("\n  Using GRBM_GFX_INDEX at 0x%04X\n", grbm_off);

    /* === Phase 2: Read current register state === */
    printf("\n=== Phase 2: Current Register State (broadcast) ===\n");
    rreg(REG_GRBM_STATUS, "GRBM_STATUS");
    rreg(REG_SCRATCH, "SCRATCH_REG0");
    rreg(REG_KIQ_BASE_LO, "KIQ_BASE_LO");
    rreg(REG_KIQ_BASE_HI, "KIQ_BASE_HI");
    rreg(REG_KIQ_RPTR, "KIQ_RPTR");
    rreg(REG_KIQ_WPTR, "KIQ_WPTR");
    rreg(REG_GFX_RING0_RPTR, "GFX_RING0_RPTR");
    rreg(REG_GFX_RING0_WPTR, "GFX_RING0_WPTR");
    rreg(REG_CP_HQD_ACTIVE, "CP_HQD_ACTIVE (broadcast)");
    rreg(REG_CP_HQD_VMID, "CP_HQD_VMID (broadcast)");
    rreg(REG_CP_HQD_PERSISTENT_STATE, "CP_HQD_PERSISTENT_STATE");
    rreg(REG_CP_HQD_PQ_BASE, "CP_HQD_PQ_BASE");
    rreg(REG_CP_HQD_PQ_CONTROL, "CP_HQD_PQ_CONTROL");
    rreg(REG_CP_HQD_PQ_RPTR, "CP_HQD_PQ_RPTR");
    rreg(REG_CP_HQD_PQ_WPTR_LO, "CP_HQD_PQ_WPTR_LO");
    rreg(REG_CP_HQD_EOP_BASE_ADDR, "CP_HQD_EOP_BASE_ADDR");
    rreg(REG_CP_HQD_EOP_CONTROL, "CP_HQD_EOP_CONTROL");
    rreg(REG_CP_HQD_PQ_WPTR_POLL_CNTL, "CP_PQ_WPTR_POLL_CNTL");
    rreg(REG_RLC_CP_SCHEDULERS, "RLC_CP_SCHEDULERS");
    rreg(REG_CP_HQD_PQ_WPTR_HI, "CP_HQD_PQ_WPTR_HI");

    /* === Phase 3: Select KIQ via SRBM, verify HQD registers change === */
    printf("\n=== Phase 3: Select KIQ via SRBM (GRBM_GFX_INDEX=0x%08X) ===\n",
        REG_GRBM_GFX_INDEX_KIQ_VAL);
    wreg(grbm_off, REG_GRBM_GFX_INDEX_KIQ_VAL);
    unsigned long sel_check = rd(grbm_off);
    printf("  GRBM_GFX_INDEX readback = 0x%08X %s\n", sel_check,
        (sel_check == REG_GRBM_GFX_INDEX_KIQ_VAL) ? "OK" : "DIFF");

    /* Read same registers again — values should differ from broadcast if selection works */
    printf("\n  === Registers AFTER KIQ selection ===\n");
    rreg(REG_CP_HQD_ACTIVE, "CP_HQD_ACTIVE (KIQ selected)");
    rreg(REG_CP_HQD_VMID, "CP_HQD_VMID (KIQ selected)");
    rreg(REG_CP_HQD_PERSISTENT_STATE, "CP_HQD_PERSISTENT_STATE");
    rreg(REG_CP_HQD_PQ_BASE, "CP_HQD_PQ_BASE");
    rreg(REG_CP_HQD_PQ_CONTROL, "CP_HQD_PQ_CONTROL");
    rreg(REG_CP_HQD_PQ_RPTR, "CP_HQD_PQ_RPTR");
    rreg(REG_CP_HQD_PQ_WPTR_LO, "CP_HQD_PQ_WPTR_LO");
    rreg(REG_CP_HQD_EOP_BASE_ADDR, "CP_HQD_EOP_BASE_ADDR");
    rreg(REG_CP_HQD_EOP_CONTROL, "CP_HQD_EOP_CONTROL");
    rreg(REG_CP_HQD_PQ_WPTR_HI, "CP_HQD_PQ_WPTR_HI");

    /* === Phase 4: Full HQD KIQ Init (Linux gfx_v10_0_kiq_init_register) === */
    printf("\n=== Phase 4: Full HQD KIQ Init Sequence ===\n");

    printf("\n  --- Step 1: Deactivate queue ---\n");
    wreg_chk(REG_CP_HQD_ACTIVE, 0, "CP_HQD_ACTIVE=0");

    printf("\n  --- Step 2: Disable wptr polling ---\n");
    wreg_chk(REG_CP_HQD_PQ_WPTR_POLL_CNTL, 0, "CP_PQ_WPTR_POLL_CNTL=0");

    printf("\n  --- Step 3: Disable doorbell ---\n");
    wreg_chk(REG_CP_HQD_PQ_DOORBELL_CONTROL, 0, "CP_HQD_PQ_DOORBELL_CONTROL=0");

    printf("\n  --- Step 4: Set EOP base/control ---\n");
    wreg_chk(REG_CP_HQD_EOP_BASE_ADDR, 0, "CP_HQD_EOP_BASE_ADDR=0");
    wreg(REG_CP_HQD_EOP_BASE_ADDR_HI, 0);
    wreg_chk(REG_CP_HQD_EOP_CONTROL, 0x08000000, "CP_HQD_EOP_CONTROL=0x08000000");

    printf("\n  --- Step 5: Set MQD base (zero) ---\n");
    wreg_chk(REG_CP_MQD_BASE_ADDR, 0, "CP_MQD_BASE_ADDR=0");
    wreg(REG_CP_MQD_BASE_ADDR_HI, 0);

    printf("\n  --- Step 6: Set PQ base (ring buffer address) ---\n");
    printf("  [0x%05X] %-32s = 0x%08X (skip write-0xDEAD, proven writable from probe, unsafe)\n",
        REG_CP_HQD_PQ_BASE, "CP_HQD_PQ_BASE", rd(REG_CP_HQD_PQ_BASE));
    wreg_chk(REG_CP_HQD_PQ_BASE, 0, "CP_HQD_PQ_BASE=0");

    printf("\n  --- Step 7: Set PQ control (ring size) ---\n");
    wreg_chk(REG_CP_HQD_PQ_CONTROL, 8, "CP_HQD_PQ_CONTROL=8");

    printf("\n  --- Step 8: Clear RPTR/WPTR addresses ---\n");
    wreg(REG_CP_HQD_PQ_RPTR_REPORT_ADDR, 0);
    wreg(REG_CP_HQD_PQ_RPTR_REPORT_ADDR_HI, 0);
    wreg(REG_CP_HQD_PQ_WPTR_POLL_ADDR, 0);
    wreg(REG_CP_HQD_PQ_WPTR_POLL_ADDR_HI, 0);

    printf("\n  --- Step 9: Set WPTR=0 (test write+read) ---\n");
    wreg_chk(REG_CP_HQD_PQ_WPTR_LO, 0, "CP_HQD_PQ_WPTR_LO=0");

    printf("\n  --- Step 10: Set VMID ---\n");
    wreg_chk(REG_CP_HQD_VMID, 0, "CP_HQD_VMID=0");

    printf("\n  --- Step 11: Set persistent state ---\n");
    wreg_chk(REG_CP_HQD_PERSISTENT_STATE, 0xE001, "CP_HQD_PERSISTENT_STATE=0xE001");

    printf("\n  --- Step 12: Activate queue ---\n");
    wreg_chk(REG_CP_HQD_ACTIVE, 1, "CP_HQD_ACTIVE=1");
    unsigned long active_val = rd(REG_CP_HQD_ACTIVE);
    printf("    -> CP_HQD_ACTIVE = 0x%08X %s\n", active_val,
        (active_val == 1) ? "*** ACTIVATED ***" : (active_val == 0 ? "STILL 0" : "UNEXPECTED"));

    /* Restore broadcast */
    wreg(grbm_off, REG_GRBM_GFX_INDEX_BROADCAST);

    printf("\n  --- Step 13: Notify RLC scheduler ---\n");
    wreg_chk(REG_RLC_CP_SCHEDULERS, REG_RLC_SCHED_KIQ_VAL, "RLC_CP_SCHEDULERS=0xA0");

    /* Restore RLC */
    wreg(REG_RLC_CP_SCHEDULERS, 0x002000E4); /* restore original */

    /* === Phase 4b: Try GFX queue (ME=0) === */
    printf("\n=== Phase 4b: Try GFX queue (ME=0, GRBM_GFX_INDEX=0x84000000) ===\n");
    wreg(grbm_off, 0x84000000); /* SE_BROADCAST | INSTANCE_BROADCAST, ME=0 */
    printf("  GRBM_GFX_INDEX readback = 0x%08X\n", rd(grbm_off));

    printf("\n  --- GFX: Read CP_HQD registers under ME=0 ---\n");
    rreg(REG_CP_HQD_ACTIVE, "CP_HQD_ACTIVE (GFX)");
    rreg(REG_CP_HQD_PQ_CONTROL, "CP_HQD_PQ_CONTROL (GFX)");
    rreg(REG_CP_HQD_PQ_BASE, "CP_HQD_PQ_BASE (GFX)");
    rreg(REG_CP_HQD_PERSISTENT_STATE, "CP_HQD_PERSISTENT_STATE (GFX)");
    rreg(REG_CP_HQD_PQ_WPTR_LO, "CP_HQD_PQ_WPTR_LO (GFX)");
    rreg(REG_CP_HQD_EOP_BASE_ADDR, "CP_HQD_EOP_BASE_ADDR (GFX)");
    rreg(REG_CP_HQD_EOP_CONTROL, "CP_HQD_EOP_CONTROL (GFX)");

    printf("\n  --- GFX: Test write to CP_HQD_PQ_CONTROL ---\n");
    wreg_chk(REG_CP_HQD_PQ_CONTROL, 8, "CP_HQD_PQ_CONTROL=8");
    printf("\n  --- GFX: Test activate ---\n");
    wreg(REG_CP_HQD_ACTIVE, 0); /* deactivate first */
    wreg_chk(REG_CP_HQD_ACTIVE, 1, "CP_HQD_ACTIVE=1");
    unsigned long gfx_active = rd(REG_CP_HQD_ACTIVE);
    printf("    -> CP_HQD_ACTIVE = 0x%08X %s\n", gfx_active,
        (gfx_active == 1) ? "*** ACTIVATED ***" : (gfx_active == 0 ? "STILL 0" : "UNEXPECTED"));

    /* Restore to broadcast */
    wreg(grbm_off, REG_GRBM_GFX_INDEX_BROADCAST);

    /* === Phase 5: Verify RPTR advance (no ring buffer, just verify status) === */
    printf("\n=== Phase 5: Final Register State ===\n");
    rreg(REG_GRBM_STATUS, "GRBM_STATUS");
    rreg(REG_SCRATCH, "SCRATCH_REG0");
    rreg(REG_CP_HQD_ACTIVE, "CP_HQD_ACTIVE");
    rreg(REG_CP_HQD_PQ_RPTR, "CP_HQD_PQ_RPTR");
    rreg(REG_CP_HQD_PQ_WPTR_LO, "CP_HQD_PQ_WPTR_LO");
    rreg(REG_CP_HQD_PQ_WPTR_HI, "CP_HQD_PQ_WPTR_HI");
    rreg(REG_CP_HQD_PQ_BASE, "CP_HQD_PQ_BASE");
    rreg(REG_CP_HQD_PQ_CONTROL, "CP_HQD_PQ_CONTROL");
    rreg(REG_KIQ_RPTR, "KIQ_RPTR");
    rreg(REG_KIQ_WPTR, "KIQ_WPTR");

    /* === Summary === */
    printf("\n===================================\n");
    printf("  TEST SUMMARY\n");
    printf("===================================\n");
    printf("  GRBM_GFX_INDEX: 0x%04X (initial=0x%08X, readback=0x%08X)\n",
        grbm_off, rd(grbm_off), sel_check);
    printf("  CP_HQD_ACTIVE after activate: 0x%08X\n", active_val);
    if (active_val == 1) {
        printf("  *** KIQ QUEUE ACTIVATED SUCCESSFULLY ***\n");
        printf("  Next step: run with ring buffer to test PM4 submission\n");
    } else {
        printf("  KIQ queue NOT activated. Possible causes:\n");
        printf("   - Wrong GRBM_GFX_INDEX offset\n");
        printf("   - CP_HQD register offset wrong\n");
        printf("   - CP hardware doesn't support this queue\n");
    }

    CloseHandle(g_hDev);
    return 0;
}
