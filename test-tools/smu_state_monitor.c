/* smu_state_monitor.c - read-only BC-250 SMU/VRAM state monitor (Variant B pre-flight).
 *
 * Uses ONLY the real driver surface (inc/amdbc250_ioctl.h):
 *   device = \\\\.\\AMDBC250DreamV43
 *   IOCTL_AMDBC250_INIT_HARDWARE (NBIO_MAP) -> maps BAR5 for SMN mailbox
 *   IOCTL_AMDBC250_GET_SMU_TELEMETRY        -> live SMU snapshot
 *   IOCTL_AMDBC250_GET_VRAM_INFO            -> Total/Visible VRAM
 *   IOCTL_AMDBC250_SMU_CPU_MSG              -> Q3 0x36 (cpu mV), Q3 0x43 (per-core MHz)
 *   IOCTL_AMDBC250_CORE_UNLOCK              -> read-only query (does NOT write unless --unlock)
 *
 * Usage:
 *   smu_state_monitor.exe            - read-only snapshot (safe before vulkaninfoSDK)
 *   smu_state_monitor.exe --unlock   - also run safe core-unlock if mask != 0xFF
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "..\inc\amdbc250_ioctl.h"

static int send_cpu_msg(HANDLE h, unsigned q, unsigned msg, unsigned arg,
                        AMDBC250_IOCTL_SMU_CPU_MSG *sm) {
    DWORD br = 0;
    ZeroMemory(sm, sizeof(*sm));
    sm->Queue = q;
    sm->Message = msg;
    sm->Argument = arg;
    if (!DeviceIoControl(h, IOCTL_AMDBC250_SMU_CPU_MSG,
                         sm, sizeof(*sm), sm, sizeof(*sm), &br, NULL))
        return 0;
    return 1;
}

int main(int argc, char **argv) {
    int do_unlock = (argc > 1 && strcmp(argv[1], "--unlock") == 0);
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== BC-250 SMU state monitor (read-only%s) ===\n",
           do_unlock ? " + --unlock" : "");

    HANDLE h = CreateFileA("\\\\.\\AMDBC250DreamV43",
        GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("FAIL: CreateFile gle=%lu (is atikmdag.sys installed+started?)\n",
               GetLastError());
        return 1;
    }

    /* Map BAR5 (NBIO only, no full HW init -> no TDR risk). */
    AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br = 0;
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL;
    ih.MmioSize = 0x80000;
    ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
    if (!DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE,
                         &ih, sizeof(ih), &ih, sizeof(ih), &br, NULL))
        printf("WARN: INIT_HARDWARE(NBIO_MAP) gle=%lu (telemetry may fail)\n",
               GetLastError());

    /* --- VRAM (both code paths: header macro vs legacy literal) --- */
    printf("VRAM codes: macro=0x%08X literal=0x80000804\n",
           (unsigned)IOCTL_AMDBC250_GET_VRAM_INFO);
    {
        AMDBC250_IOCTL_VRAM_INFO vi;
        DWORD gle = 0;
        ZeroMemory(&vi, sizeof(vi));
        if (DeviceIoControl(h, IOCTL_AMDBC250_GET_VRAM_INFO,
                            NULL, 0, &vi, sizeof(vi), &br, NULL)) {
            printf("VRAM[macro]: total=%llu MB visible=%llu MB used=%llu MB segs=%u\n",
                   vi.TotalVramBytes / (1024 * 1024),
                   vi.VisibleVramBytes / (1024 * 1024),
                   vi.UsedVramBytes / (1024 * 1024), vi.SegmentCount);
        } else {
            gle = GetLastError();
            printf("VRAM[macro]: FAILED gle=%lu\n", gle);
        }
        ZeroMemory(&vi, sizeof(vi));
        if (DeviceIoControl(h, 0x80000804,
                            NULL, 0, &vi, sizeof(vi), &br, NULL)) {
            printf("VRAM[literal]: total=%llu MB visible=%llu MB used=%llu MB segs=%u\n",
                   vi.TotalVramBytes / (1024 * 1024),
                   vi.VisibleVramBytes / (1024 * 1024),
                   vi.UsedVramBytes / (1024 * 1024), vi.SegmentCount);
        } else {
            printf("VRAM[literal]: FAILED gle=%lu\n", GetLastError());
        }
    }

    /* --- SMU telemetry snapshot --- */
    AMDBC250_IOCTL_SMU_TELEMETRY t;
    ZeroMemory(&t, sizeof(t));
    if (DeviceIoControl(h, IOCTL_AMDBC250_GET_SMU_TELEMETRY,
                        NULL, 0, &t, sizeof(t), &br, NULL) && t.Result) {
        printf("SMU: ver=0x%08X (%u.%u.%u) drv_if=%u gfx=%uMHz vid=%u (%umV) "
               "WGP=%u feats=0x%08X\n",
               t.SmuVersion, (t.SmuVersion >> 16) & 0xFF,
               (t.SmuVersion >> 8) & 0xFF, t.SmuVersion & 0xFF,
               t.DriverIfVersion, t.GfxFreqMhz, t.GfxVid, t.GfxMillivolts,
               t.ActiveWgps, t.EnabledSmuFeatures);
        printf("CPU: mask=0x%02X (%s) cpu=%umV gpu=%umV\n",
               t.CpuCoreMask,
               t.CpuCoreMask == 0xFF ? "8c/16t UNLOCKED" :
               t.CpuCoreMask == 0x77 ? "6c/12t stock" : "other",
               t.CpuVoltageMv, t.GpuVoltageMv);
        printf("GFXOFF: %s (WGP=%u, feats bit2=%s)\n",
               t.ActiveWgps == 0 ? "likely ON / deep sleep" : "OFF (powered)",
               t.ActiveWgps, (t.EnabledSmuFeatures & 0x4) ? "set" : "clear");
    } else {
        printf("SMU telemetry: FAILED gle=%lu result=%u status=0x%X\n",
               GetLastError(), t.Result, t.MsgStatus);
    }

    /* --- Per-core freq (Q3 0x43) + CPU voltage (Q3 0x36) --- */
    AMDBC250_IOCTL_SMU_CPU_MSG sm;
    if (send_cpu_msg(h, 3, 0x36, 0, &sm) && sm.Result)
        printf("Q3 0x36 cpu voltage: %u mV (status=%u)\n", sm.Response, sm.ResponseStatus);
    printf("Q3 0x43 per-core MHz:");
    for (unsigned c = 0; c < 8; c++) {
        if (send_cpu_msg(h, 3, 0x43, c, &sm) && sm.Result)
            printf(" c%u=%u", c, sm.Response);
        else
            printf(" c%u=?", c);
    }
    printf("\n");

    /* --- Core unlock state (query only unless --unlock) --- */
    AMDBC250_IOCTL_CORE_UNLOCK cu;
    ZeroMemory(&cu, sizeof(cu));
    if (do_unlock) {
        if (DeviceIoControl(h, IOCTL_AMDBC250_CORE_UNLOCK,
                            &cu, sizeof(cu), &cu, sizeof(cu), &br, NULL))
            printf("CORE_UNLOCK: before=0x%02X after=0x%02X result=%u smu=%u\n",
                   cu.CoreMaskBefore, cu.CoreMaskAfter, cu.Result, cu.SmuStatus);
        else
            printf("CORE_UNLOCK: FAILED gle=%lu\n", GetLastError());
    } else {
        printf("CORE_UNLOCK: skipped (pass --unlock to apply; safe-guarded in kernel)\n");
    }

    CloseHandle(h);
    printf("=== done ===\n");
    return 0;
}
