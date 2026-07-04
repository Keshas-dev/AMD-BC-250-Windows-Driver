#include <windows.h>
#include <stdio.h>

#define PSP_DEVICE      L"\\\\.\\AmdBcPsp"
#define PSP_CTL(fn)     CTL_CODE(FILE_DEVICE_UNKNOWN, fn, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PSP_LOAD_TOC  PSP_CTL(0x820)
#define IOCTL_PSP_READ_REG  PSP_CTL(0x800)

static HANDLE gPsp = INVALID_HANDLE_VALUE;

static ULONG ReadReg(ULONG off) {
    DWORD br = 0;
    ULONG in[2] = {off, 0}, out[2] = {0};
    DeviceIoControl(gPsp, IOCTL_PSP_READ_REG, in, 4, out, 8, &br, NULL);
    return out[0];
}

int main(void) {
    gPsp = CreateFileW(PSP_DEVICE, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (gPsp == INVALID_HANDLE_VALUE) { printf("FAIL: Cannot open PSP driver (err=%lu)\n", GetLastError()); return 1; }
    printf("[+] PSP driver opened\n");

    /* Step 1: INIT_HW to map GPU BAR5 */
    printf("\n[1] INIT_HW (map GPU BAR5)\n");
    UCHAR req[32] = {0};
    *(ULONGLONG*)(req+0) = 0xFE800000ULL;
    *(ULONG*)(req+8) = 0x80000;
    ULONG outVal = 0; DWORD br = 0;
    BOOL ok = DeviceIoControl(gPsp, PSP_CTL(0x803), req, 12, &outVal, 4, &br, NULL);
    printf("    %s VA=0x%08lX\n", ok ? "OK" : "FAIL", outVal);
    if (!ok) { CloseHandle(gPsp); return 1; }

    /* Step 2: SMU status BEFORE TOC load */
    printf("\n[2] SMU before TOC:\n");
    printf("    C2PMSG_66 = 0x%08X\n", ReadReg(0x16A08));
    printf("    C2PMSG_82 = 0x%08X\n", ReadReg(0x16A48));
    printf("    C2PMSG_90 = 0x%08X\n", ReadReg(0x16A68));
    printf("    PSP_C2PMSG_81 = 0x%08X\n", ReadReg(0x10614));

    /* Step 3: Send LOAD_TOC */
    printf("\n[3] Sending LOAD_TOC via IOCTL_PSP_LOAD_TOC...\n");
    ULONG resp[8] = {0};
    ok = DeviceIoControl(gPsp, IOCTL_PSP_LOAD_TOC, NULL, 0, resp, sizeof(resp), &br, NULL);
    if (ok) {
        ULONG success = resp[0];
        printf("    Success=%lu C2PMSG_81=0x%08X\n", success, resp[1]);
        printf("    SMU [66]=0x%08X [82]=0x%08X [90]=0x%08X\n", resp[2], resp[3], resp[4]);
    } else {
        printf("    FAILED (err=%lu)\n", GetLastError());
    }

    /* Step 4: SMU status AFTER TOC */
    printf("\n[4] SMU after TOC:\n");
    printf("    C2PMSG_66 = 0x%08X\n", ReadReg(0x16A08));
    printf("    C2PMSG_82 = 0x%08X\n", ReadReg(0x16A48));
    printf("    C2PMSG_90 = 0x%08X\n", ReadReg(0x16A68));
    printf("    PSP_C2PMSG_81 = 0x%08X\n", ReadReg(0x10614));

    /* Step 5: Try SMU wake */
    printf("\n[5] SMU_Wake(GetSmuInfo msg=0x01):\n");
    ULONG in[4] = {0x01, 0, 0, 0}, out[4] = {0};
    ok = DeviceIoControl(gPsp, PSP_CTL(0x821), in, sizeof(in), out, sizeof(out), &br, NULL);
    printf("    IOCTL=%s Resp=0x%08X Status=%lu\n", ok ? "OK" : "FAIL", out[2], out[3]);

    /* After TOC */
    printf("\n[6] Post-TOC SMU:\n");
    printf("    C2PMSG_66 = 0x%08X\n", ReadReg(0x16A08));
    printf("    C2PMSG_82 = 0x%08X\n", ReadReg(0x16A48));
    printf("    C2PMSG_90 = 0x%08X\n", ReadReg(0x16A68));

    printf("\n=== DONE ===\n");
    CloseHandle(gPsp);
    return 0;
}
