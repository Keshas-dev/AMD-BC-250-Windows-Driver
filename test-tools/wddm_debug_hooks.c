/* wddm_debug_hooks.c - BC-250 submit/SMU debug hooks for Variant B (Mesa RADV Win32).
 *
 * Drop-in: #include this file from Mesa src/amd/vulkan/wddm/wddm_queue.c,
 * wddm_device.c, or from our ICD stub (src/vulkan/bc250_vulkan_icd.c).
 * No Mesa headers required - windows.h + stdio only.
 *
 * Purpose: log exact Win32/SOS failure point BEFORE a crash, so DebugView
 * shows whether we hit SOS-lock, whitelist refusal, or transport failure.
 *
 * Real driver surface (inc/amdbc250_ioctl.h):
 *   device   = "\\\\.\\AMDBC250DreamV43"  (NOT BC250HardwareBridge)
 *   SMU msgs = IOCTL_AMDBC250_SMU_CPU_MSG (0x80000C2C), Q0/Q3 whitelist
 *   submit   = IOCTL_AMDBC250_SUBMIT_COMMANDS
 */
#include <windows.h>
#include <stdio.h>

#ifndef BC250_WDDM_DEBUG_HOOKS_H
#define BC250_WDDM_DEBUG_HOOKS_H

/* NTSTATUS values seen on BC-250 submit path */
#define BC250_STATUS_SUCCESS               0x00000000UL
#define BC250_STATUS_GRAPHICS_GPU_EXCEPTION 0xC01E000EL /* SOS rejected PM4 */
#define BC250_STATUS_INVALID_PARAMETER     0xC000000DL /* bad struct / whitelist refusal */
#define BC250_STATUS_DEVICE_NOT_READY      0xC00000A3L /* rings not init (KIQ_SIZE=0 class) */
#define BC250_STATUS_TIMEOUT               0x00000102UL

static const char *bc250_ntstatus_name(unsigned long st) {
    switch (st) {
    case BC250_STATUS_SUCCESS:                return "STATUS_SUCCESS";
    case BC250_STATUS_GRAPHICS_GPU_EXCEPTION: return "STATUS_GRAPHICS_GPU_EXCEPTION_ON_DEVICE (SOS-lock)";
    case BC250_STATUS_INVALID_PARAMETER:      return "STATUS_INVALID_PARAMETER (whitelist/bad struct)";
    case BC250_STATUS_DEVICE_NOT_READY:       return "STATUS_DEVICE_NOT_READY (ring/HW not init)";
    case BC250_STATUS_TIMEOUT:                return "STATUS_TIMEOUT";
    default:                                  return "UNKNOWN";
    }
}

static const char *bc250_smu_resp_name(unsigned long s) {
    switch (s) {
    case 1:      return "OK";
    case 0xFF:   return "FAIL";
    case 0xFE:   return "UNKNOWN_CMD";
    case 0xFD:   return "REJECTED (whitelist)";
    case 0xFC:   return "BUSY";
    case 0:      return "TIMEOUT";
    default:     return "?";
    }
}

/* Call right after DeviceIoControl(SUBMIT_COMMANDS / SEND_PM4 / EXECUTE_RING_PM4).
 * ok = DeviceIoControl return, gle = GetLastError(), cmd_len = bytes, dw0 = first PM4 dword. */
static void bc250_debug_submit_status(int ok, unsigned long gle,
                                      unsigned long cmd_len, unsigned long dw0) {
    char msg[256];
    if (ok) {
        _snprintf(msg, sizeof(msg),
            "[BC-250 RADV DBG] submit OK, len=%lu bytes, dw0=0x%08lX\n",
            cmd_len, dw0);
    } else if (gle == ERROR_INVALID_PARAMETER) {
        _snprintf(msg, sizeof(msg),
            "[BC-250 DBG] KMD refused submit (gle=87 INVALID_PARAMETER). "
            "len=%lu dw0=0x%08lX = struct/whitelist or SOS-locked ring BASE\n",
            cmd_len, dw0);
    } else {
        _snprintf(msg, sizeof(msg),
            "[BC-250 DBG] submit failed ok=%d gle=%lu, len=%lu dw0=0x%08lX\n",
            ok, gle, cmd_len, dw0);
    }
    msg[sizeof(msg) - 1] = 0;
    OutputDebugStringA(msg);
    fputs(msg, stdout);
    fflush(stdout);
    /* stdout from inside a loader-loaded DLL is not always visible;
     * mirror to a log file so the trace survives either way. */
    {
        FILE *f = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\icd-submit-trace.log", "a");
        if (f) { fputs(msg, f); fclose(f); }
    }
}

/* Call after IOCTL_AMDBC250_SMU_CPU_MSG round-trip.
 * queue/msg/arg = input, resp_status = ResponseStatus, result = Result. */
static void bc250_debug_smu_status(unsigned long queue, unsigned long msg,
                                   unsigned long arg, unsigned long resp_status,
                                   unsigned long result) {
    char msgbuf[256];
    _snprintf(msgbuf, sizeof(msgbuf),
        "[BC-250 SMU] q=%lu msg=0x%02lX arg=0x%lX -> %s (0x%02lX) result=%lu\n",
        queue, msg, arg, bc250_smu_resp_name(resp_status), resp_status, result);
    msgbuf[sizeof(msgbuf) - 1] = 0;
    OutputDebugStringA(msgbuf);
    fputs(msgbuf, stdout);
    fflush(stdout);
}

#endif /* BC250_WDDM_DEBUG_HOOKS_H */
