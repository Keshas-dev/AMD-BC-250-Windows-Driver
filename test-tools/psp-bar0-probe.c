/* psp-bar0-probe.c
 *
 * Probes the CPU-PSP device (1022:143E) driver \\.\AmdBcPsp:
 *  1. Verifies driver responds
 *  2. Dumps PSP BAR0 window 0xC000-0xC1FF (the only range its READ_REG
 *     routes to Bar0Base) looking for live registers
 *  3. Sanity-checks GPU BAR5 routing (offset 0x0000 should give GPU_ID)
 *
 * READ-ONLY — safe on live system.
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

#define DEV_PATH L"\\\\.\\AmdBcPsp"

/* CTL_CODE(FILE_DEVICE_UNKNOWN=0x22, f, METHOD_BUFFERED, FILE_ANY_ACCESS) */
#define PSP_READ_REG   0x222000UL   /* fn 0x800 */
#define PSP_GET_STATUS 0x222020UL   /* fn 0x808 */

typedef struct { ULONG Offset; ULONG Reserved; } RD_REQ;
typedef struct { ULONG Value;  ULONG Status;  } RD_RSP;

static HANDLE h = INVALID_HANDLE_VALUE;

static int ReadReg(ULONG offset, ULONG* out) {
    RD_REQ req; RD_RSP rsp;
    DWORD ret = 0;
    req.Offset = offset; req.Reserved = 0;
    rsp.Value = 0; rsp.Status = 0xFFFFFFFF;
    if (!DeviceIoControl(h, PSP_READ_REG, &req, sizeof(req), &rsp, sizeof(rsp), &ret, NULL))
        return -1;
    *out = rsp.Value;
    return 0;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    h = CreateFileW(DEV_PATH, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("FAIL: cannot open \\\\.\\AmdBcPsp gle=%lu\n", GetLastError());
        return 1;
    }
    printf("Opened \\\\.\\AmdBcPsp OK\n\n");

    /* 1. GPU BAR5 routing sanity: offset 0x0000 -> GPU_ID via GpuMmioBase/proxy */
    ULONG v = 0;
    if (ReadReg(0x0000, &v) == 0)
        printf("GPU route check  (off 0x0000): 0x%08X %s\n", v,
               (v == 0x9FFF9700 || v == 0x9FFF9714) ? "(GPU_ID alive)" : "");
    else
        printf("GPU route check  (off 0x0000): IOCTL FAILED gle=%lu\n", GetLastError());

    /* 2. NBIO signature regs inside BAR0 window */
    printf("\nNBIO sigs (BAR0 window):\n");
    ReadReg(0xC100, &v); printf("  0xC100: 0x%08X\n", v);
    ReadReg(0xC180, &v); printf("  0xC180: 0x%08X\n", v);

    /* 3. Full dump of the BAR0-accessible window 0xC000-0xC1FF */
    printf("\n=== PSP BAR0 dump 0xC000-0xC1FF (dwords) ===\n");
    int nonzero = 0;
    for (ULONG off = 0xC000; off < 0xC200; off += 4) {
        if (ReadReg(off, &v) != 0) { printf("  read fail at 0x%04X\n", off); break; }
        if ((off & 0x1F) == 0) printf("\n  0xC%03X:", off & 0xFFF);
        printf(" %08X", v);
        if (v != 0 && v != 0xFFFFFFFF) nonzero++;
    }
    printf("\n\nNon-zero/non-FF dwords in window: %d\n", nonzero);

    CloseHandle(h);
    return 0;
}
