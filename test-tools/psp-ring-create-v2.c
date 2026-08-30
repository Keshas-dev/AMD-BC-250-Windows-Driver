// psp-ring-create-v2.c - PSP KM ring create at the CORRECT MP0 base (0x58000)
// following Linux psp_v11_0_8_ring_create() exactly.
//
// Context: the 2026-08-18 psp-ring-probe found the real PSP C2PMSG block at
// BAR5 byte base 0x58000 (ip_discovery MP0 base 0x16000 is in DWORD units).
//   C2PMSG_64 @ 0x58200 = 0x80000000  (TOS READY bit31 SET)
//   C2PMSG_33 @ 0x58184 = 0x80000000  (PSP init complete)
//   C2PMSG_81 @ 0x58244 = 0x00395C21  (SOS present)
// All previous tests used the WRONG base 0x103D0/0x103E0/0x16000-as-byte.
//
// Linux psp_v11_0_8_ring_create (non-SRIOV) sequence:
//   1. wait C2PMSG_64 bit31 (MBOX_TOS_READY_FLAG, mask RESP|STATUS)
//   2. C2PMSG_69 = ring_mem_mc_addr low32
//   3. C2PMSG_70 = ring_mem_mc_addr high32
//   4. C2PMSG_71 = ring_size (0x1000)
//   5. C2PMSG_64 = ring_type << 16   (PSP_RING_TYPE__KM = 2 -> 0x00020000)
//   6. mdelay(20)
//   7. wait C2PMSG_64 bit31 (MBOX_TOS_RESP_FLAG)
//
// Requires: GPU driver atikmdag.sys (device \\.\AMDBC250DreamV43).
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static HANDLE g_hDev = INVALID_HANDLE_VALUE;

#define IOCTL_GPU_INIT      0x80000B80
#define IOCTL_GPU_READ      0x80000B88
#define IOCTL_GPU_WRITE     0x80000B8C
#define IOCTL_ALLOC_VIDMEM  0x80000840

typedef struct { UINT32 RegisterOffset; UINT32 Value; } REG_IO;

static BOOL WriteReg(uint32_t offset, uint32_t value) {
    REG_IO r; DWORD returned = 0;
    r.RegisterOffset = offset; r.Value = value;
    return DeviceIoControl(g_hDev, IOCTL_GPU_WRITE, &r, sizeof(r), &r, sizeof(r), &returned, NULL);
}
static uint32_t ReadReg(uint32_t offset) {
    REG_IO r; DWORD returned = 0;
    r.RegisterOffset = offset; r.Value = 0;
    if (DeviceIoControl(g_hDev, IOCTL_GPU_READ, &r, sizeof(r), &r, sizeof(r), &returned, NULL)) return r.Value;
    return 0xFFFFFFFF;
}

/* Correct MP0 C2PMSG block (BAR5 byte base 0x58000) */
#define MP0_BASE   0x58000
#define C2PMSG_64  (MP0_BASE + 0x200)   /* command / TOS-ready / response */
#define C2PMSG_67  (MP0_BASE + 0x20C)   /* ring WPTR (not used by ring_create) */
#define C2PMSG_69  (MP0_BASE + 0x214)   /* ring addr low32 */
#define C2PMSG_70  (MP0_BASE + 0x218)   /* ring addr high32 */
#define C2PMSG_71  (MP0_BASE + 0x21C)   /* ring size */
#define C2PMSG_81  (MP0_BASE + 0x244)   /* SOS status */

#define MBOX_TOS_READY_FLAG  0x80000000
#define MBOX_TOS_RESP_FLAG   0x80000000
#define MBOX_TOS_MASK        0x8000FFFF   /* RESP(bit31) | STATUS(bits15:0) */
#define PSP_RING_TYPE__KM    2

static int WaitC2p64(uint32_t expect_flag, int timeoutMs) {
    for (int i = 0; i < timeoutMs; i++) {
        uint32_t v = ReadReg(C2PMSG_64);
        if ((v & MBOX_TOS_MASK) == expect_flag) return 1;
        Sleep(1);
    }
    return 0;
}

