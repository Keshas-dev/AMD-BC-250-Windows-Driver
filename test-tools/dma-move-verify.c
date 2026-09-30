/*
 * dma-move-verify.c - PROVE that the software PM4 executor's IT_DMA_DATA path
 * actually moves bytes, rather than merely parsing and discarding the packet.
 *
 * WHY THIS TEST EXISTS
 * --------------------
 * dma-data-test.c verified the parser and the refuse-foreign-address policy, but
 * it could not assert that a copy happened: the only windows the operand resolver
 * accepts are driver-owned, and none of them are readable from user mode. So a
 * DMA_DATA that silently did nothing and one that copied correctly looked
 * identical.
 *
 * The trick is that the global fence page is BOTH a driver-owned window and
 * readable: the GET_HW_STATUS handler reports
 *     HwStatus->FenceValue = *DevExt->GlobalFence.VirtualAddress;
 * (kmd.c:5232) - a real read of fence memory, not a software bookkeeping value.
 * That makes the fence a window with a readback channel, so a copy can be
 * observed from user mode without adding a driver read path.
 *
 * THE TEST
 * --------
 *   step 1  EOP writes the sentinel 0x1122334455667788 into the fence
 *   step 2  read back and confirm the sentinel landed
 *   step 3  DMA_DATA copies 4 bytes from fence+4 to fence+0
 *   step 4  read back: the LOW dword must now hold what the HIGH dword held
 *
 * In little-endian terms, before step 3 the fence page is
 *     offset 0: 0x55667788   offset 4: 0x11223344
 * and copying offset 4 -> offset 0 makes it
 *     offset 0: 0x11223344   offset 4: 0x11223344
 * so the 64-bit readback becomes 0x1122334411223344 instead of
 * 0x1122334455667788. Any difference proves the copy executed; no difference
 * proves it was swallowed.
 *
 * SAFETY
 * ------
 * Everything here is the fence page - one contiguous page the driver allocated
 * for exactly this purpose. No GPU register is written, no ring is kicked, no
 * MMIO register is touched, so there is no display hazard. The fence is left
 * holding a test pattern, which is harmless: nothing waits on it (WAIT_FENCE is
 * a software counter plus its own event, and the fence is re-zeroed on TDR).
 *
 * Usage: Administrator, atikmdag loaded, HwInitMaxStep=1 + one full init so the
 * fence page exists (see AGENTS.md).
 */

#include <windows.h>
#include <stdio.h>
#include "..\\inc\\amdbc250_ioctl.h"

#define DEV L"\\\\.\\AMDBC250DreamV43"

#define PM4_TYPE3_HDR(op, count) \
    ((3u << 30) | (((count) - 1) << 16) | ((op) << 8))
#define IT_EVENT_WRITE_EOP 0x47
#define IT_DMA_DATA        0x50
#define PM4_DMA_LENGTH_MASK 0x001FFFFFu
#define DMA_CTRL_FLAGS \
    ((1u << 31) | (2u << 25) | (1u << 27) | (2u << 13) | (1u << 15))

#define SENTINEL_FULL 0x1122334455667788ULL
/* After copying offset 4 -> offset 0, both dwords hold the original high one. */
#define EXPECT_AFTER  0x1122334411223344ULL

static HANDLE g_dev = INVALID_HANDLE_VALUE;
static int g_pass = 0, g_fail = 0;

static void check(const char *name, int cond, const char *detail)
{
    if (cond) {
        g_pass++;
        printf("  [PASS] %s%s%s\n", name, detail ? " - " : "", detail ? detail : "");
    } else {
        g_fail++;
        printf("  [FAIL] %s%s%s\n", name, detail ? " - " : "", detail ? detail : "");
    }
}

static int get_status(AMDBC250_IOCTL_HW_STATUS *st)
{
    DWORD returned = 0;
    memset(st, 0, sizeof(*st));
    return DeviceIoControl(g_dev, IOCTL_AMDBC250_GET_HW_STATUS,
                           st, sizeof(*st), st, sizeof(*st),
                           &returned, NULL) ? 1 : 0;
}

static int send_pm4(AMDBC250_IOCTL_SEND_PM4 *req)
{
    DWORD returned = 0;
    return DeviceIoControl(g_dev, IOCTL_AMDBC250_SEND_PM4,
                           req, sizeof(*req), req, sizeof(*req),
                           &returned, NULL) ? 1 : 0;
}

/* Send an EOP packet so the software executor writes FenceValue into the fence.
 * The executor only checks opcode and count>=5; the payload is ignored, but it
 * is filled in properly anyway so the stream is a well-formed PM4 packet. */
