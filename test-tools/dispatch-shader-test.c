#include <windows.h>
#include <stdio.h>

#define IOCTL_INIT_HW            0x80000B80
#define IOCTL_READ_REG           0x80000B88
#define IOCTL_WRITE_REG          0x80000B8C
#define IOCTL_SEND_PM4           0x80000B84
#define IOCTL_WRITE_PHYSICAL_MEM 0x80000C10
#define IOCTL_READ_PHYSICAL_MEM  0x80000C14
#define IOCTL_DISCOVER_PCI       0x80000BB4

typedef struct { UINT32 Off; UINT32 Val; } REG_IO;
typedef struct { UINT32 Cmds[64]; UINT32 Cnt; UINT32 Pad; UINT64 Fence; UINT32 Q; UINT32 Pad2; } SPM4;

typedef struct {
    UINT64 PhysicalAddress;
    UINT32 Size;
    UINT32 IsMemoryBar;
    UINT32 Is64Bit;
} AMDBC250_IOCTL_PCI_BAR_INFO;

typedef struct {
    UINT16 VendorId;
    UINT16 DeviceId;
    UINT16 Command;
    UINT16 Status;
    UINT32 RevisionId;
    UINT32 ClassCode;
    AMDBC250_IOCTL_PCI_BAR_INFO Bars[6];
    UINT32 Bus;
    UINT32 Device;
    UINT32 Function;
} AMDBC250_IOCTL_PCI_CONFIG;

typedef struct {
    UINT32 VendorFound;
    UINT32 FoundBus;
    UINT32 FoundDevice;
    UINT32 FoundFunction;
    UINT32 MethodUsed;
    AMDBC250_IOCTL_PCI_CONFIG PciConfig;
} AMDBC250_IOCTL_DISCOVER_PCI;

static HANDLE gH = INVALID_HANDLE_VALUE;
static UINT32 R(UINT32 off) {
    REG_IO in={off,0}, out={0}; DWORD br=0;
    DeviceIoControl(gH, IOCTL_READ_REG, &in, sizeof(in), &out, sizeof(out), &br, NULL);
    return out.Val;
}
static void W(UINT32 off, UINT32 val) {
    REG_IO in={off,val}, out={0}; DWORD br=0;
    DeviceIoControl(gH, IOCTL_WRITE_REG, &in, sizeof(in), &out, sizeof(out), &br, NULL);
}

static BOOL WritePhys(UINT64 pa, const void* data, ULONG size) {
    UCHAR buf[4096 + 12];
    ULONG hdr_size = sizeof(ULONG) * 3;
    if (hdr_size + size > sizeof(buf)) return FALSE;
    ((PULONG)buf)[0] = (ULONG)(pa & 0xFFFFFFFF);
    ((PULONG)buf)[1] = (ULONG)(pa >> 32);
    ((PULONG)buf)[2] = size;
    memcpy(buf + hdr_size, data, size);
    DWORD br = 0;
    BOOL ok = DeviceIoControl(gH, IOCTL_WRITE_PHYSICAL_MEM, buf, hdr_size + size, NULL, 0, &br, NULL);
    if (!ok) printf("    WRITE_PA 0x%llX sz=%lu err=%lu\n", pa, size, GetLastError());
    return ok;
}

/* Returns 1 if successful, output in data */
static BOOL ReadPhys(UINT64 pa, void* data, ULONG size) {
    UCHAR inBuf[12]; /* 3 ULONGs */
    ((PULONG)inBuf)[0] = (ULONG)(pa & 0xFFFFFFFF);
    ((PULONG)inBuf)[1] = (ULONG)(pa >> 32);
    ((PULONG)inBuf)[2] = size;
    DWORD br = 0;
    BOOL ok = DeviceIoControl(gH, IOCTL_READ_PHYSICAL_MEM, inBuf, sizeof(inBuf), data, size, &br, NULL);
    if (!ok) printf("    READ_PA 0x%llX sz=%lu err=%lu\n", pa, size, GetLastError());
    return ok;
}

