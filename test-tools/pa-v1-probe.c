/* pa-v1-probe.c — AMD BC-250 CPU-PSP (1022:143E) pa_v1 platform mailbox probe.
 *
 * Linux pspv_bc250 uses platform_access = pa_v1 on BAR0 of 1022:143E
 * (fe700000, 1MB). Offsets (BAR-relative):
 *   0x10570 cmdresp_reg     (C2PMSG_28)  — cmd | (status<<?) | RESP bit31
 *   0x10574 cmdbuff_addr_lo (C2PMSG_29)  — physical address of psp_request
 *   0x10578 cmdbuff_addr_hi (C2PMSG_30)
 *   0x10690 inten           (P2CMSG_INTEN)
 *   0x10694 intsts          (P2CMSG_INTSTS)
 *   0x109ec bootloader_info (C2PMSG_59)  — expect 0x001C0102 (00.1c.01.02)
 *   0x109fc feature_reg     (C2PMSG_63)  — expect 0x00000002 (DBC|HSTI bits)
 *   0x10a24 doorbell_button (C2PMSG_73)
 *   0x10a40 doorbell_cmd    (C2PMSG_80)
 *
 * Phase 1 (this tool): READ-ONLY register probe through \\.\AmdBcPsp
 * after PspDriver routes 0x10000-0x10FFF to Bar0Base (not GPU proxy).
 * Phase 2 (later): send PSP_CMD_NONE via cmdbuff once PA-backed buffer IOCTL exists.
 *
 * Expected smoking-gun: bootloader_info == 0x001C0102 proves BAR0 window live.
 */
#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdint.h>

#define DEV_PATH L"\\\\.\\AmdBcPsp"
#define PSP_READ_REG CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct { ULONG Offset; ULONG Reserved; } RD_REQ;
typedef struct { ULONG Value;  ULONG Status;  } RD_RSP;

static HANDLE h = INVALID_HANDLE_VALUE;

static int ReadBar0(ULONG offset, ULONG* out)
{
    RD_REQ req;
    RD_RSP rsp;
    DWORD br = 0;
    req.Offset = offset;
    req.Reserved = 0;
    rsp.Value = 0;
    rsp.Status = 0xFFFFFFFF;
    if (!DeviceIoControl(h, PSP_READ_REG, &req, sizeof(req), &rsp, sizeof(rsp), &br, NULL))
        return -1;
    *out = rsp.Value;
    return 0;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    h = CreateFileW(DEV_PATH, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("FAIL: cannot open %ls gle=%lu\n", DEV_PATH, GetLastError());
        return 1;
    }
    printf("Opened %ls OK\n\n", DEV_PATH);

    static const struct { ULONG off; const char* name; } regs[] = {
        { 0x10570, "cmdresp (C2PMSG_28)" },
        { 0x10574, "cmdbuff_lo (C2PMSG_29)" },
        { 0x10578, "cmdbuff_hi (C2PMSG_30)" },
        { 0x10690, "inten (P2CMSG_INTEN)" },
        { 0x10694, "intsts (P2CMSG_INTSTS)" },
        { 0x109ec, "bootloader_info (C2PMSG_59)" },
        { 0x109fc, "feature_reg (C2PMSG_63)" },
        { 0x10a24, "doorbell_button (C2PMSG_73)" },
        { 0x10a40, "doorbell_cmd (C2PMSG_80)" },
    };

    printf("=== pa_v1 platform mailbox (CPU-PSP BAR0 0x10000-0x10FFF) ===\n");
    int ok = 0;
    ULONG boot = 0, feat = 0, cmdresp = 0;
    for (size_t i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
        ULONG v = 0;
        if (ReadBar0(regs[i].off, &v) != 0) {
            printf("  0x%04X %-32s IOCTL FAIL gle=%lu\n", regs[i].off, regs[i].name, GetLastError());
            continue;
        }
        printf("  0x%04X %-32s = 0x%08X\n", regs[i].off, regs[i].name, v);
        ok++;
        if (regs[i].off == 0x109ec) boot = v;
        if (regs[i].off == 0x109fc) feat  = v;
        if (regs[i].off == 0x10570) cmdresp = v;
    }

    printf("\n=== Verdict ===\n");
    if (ok == 0) {
        printf("FAIL: no BAR0 registers readable (driver routing or auto-init broken)\n");
        CloseHandle(h);
        return 1;
    }

    if (boot == 0x001C0102)
        printf("PASS: bootloader_info = 0x%08X (00.1c.01.02) — BAR0 window LIVE\n", boot);
    else if (boot == 0 || boot == 0xFFFFFFFF)
        printf("WARN: bootloader_info = 0x%08X (not 00.1c.01.02) — window may be dead/wrong BAR\n", boot);
    else
        printf("INFO: bootloader_info = 0x%08X (nonstandard but non-zero — window responds)\n", boot);

    if (feat == 0x00000002)
        printf("PASS: feature_reg = 0x%08X (DBC|HSTI per Linux pspv_bc250)\n", feat);
    else
        printf("INFO: feature_reg = 0x%08X (Linux expects 0x00000002)\n", feat);

    printf("INFO: cmdresp = 0x%08X (bit31=RESP %s, rest may carry status/cmd)\n",
           cmdresp, (cmdresp & 0x80000000u) ? "SET" : "clear");

    int pass = (boot == 0x001C0102) || (boot != 0 && boot != 0xFFFFFFFF);
    printf("\n%s (%d/%zu regs readable)\n", pass ? "RESULT: PA_V1 PROBE PASS" : "RESULT: PA_V1 PROBE FAIL",
           ok, sizeof(regs) / sizeof(regs[0]));

    CloseHandle(h);
    return pass ? 0 : 1;
}
