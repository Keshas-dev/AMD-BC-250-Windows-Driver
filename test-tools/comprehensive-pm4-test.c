/* comprehensive-pm4-test.c — Test multiple PM4 submission strategies */
/* Uses both PSP driver (for KIQ submit) and GPU driver (for reliable reg access) */

#include <windows.h>
#include <stdio.h>

/* GPU driver IOCTLs */
#define GPU_DEVICE      L"\\\\.\\AMDBC250DreamV43"
#define IOCTL_GPU_INIT  CTL_CODE(0x8000, 0x270+0x70, 0, 0)  /* 0x80000B80 */
#define IOCTL_GPU_READ  CTL_CODE(0x8000, 0x270+0x72, 0, 0)  /* 0x80000B88 */
#define IOCTL_GPU_WRITE CTL_CODE(0x8000, 0x270+0x73, 0, 0)  /* 0x80000B8C */
#define IOCTL_GPU_KIQ   CTL_CODE(0x8000, 0x270+0x84, 0, 0)  /* 0x80000BD0 */
#define IOCTL_GPU_KIQ_NOP CTL_CODE(0x8000, 0x270+0x88, 0, 0) /* 0x80000BDC */

/* PSP driver IOCTLs */
#define PSP_DEVICE      L"\\\\.\\AmdBcPsp"
#define PSP_IOCTL_READ  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define PSP_IOCTL_WRITE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define PSP_IOCTL_INIT  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define PSP_IOCTL_KIQ   CTL_CODE(FILE_DEVICE_UNKNOWN, 0x818, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct {
    ULONG CommandCount;
    ULONG Reserved[3];
    ULONG Commands[64];
} PSP_KIQ_REQ;

static HANDLE gGpu = INVALID_HANDLE_VALUE;
static HANDLE gPsp = INVALID_HANDLE_VALUE;