static int write_fence(UINT64 value)
{
    AMDBC250_IOCTL_SEND_PM4 req;
    int i;

    memset(&req, 0, sizeof(req));
    req.Commands[0] = PM4_TYPE3_HDR(IT_EVENT_WRITE_EOP, 5);
    req.Commands[1] = 0;   /* event control - ignored by the SW executor */
    req.Commands[2] = 0;   /* address lo    - ignored */
    req.Commands[3] = 0;   /* address hi    - ignored */
    req.Commands[4] = (UINT32)(value & 0xFFFFFFFFu);
    req.Commands[5] = (UINT32)(value >> 32);
    req.CommandCount = 6;
    req.QueueType = 0;
    req.FenceValue = value;
    (void)i;
    return send_pm4(&req);
}

/* Copy `len` bytes from fence+srcOff to fence+dstOff via PM4 IT_DMA_DATA. */
static int dma_copy(UINT64 fencePa, UINT64 srcOff, UINT64 dstOff, UINT32 len)
{
    AMDBC250_IOCTL_SEND_PM4 req;

    memset(&req, 0, sizeof(req));
    req.Commands[0] = PM4_TYPE3_HDR(IT_DMA_DATA, 6);
    req.Commands[1] = DMA_CTRL_FLAGS;
    req.Commands[2] = (UINT32)((fencePa + srcOff) & 0xFFFFFFFFu);
    req.Commands[3] = (UINT32)((fencePa + srcOff) >> 32);
    req.Commands[4] = (UINT32)((fencePa + dstOff) & 0xFFFFFFFFu);
    req.Commands[5] = (UINT32)((fencePa + dstOff) >> 32);
    req.Commands[6] = len & PM4_DMA_LENGTH_MASK;
    req.CommandCount = 7;
    req.QueueType = 0;
    req.FenceValue = 0;   /* no fence packet: do not disturb the pattern */
    return send_pm4(&req);
}

int main(void)
{
    AMDBC250_IOCTL_HW_STATUS st;
    UINT64 fencePa;

    printf("=== DMA byte-movement verification ===\n\n");

    g_dev = CreateFileW(DEV, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_dev == INVALID_HANDLE_VALUE) {
        printf("FATAL: cannot open device (err %lu) - is atikmdag loaded?\n",
               GetLastError());
        return 2;
    }

    if (!get_status(&st)) {
        printf("FATAL: GET_HW_STATUS failed (err %lu)\n", GetLastError());
        CloseHandle(g_dev);
        return 2;
    }

    fencePa = st.FencePhysAddr;
    printf("fence page PA = 0x%llX, FenceInitialized=%u\n\n",
           (unsigned long long)fencePa, st.FenceInitialized);

    if (fencePa == 0) {
        printf("FATAL: no fence page. Run full-init-test.exe with\n"
               "       HwInitMaxStep=1 first (AGENTS.md).\n");
        CloseHandle(g_dev);
        return 2;
    }

    /* Step 1-2: establish a known pattern and read it back. */
    printf("Step 1: EOP writes the sentinel into the fence\n");
    check("EOP submit accepted", write_fence(SENTINEL_FULL), NULL);

    if (!get_status(&st)) {
        printf("FATAL: readback failed (err %lu)\n", GetLastError());
        CloseHandle(g_dev);
        return 2;
    }
    printf("        readback = 0x%016llX (expected 0x%016llX)\n",
           (unsigned long long)st.FenceValue,
           (unsigned long long)SENTINEL_FULL);
    if (st.FenceValue != SENTINEL_FULL) {
        check("sentinel landed in the fence", 0,
              "readback does not match - the EOP path is not writing the fence, "
              "so a later difference could not be attributed to the copy");
        printf("\n=== %d passed, %d failed ===\n", g_pass, g_fail);
        CloseHandle(g_dev);
        return 1;
    }
    check("sentinel landed in the fence", 1, NULL);

    /* Step 3-4: the copy, then the decisive readback. */
    printf("\nStep 2: DMA_DATA copies 4 bytes from fence+4 to fence+0\n");
    check("DMA_DATA submit accepted", dma_copy(fencePa, 4, 0, 4), NULL);

    if (!get_status(&st)) {
        printf("FATAL: readback failed (err %lu)\n", GetLastError());
        CloseHandle(g_dev);
        return 2;
    }
    printf("        readback = 0x%016llX (expected 0x%016llX)\n",
           (unsigned long long)st.FenceValue,
           (unsigned long long)EXPECT_AFTER);

    if (st.FenceValue == EXPECT_AFTER) {
        check("BYTE MOVEMENT PROVEN", 1,
              "the low dword now holds the high dword's value, so the "
              "executor really copied");
    } else if (st.FenceValue == SENTINEL_FULL) {
        check("BYTE MOVEMENT PROVEN", 0,
              "fence unchanged: the packet was parsed and discarded, not executed");
    } else {
        check("BYTE MOVEMENT PROVEN", 0, "unexpected value");
    }

    printf("\n=== %d passed, %d failed ===\n", g_pass, g_fail);
    CloseHandle(g_dev);
    return g_fail ? 1 : 0;
}
