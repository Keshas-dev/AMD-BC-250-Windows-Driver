/* smu-cpu-msg-test.c - exercise the whitelisted SMU CPU messages via the kernel
 * IOCTL IOCTL_AMDBC250_SMU_CPU_MSG (0x80000C2C). Port of the bc250_smu_oc Linux
 * message map. The kernel validates message ID + argument against a fixed
 * whitelist; anything else is refused (STATUS_INVALID_PARAMETER).
 *
 * Usage:
 *   smu-cpu-msg-test.exe                - run the safe read-only query set
 *   smu-cpu-msg-test.exe QUERY          - same (queries only, no writes)
 *   smu-cpu-msg-test.exe 0 0x2C <mask>  - Q0 set_core_enable_mask (0x00..0xFF)
 *   smu-cpu-msg-test.exe 0 0x35 <a>     - Q0 set_soft_min_cclk
 *   smu-cpu-msg-test.exe 0 0x36 <a>     - Q0 set_soft_max_cclk
 *   smu-cpu-msg-test.exe 3 0x50 <v>     - Q3 scale_f_vid_curve (signed16, |v|<=0x3FFF)
 *   smu-cpu-msg-test.exe 3 0x8F <MHz>   - Q3 set_max_cpu_boost_clk (3500..5000)
 *   smu-cpu-msg-test.exe 3 0x8B <C>     - Q3 set_cpu_max_temperature (30..100)
 *   smu-cpu-msg-test.exe 3 0x8C <C>     - Q3 set_gpu_max_temperature (30..100)
 *   smu-cpu-msg-test.exe 3 0x9A <0|1>   - Q3 disable_extra_cpu_gpu_voltage
 *   smu-cpu-msg-test.exe 3 0x36         - Q3 get_current_cpu_voltage (returns mV)
 *   smu-cpu-msg-test.exe 3 0x43 <core>  - Q3 get_core_freq (core 0..7)
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "..\inc\amdbc250_ioctl.h"

static const char *resp_name(uint32_t s) {
    switch (s) {
    case 1:         return "OK";
    case 0xFF:      return "FAIL";
    case 0xFE:      return "UNKNOWN_CMD";
    case 0xFD:      return "REJECTED";
    case 0xFC:      return "BUSY";
    case 0:         return "TIMEOUT";
    default:        return "?";
    }
}

static int send_one(HANDLE h, uint32_t queue, uint32_t msg, uint32_t arg,
                    AMDBC250_IOCTL_SMU_CPU_MSG *sm) {
    DWORD br = 0;
    ZeroMemory(sm, sizeof(*sm));
    sm->Queue = queue;
    sm->Message = msg;
    sm->Argument = arg;
    if (!DeviceIoControl(h, IOCTL_AMDBC250_SMU_CPU_MSG,
                         sm, sizeof(*sm), sm, sizeof(*sm), &br, NULL)) {
        printf("  FAIL: SMU_CPU_MSG q=%u msg=0x%X arg=0x%X (gle=%lu%s)\n",
               queue, msg, arg, GetLastError(),
               (GetLastError() == ERROR_INVALID_PARAMETER) ?
                   " = refused by whitelist / bad arg" : "");
        return 0;
    }
    return 1;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== BC-250 SMU CPU message (whitelist) via IOCTL 0x80000C2C ===\n\n");

    HANDLE h = CreateFileA("\\\\.\\AMDBC250DreamV43",
        GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("FAIL: CreateFile gle=%lu\n", GetLastError());
        return 1;
    }

    /* Map BAR5 so MmioVirtualBase is available (SMN mailbox needs it). */
    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br = 0;
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL;
    ih.MmioSize = 0x80000;
    ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
    DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &br, NULL);

    AMDBC250_IOCTL_SMU_CPU_MSG sm;

    /* --- Arbitrary message send mode: smu-cpu-msg-test.exe <q> <msg> [arg] --- */
    if (argc >= 3) {
        uint32_t q = (uint32_t)strtoul(argv[1], NULL, 0);
        uint32_t m = (uint32_t)strtoul(argv[2], NULL, 0);
        uint32_t a = (argc >= 4) ? (uint32_t)strtoul(argv[3], NULL, 0) : 0;
        printf("--- send q=%u msg=0x%X arg=0x%X ---\n", q, m, a);
        if (!send_one(h, q, m, a, &sm)) { CloseHandle(h); return 1; }
        printf("  Response      : 0x%08X\n", sm.Response);
        printf("  ResponseStatus: 0x%02X (%s)\n", sm.ResponseStatus, resp_name(sm.ResponseStatus));
        printf("  Result        : %u\n", sm.Result);
        CloseHandle(h);
        return 0;
    }

    printf("--- QUERY set (read-only, no writes) ---\n");

    /* Q3: get_current_cpu_voltage (0x36, arg=0) -> mV */
    if (send_one(h, 3, AMDBC250_SMU_Q3_GET_CURRENT_CPU_VOLT, 0, &sm))
        printf("  Q3 0x36 CPU voltage : %u mV  (st=0x%02X %s, res=%u)\n",
               sm.Response, sm.ResponseStatus, resp_name(sm.ResponseStatus), sm.Result);

    /* Q3: get_core_freq (0x43) for cores 0..7 -> MHz each */
    for (uint32_t core = 0; core < 8; core++) {
        if (send_one(h, 3, AMDBC250_SMU_Q3_GET_CORE_FREQ, core, &sm))
            printf("  Q3 0x43 core[%u] freq : %u MHz  (st=0x%02X %s, res=%u)\n",
                   core, sm.Response, sm.ResponseStatus,
                   resp_name(sm.ResponseStatus), sm.Result);
    }

    /* Deliberate negative test: Q3 0x8F with out-of-range arg must be refused. */
    {
        AMDBC250_IOCTL_SMU_CPU_MSG bad;
        ZeroMemory(&bad, sizeof(bad));
        bad.Queue = 3; bad.Message = AMDBC250_SMU_Q3_SET_MAX_CPU_BOOST_CLK;
        bad.Argument = 3000; /* below 3500 -> must be refused */
        if (!DeviceIoControl(h, IOCTL_AMDBC250_SMU_CPU_MSG,
                             &bad, sizeof(bad), &bad, sizeof(bad), &br, NULL)) {
            printf("  [expected] Q3 0x8F arg=3000 REFUSED by whitelist (gle=%lu)\n",
                   GetLastError());
        } else {
            printf("  [WARNING] Q3 0x8F arg=3000 was NOT refused!\n");
        }
    }

    /* Deliberate negative test: Q0 msg 0x98 (ungated SMN write) not whitelisted. */
    {
        AMDBC250_IOCTL_SMU_CPU_MSG bad;
        ZeroMemory(&bad, sizeof(bad));
        bad.Queue = 0; bad.Message = 0x98; bad.Argument = 0x0115A870;
        if (!DeviceIoControl(h, IOCTL_AMDBC250_SMU_CPU_MSG,
                             &bad, sizeof(bad), &bad, sizeof(bad), &br, NULL)) {
            printf("  [expected] Q0 msg 0x98 REFUSED (not in whitelist)\n");
        } else {
            printf("  [WARNING] Q0 msg 0x98 was NOT refused!\n");
        }
    }

    CloseHandle(h);
    return 0;
}