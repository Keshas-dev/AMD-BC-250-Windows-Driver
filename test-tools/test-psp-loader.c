#define INITGUID
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

static FILE *gLog = NULL;

static void Log(const char *fmt, ...) {
    va_list a;
    va_start(a, fmt);
    vfprintf(stdout, fmt, a);
    va_end(a);
    if (gLog) {
        va_start(a, fmt);
        vfprintf(gLog, fmt, a);
        va_end(a);
        fflush(gLog);
    }
}

static HANDLE OpenMyDriver(void) {
    return CreateFileW(L"\\\\.\\AMDBC250DreamV43",
        GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
}

static BOOL InitHardware(HANDLE h) {
    UCHAR in[32] = {0}, out[32] = {0};
    DWORD br = 0;
    *(UINT64*)(in + 0)  = 0xFE800000ULL;  /* MMIO BAR5 */
    *(UINT32*)(in + 8)  = 0x00080000;      /* 512KB */
    *(UINT32*)(in + 12) = 1;                /* Flags=1: map only, no GPU init */
    *(UINT64*)(in + 16) = 0xC0000000ULL;  /* VRAM BAR0 */
    *(UINT32*)(in + 24) = 0x10000000;      /* 256MB */
    return DeviceIoControl(h, 0x80000B80, in, sizeof(in), out, sizeof(out), &br, NULL);
}

static BOOL ReadReg(HANDLE h, UINT32 offset, UINT32 *val) {
    UINT32 ra[2] = {offset, 0xDEADBEEF};
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, 0x80000B88, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
    *val = ra[1];
    return ok;
}

static BOOL WriteReg(HANDLE h, UINT32 offset, UINT32 val) {
    UINT32 ra[2] = {offset, val};
    DWORD br = 0;
    return DeviceIoControl(h, 0x80000B8C, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
}

/* Allocate contiguous physical memory via driver IOCTL */
static BOOL AllocPhysMem(HANDLE h, UINT64 size, UINT64 *physAddr, UINT64 *virtAddr, UINT64 *mdlHandle) {
    /* IOCTL_AMDBC250_ALLOC_VIDMEM = 0x80000840 */
    /* Input: ULONG[3] = {Size, Alignment, Flags} */
    /* Output: ULONG64[2] = {PhysicalAddress, VirtualAddress} */
    ULONG in[3] = {0};
    ULONG64 out[2] = {0};
    in[0] = (ULONG)size;
    in[1] = 0;  /* alignment (not used by driver) */
    in[2] = 0;  /* flags */
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, 0x80000840, in, sizeof(in), out, sizeof(out), &br, NULL);
    if (ok && br >= 16) {
        *physAddr = out[0];
        *virtAddr = out[1];
        *mdlHandle = 0;  /* MDL not returned by this IOCTL */
    }
    return ok;
}

/* Free contiguous physical memory */
static void FreePhysMem(HANDLE h, UINT64 virtAddr, UINT64 mdlHandle) {
    struct { UINT64 virt; UINT64 mdl; } in = {virtAddr, mdlHandle};
    DWORD br = 0;
    DeviceIoControl(h, 0x80000B88, &in, sizeof(in), NULL, 0, &br, NULL);
}

static void CheckBlockedRegs(HANDLE h, const char *label) {
    UINT32 v;
    Log("  [%s] Blocked register status:\n", label);
    ReadReg(h, 0x2004, &v); Log("    GRBM_STATUS[0x2004] = 0x%08X%s\n", v,
        (v != 0xFFFFFFFF && v != 0x00000000) ? "  *** UNBLOCKED! ***" : "");
    ReadReg(h, 0x2000, &v); Log("    CP[0x2000]          = 0x%08X%s\n", v,
        (v != 0xFFFFFFFF && v != 0x00000000) ? "  *** UNBLOCKED! ***" : "");
    ReadReg(h, 0x0D00, &v); Log("    CLK[0x0D00]         = 0x%08X%s\n", v,
        (v != 0xFFFFFFFF && v != 0x00000000) ? "  *** UNBLOCKED! ***" : "");
    ReadReg(h, 0x2074, &v); Log("    Scratch[0x2074]     = 0x%08X%s\n", v,
        (v != 0xFFFFFFFF && v != 0x00000000) ? "  *** UNBLOCKED! ***" : "");
}

int main(void) {
    gLog = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\psp-loader.log", "w");
    if (!gLog) { printf("Cannot open log file\n"); return 1; }

    Log("=== PSP Firmware Loader Test (C2PMSG Sequence) ===\n\n");

    HANDLE h = OpenMyDriver();
    if (h == INVALID_HANDLE_VALUE) {
        Log("ERROR: Cannot open driver (err=%lu)\n", GetLastError());
        fclose(gLog);
        return 1;
    }

    /* Initialize hardware */
    if (!InitHardware(h)) {
        Log("ERROR: INIT_HARDWARE failed (err=%lu)\n", GetLastError());
        fclose(gLog);
        return 1;
    }
    Log("Hardware initialized (MMIO mapped)\n\n");

    /* Baseline */
    Log("=== BASELINE (before PSP init) ===\n");
    CheckBlockedRegs(h, "baseline");

    /* Step 1: Find MP0 discovery base */
    Log("\n=== Step 1: Finding MP0 Discovery Base ===\n");
    /*
     * MP0 SMN_C2PMSG_81 (Sign of Life) is at:
     * BAR5 + (discovery_dword + 0x0091) * 4
     *
     * For Cyan Skillfish, MP0 discovery base is typically at 0x14000 (DWORD offset)
     * So: BAR5 + (0x14000 + 0x0091) * 4 = BAR5 + 0x50244
     *
     * But we need to scan for it - check multiple candidates
     */
    UINT64 bar5Phys = 0xFE800000ULL;
    UINT64 candidates[] = {
        0x14000ULL,  /* Standard Navi10 MP0 */
        0x16000ULL,  /* Alternative */
        0x18000ULL,  /* Alternative */
        0x1A000ULL,  /* Alternative */
        0x1C000ULL,  /* Alternative */
    };

    UINT32 solValue = 0;
    UINT64 mp0Base = 0;

    for (int i = 0; i < (int)(sizeof(candidates)/sizeof(candidates[0])); i++) {
        /* C2PMSG_81 at discovery_dword + 0x0091 */
        UINT32 c2pmsg81Offset = (UINT32)((candidates[i] + 0x0091) * 4);
        UINT32 val = 0;
        if (ReadReg(h, c2pmsg81Offset, &val)) {
            Log("  MP0 candidate 0x%llX: C2PMSG_81[0x%05X] = 0x%08X",
                candidates[i], c2pmsg81Offset, val);
            if (val != 0 && val != 0xFFFFFFFF) {
                Log("  *** FOUND! ***");
                solValue = val;
                mp0Base = candidates[i];
            }
            Log("\n");
        }
    }

    if (mp0Base == 0) {
        Log("WARNING: No MP0 discovery base found, using default 0x14000\n");
        mp0Base = 0x14000;
    }

    /* Calculate C2PMSG register offsets */
    UINT32 c2pmsg35 = (UINT32)((mp0Base + 0x0063) * 4);  /* Bootloader command */
    UINT32 c2pmsg36 = (UINT32)((mp0Base + 0x0064) * 4);  /* Firmware address */
    UINT32 c2pmsg64 = (UINT32)((mp0Base + 0x0080) * 4);  /* TOS mailbox */
    UINT32 c2pmsg69 = (UINT32)((mp0Base + 0x0085) * 4);  /* Ring base low */
    UINT32 c2pmsg70 = (UINT32)((mp0Base + 0x0086) * 4);  /* Ring base high */
    UINT32 c2pmsg71 = (UINT32)((mp0Base + 0x0087) * 4);  /* Ring size */
    UINT32 c2pmsg81 = (UINT32)((mp0Base + 0x0091) * 4);  /* Sign of Life */

    Log("\n  MP0 Base: 0x%llX\n", mp0Base);
    Log("  C2PMSG_35 (cmd):     0x%05X\n", c2pmsg35);
    Log("  C2PMSG_36 (fw addr): 0x%05X\n", c2pmsg36);
    Log("  C2PMSG_64 (mailbox): 0x%05X\n", c2pmsg64);
    Log("  C2PMSG_81 (SOS):     0x%05X\n", c2pmsg81);

    /* Step 2: Check if SOS is already alive */
    Log("\n=== Step 2: Checking SOS Status ===\n");
    ReadReg(h, c2pmsg81, &solValue);
    Log("  C2PMSG_81 (Sign of Life) = 0x%08X\n", solValue);
    if (solValue & 0x80000000) {
        Log("  SOS is ALIVE (bit31=1)\n");
    } else {
        Log("  SOS is NOT alive (bit31=0)\n");
    }

    /* Step 3: Read current C2PMSG values */
    Log("\n=== Step 3: Current C2PMSG Values ===\n");
    UINT32 val35, val36, val64;
    ReadReg(h, c2pmsg35, &val35);
    ReadReg(h, c2pmsg36, &val36);
    ReadReg(h, c2pmsg64, &val64);
    Log("  C2PMSG_35 = 0x%08X\n", val35);
    Log("  C2PMSG_36 = 0x%08X\n", val36);
    Log("  C2PMSG_64 = 0x%08X\n", val64);

    /* Try SMN access for PSP registers */
    Log("\n=== Step 3b: SMN Access Test ===\n");
    /* SMN addresses for MP0 C2PMSG registers (from Linux: mp_11_0_8_offset.h) */
    /* MP0 SMN base = 0x00016000 (same as discovery dword) */
    /* C2PMSG_35 SMN address = 0x16000 + 0x0063 = 0x16063 */
    /* C2PMSG_36 SMN address = 0x16000 + 0x0064 = 0x16064 */
    /* C2PMSG_81 SMN address = 0x16000 + 0x0091 = 0x16091 */
    UINT32 smnAddrs[] = {0x16063, 0x16064, 0x16080, 0x16091};
    const char *smnNames[] = {"C2PMSG_35", "C2PMSG_36", "C2PMSG_64", "C2PMSG_81"};

    for (int i = 0; i < 4; i++) {
        struct { UINT32 SmnAddr; UINT32 Data; UINT32 IsWrite; UINT32 Result; UINT32 IndexPort; UINT32 DataPort; } smnIn = {0};
        struct { UINT32 SmnAddr; UINT32 Data; UINT32 IsWrite; UINT32 Result; UINT32 IndexPort; UINT32 DataPort; } smnOut = {0};
        smnIn.SmnAddr = smnAddrs[i];
        smnIn.IsWrite = 0;  /* read */
        DWORD br = 0;
        BOOL ok = DeviceIoControl(h, 0x80000BC4, &smnIn, sizeof(smnIn), &smnOut, sizeof(smnOut), &br, NULL);
        if (ok) {
            Log("  SMN %s (0x%05X) = 0x%08X (result=%u)\n",
                smnNames[i], smnAddrs[i], smnOut.Data, smnOut.Result);
        } else {
            Log("  SMN %s (0x%05X) FAILED (err=%lu)\n",
                smnNames[i], smnAddrs[i], GetLastError());
        }
    }

    /* Step 3c: SMC Index/Data Indirect Access Test */
    Log("\n=== Step 3c: SMC Index/Data Indirect Access ===\n");
    UINT32 smcIndexOffsets[] = {0x1B8, 0x200, 0x204};
    UINT32 smcDataOffsets[] = {0x1BC, 0x204, 0x208};

    for (int i = 0; i < 3; i++) {
        UINT32 idxOff = smcIndexOffsets[i];
        UINT32 datOff = smcDataOffsets[i];

        WriteReg(h, idxOff, c2pmsg36);
        WriteReg(h, datOff, 0x000007E5);  /* test value */

        UINT32 readBack = 0;
        ReadReg(h, c2pmsg36, &readBack);
        Log("  SMC Index=0x%03X Data=0x%03X: C2PMSG_36=0x%08X\n",
            idxOff, datOff, readBack);
    }

    /* Step 3d: C2PMSG_64 Busy Bit Check */
    Log("\n=== Step 3d: C2PMSG_64 Busy Bit Check ===\n");
    UINT32 val64Check = 0;
    ReadReg(h, c2pmsg64, &val64Check);
    Log("  C2PMSG_64 initial: 0x%08X (bit31=%s)\n",
        val64Check, (val64Check & 0x80000000) ? "BUSY" : "READY");

    if (val64Check & 0x80000000) {
        Log("  Waiting for C2PMSG_64 bit31 to clear...\n");
        for (int i = 0; i < 50; i++) {
            ReadReg(h, c2pmsg64, &val64Check);
            if (!(val64Check & 0x80000000)) {
                Log("  C2PMSG_64 cleared after %d ms: 0x%08X\n", i * 10, val64Check);
                break;
            }
            Sleep(10);
        }
        if (val64Check & 0x80000000) {
            Log("  TIMEOUT: C2PMSG_64 still BUSY after 500ms\n");
        }
    }

    WriteReg(h, c2pmsg64, 0);
    ReadReg(h, c2pmsg64, &val64Check);
    Log("  After writing 0 to C2PMSG_64: 0x%08X\n", val64Check);

    /* Step 3e: Quick addr >> 12 vs >> 20 test */
    Log("\n=== Step 3e: addr >> 12 vs >> 20 (will use TMR after Step 4) ===\n");
    /* Will be tested after TMR allocation in Step 6 */

    /* Step 3f: Doorbell Register Test */
    Log("\n=== Step 3f: Doorbell Register Test ===\n");
    /*
     * AMD PSP can be triggered via Doorbell registers
     * instead of direct C2PMSG writes
     * Common doorbell offsets: 0x124, 0x180, 0x1A0
     */
    UINT32 doorbellOffsets[] = {0x124, 0x180, 0x1A0, 0x1C0};
    for (int i = 0; i < 4; i++) {
        UINT32 dbOff = doorbellOffsets[i];
        UINT32 dbVal = 0;
        ReadReg(h, dbOff, &dbVal);
        Log("  Doorbell[0x%03X] = 0x%08X\n", dbOff, dbVal);

        /* Try writing PSP doorbell trigger */
        WriteReg(h, dbOff, 0x00000001);
        ReadReg(h, dbOff, &dbVal);
        Log("  After write 1: 0x%08X\n", dbVal);
    }

    /* Step 3g: Alternative SMN Base Addresses */
    Log("\n=== Step 3g: Alternative SMN Base Addresses ===\n");
    /*
     * Cyan Skillfish might use different SMN base for MP0
     * Try: 0x19B00000, 0x13800000, 0x16000 (current)
     *
     * SMN_ADDR = SMN_BASE + (RegIndex * 4)
     * For C2PMSG_35 (index 0x63): SMN_BASE + 0x18C
     * For C2PMSG_36 (index 0x64): SMN_BASE + 0x190
     */
    UINT32 smnBases[] = {0x16000, 0x13800000, 0x19B00000};
    const char *smnBaseNames[] = {"0x16000 (current)", "0x13800000", "0x19B00000"};

    for (int b = 0; b < 3; b++) {
        UINT32 smnBase = smnBases[b];
        Log("  SMN Base: %s\n", smnBaseNames[b]);

        /* C2PMSG_35 = base + 0x63*4 = base + 0x18C */
        /* C2PMSG_36 = base + 0x64*4 = base + 0x190 */
        UINT32 smn35Addr = smnBase + 0x18C;
        UINT32 smn36Addr = smnBase + 0x190;
        UINT32 smn81Addr = smnBase + 0x244;

        /* Test SMN read via IOCTL */
        struct { UINT32 SmnAddr; UINT32 Data; UINT32 IsWrite; UINT32 Result; UINT32 IndexPort; UINT32 DataPort; } smnIn = {0};
        struct { UINT32 SmnAddr; UINT32 Data; UINT32 IsWrite; UINT32 Result; UINT32 IndexPort; UINT32 DataPort; } smnOut = {0};
        DWORD br = 0;

        /* Read C2PMSG_81 (SOL) */
        smnIn.SmnAddr = smn81Addr;
        smnIn.IsWrite = 0;
        if (DeviceIoControl(h, 0x80000BC4, &smnIn, sizeof(smnIn), &smnOut, sizeof(smnOut), &br, NULL)) {
            Log("    SMN C2PMSG_81 (0x%08X) = 0x%08X (result=%u)\n",
                smn81Addr, smnOut.Data, smnOut.Result);
        }

        /* Read C2PMSG_35 */
        smnIn.SmnAddr = smn35Addr;
        smnIn.IsWrite = 0;
        if (DeviceIoControl(h, 0x80000BC4, &smnIn, sizeof(smnIn), &smnOut, sizeof(smnOut), &br, NULL)) {
            Log("    SMN C2PMSG_35 (0x%08X) = 0x%08X (result=%u)\n",
                smn35Addr, smnOut.Data, smnOut.Result);
        }

        /* Read C2PMSG_36 */
        smnIn.SmnAddr = smn36Addr;
        smnIn.IsWrite = 0;
        if (DeviceIoControl(h, 0x80000BC4, &smnIn, sizeof(smnIn), &smnOut, sizeof(smnOut), &br, NULL)) {
            Log("    SMN C2PMSG_36 (0x%08X) = 0x%08X (result=%u)\n",
                smn36Addr, smnOut.Data, smnOut.Result);
        }

        /* Try SMN write to C2PMSG_36 */
        smnIn.SmnAddr = smn36Addr;
        smnIn.Data = 0x000007E5;
        smnIn.IsWrite = 1;
        if (DeviceIoControl(h, 0x80000BC4, &smnIn, sizeof(smnIn), &smnOut, sizeof(smnOut), &br, NULL)) {
            Log("    SMN write C2PMSG_36 = 0x000007E5 (result=%u)\n", smnOut.Result);

            /* Read back */
            smnIn.IsWrite = 0;
            if (DeviceIoControl(h, 0x80000BC4, &smnIn, sizeof(smnIn), &smnOut, sizeof(smnOut), &br, NULL)) {
                Log("    Readback C2PMSG_36 = 0x%08X\n", smnOut.Data);
            }
        }
    }

    /* Step 4: Allocate TMR (4MB) */
    Log("\n=== Step 4: Allocating TMR (4MB) ===\n");
    UINT64 tmrPhys = 0, tmrVirt = 0, tmrMdl = 0;
    if (AllocPhysMem(h, 0x400000, &tmrPhys, &tmrVirt, &tmrMdl)) {
        Log("  TMR allocated: Phys=0x%llX, Virt=0x%llX, Mdl=0x%llX\n",
            tmrPhys, tmrVirt, tmrMdl);
    } else {
        Log("  ERROR: TMR allocation failed (err=%lu)\n", GetLastError());
        /* Try smaller size */
        Log("  Trying 1MB...\n");
        if (AllocPhysMem(h, 0x100000, &tmrPhys, &tmrVirt, &tmrMdl)) {
            Log("  TMR allocated (1MB): Phys=0x%llX\n", tmrPhys);
        } else {
            Log("  ERROR: 1MB allocation also failed\n");
        }
    }

    if (tmrPhys == 0) {
        Log("\nFATAL: Cannot allocate TMR memory. Aborting.\n");
        fclose(gLog);
        return 1;
    }

    /* Step 5: Write firmware to TMR */
    Log("\n=== Step 5: Writing Firmware to TMR ===\n");

    /* Try to load firmware from file */
    const char *fwPath = "C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\cyan_skillfish2_sos_extracted.bin";
    FILE *fwFile = fopen(fwPath, "rb");
    if (fwFile) {
        fseek(fwFile, 0, SEEK_END);
        long fwSize = ftell(fwFile);
        fseek(fwFile, 0, SEEK_SET);

        if (fwSize > 0 && (size_t)fwSize <= 0x400000) {
            size_t bytesRead = fread((void*)(UINT_PTR)tmrVirt, 1, fwSize, fwFile);
            fclose(fwFile);
            Log("  Firmware loaded: %ld bytes from %s\n", fwSize, fwPath);
            Log("  First 16 bytes: ");
            for (int i = 0; i < 16 && i < (int)bytesRead; i++) {
                Log("%02X ", *(UINT8*)(UINT_PTR)(tmrVirt + i));
            }
            Log("\n");
        } else {
            fclose(fwFile);
            Log("  WARNING: Firmware file too large (%ld bytes), using empty TMR\n", fwSize);
            memset((void*)(UINT_PTR)tmrVirt, 0, 0x1000);
        }
    } else {
        Log("  WARNING: Cannot open firmware file: %s\n", fwPath);
        Log("  Using empty TMR for testing\n");
        memset((void*)(UINT_PTR)tmrVirt, 0, 0x1000);
    }

    /* Step 6: Send firmware address to C2PMSG_36 */
    Log("\n=== Step 6: Writing Firmware Address to C2PMSG_36 ===\n");

    /* First check C2PMSG_64 busy bit */
    ReadReg(h, c2pmsg64, &val64);
    Log("  C2PMSG_64 before write: 0x%08X (bit31=%s)\n",
        val64, (val64 & 0x80000000) ? "BUSY" : "READY");

    /* Try to clear C2PMSG_64 busy bit */
    if (val64 & 0x80000000) {
        WriteReg(h, c2pmsg64, 0);
        Sleep(10);
        ReadReg(h, c2pmsg64, &val64);
        Log("  After clearing C2PMSG_64: 0x%08X\n", val64);
    }

    /* Try both addr >> 12 and addr >> 20 */
    UINT32 addrShift12 = (UINT32)(tmrPhys >> 12);
    UINT32 addrShift20 = (UINT32)(tmrPhys >> 20);

    Log("\n  Method A: Direct write addr >> 20 (MB boundary)\n");
    WriteReg(h, c2pmsg36, addrShift20);
    ReadReg(h, c2pmsg36, &val36);
    Log("  C2PMSG_36 after >> 20: 0x%08X (expected 0x%08X)\n", val36, addrShift20);

    Log("\n  Method B: Direct write addr >> 12 (PFN)\n");
    WriteReg(h, c2pmsg36, addrShift12);
    ReadReg(h, c2pmsg36, &val36);
    Log("  C2PMSG_36 after >> 12: 0x%08X (expected 0x%08X)\n", val36, addrShift12);

    Log("\n  Method C: SMC Index/Data indirect access\n");
    /* Try SMC Index/Data at various offsets */
    for (int smcIdx = 0; smcIdx < 3; smcIdx++) {
        UINT32 idxOff = smcIndexOffsets[smcIdx];
        UINT32 datOff = smcDataOffsets[smcIdx];

        /* Write C2PMSG_36 physical address to SMC Index */
        WriteReg(h, idxOff, c2pmsg36);
        /* Write firmware address to SMC Data */
        WriteReg(h, datOff, addrShift20);

        /* Read back C2PMSG_36 */
        ReadReg(h, c2pmsg36, &val36);
        Log("  SMC[0x%03X/0x%03X]: C2PMSG_36=0x%08X\n", idxOff, datOff, val36);
    }

    /* Use the best method for subsequent steps */
    /* Try >> 20 first (Linux method) */
    WriteReg(h, c2pmsg36, addrShift20);

    /* Step 6b: Alternative approach - C2PMSG_64 + Doorbell */
    Log("\n=== Step 6b: C2PMSG_64 + Doorbell Approach ===\n");
    /*
     * Since C2PMSG_36 is blocked but C2PMSG_64 is writable,
     * try using C2PMSG_64 as firmware address register
     * and Doorbell[0x124] as trigger
     */

    /* Clear C2PMSG_64 first */
    WriteReg(h, c2pmsg64, 0);
    Sleep(10);
    ReadReg(h, c2pmsg64, &val64);
    Log("  C2PMSG_64 cleared: 0x%08X\n", val64);

    /* Write firmware address >> 12 to C2PMSG_64 */
    WriteReg(h, c2pmsg64, addrShift12);
    ReadReg(h, c2pmsg64, &val64);
    Log("  C2PMSG_64 after PFN write: 0x%08X (expected 0x%08X)\n", val64, addrShift12);

    /* Write firmware address >> 20 to C2PMSG_64 */
    WriteReg(h, c2pmsg64, addrShift20);
    ReadReg(h, c2pmsg64, &val64);
    Log("  C2PMSG_64 after MB write: 0x%08X (expected 0x%08X)\n", val64, addrShift20);

    /* Trigger with Doorbell[0x124] */
    Log("  Triggering Doorbell[0x124]...\n");
    WriteReg(h, 0x124, 0x00000001);
    Sleep(100);

    /* Check if PSP responded */
    ReadReg(h, c2pmsg81, &solValue);
    Log("  C2PMSG_81 (SOL) after Doorbell: 0x%08X\n", solValue);
    if (solValue & 0x80000000) {
        Log("  *** SOS ALIVE after Doorbell! ***\n");
    }

    /* Check blocked registers */
    UINT32 grbm = 0;
    ReadReg(h, 0x2004, &grbm);
    Log("  GRBM_STATUS: 0x%08X%s\n", grbm,
        (grbm != 0xFFFFFFFF && grbm != 0x00000000) ? " *** UNBLOCKED! ***" : "");

    /* Try different Doorbell values */
    Log("\n  Trying different Doorbell values...\n");
    UINT32 doorbellVals[] = {0x00000001, 0x00000002, 0x00000004, 0x80000000, 0x000000FF};
    for (int i = 0; i < 5; i++) {
        WriteReg(h, 0x124, doorbellVals[i]);
        Sleep(50);
        ReadReg(h, c2pmsg81, &solValue);
        ReadReg(h, 0x2004, &grbm);
        Log("    Doorbell=0x%08X: SOL=0x%08X, GRBM=0x%08X\n",
            doorbellVals[i], solValue, grbm);
    }

    /* Reset C2PMSG_64 */
    WriteReg(h, c2pmsg64, 0);
    WriteReg(h, 0x124, 0);

    /* Step 6c: Full PSP Boot Sequence via C2PMSG_64 + Doorbell */
    Log("\n=== Step 6c: Full PSP Boot Sequence via C2PMSG_64 + Doorbell ===\n");
    /*
     * Full sequence:
     * 1. C2PMSG_64 = firmware address >> 20 (or >> 12)
     * 2. Doorbell[0x124] = trigger
     * 3. C2PMSG_64 = LOAD_SOS command (0x20000000)
     * 4. Doorbell[0x124] = trigger again
     * 5. Wait for SOS alive (C2PMSG_81 bit31)
     */

    /* Step 6c.1: Write firmware address to C2PMSG_64 */
    Log("  Step 6c.1: Write firmware address to C2PMSG_64\n");
    WriteReg(h, c2pmsg64, 0);
    Sleep(10);
    WriteReg(h, c2pmsg64, addrShift20);  /* addr >> 20 */
    ReadReg(h, c2pmsg64, &val64);
    Log("    C2PMSG_64 = 0x%08X (firmware addr >> 20)\n", val64);

    /* Step 6c.2: Trigger Doorbell to send address */
    Log("  Step 6c.2: Trigger Doorbell[0x124] (address)\n");
    WriteReg(h, 0x124, 0x00000001);
    Sleep(100);

    /* Check SOL after address trigger */
    ReadReg(h, c2pmsg81, &solValue);
    Log("    SOL after address trigger: 0x%08X\n", solValue);

    /* Step 6c.3: Write LOAD_SOS command to C2PMSG_64 */
    Log("  Step 6c.3: Write LOAD_SOS command to C2PMSG_64\n");
    UINT32 loadSosCmd = 0x20000000;  /* PSP_BL__LOAD_SOS */
    WriteReg(h, c2pmsg64, loadSosCmd);
    ReadReg(h, c2pmsg64, &val64);
    Log("    C2PMSG_64 = 0x%08X (LOAD_SOS command)\n", val64);

    /* Step 6c.4: Trigger Doorbell to send command */
    Log("  Step 6c.4: Trigger Doorbell[0x124] (command)\n");
    WriteReg(h, 0x124, 0x00000001);
    Sleep(200);

    /* Step 6c.5: Wait for SOS alive */
    Log("  Step 6c.5: Waiting for SOS alive...\n");
    for (int i = 0; i < 50; i++) {  /* 50 * 100ms = 5 seconds */
        ReadReg(h, c2pmsg81, &solValue);
        if (solValue & 0x80000000) {
            Log("    *** SOS ALIVE after %d ms! SOL=0x%08X ***\n", i * 100, solValue);
            break;
        }
        if (i % 10 == 0) {
            Log("    Waiting... SOL=0x%08X\n", solValue);
        }
        Sleep(100);
    }

    /* Check GRBM after SOS */
    ReadReg(h, 0x2004, &grbm);
    Log("    GRBM_STATUS: 0x%08X%s\n", grbm,
        (grbm != 0xFFFFFFFF && grbm != 0x00000000) ? " *** UNBLOCKED! ***" : "");

    /* Check other blocked registers */
    UINT32 cp = 0, clk = 0;
    ReadReg(h, 0x2000, &cp);
    ReadReg(h, 0x0D00, &clk);
    Log("    CP[0x2000]: 0x%08X%s\n", cp,
        (cp != 0xFFFFFFFF && cp != 0x00000000) ? " *** UNBLOCKED! ***" : "");
    Log("    CLK[0x0D00]: 0x%08X%s\n", clk,
        (clk != 0xFFFFFFFF && clk != 0x00000000) ? " *** UNBLOCKED! ***" : "");

    /* Step 6c.6: Try alternative - write address then command in sequence */
    Log("\n  Step 6c.6: Alternative - address then command in quick sequence\n");
    WriteReg(h, c2pmsg64, 0);
    Sleep(10);

    /* Write address */
    WriteReg(h, c2pmsg64, addrShift20);
    /* Immediately write command (without doorbell in between) */
    WriteReg(h, c2pmsg64, loadSosCmd);
    /* Then trigger */
    WriteReg(h, 0x124, 0x00000001);
    Sleep(500);

    ReadReg(h, c2pmsg81, &solValue);
    ReadReg(h, 0x2004, &grbm);
    Log("    SOL=0x%08X, GRBM=0x%08X\n", solValue, grbm);

    /* Reset */
    WriteReg(h, c2pmsg64, 0);
    WriteReg(h, 0x124, 0);

    /* Step 7: Send LOAD_SOS command to C2PMSG_35 */
    Log("\n=== Step 7: Sending LOAD_SOS Command to C2PMSG_35 ===\n");
    /*
     * PSP bootloader command format:
     * bit 31: ready flag
     * bits 30:0: command specific
     *
     * For LOAD_SOS: 0x20000000 (bit 29 set)
     * Actually, from Linux: PSP_BL__LOAD_SOS = 0x20000000
     */
    UINT32 loadCmd = 0x20000000;
    Log("  Command: 0x%08X (LOAD_SOS)\n", loadCmd);
    WriteReg(h, c2pmsg35, loadCmd);

    /* Step 8: Wait for bootloader response */
    Log("\n=== Step 8: Waiting for Bootloader Response ===\n");
    UINT32 response = 0;
    BOOL gotResponse = FALSE;
    for (int i = 0; i < 100; i++) {  /* 100 * 10ms = 1 second timeout */
        ReadReg(h, c2pmsg35, &response);
        if (response & 0x80000000) {  /* bit31 = response ready */
            gotResponse = TRUE;
            Log("  Response received after %d ms: 0x%08X\n", i * 10, response);
            break;
        }
        Sleep(10);
    }

    if (!gotResponse) {
        Log("  TIMEOUT: No response after 1 second\n");
        Log("  Last C2PMSG_35 = 0x%08X\n", response);
    }

    /* Step 9: Check SOS status after command */
    Log("\n=== Step 9: Checking SOS Status After LOAD_SOS ===\n");
    ReadReg(h, c2pmsg81, &solValue);
    Log("  C2PMSG_81 (Sign of Life) = 0x%08X\n", solValue);
    if (solValue & 0x80000000) {
        Log("  SOS is ALIVE! (bit31=1)\n");
    } else {
        Log("  SOS still not alive (bit31=0)\n");
    }

    /* Step 10: Check blocked registers */
    Log("\n=== Step 10: Checking Blocked Registers ===\n");
    CheckBlockedRegs(h, "after-LOAD_SOS");

    /* Step 11: Try ring creation if SOS is alive */
    if (solValue & 0x80000000) {
        Log("\n=== Step 11: Ring Creation (SOS is alive) ===\n");

        /* Allocate ring buffer (16KB) */
        UINT64 ringPhys = 0, ringVirt = 0, ringMdl = 0;
        if (AllocPhysMem(h, 0x4000, &ringPhys, &ringVirt, &ringMdl)) {
            Log("  Ring buffer allocated: Phys=0x%llX\n", ringPhys);

            /* Write ring base to C2PMSG_69/70 */
            WriteReg(h, c2pmsg69, (UINT32)(ringPhys & 0xFFFFFFFF));  /* Low */
            WriteReg(h, c2pmsg70, (UINT32)(ringPhys >> 32));          /* High */

            /* Write ring size to C2PMSG_71 */
            WriteReg(h, c2pmsg71, 0x4000);  /* 16KB */

            /* Write ring create command to C2PMSG_64 */
            /* Ring type: 2 = GFX, or we can use the value from Linux */
            UINT32 ringCmd = 0x80000000 | 0x02;  /* bit31=ready, type=GFX */
            Log("  Sending ring create command: 0x%08X\n", ringCmd);
            WriteReg(h, c2pmsg64, ringCmd);

            /* Wait for ring create response */
            UINT32 ringResp = 0;
            for (int i = 0; i < 100; i++) {
                ReadReg(h, c2pmsg64, &ringResp);
                if (ringResp & 0x80000000) {
                    Log("  Ring create response: 0x%08X (after %d ms)\n", ringResp, i * 10);
                    break;
                }
                Sleep(10);
            }

            /* Check final register status */
            Log("\n  Final register status:\n");
            CheckBlockedRegs(h, "after-ring-create");

            /* Cleanup */
            FreePhysMem(h, ringVirt, ringMdl);
        } else {
            Log("  ERROR: Ring buffer allocation failed\n");
        }
    }

    /* Cleanup TMR */
    if (tmrVirt) {
        FreePhysMem(h, tmrVirt, tmrMdl);
    }

    /* Final check */
    Log("\n=== FINAL STATUS ===\n");
    CheckBlockedRegs(h, "final");

    CloseHandle(h);
    Log("\n=== PSP Loader Test Complete ===\n");
    if (gLog) fclose(gLog);
    printf("Done. Check output\\psp-loader.log\n");
    return 0;
}
