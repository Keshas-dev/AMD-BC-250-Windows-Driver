/* mmhub-1a144-test.c — Test if MMHUB 0x1A144 is writable
 *
 * Hypothesis: 0x1A144 = AGP_BASE flat mapping register
 * If writable → we can reprogram GPU memory mapping
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
    printf("=== MMHUB 0x1A144 Flat Mapping Test ===\n\n");

    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) {
        printf("Cannot open device\n");
        return 1;
    }

    /* Read current values */
    printf("--- Current MMHUB MMEA values (0x1A00-0x1A60) ---\n");
    for (UINT32 off = 0x1A00; off <= 0x1A60; off += 4) {
        UINT32 v = R(h, off);
        if (v != 0xFFFFFFFF) {
            printf("  0x%05X = 0x%08X\n", off, v);
        }
    }

    /* The critical register */
    printf("\n--- 0x1A144 (AGP_BASE?) test ---\n");
    UINT32 orig_1A144 = R(h, 0x1A144);
    printf("  Current: 0x%08X\n", orig_1A144);

    /* Test 1: Write shifted value (one page up) */
    W(h, 0x1A144, 0x01201000);
    UINT32 readback1 = R(h, 0x1A144);
    printf("  Write 0x01201000: readback=0x%08X %s\n",
           readback1, readback1 == 0x01201000 ? "WRITABLE!" : "READ_ONLY");

    /* Test 2: Write completely different value */
    W(h, 0x1A144, 0xDEADBEEF);
    UINT32 readback2 = R(h, 0x1A144);
    printf("  Write 0xDEADBEEF: readback=0x%08X %s\n",
           readback2, readback2 == 0xDEADBEEF ? "WRITABLE!" : "READ_ONLY");

    /* Test 3: Try 0x1A148 as well */
    printf("\n--- 0x1A148 (AGP_BOT?) test ---\n");
    UINT32 orig_1A148 = R(h, 0x1A148);
    printf("  Current: 0x%08X\n", orig_1A148);
    W(h, 0x1A148, 0x00007000);
    UINT32 readback3 = R(h, 0x1A148);
    printf("  Write 0x00007000: readback=0x%08X %s\n",
           readback3, readback3 == 0x00007000 ? "WRITABLE!" : "READ_ONLY");

    /* Restore originals */
    W(h, 0x1A144, orig_1A144);
    W(h, 0x1A148, orig_1A148);

    /* Test 4: Scan wider MMHUB range for other alive registers */
    printf("\n--- Wider MMHUB scan (0x1A00-0x1A200) alive registers ---\n");
    int count = 0;
    for (UINT32 off = 0x1A00; off <= 0x1A200; off += 4) {
        UINT32 v = R(h, off);
        if (v != 0xFFFFFFFF && v != 0x00000000) {
            printf("  0x%05X = 0x%08X\n", off, v);
            count++;
        }
    }
    printf("  Total alive: %d\n", count);

    CloseHandle(h);

    printf("\n=== Conclusion ===\n");
    if (readback1 == 0x01201000 || readback2 == 0xDEADBEEF) {
        printf("  0x1A144 IS WRITABLE → Flat mapping path AVAILABLE!\n");
        printf("  We can reprogram MMHUB to map arbitrary system RAM.\n");
    } else {
        printf("  0x1A144 is READ_ONLY → Flat mapping locked by hardware.\n");
        printf("  Need to find alternative path (GCVM TLB injection).\n");
    }

    return 0;
}
