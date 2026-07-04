#include <windows.h>
#include <stdio.h>

#define PSP_DEVICE      L"\\\\.\\AmdBcPsp"
#define PSP_CTL(fn)     CTL_CODE(FILE_DEVICE_UNKNOWN, fn, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PSP_INIT_HW    PSP_CTL(0x803)
#define IOCTL_PSP_READ_REG   PSP_CTL(0x800)
#define IOCTL_PSP_WRITE_REG  PSP_CTL(0x801)
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

static void WriteReg(ULONG off, ULONG val) {
    ULONG in[2] = {off, val}, br = 0;
    DeviceIoControl(gPsp, IOCTL_PSP_WRITE_REG, in, 8, NULL, 0, &br, NULL);
}

static void PrintSMUStatus(void) {
    printf("\n=== SMU Status Registers ===\n");
    printf("C2PMSG_66 (MSG)  = 0x%08X\n", ReadReg(0x16A08));
    printf("C2PMSG_82 (PARAM)= 0x%08X\n", ReadReg(0x16A48));
    printf("C2PMSG_90 (RESP) = 0x%08X\n", ReadReg(0x16A68));
    
    printf("\n=== PSP Mailbox Registers ===\n");
    printf("PSP_MAILBOX_00   = 0x%08X\n", ReadReg(0x16000));
    printf("PSP_MAILBOX_01   = 0x%08X\n", ReadReg(0x16004));
    printf("PSP_MAILBOX_02   = 0x%08X\n", ReadReg(0x16008));
    printf("PSP_MAILBOX_03   = 0x%08X\n", ReadReg(0x1600C));
    printf("PSP_MAILBOX_04   = 0x%08X\n", ReadReg(0x16010));
    printf("PSP_MAILBOX_05   = 0x%08X\n", ReadReg(0x16014));
    
    printf("\n=== SMU Firmware Status ===\n");
    printf("MP0_TRC_8 (0x16020) = 0x%08X\n", ReadReg(0x16020));
    printf("MP0_TRC_9 (0x16024) = 0x%08X\n", ReadReg(0x16024));
    printf("MP0_C2PMSG_100      = 0x%08X\n", ReadReg(0x16064));
    printf("MP0_C2PMSG_101      = 0x%08X\n", ReadReg(0x16068));
    
    printf("\n=== PSP SOS Status ===\n");
    printf("MP0_C2PMSG_112      = 0x%08X\n", ReadReg(0x16070));
    printf("MP0_C2PMSG_113      = 0x%08X\n", ReadReg(0x16074));
    printf("PSP_BOOT_TRACEDONE  = 0x%08X\n", ReadReg(0x16038));
}

int main(void) {
    gPsp = CreateFileW(PSP_DEVICE, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (gPsp == INVALID_HANDLE_VALUE) { printf("FAIL: Cannot open PSP driver (err=%lu)\n", GetLastError()); return 1; }
    printf("[+] PSP driver opened\n");

    printf("\n[+] Step 1: INIT_HW (GPU BAR5 mapping)\n");
    UCHAR req[32] = {0};
    *(ULONGLONG*)(req+0) = 0xFE800000ULL;
    *(ULONG*)(req+8) = 0x80000;
    ULONG out = 0; DWORD br = 0;
    BOOL ok = DeviceIoControl(gPsp, IOCTL_PSP_INIT_HW, req, 12, &out, 4, &br, NULL);
    printf("    INIT_HW: %s (VA=0x%08lX)\n", ok ? "OK" : "FAIL", out);
    if (!ok) { printf("    ERROR: Cannot map GPU BAR5\n"); CloseHandle(gPsp); return 1; }

    printf("\n[+] Step 2: Initial SMU status\n");
    PrintSMUStatus();

    printf("\n[+] Step 3: Test SMU_GetSmuInfo (msg=0x01)\n");
    {
        ULONG resp = 0, status = 0;
        ok = SmuWake(0x01, 0, &resp, &status);
        printf("    GetSmuInfo: IOCTL=%s Resp=0x%08X Status=0x%08X\n", ok ? "OK" : "FAIL", resp, status);
    }
    PrintSMUStatus();

    printf("\n[+] Step 4: Test SMU_PowerGateGFX (msg=0x4E)\n");
    {
        ULONG resp = 0, status = 0;
        ok = SmuWake(0x4E, 0, &resp, &status);
        printf("    PowerGateGFX: IOCTL=%s Resp=0x%08X Status=0x%08X\n", ok ? "OK" : "FAIL", resp, status);
    }
    PrintSMUStatus();

    printf("\n[+] Step 5: Test SMU_EnableSmuFeatures (msg=0x4B)\n");
    {
        ULONG resp = 0, status = 0;
        ok = SmuWake(0x4B, 0, &resp, &status);
        printf("    EnableSmuFeatures: IOCTL=%s Resp=0x%08X Status=0x%08X\n", ok ? "OK" : "FAIL", resp, status);
    }
    PrintSMUStatus();

    printf("\n[+] Step 6: Test SMU_SetSoftFrequency (msg=0x4D, arg=0x10000)\n");
    {
        ULONG resp = 0, status = 0;
        ok = SmuWake(0x4D, 0x10000, &resp, &status);
        printf("    SetSoftFrequency: IOCTL=%s Resp=0x%08X Status=0x%08X\n", ok ? "OK" : "FAIL", resp, status);
    }
    PrintSMUStatus();

    printf("\n[+] Step 7: Poll SMU response register (0x16A68) for 5 seconds\n");
    ULONG initial = ReadReg(0x16A68);
    printf("    Initial RESP = 0x%08X\n", initial);
    for (int i = 0; i < 50; i++) {
        Sleep(100);
        ULONG current = ReadReg(0x16A68);
        if (current != initial) {
            printf("    RESP changed at %dms: 0x%08X -> 0x%08X\n", (i+1)*100, initial, current);
            break;
        }
    }

    printf("\n[+] Step 8: Final SMU status\n");
    PrintSMUStatus();

    printf("\n=== TEST COMPLETE ===\n");
    CloseHandle(gPsp);
    return 0;
}