/* gcvm-tlb-inject.c — Try PTE injection into Context0 zero entries
 *
 * Context0 entries at 0x0B410-0x0B418 are ZERO (unused).
 * Try writing PTE-format values to see if they create TLB mappings.
 *
 * SAFE: Only writes to zero entries, restores originals.
 * If GPU can access ring buffer after injection → TLB entries are data.
 */

#include <windows.h>
#include <stdio.h>

#define IOCTL_AMDBC250_READ_REG  0x80000B88
#define IOCTL_AMDBC250_WRITE_REG 0x80000B8C

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

int main(void) {
    printf("=== GCVM Context0 TLB Injection Test ===\n\n");

    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) {
        printf("Cannot open device\n");
        return 1;
    }

    /* Step 1: Read all Context0 entries */
    printf("--- Step 1: Context0 entries (0x0B404-0x0B454) ---\n");
    UINT32 entries[21];
    for (int i = 0; i < 21; i++) {
        UINT32 off = 0x0B404 + i * 4;
        entries[i] = R(h, off);
        printf("  0x%05X = 0x%08X %s\n", off, entries[i],
               entries[i] == 0 ? "(ZERO - potential target)" : "");
    }

    /* Step 2: Identify zero entries */
    printf("\n--- Step 2: Zero entries (safe to overwrite) ---\n");
    int zero_count = 0;
    for (int i = 0; i < 21; i++) {
        if (entries[i] == 0) {
            printf("  Entry %d (0x%05X) = ZERO\n", i, 0x0B404 + i * 4);
            zero_count++;
        }
    }
    printf("  Total zero entries: %d\n", zero_count);

    if (zero_count == 0) {
        printf("  No zero entries available for injection. Aborting.\n");
        CloseHandle(h);
        return 0;
    }

    /* Step 3: Try PTE injection into first zero entry */
    printf("\n--- Step 3: PTE injection test ---\n");

    /* Known ring buffer: BIOS KIQ at PA=0x48B22000 */
    /* PTE format (RDNA2): bit0=VALID, bit1=SYSTEM, bit5=READABLE, bit6=WRITABLE */
    /* PA >> 12 = 0x48B22, flags = 0x63 */
    UINT32 pte_kiq = (0x48B22 << 12) | 0x63;  /* VALID|SYSTEM|R|W */
    printf("  PTE for KIQ ring (PA=0x48B22000): 0x%08X\n", pte_kiq);
    printf("  PTE breakdown: PA=0x%05X000, flags=0x%02X\n", 0x48B22, 0x63);

    /* Find first zero entry and try writing */
    for (int i = 0; i < 21; i++) {
        if (entries[i] == 0) {
            UINT32 target_off = 0x0B404 + i * 4;
            printf("  Target: entry %d at 0x%05X\n", i, target_off);

            /* Write PTE */
            W(h, target_off, pte_kiq);
            UINT32 readback = R(h, target_off);
            printf("  Write result: readback=0x%08X %s\n",
                   readback, readback == pte_kiq ? "OK" : "MISMATCH");

            if (readback == pte_kiq) {
                printf("  *** PTE INJECTION SUCCESSFUL! ***\n");
                printf("  Entry is now: PA=0x%05X000, flags=0x%02X\n",
                       readback >> 12, readback & 0xFF);
            }

            /* Restore */
            W(h, target_off, 0);
            UINT32 restored = R(h, target_off);
            printf("  Restored to 0: readback=0x%08X %s\n",
                   restored, restored == 0 ? "OK" : "FAILED TO RESTORE");
            break;
        }
    }

    /* Step 4: Also try with different PTE patterns */
    printf("\n--- Step 4: Multiple PTE patterns ---\n");
    UINT32 pte_patterns[] = {
        0x00000063,  /* PA=0, VALID|SYSTEM|R|W */
        0x48B22063,  /* PA=0x48B22, VALID|SYSTEM|R|W (if format is PA|flags) */
        0x48B22001,  /* PA=0x48B22, VALID only */
        0x00000001,  /* VALID only */
        0x00000003,  /* VALID|SYSTEM */
    };
    const char *names[] = {
        "PA=0 + flags",
        "PA=0x48B22 + flags (shifted)",
        "PA=0x48B22 + VALID only",
        "VALID only",
        "VALID|SYSTEM",
    };

    /* Find a zero entry for testing */
    int test_idx = -1;
    for (int i = 0; i < 21; i++) {
        if (entries[i] == 0) { test_idx = i; break; }
    }
    if (test_idx < 0) {
        printf("  No zero entries found.\n");
        CloseHandle(h);
        return 0;
    }
    UINT32 test_off = 0x0B404 + test_idx * 4;

    for (int p = 0; p < sizeof(pte_patterns)/sizeof(pte_patterns[0]); p++) {
        W(h, test_off, pte_patterns[p]);
        UINT32 rb = R(h, test_off);
        printf("  Pattern %d (%s): wrote=0x%08X read=0x%08X %s\n",
               p, names[p], pte_patterns[p], rb,
               rb == pte_patterns[p] ? "OK" : "MISMATCH");
        W(h, test_off, 0);  /* restore */
    }

    /* Step 5: Check CNTL and SCRATCH for any reaction */
    printf("\n--- Step 5: System state after injection ---\n");
    printf("  CNTL (0x0B460) = 0x%08X\n", R(h, 0x0B460));
    printf("  SCRATCH (0x32D4) = 0x%08X\n", R(h, 0x32D4));
    printf("  PT_BASE_LO (0x0B608) = 0x%08X\n", R(h, 0x0B608));

    /* Verify Context0 entries are restored */
    printf("\n--- Verification: entries restored to original ---\n");
    for (int i = 0; i < 21; i++) {
        UINT32 off = 0x0B404 + i * 4;
        UINT32 current = R(h, off);
        if (current != entries[i]) {
            printf("  WARNING: 0x%05X changed! was=0x%08X now=0x%08X\n",
                   off, entries[i], current);
        }
    }
    printf("  All entries verified.\n");

    CloseHandle(h);

    printf("\n=== Analysis ===\n");
    printf("  If all patterns wrote OK → entries are普通 writable registers.\n");
    printf("  If some patterns mismatched → hardware is modifying values.\n");
    printf("  Next: check if GPU can access PA=0x48B22000 after injection.\n");
    printf("  (Requires PM4 test with injected PTE pointing to ring buffer.)\n");

    return 0;
}
