#include <windows.h>
#include <stdio.h>

#define PSP_DEVICE      L"\\\\.\\AmdBcPsp"
#define PSP_CTL(fn)     CTL_CODE(FILE_DEVICE_UNKNOWN, fn, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PSP_READ_REG   PSP_CTL(0x800)
#define IOCTL_PSP_WRITE_REG  PSP_CTL(0x801)
#define IOCTL_PSP_SMU_WAKE   PSP_CTL(0x821)

static HANDLE gPsp = INVALID_HANDLE_VALUE;

static ULONG ReadReg(ULONG off) {
    DWORD br = 0;
    ULONG in[2] = {off, 0}, out[2] = {0};
    DeviceIoControl(gPsp, IOCTL_PSP_READ_REG, in, 4, out, 8, &br, NULL);
    return out[0];
}

static void WriteReg(ULONG off, ULONG val) {
    DWORD br = 0;
    ULONG in[2] = {off, val};
    DeviceIoControl(gPsp, IOCTL_PSP_WRITE_REG, in, 8, NULL, 0, &br, NULL);
}

typedef struct {
    ULONG Message;
    ULONG Argument;
    ULONG Reserved[2];
} SMU_WAKE_REQ;
typedef struct {
    ULONG Message;
    ULONG Argument;
    ULONG Response;
    ULONG Status;
} SMU_WAKE_RESP;

static BOOL SmuWake(ULONG msg, ULONG arg, PULONG resp, PULONG status) {
    SMU_WAKE_REQ req = { msg, arg, {0, 0} };
    SMU_WAKE_RESP out = {0};
    DWORD br = 0;
    BOOL ok = DeviceIoControl(gPsp, IOCTL_PSP_SMU_WAKE, &req, sizeof(req), &out, sizeof(out), &br, NULL);
    if (resp) *resp = out.Response;
    if (status) *status = out.Status;
    return ok;
}

int main(void) {
    gPsp = CreateFileW(PSP_DEVICE, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (gPsp == INVALID_HANDLE_VALUE) { printf("FAIL: Cannot open PSP driver (err=%lu)\n", GetLastError()); return 1; }
    printf("[+] PSP driver opened\n\n");

    /* Step 1: Read key registers BEFORE INIT_HW (uses PSP BAR0 for PSP regs, GPU BAR5 not mapped yet) */
    printf("=== BEFORE INIT_HW (PSP BAR0) ===\n");
    printf("  PSP_C2PMSG_35 (0x1056C) = 0x%08X\n", ReadReg(0x1056C));
    printf("  PSP_C2PMSG_36 (0x10570) = 0x%08X\n", ReadReg(0x10570));
    printf("  PSP_C2PMSG_64 (0x105E0) = 0x%08X\n", ReadReg(0x105E0));
    printf("  PSP_C2PMSG_81 (0x10614) = 0x%08X\n", ReadReg(0x10614));
    printf("  PSP_C2PMSG_101(0x10660) = 0x%08X\n", ReadReg(0x10660));
    printf("  NBIO_SIG1 (0xC100)      = 0x%08X\n", ReadReg(0xC100));
    printf("  NBIO_SIG2 (0xC180)      = 0x%08X\n", ReadReg(0xC180));

    /* Step 2: INIT_HW to map GPU BAR5 */
    printf("\n=== INIT_HW (map GPU BAR5) ===\n");
    UCHAR reqBuf[32] = {0};
    *(ULONGLONG*)(reqBuf+0) = 0xFE800000ULL;
    *(ULONG*)(reqBuf+8) = 0x80000;
    ULONG outVal = 0; DWORD br = 0;
    BOOL ok = DeviceIoControl(gPsp, PSP_CTL(0x803), reqBuf, 12, &outVal, 4, &br, NULL);
    printf("  INIT_HW: %s VA=0x%08lX\n", ok ? "OK" : "FAIL", outVal);

    /* Step 3: Read GC registers via GPU BAR5 */
    printf("\n=== GC Registers (GPU BAR5) ===\n");
    printf("  GRBM_STATUS   (0x3260) = 0x%08X\n", ReadReg(0x1260 + 0x2000));
    printf("  CC_ARRAY_CFG  (0x9C1C) = 0x%08X\n", ReadReg(0x9C1C));
    printf("  SPI_WGP_MASK  (0x5C3C) = 0x%08X\n", ReadReg(0x5C3C));
    printf("  KIQ_BASE_LO   (0x6C00) = 0x%08X\n", ReadReg(0x6C00));
    printf("  KIQ_SIZE      (0x6C0C) = 0x%08X\n", ReadReg(0x6C0C));
    printf("  GFX_RB_BASE   (0xDA60) = 0x%08X\n", ReadReg(0xDA60));

    /* Step 4: SMU registers via GPU BAR5 */
    printf("\n=== SMU Registers (GPU BAR5) ===\n");
    printf("  C2PMSG_66 (MSG)  (0x16A08) = 0x%08X\n", ReadReg(0x16A08));
    printf("  C2PMSG_82 (PARAM)(0x16A48) = 0x%08X\n", ReadReg(0x16A48));
    printf("  C2PMSG_90 (RESP) (0x16A68) = 0x%08X\n", ReadReg(0x16A68));
    printf("  C2PMSG_83 (0x16A4C)        = 0x%08X\n", ReadReg(0x16A4C));
    printf("  C2PMSG_97 (0x16AAC)        = 0x%08X\n", ReadReg(0x16AAC));

    /* Step 5: Check if PSP regs are same via GPU BAR5 (offset 0x1056C) */
    printf("\n=== PSP Regs via GPU BAR5 (offset 0x1056C) ===\n");
    printf("  (If this differs from step 1, GPU BAR5 mapping != PSP BAR0)\n");
    printf("  Offset 0x1056C (GPU BAR5)  = 0x%08X\n", ReadReg(0x1056C));
    printf("  Offset 0x10614 (GPU BAR5)  = 0x%08X\n", ReadReg(0x10614));
    printf("  Offset 0x10660 (GPU BAR5)  = 0x%08X\n", ReadReg(0x10660));

    /* Step 6: Try SMU wake */
    printf("\n=== SMU Wake Test ===\n");
    ULONG resp, status;
    ok = SmuWake(0x01, 0, &resp, &status);
    printf("  GetSmuInfo: IOCTL=%s Resp=0x%08X Status=%lu\n", ok ? "OK" : "FAIL", resp, status);

    ok = SmuWake(0x4E, 0, &resp, &status);
    printf("  PowerGateGFX: IOCTL=%s Resp=0x%08X Status=%lu\n", ok ? "OK" : "FAIL", resp, status);

    printf("\n=== DONE ===\n");
    CloseHandle(gPsp);
    return 0;
}