static int AllocRing(uint32_t size, uint64_t *pPA, void **pVA) {
    ULONG in[3] = { size, 0, 0 };
    ULONG64 out[2] = { 0, 0 };
    DWORD returned = 0;
    if (!DeviceIoControl(g_hDev, IOCTL_ALLOC_VIDMEM, in, sizeof(in), out, sizeof(out), &returned, NULL)) {
        printf("  ALLOC_VIDMEM FAILED gle=%lu\n", GetLastError());
        return -1;
    }
    *pPA = out[0];
    *pVA = (void*)(UINT_PTR)out[1];
    printf("  ring buffer: PA=0x%llX VA=%p size=%u\n", *pPA, *pVA, size);
    if (*pPA == 0) return -1;
    return 0;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("BC-250 PSP ring create v2 (correct MP0 base 0x58000)\n");

    g_hDev = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (g_hDev == INVALID_HANDLE_VALUE) { printf("FAIL: CreateFile gle=%lu\n", GetLastError()); return 1; }
    printf("CreateFile OK\n");

    typedef struct { UINT64 MmioPhysicalBase; UINT32 MmioSize; UINT32 Flags; UINT64 FbPhysicalBase; UINT32 FbSize; } INIT_HW; /* FIX: full 32-byte struct */
    INIT_HW ih; DWORD ret = 0;
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL;
    ih.MmioSize = 0x80000;
    ih.Flags = 1;
    BOOL ok = DeviceIoControl(g_hDev, IOCTL_GPU_INIT, &ih, sizeof(ih), &ih, sizeof(ih), &ret, NULL);
    printf("INIT_HW: ok=%d gle=%lu\n", ok, GetLastError());

    uint32_t gpuId = ReadReg(0x0000);
    if (gpuId == 0xFFFFFFFF) { printf("READ_REG not working\n"); return 1; }
    printf("GPU_ID = 0x%08X\n", gpuId);

    /* Pre-conditions (read-only) */
    uint32_t c33 = ReadReg(MP0_BASE + 0x184);
    uint32_t c64 = ReadReg(C2PMSG_64);
    uint32_t c81 = ReadReg(C2PMSG_81);
    printf("C2PMSG_33 = 0x%08X (bit31 PSP-init=%d)\n", c33, (c33>>31)&1);
    printf("C2PMSG_64 = 0x%08X (bit31 TOS-ready=%d)\n", c64, (c64>>31)&1);
    printf("C2PMSG_81 = 0x%08X (bit31 SOS=%d)\n", c81, (c81>>31)&1);

    /* Step 1: wait TOS ready (Linux psp_v11_0_8_ring_create line 98-100) */
    printf("\n[1] Wait TOS ready (C2PMSG_64 bit31)...\n");
    if (!WaitC2p64(MBOX_TOS_READY_FLAG, 500)) {
        printf("  FAIL: TOS ready never set (mask 0x%08X)\n", ReadReg(C2PMSG_64));
        return 1;
    }
    printf("  OK: TOS ready\n");

    /* Allocate 4KB ring buffer (Linux ring_size = 0x1000) */
    uint64_t ringPA = 0;
    void *ringVA = NULL;
    if (AllocRing(0x1000, &ringPA, &ringVA) != 0) { printf("  cannot allocate ring\n"); return 1; }

    /* Steps 2-4: write ring addr (lo/hi) and size */
    printf("[2] C2PMSG_69 = ring PA low32  = 0x%08X\n", (uint32_t)(ringPA & 0xFFFFFFFF));
    WriteReg(C2PMSG_69, (uint32_t)(ringPA & 0xFFFFFFFF));
    printf("[3] C2PMSG_70 = ring PA high32 = 0x%08X\n", (uint32_t)(ringPA >> 32));
    WriteReg(C2PMSG_70, (uint32_t)(ringPA >> 32));
    printf("[4] C2PMSG_71 = ring size      = 0x1000\n");
    WriteReg(C2PMSG_71, 0x1000);

    /* Step 5: issue ring init command: ring_type << 16 (KM=2 -> 0x00020000) */
    uint32_t cmd = PSP_RING_TYPE__KM << 16;
    printf("[5] C2PMSG_64 = 0x%08X (KM ring init cmd)\n", cmd);
    WriteReg(C2PMSG_64, cmd);

    /* Step 6: 20ms handshake delay (Linux mdelay(20)) */
    printf("[6] delay 20ms...\n");
    Sleep(20);

    /* Step 7: wait response flag bit31 in C2PMSG_64 */
    printf("[7] Wait TOS response (C2PMSG_64 bit31)...\n");
    uint32_t resp = ReadReg(C2PMSG_64);
    int waited = 0;
    for (int i = 0; i < 500; i++) {
        resp = ReadReg(C2PMSG_64);
        if (resp & MBOX_TOS_RESP_FLAG) { waited = 1; break; }
        Sleep(1);
    }
    printf("  C2PMSG_64 = 0x%08X\n", resp);
    if (waited) {
        printf("  *** RING CREATED: response bit31 SET ***\n");
        printf("  C2PMSG_67 (wptr) = 0x%08X\n", ReadReg(C2PMSG_67));
        printf("  C2PMSG_69 (addr) = 0x%08X\n", ReadReg(C2PMSG_69));
        printf("  C2PMSG_70 (addr) = 0x%08X\n", ReadReg(C2PMSG_70));
        printf("  C2PMSG_71 (size) = 0x%08X\n", ReadReg(C2PMSG_71));
    } else {
        printf("  *** ring create did not ack ***\n");
    }

    printf("\n=== DONE ===\n");
    CloseHandle(g_hDev);
    return waited ? 0 : 1;
}
