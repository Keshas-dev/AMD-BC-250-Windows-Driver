/* gcvm-cntl-bits-test.c — Systematically test GCVM_CONTEXT0_CNTL bits
 *
 * Goal: Find if any CNTL bit enables flat/physical addressing
 * so GPU can access system RAM without page tables.
 *
 * Strategy: Set each bit individually, test if GPU can see our memory.
 * SAFE: Only tests CNTL register, no PT_BASE writes, no MMHUB writes.
 *
 * DANGER: Do NOT write to 0x1A144 or 0x1A148 — causes system hang!
 */

#include <windows.h>
#include <stdio.h>

#define IOCTL_AMDBC250_READ_REG   0x80000B88
#define IOCTL_AMDBC250_WRITE_REG  0x80000B8C
#define IOCTL_AMDBC250_SEND_PM4   0x80000B94
#define IOCTL_AMDBC250_LOAD_CP_FW 0x80000BD4

typedef struct { UINT32 Offset; UINT32 Value; } REG_IO;

static HANDLE OpenDevice(void) {
    return CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
                       0, NULL, OPEN_EXISTING, 0, NULL);
}

static UINT32 R(HANDLE h, UINT32 offset) {
    REG_IO io = { offset, 0 };
    DWORD bytes;
    DeviceIoControl(h, IOCTL_AMDBC250_READ_REG, &io, sizeof(io), &io, sizeof(io), &bytes, NULL);
    return io.Value;
}

static void W(HANDLE h, UINT32 offset, UINT32 value) {
    REG_IO io = { offset, value };
    DWORD bytes;
    DeviceIoControl(h, IOCTL_AMDBC250_WRITE_REG, &io, sizeof(io), &io, sizeof(io), &bytes, NULL);
}

/* Read SCRATCH to check if GPU executed PM4 */
static UINT32 ReadScratch(HANDLE h) { return R(h, 0x32D4); }

