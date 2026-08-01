#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>

#define AMDBC250_DEVICE_PATH L"\\\\.\\AMDBC250DreamV43"
#define IOCTL_AMDBC250_READ_REG   ((ULONG)0x80000B88)
#define IOCTL_AMDBC250_WRITE_REG  ((ULONG)0x80000B8C)
#define IOCTL_AMDBC250_INIT_HARDWARE ((ULONG)0x80000B80)

typedef struct { ULONG Offset, Value, Status; } REG_IOCTL;
typedef struct { ULONG64 MmioPhysicalBase; ULONG MmioSize, Flags; ULONG64 FbPhysicalBase; ULONG FbSize; } INIT_HW;

static HANDLE g_h;
static int OpenGpu(void) {
    g_h = CreateFileW(AMDBC250_DEVICE_PATH, GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    return g_h != INVALID_HANDLE_VALUE ? 0 : -1;
}
static ULONG ReadReg(ULONG offset) {
    REG_IOCTL req = {offset,0,0}; DWORD ret;
    DeviceIoControl(g_h, IOCTL_AMDBC250_READ_REG, &req,sizeof(req), &req,sizeof(req), &ret, NULL);
    return req.Value;
}
static int WriteReg(ULONG offset, ULONG value) {
    REG_IOCTL req = {offset,value,0}; DWORD ret;
    return DeviceIoControl(g_h, IOCTL_AMDBC250_WRITE_REG, &req,sizeof(req), &req,sizeof(req), &ret, NULL) ? 0 : -1;
}
static int InitHw(void) {
    INIT_HW ih = {0}; ih.MmioPhysicalBase = 0xFE800000ULL; ih.MmioSize = 0x80000; ih.Flags = 1; DWORD ret;
    return DeviceIoControl(g_h, IOCTL_AMDBC250_INIT_HARDWARE, &ih,sizeof(ih), &ih,sizeof(ih), &ret, NULL) ? 0 : -1;
}

/* SMN access via NBIO BAR5+0x38/0x3C */
static ULONG SmnRead(ULONG smnAddr) {
    WriteReg(0x38, smnAddr); MemoryBarrier();
    return ReadReg(0x3C);
}
static void SmnWrite(ULONG smnAddr, ULONG val) {
    WriteReg(0x38, smnAddr); MemoryBarrier();
    WriteReg(0x3C, val); MemoryBarrier();
}

/* PSP mailbox: send GFX command via C2PMSG_35/36/37 */
static int PspMailboxCmd(ULONG cmd, ULONG arg1, ULONG arg2) {
    ULONG c35 = ReadReg(0x1056C);
    if (c35 != 0) { printf("  mailbox busy (C35=0x%08X)\n", c35); return -1; }
    WriteReg(0x10570, arg1);  /* C2PMSG_36 */
    WriteReg(0x10574, arg2);  /* C2PMSG_37 */
    WriteReg(0x1056C, cmd);   /* C2PMSG_35 = GFX_CMD_ID */
    for (int i = 0; i < 100; i++) {
        Sleep(10);
        c35 = ReadReg(0x1056C);
        if (c35 == 0) return 0;  /* consumed */
    }
    return -1; /* timeout */
}

/* SMU Queue 2 protocol (feature enable/disable) */
static int SmuQueue2Msg(ULONG msg, ULONG arg) {
    ULONG rsp;
    /* Queue 2: cmd=0x3B10528, rsp=0x3B10564, arg=0x3B10998 */
    for (int i = 0; i < 100; i++) {
        rsp = SmnRead(0x03B10564);  /* read RSP/control */
        if (rsp == 1) break;        /* ready */
        Sleep(10);
    }
    if (rsp != 1) { printf("  Q2 not ready (rsp=0x%08X)\n", rsp); return -1; }
    SmnWrite(0x03B10564, 0);       /* ack RSP */
    SmnWrite(0x03B10998, arg);     /* write ARG */
    SmnWrite(0x03B10528, msg);     /* write CMD */
    for (int i = 0; i < 200; i++) {
        Sleep(10);
        rsp = SmnRead(0x03B10564);
        if (rsp != 0) break;
    }
    ULONG resp = SmnRead(0x03B10998);  /* read response from ARG reg */
    printf("  Q2 msg=0x%X arg=0x%X -> rsp=0x%X resp=0x%X\n", msg, arg, rsp, resp);
    return (rsp == 1) ? 0 : -1;
}

int main(void) {
    if (OpenGpu() != 0) { printf("Cannot open GPU\n"); return 1; }
    if (InitHw() != 0) { printf("InitHw FAILED err=%lu\n", GetLastError()); return 1; }
    printf("=== Triple Try: REG_ACCESS + Queue 2 + RLC Init ===\n\n");

    /* ===== 1. REG_ACCESS (GFX_CMD_ID 0x0D) ===== */
    printf("--- 1. REG_ACCESS (0x0D) ---\n");
    /* Try reading SOC15 register via PSP */
    ULONG saved = ReadReg(0x32D4);  /* SCRATCH before */
    printf("Before: SCRATCH=0x%08X\n", saved);
    /* Read SCRATCH via PSP: arg1=offset, arg2=0 (read) */
    for (ULONG off = 0; off <= 0x80000; off += 0x1000) {
        if (PspMailboxCmd(0x0D, off, 0) == 0) {
            ULONG resp36 = ReadReg(0x10570);
            ULONG resp37 = ReadReg(0x10574);
            if (resp36 != 0 || resp37 != 0)
                printf("  REG_ACCESS[0x%05X]: C36=0x%08X C37=0x%08X\n", off, resp36, resp37);
        }
    }
    printf("After:  SCRATCH=0x%08X\n", ReadReg(0x32D4));

    /* ===== 2. Queue 2 SMU Messages ===== */
    printf("\n--- 2. Queue 2 SMU ---\n");
    /* Try Q2 test message (msg 0x01) */
    SmuQueue2Msg(0x01, 0x1234);
    /* Try Q2 GetConstant (msg 0x03) */
    SmuQueue2Msg(0x03, 0);
    /* Try Q2 EnableSmuFeatures (msg 0x05) - disable GFXOFF/CG/PG */
    ULONG featuresBefore = SmnRead(0x03B10024);  /* SMU FW_FLAGS */
    printf("SMU FW_FLAGS before=0x%08X\n", featuresBefore);
    /* Q2 0x06 = DisableSmuFeatures, arg=feature bits */
    SmuQueue2Msg(0x06, 0x14);  /* disable bit 2 (GFXOFF) + bit 4 (PG) = 0x14 */

    /* ===== 3. RLC Init Sequence ===== */
    printf("\n--- 3. RLC Init (Linux rlc_resume) ---\n");
    /* Need register offsets for BC-250.
     * Linux mmRLC_CNTL is at GC_BASE=0x1260 + offset.
     * Let's probe common RLC registers: */
    ULONG rlcRegs[] = {0x3CA0, 0x3CA4, 0x3CA8, 0x3CAC, 0x3D00, 0x3D04, 0x3D10, 0x3D14, 0x3D20, 0x3D60};
    printf("RLC register scan:\n");
    for (int i = 0; i < sizeof(rlcRegs)/sizeof(rlcRegs[0]); i++) {
        ULONG v = ReadReg(rlcRegs[i]);
        printf("  0x%05X = 0x%08X\n", rlcRegs[i], v);
    }

    /* Try RLC_STOP: find RLC_CNTL */
    /* Linux: mmRLC_CNTL * 4 + GC_BASE */
    /* Let's try some common RLC CNTL offsets */
    ULONG rlcCntlCandidates[] = {0x3CA4, 0x3D00, 0x3D10, 0x3CAC, 0x3D50, 0x3D54, 0x4A00, 0x4A04, 0x4A10};
    printf("\nRLC_CNTL candidates:\n");
    for (int i = 0; i < sizeof(rlcCntlCandidates)/sizeof(rlcCntlCandidates[0]); i++) {
        ULONG v = ReadReg(rlcCntlCandidates[i]);
        printf("  0x%05X = 0x%08X\n", rlcCntlCandidates[i], v);
    }

    /* Try to find RLC_CSIB_ADDR registers */
    printf("\nRLC_CSIB candidates (0x3D60 range):\n");
    for (ULONG off = 0x3D60; off <= 0x3D80; off += 4) {
        ULONG v = ReadReg(off);
        if (v != 0 && v != 0xFFFFFFFF) printf("  0x%05X = 0x%08X\n", off, v);
    }

    /* After all tests, check GRBM_STATUS */
    printf("\nGRBM_STATUS = 0x%08X\n", ReadReg(0x3260));
    printf("GRBM_GFX_INDEX = 0x%08X\n", ReadReg(0x34D0));

    CloseHandle(g_h);
    return 0;
}