static void OpenDevices(void) {
    gGpu = CreateFileW(GPU_DEVICE, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (gGpu == INVALID_HANDLE_VALUE) {
        printf("  GPU driver: MISSING (err=%lu)\n", GetLastError());
    } else {
        printf("  GPU driver: OPEN (handle=%p)\n", gGpu);
    }
    
    gPsp = CreateFileW(PSP_DEVICE, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (gPsp == INVALID_HANDLE_VALUE) {
        printf("  PSP driver: MISSING (err=%lu)\n", GetLastError());
    } else {
        printf("  PSP driver: OPEN (handle=%p)\n", gPsp);
    }
}

static ULONG GpuRead(ULONG offset) {
    UCHAR buf[8] = {0};
    *(ULONG*)(buf+0) = offset;
    *(ULONG*)(buf+4) = 0xDEADBEEF;
    DWORD br = 0;
    if (DeviceIoControl(gGpu, IOCTL_GPU_READ, buf, sizeof(buf), buf, sizeof(buf), &br, NULL)) {
        return *(ULONG*)(buf+4);
    }
    return 0xFFFFFFFF;
}

static BOOL GpuWrite(ULONG offset, ULONG val) {
    UCHAR buf[8] = {0};
    *(ULONG*)(buf+0) = offset;
    *(ULONG*)(buf+4) = val;
    DWORD br = 0;
    return DeviceIoControl(gGpu, IOCTL_GPU_WRITE, buf, sizeof(buf), buf, sizeof(buf), &br, NULL);
}

static ULONG PspRead(ULONG offset) {
    UCHAR buf[8] = {0};
    *(ULONG*)(buf+0) = offset;
    DWORD br = 0;
    if (DeviceIoControl(gPsp, PSP_IOCTL_READ, buf, sizeof(buf), buf, sizeof(buf), &br, NULL)) {
        return *(ULONG*)buf;
    }
    return 0xFFFFFFFF;
}

static BOOL PspKiqSubmit(ULONG cmdCount, ULONG* cmds) {
    PSP_KIQ_REQ req;
    ZeroMemory(&req, sizeof(req));
    req.CommandCount = cmdCount;
    for (ULONG i = 0; i < cmdCount && i < 64; i++) req.Commands[i] = cmds[i];
    DWORD br = 0;
    return DeviceIoControl(gPsp, PSP_IOCTL_KIQ, &req, sizeof(req), NULL, 0, &br, NULL);
}

static void DumpGpuRegs(const char* tag) {
    printf("\n--- Registers (%s) ---\n", tag);
    printf("  GPU_ID(0x0000)   = 0x%08X\n", GpuRead(0x0000));
    printf("  SCRATCH(0x32D4)  = 0x%08X\n", GpuRead(0x32D4));
    printf("  ME_CNTL(0x4A74)  = 0x%08X\n", GpuRead(0x4A74));
    printf("  KIQ_BASE(0xE060) = 0x%08X\n", GpuRead(0xE060));
    printf("  KIQ_WPTR(0xE078) = 0x%08X\n", GpuRead(0xE078));
    printf("  KIQ_RPTR(0xE06C) = 0x%08X\n", GpuRead(0xE06C));
    printf("  HQD_ACTIVE(0xDAC0) = 0x%08X\n", GpuRead(0xDAC0));
}

/* Test 1: Direct register writes via GPU driver */
static void TestDirectRegWrite(void) {
    printf("\n=== Test 1: Direct Register Write ===");
    ULONG before = GpuRead(0x32D4);
    GpuWrite(0x32D4, 0xAABBCCDD);
    ULONG after = GpuRead(0x32D4);
    printf("  Before=0x%08X -> After=0x%08X %s\n",
           before, after, after == 0xAABBCCDD ? "WRITABLE" : "READONLY");
    GpuWrite(0x32D4, before); // restore
}

/* Test 2: GPU driver KIQ test */
static void TestGpuKiq(void) {
    printf("\n=== Test 2: GPU Driver KIQ NOP ===");
    UCHAR out[128] = {0};
    DWORD br = 0;
    BOOL ok = DeviceIoControl(gGpu, IOCTL_GPU_KIQ_NOP, NULL, 0, out, sizeof(out), &br, NULL);
    if (ok && br >= 12) {
        ULONG* r = (ULONG*)out;
        printf("  Result=%u Scratch=0x%08X->0x%08X RPTR=%u->%u\n",
               r[0], r[1], r[2], r[3], r[4]);
        if (r[2] == 0xCAFEBABE) printf("  *** PM4 EXECUTED! ***\n");
        else printf("  PM4 NOT executed (GCVM issue)\n");
    } else {
        printf("  IOCTL failed (err=%lu)\n", GetLastError());
    }
}

/* Test 3: PSP KIQ submit with different PM4 formats */
static void TestPspKiqFormats(void) {
    printf("\n=== Test 3: PSP KIQ Submit (different formats) ===");
    
    /* Format A: IT_WRITE_DATA with DWORD offset */
    ULONG cmdsA[] = {
        0xC0370003,  // IT_WRITE_DATA count=3
        0x10100000,  // CONTROL: DST_SEL=register, WR_CONFIRM
        0x000032D4,  // ADDR_LO = byte address of SCRATCH
        0x00000000,  // ADDR_HI = 0
        0xCAFEBABE   // DATA
    };
    printf("\n  [Format A] DWORD offset:");
    ULONG scratchBefore = GpuRead(0x32D4);
    BOOL ok = PspKiqSubmit(5, cmdsA);
    ULONG scratchAfter = GpuRead(0x32D4);
    if (scratchAfter == 0xCAFEBABE) printf(" *** PM4 EXECUTED! ***");
    else printf(" SCRATCH=0x%08X (unchanged)", scratchAfter);
    
    /* Format B: IT_WRITE_DATA with byte offset (WRITE_DATA uses bytes) */
    ULONG cmdsB[] = {
        0xC0370003,  // IT_WRITE_DATA count=3
        0x10100000,  // CONTROL: DST_SEL=register, WR_CONFIRM
        0x000032D4,  // ADDR_LO = byte address of SCRATCH
        0x00000000,  // ADDR_HI = 0
        0xDEADBEEF   // DATA
    };
    printf("\n  [Format B] Byte offset:");
    PspKiqSubmit(5, cmdsB);
    scratchAfter = GpuRead(0x32D4);
    if (scratchAfter == 0xDEADBEEF) printf(" *** PM4 EXECUTED! ***");
    else printf(" SCRATCH=0x%08X (unchanged)", scratchAfter);
    
    /* Format C: IT_NOP only (lightest test) */
    ULONG cmdsC[] = { 0xC0001000 };  // NOP
    printf("\n  [Format C] NOP only:");
    ok = PspKiqSubmit(1, cmdsC);
    printf(" IOCTL=%s", ok ? "OK" : "FAILED");
    
    /* Format D: IT_SET_RESOURCES (VMID context) */
    ULONG cmdsD[] = {
        0xC0002C00,  // IT_SET_RESOURCES count=0
        0xC0001000   // NOP
    };
    printf("\n  [Format D] SET_RESOURCES + NOP:");
    ok = PspKiqSubmit(2, cmdsD);
    printf(" IOCTL=%s", ok ? "OK" : "FAILED");
}

/* Test 4: Scratch write via GPU driver after PSP KIQ */
static void TestGpuScratchAfterKiq(void) {
    printf("\n=== Test 4: GPU SCRATCH after PSP KIQ ===");
    ULONG scratchBefore = GpuRead(0x32D4);
    
    /* Do a KIQ submit then check if GPU state changed */
    ULONG cmds[] = {
        0xC0370003,
        0x10100000,
        0x000032D4,  // ADDR_LO = byte address of SCRATCH
        0x00000000,  // ADDR_HI = 0
        0x12345678   // DATA
    };
    BOOL ok = PspKiqSubmit(5, cmds);
    Sleep(50);  // Wait 50ms
    
    ULONG scratchAfter = GpuRead(0x32D4);
    ULONG wptrAfter = GpuRead(0xE078);
    ULONG rptrAfter = GpuRead(0xE06C);
    
    printf("  KIQ submit %s\n", ok ? "OK" : "FAILED");
    printf("  SCRATCH: 0x%08X -> 0x%08X\n", scratchBefore, scratchAfter);
    printf("  KIQ_WPTR=0x%08X KIQ_RPTR=0x%08X\n", wptrAfter, rptrAfter);
    
    if (scratchAfter == 0x12345678) {
        printf("  *** PM4 EXECUTED! ***\n");
    } else {
        printf("  PM4 NOT executed\n");
    }
}

/* Test 5: Read GPU registers via PSP proxy */
static void TestPspProxyRead(void) {
    printf("\n=== Test 5: PSP Proxy Register Read ===");
    
    ULONG pspRegs[] = {
        0x0000, 0x32D4, 0x4A74, 0xE060, 0xE078, 0xE06C, 0xDAC0, 0xDAD8
    };
    const char* regNames[] = {
        "GPU_ID", "SCRATCH", "ME_CNTL", "KIQ_BASE", 
        "KIQ_WPTR", "KIQ_RPTR", "HQD_ACTIVE", "HQD_PQ_BASE"
    };
    
    for (int i = 0; i < 8; i++) {
        ULONG pspVal = PspRead(pspRegs[i]);
        ULONG gpuVal = GpuRead(pspRegs[i]);
        printf("  %s [0x%04X]: PSP=0x%08X GPU=0x%08X %s\n",
               regNames[i], pspRegs[i], pspVal, gpuVal,
               pspVal == gpuVal ? "MATCH" : "DIFF");
    }
}

int main(void) {
    printf("=== Comprehensive PM4 Test Suite ===\n\n");
    
    OpenDevices();
    
    if (gGpu == INVALID_HANDLE_VALUE && gPsp == INVALID_HANDLE_VALUE) {
        printf("ERROR: Cannot open any driver\n");
        return 1;
    }
    
    /* Init GPU driver */
    if (gGpu != INVALID_HANDLE_VALUE) {
        UCHAR initIn[32] = {0};
        *(unsigned __int64*)(initIn + 0) = 0xFE800000ULL;
        *(unsigned*)(initIn + 8) = 0x00080000;
        *(unsigned*)(initIn + 12) = 1;
        *(unsigned __int64*)(initIn + 16) = 0xC0000000ULL;
        *(unsigned*)(initIn + 24) = 0x10000000;
        DWORD br = 0;
        BOOL ok = DeviceIoControl(gGpu, IOCTL_GPU_INIT, initIn, sizeof(initIn), NULL, 0, &br, NULL);
        printf("GPU INIT_HW: %s\n", ok ? "OK" : "FAILED");
    }
    
    /* Init PSP driver */
    if (gPsp != INVALID_HANDLE_VALUE) {
        struct { unsigned __int64 PA; unsigned size; } req = {0xFE800000ULL, 0x00080000};
        DWORD br = 0;
        BOOL ok = DeviceIoControl(gPsp, PSP_IOCTL_INIT, &req, sizeof(req), NULL, 0, &br, NULL);
        printf("PSP INIT_HW: %s\n", ok ? "OK" : "FAILED");
    }
    
    DumpGpuRegs("Initial");
    TestDirectRegWrite();
    TestGpuKiq();
    TestPspKiqFormats();
    TestGpuScratchAfterKiq();
    TestPspProxyRead();
    DumpGpuRegs("Final");
    
    if (gGpu != INVALID_HANDLE_VALUE) CloseHandle(gGpu);
    if (gPsp != INVALID_HANDLE_VALUE) CloseHandle(gPsp);
    
    printf("\n=== Test Suite Complete ===\n");
    return 0;
}