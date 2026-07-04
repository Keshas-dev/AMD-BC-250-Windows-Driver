#include <windows.h>
#include <stdio.h>

#define PSP_DEVICE      L"\\\\.\\AmdBcPsp"
#define PSP_CTL(fn)     CTL_CODE(FILE_DEVICE_UNKNOWN, fn, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PSP_INIT_HW    PSP_CTL(0x803)
#define IOCTL_PSP_READ_REG   PSP_CTL(0x800)
#define IOCTL_PSP_SMU_WAKE   PSP_CTL(0x821)

static HANDLE gPsp = INVALID_HANDLE_VALUE;

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

static ULONG ReadReg(ULONG off) {
    ULONG in[2] = {off, 0}, out[2] = {0}, br = 0;
    DeviceIoControl(gPsp, IOCTL_PSP_READ_REG, in, 4, out, 8, &br, NULL);
    return out[0];
}

int main(void) {
    gPsp = CreateFileW(PSP_DEVICE, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (gPsp == INVALID_HANDLE_VALUE) { printf("FAIL PSP err=%lu\n", GetLastError()); return 1; }
    printf("Step 1: Open PSP OK\n");

    /* Init HW to map GPU BAR5 (needed for SMU register access) */
    printf("Step 2: PSP INIT_HW (GPU BAR5)\n");
    {
        UCHAR req[32] = {0};
        *(ULONGLONG*)(req+0) = 0xFE800000ULL;
        *(ULONG*)(req+8) = 0x80000;
        ULONG out = 0; DWORD br = 0;
        BOOL ok = DeviceIoControl(gPsp, IOCTL_PSP_INIT_HW, req, 12, &out, 4, &br, NULL);
        printf("  INIT_HW: %s VA=0x%08lX\n", ok ? "OK" : "FAIL", out);
        if (!ok) return 1;
    }

    /* Check SMU probe registers */
    printf("\nStep 3: Probe SMU C2PMSG registers\n");
    printf("  C2PMSG_66 (0x16A08) = 0x%08X\n", ReadReg(0x16A08));
    printf("  C2PMSG_82 (0x16A48) = 0x%08X\n", ReadReg(0x16A48));
    printf("  C2PMSG_90 (0x16A68) = 0x%08X\n", ReadReg(0x16A68));

    printf("\nStep 4: SMU_WAKE - GetSmuInfo (msg=0x01 arg=0)\n");
    {
        ULONG resp = 0, status = 0;
        BOOL ok = SmuWake(0x01, 0, &resp, &status);
        printf("  IOCTL ret=%d Resp=0x%08X Status=%lu\n", ok, resp, status);
    }

    /* Try SMU power gate enable (maybe wakes engines) */
    printf("\nStep 5: Probe more SMU registers\n");
    printf("  C2PMSG_66 after = 0x%08X\n", ReadReg(0x16A08));
    printf("  C2PMSG_82 after = 0x%08X\n", ReadReg(0x16A48));
    printf("  C2PMSG_90 after = 0x%08X\n", ReadReg(0x16A68));

    printf("\n=== DONE ===\n");
    CloseHandle(gPsp);
    return 0;
}