int main(void) {
    printf("=== COMPUTE DISPATCH + SHADER TEST (v4) ===\n\n");
    gH = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (gH == INVALID_HANDLE_VALUE) {
        printf("ERROR: Cannot open GPU driver (err=%lu)\n", GetLastError());
        return 1;
    }
    printf("Driver opened\n");

    /* Discover PCI to get actual BAR addresses */
    AMDBC250_IOCTL_DISCOVER_PCI pci = {0};
    DWORD br = 0;
    BOOL ok = DeviceIoControl(gH, IOCTL_DISCOVER_PCI, NULL, 0, &pci, sizeof(pci), &br, NULL);
    printf("DISCOVER_PCI: %s\n", ok ? "OK" : "FAILED");
    if (ok && pci.VendorFound) {
        printf("  GPU at %lu:%lu.%lu\n", pci.FoundBus, pci.FoundDevice, pci.FoundFunction);
        for (int i = 0; i < 6; i++) {
            if (pci.PciConfig.Bars[i].PhysicalAddress) {
                printf("  BAR%d: 0x%llX size=0x%X %s%s\n", i,
                    pci.PciConfig.Bars[i].PhysicalAddress,
                    pci.PciConfig.Bars[i].Size,
                    pci.PciConfig.Bars[i].IsMemoryBar ? "MEM" : "IO",
                    pci.PciConfig.Bars[i].Is64Bit ? " 64-bit" : "");
            }
        }
    } else {
        printf("  Using hardcoded defaults\n");
    }

    /* Find VRAM BAR (should be first 64-bit MEM BAR that is not BAR5) */
    UINT64 vramCpuBase = 0;
    UINT32 vramSize = 0;
    for (int i = 0; i < 6; i++) {
        if (pci.PciConfig.Bars[i].PhysicalAddress &&
            pci.PciConfig.Bars[i].IsMemoryBar &&
            pci.PciConfig.Bars[i].Is64Bit &&
            pci.PciConfig.Bars[i].PhysicalAddress != 0xFE800000) {
            vramCpuBase = pci.PciConfig.Bars[i].PhysicalAddress;
            vramSize = pci.PciConfig.Bars[i].Size;
            printf("  Using BAR%d as VRAM: 0x%llX (size 0x%X)\n", i, vramCpuBase, vramSize);
            break;
        }
    }
    if (vramCpuBase == 0) {
        vramCpuBase = 0xC0000000ULL;
        vramSize = 0x20000000;
        printf("  Using fallback VRAM: 0x%llX\n", vramCpuBase);
    }

    UINT8 initBuf[32] = {0};
    *(UINT64*)(initBuf+0)  = 0xFE800000ULL;
    *(UINT32*)(initBuf+8)  = 0x00080000;
    *(UINT32*)(initBuf+12) = 1;
    *(UINT64*)(initBuf+16) = vramCpuBase;
    *(UINT32*)(initBuf+24) = vramSize;
    ok = DeviceIoControl(gH, IOCTL_INIT_HW, initBuf, sizeof(initBuf), NULL, 0, &br, NULL);
    printf("INIT_HARDWARE: %s\n", ok ? "OK" : "FAILED");
    if (!ok) { CloseHandle(gH); return 1; }
    printf("GRBM_STATUS = 0x%08X\n\n", R(0x3260));

    /* Save original GCVM state */
    UINT32 ctxCntl = R(0x0B460);
    printf("=== GCVM State ===\n");
    printf("  GCVM_CONTEXT0_CNTL (0x0B460) = 0x%08X%s%s\n",
           ctxCntl, (ctxCntl & 1) ? " TRANSLATION ON" : " TRANSLATION OFF",
           (ctxCntl & 2) ? " +DEFAULT_PAGE" : "");
    printf("  PT_BASE_LO (0x6C8C) = 0x%08X\n", R(0x6C8C));
    printf("  PT_BASE_HI (0x6C90) = 0x%08X\n\n", R(0x6C90));

    /* Step 0: verify WRITE_PHYSICAL_MEM via BAR5 SCRATCH */
    printf("=== Step 0: WRITE_PHYSICAL_MEM verification ===\n");
    {
        UINT64 bar5Addr = 0xFE800000ULL + 0x32D4;
        UINT32 testVal = 0xCAFEBABE;
        WritePhys(bar5Addr, &testVal, sizeof(testVal));
        UINT32 rb = R(0x32D4);
        printf("  BAR5 SCRATCH: wrote 0x%08X, read 0x%08X %s\n",
               testVal, rb, rb == testVal ? "MATCH" : "MISMATCH");

        /* Now verify READ_PHYSICAL_MEM */
        if (ok) {
            UINT32 readBack = 0;
            ReadPhys(bar5Addr, &readBack, sizeof(readBack));
            printf("  READ_PHYSICAL_MEM from 0x%llX: 0x%08X %s\n",
                   bar5Addr, readBack, readBack == testVal ? "MATCH" : "MISMATCH");
        }
    }

    /* Write shader to VRAM, verify with READ_PHYSICAL_MEM */
    UINT64 vramWrAddr = vramCpuBase + 0x100000ULL;
    printf("\n=== Step 1: Write shader to VRAM @ 0x%llX ===\n", vramWrAddr);

    /* Write a distinctive 32-byte pattern */
    UINT32 shaderBuf[8] = {0xBF9F0000, 0xBF9F0000, 0xBF9F0000, 0xBF9F0000,
                           0xBF9F0000, 0xBF9F0000, 0xBF9F0000, 0xBF9F0000};
    ok = WritePhys(vramWrAddr, shaderBuf, sizeof(shaderBuf));
    printf("  Write to VRAM: %s\n", ok ? "OK" : "FAILED");

    if (ok) {
        UINT32 readBack[8] = {0};
        BOOL rOK = ReadPhys(vramWrAddr, readBack, sizeof(readBack));
        printf("  Read back from VRAM: %s\n", rOK ? "OK" : "FAILED");
        if (rOK) {
            int match = (memcmp(shaderBuf, readBack, sizeof(shaderBuf)) == 0);
            printf("  Data: %08X %08X %08X %08X ... %s\n",
                   readBack[0], readBack[1], readBack[2], readBack[3],
                   match ? "MATCH" : "MISMATCH");
        }
    }

    /* Try DISPATCH with PGM at GPU physical addresses */
    /* GPU physical 0x100000 = VRAM offset 0x100000 (with GCVM off) */
    static const UINT64 gpuPhysAddrs[] = {0x100000, 0, 0x200000};

    /* Test GCVM DISABLED */
    UINT32 saveCtxCntl = ctxCntl;
    W(0x0B460, ctxCntl & ~1);
    UINT32 ctxAfter = R(0x0B460);
    printf("  CONTEXT0_CNTL: 0x%08X -> 0x%08X (GCVM %s)\n\n",
           ctxCntl, ctxAfter, (ctxAfter & 1) ? "ON" : "OFF");

    /* Write shader code (GCM s_endpgm repeated) */
    {
        UINT32 buf[256];
        for (int i = 0; i < 256; i++) buf[i] = 0xBF9F0000;
        WritePhys(vramWrAddr, buf, sizeof(buf));
        printf("  Shader (256 s_endpgm) written to VRAM CPU=0x%llX GPU=0x100000\n", vramWrAddr);
    }

    for (int gi = 0; gi < 3; gi++) {
        UINT64 gpuPa = gpuPhysAddrs[gi];
        printf("\n=== Trial: PGM=GPU_PA 0x%llX ===\n", gpuPa);

        /* Set PGM */
        if (gpuPa) {
            W(0xDC70, (UINT32)(gpuPa >> 8));
            W(0xDC74, (UINT32)(gpuPa >> 40));
        } else {
            W(0xDC70, 0);
            W(0xDC74, 0);
        }
        printf("  PGM_LO=0x%08X HI=0x%08X\n", R(0xDC70), R(0xDC74));

        /* Set resource limits */
        W(0xDC78, 0x00001001);  /* thread groups per CU = 1 */
        printf("  LIMITS=0x%08X\n", R(0xDC78));

        /* Set RSRC1 - basic wavefront config */
        W(0xDC68, 0x00000100);  /* WAVEFRONT_SIZE=256, TGSIZE_EN=1 */
        printf("  RSRC1=0x%08X\n", R(0xDC68));

        /* Set RSRC2 - scratch + user SGPR */
        W(0xDC6C, 0x00000000);
        printf("  RSRC2=0x%08X\n", R(0xDC6C));

        /* Read START before */
        printf("  START before=0x%08X STATUS before=0x%08X DIR=0x%08X\n",
               R(0xDC64), R(0x3260), R(0xDC60));

        /* DISPATCH with VALID=1 via SW PM4 */
        SPM4 spm4 = {0};
        spm4.Cmds[0] = (3<<30)|((4-1)<<16)|(0x15<<8);
        spm4.Cmds[1] = 1; spm4.Cmds[2] = 1; spm4.Cmds[3] = 1;
        spm4.Cmds[4] = 0x80000000;  /* VALID=1 */
        spm4.Cnt = 5;
        DeviceIoControl(gH, IOCTL_SEND_PM4, &spm4, sizeof(spm4), NULL, 0, &br, NULL);

        printf("  START after=0x%08X STATUS=0x%08X\n", R(0xDC64), R(0x3260));

        /* Poll for 500 iterations */
        int busy = 0;
        for (int i = 0; i < 500; i++) {
            UINT32 gs = R(0x3260);
            if (gs != 0) {
                printf("  *** BUSY at iter %d: STATUS=0x%08X ***\n", i, gs);
                busy = 1;
                break;
            }
        }
        if (!busy) printf("  GRBM_STATUS stayed 0 (idle)\n");

        printf("  FINAL: STATUS=0x%08X GUILTY=0x%08X DIR=0x%08X START=0x%08X\n",
               R(0x3260), R(0x3264), R(0xDC60), R(0xDC64));
    }

    /* Restore */
    W(0x0B460, saveCtxCntl);
    W(0xDC70, 0); W(0xDC74, 0); W(0xDC78, 0); W(0xDC68, 0); W(0xDC6C, 0);
    printf("\n  GCVM restored to 0x%08X\n", R(0x0B460));

    printf("\n=== Done ===\n");
    CloseHandle(gH);
    return 0;
}
