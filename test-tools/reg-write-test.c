#include <windows.h>
#include <stdio.h>
#pragma pack(push, 1)
typedef struct { UINT32 Off; UINT32 Val; } REG_IO;
#pragma pack(pop)
static HANDLE gH;

static UINT32 R(UINT32 off) {
    REG_IO in={off,0}, out={0}; DWORD br=0;
    DeviceIoControl(gH, 0x80000B88, &in, 8, &out, 8, &br, NULL);
    return out.Val;
}
static void W(UINT32 off, UINT32 val) {
    REG_IO in={off,val}, out={0}; DWORD br=0;
    DeviceIoControl(gH, 0x80000B8C, &in, 8, &out, 8, &br, NULL);
}

int main(void) {
    gH = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL);
    if (gH == INVALID_HANDLE_VALUE) { printf("FAIL\n"); return 1; }

    UINT8 initBuf[32] = {0};
    *(UINT64*)(initBuf+0)=0xFE800000; *(UINT32*)(initBuf+8)=0x80000;
    *(UINT32*)(initBuf+12)=1; *(UINT64*)(initBuf+16)=0xC0000000;
    *(UINT32*)(initBuf+24)=0x20000000;
    DWORD br=0; DeviceIoControl(gH, 0x80000B80, initBuf, 32, NULL, 0, &br, NULL);

    printf("=== COMPUTE Register Direct Write Test ===\n");
    UINT32 regs[] = {
        0x80E0, /* DISPATCH_INITIATOR */
        0x80E4, /* DIM_X */
        0x8110, /* PGM_LO */
        0x8114, /* PGM_HI */
        0x8128, /* PGM_RSRC1 */
        0x812C, /* PGM_RSRC2 */
        0x8138, /* STATIC_THREAD_MGMT_SE0 */
        0x8140, /* TMPRING_SIZE */
        0x9104, /* CP_MQD_BASE_ADDR */
        0x910C, /* CP_HQD_ACTIVE */
        0x9110, /* CP_HQD_VMID */
        0x9124, /* CP_HQD_PQ_BASE */
    };
    char* names[] = {
        "DISPATCH_INITIATOR","DIM_X","PGM_LO","PGM_HI",
        "PGM_RSRC1","PGM_RSRC2","STATIC_THREAD_MGMT","TMPRING_SIZE",
        "CP_MQD_BASE_ADDR","CP_HQD_ACTIVE","CP_HQD_VMID","CP_HQD_PQ_BASE"
    };
    for (int i=0;i<12;i++) {
        UINT32 orig = R(regs[i]);
        UINT32 test = (orig == 0xFFFFFFFF) ? 0xAAAAAAAA : (orig ^ 0xFFFF0000);
        if (test == orig) test = orig ^ 0xAAAAAAAA;
        W(regs[i], test);
        UINT32 read = R(regs[i]);
        printf("%-20s 0x%04X: %s (0x%08X -> 0x%08X)\n",
            names[i], regs[i],
            (read == test) ? "WRITABLE" : (read == orig ? "READ-ONLY" : "OTHER"),
            orig, read);
        W(regs[i], orig);  /* restore */
    }

    printf("\n=== KIQ Registers ===\n");
    UINT32 kiqRegs[] = {0xE060,0xE064,0xE068,0xE06C,0xE078,0xE07C,0xE080};
    char* kNames[] = {"KIQ_BASE_LO","KIQ_BASE_HI","KIQ_SIZE","KIQ_RPTR","KIQ_WPTR","KIQ_WPTR_HI","KIQ_ACTIVE"};
    for (int i=0;i<7;i++) {
        UINT32 orig = R(kiqRegs[i]);
        UINT32 test = 0xDEADBEEF;
        if (test == orig) test = 0x12345678;
        W(kiqRegs[i], test);
        UINT32 read = R(kiqRegs[i]);
        printf("%-20s 0x%04X: %s (0x%08X -> 0x%08X)\n",
            kNames[i], kiqRegs[i],
            (read == test) ? "WRITABLE" : (read == orig ? "READ-ONLY" : "OTHER"),
            orig, read);
        W(kiqRegs[i], orig);
    }

    /* Also probe PGM_LO-like registers in COMPUTE block */
    printf("\n=== Surrounding 0x8110 probe ===\n");
    for (UINT32 off = 0x8100; off <= 0x8140; off += 4) {
        printf("  0x%04X = 0x%08X\n", off, R(off));
    }

    CloseHandle(gH);
    return 0;
}