int main(void) {
    printf("=== GCVM CONTEXT0_CNTL Bit Test ===\n");
    printf("Tests each bit to find flat/physical mapping mode.\n");
    printf("DANGER: Do NOT write 0x1A144 or 0x1A148!\n\n");

    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) {
        printf("Cannot open device\n");
        return 1;
    }

    UINT32 cntl_orig = R(h, 0x0B460);
    printf("Original CNTL = 0x%08X\n\n", cntl_orig);

    /* Test each bit individually */
    printf("--- Test 1: Set each bit individually (bit 0-31) ---\n");
    printf("For each bit: set bit, read CNTL, restore original\n\n");

    for (int bit = 0; bit < 32; bit++) {
        UINT32 test_val = cntl_orig | (1u << bit);
        W(h, 0x0B460, test_val);
        UINT32 readback = R(h, 0x0B460);
        W(h, 0x0B460, cntl_orig);  /* restore immediately */

        if (readback == test_val) {
            printf("  Bit %2d: SET OK (0x%08X) — bit is WRITABLE\n", bit, readback);
        } else if (readback == cntl_orig) {
            printf("  Bit %2d: IGNORED (readback=0x%08X) — bit is RO/hardware-managed\n",
                   bit, readback);
        } else {
            printf("  Bit %2d: UNEXPECTED (wrote 0x%08X, read 0x%08X)\n",
                   bit, test_val, readback);
        }
    }

    /* Test 2: Clear each bit individually */
    printf("\n--- Test 2: Clear each bit individually (bit 0-31) ---\n");
    for (int bit = 0; bit < 32; bit++) {
        if (!(cntl_orig & (1u << bit))) continue;  /* skip bits already 0 */

        UINT32 test_val = cntl_orig & ~(1u << bit);
        W(h, 0x0B460, test_val);
        UINT32 readback = R(h, 0x0B460);
        W(h, 0x0B460, cntl_orig);  /* restore */

        if (readback == test_val) {
            printf("  Bit %2d: CLEARED OK — bit is WRITABLE\n", bit);
        } else {
            printf("  Bit %2d: IGNORED (readback=0x%08X) — bit is RO\n", bit, readback);
        }
    }

    /* Test 3: Known bits from AGENTS.md analysis */
    printf("\n--- Test 3: Known CNTL bits ---\n");
    printf("  Current CNTL = 0x%08X\n", cntl_orig);
    printf("  Bit  0 (L1_TLB_EN?)  = %d\n", (cntl_orig >> 0) & 1);
    printf("  Bit  1 (DEFAULT_PG?) = %d\n", (cntl_orig >> 1) & 1);
    printf("  Bit  2               = %d\n", (cntl_orig >> 2) & 1);
    printf("  Bit  3               = %d\n", (cntl_orig >> 3) & 1);
    printf("  Bit  7               = %d\n", (cntl_orig >> 7) & 1);
    printf("  Bit 11               = %d\n", (cntl_orig >> 11) & 1);
    printf("  Bit 13               = %d\n", (cntl_orig >> 13) & 1);
    printf("  Bit 15               = %d\n", (cntl_orig >> 15) & 1);
    printf("  Bit 18               = %d\n", (cntl_orig >> 18) & 1);
    printf("  Bit 19               = %d\n", (cntl_orig >> 19) & 1);
    printf("  Bit 24               = %d\n", (cntl_orig >> 24) & 1);
    printf("  Bit 31 (LOCK?)       = %d\n", (cntl_orig >> 31) & 1);

    /* Test 4: Try CNTL=0x01 (just bit 0) */
    printf("\n--- Test 4: Minimal CNTL values ---\n");

    /* Save SCRATCH before test */
    UINT32 scratch_orig = ReadScratch(h);
    printf("  SCRATCH before = 0x%08X\n", scratch_orig);

    /* CNTL = 0x01 (bit 0 only) */
    W(h, 0x0B460, 0x01);
    UINT32 cntl_after = R(h, 0x0B460);
    printf("  Set CNTL=0x01: readback=0x%08X\n", cntl_after);
    W(h, 0x0B460, cntl_orig);

    /* CNTL = 0x02 (bit 1 only - possible flat mapping) */
    W(h, 0x0B460, 0x02);
    cntl_after = R(h, 0x0B460);
    printf("  Set CNTL=0x02: readback=0x%08X\n", cntl_after);
    W(h, 0x0B460, cntl_orig);

    /* CNTL = 0x00 (everything off) */
    W(h, 0x0B460, 0x00);
    cntl_after = R(h, 0x0B460);
    printf("  Set CNTL=0x00: readback=0x%08X\n", cntl_after);
    W(h, 0x0B460, cntl_orig);

    /* Test 5: Check if SCRATCH changed after CNTL modifications */
    UINT32 scratch_after = ReadScratch(h);
    printf("\n  SCRATCH after = 0x%08X\n", scratch_after);
    if (scratch_after != scratch_orig) {
        printf("  SCRATCH CHANGED! GPU may have reacted to CNTL changes.\n");
    }

    /* Test 6: Check Context0 config registers after CNTL changes */
    printf("\n--- Test 6: Context0 config after CNTL test ---\n");
    W(h, 0x0B460, 0x00);
    W(h, 0x0B460, cntl_orig);
    for (UINT32 off = 0x0B404; off <= 0x0B4D4; off += 4) {
        UINT32 v = R(h, off);
        if (v != 0) {
            printf("  0x%05X = 0x%08X\n", off, v);
        }
    }

    /* Restore everything */
    W(h, 0x0B460, cntl_orig);

    CloseHandle(h);

    printf("\n=== Analysis ===\n");
    printf("  Look for: bits that are WRITABLE and might control addressing.\n");
    printf("  Bit 1 (DEFAULT_PG?) is particularly interesting.\n");
    printf("  If bit 1 enables flat mapping, GPU could access system RAM.\n");

    return 0;
}
