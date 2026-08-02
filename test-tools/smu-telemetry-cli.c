/* smu-telemetry-cli.c — Live SMU telemetry via the driver's real telemetry
 * IOCTL (IOCTL_AMDBC250_GET_SMU_TELEMETRY, CTL_CODE 0x77). Unlike the legacy
 * GET_POWER_TELEMETRY stub, this returns data queried live from the SMU
 * mailbox (freq, VID/mV, enabled features, active WGPs) plus raw SMN sensor
 * probes — all in one kernel-side round trip.
 *
 * Usage:
 *   smu-telemetry-cli [--once] [--csv file.csv] [--delay N]
 *     --once    print a single snapshot and exit (default is a live loop)
 *     --csv X   also append each sample to X (CSV)
 *     --delay N loop delay in ms (default 1000)
 *
 * IOCTLs: INIT_HW 0x80000B80, GET_SMU_TELEMETRY 0x80000B9C (CTL_CODE 0x77).
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE g_h;

static BOOL init_driver(void) {
    g_h = CreateFileA("\\\\.\\AMDBC250DreamV43",
                      GENERIC_READ | GENERIC_WRITE, 0, NULL,
                      OPEN_EXISTING, 0, NULL);
    if (g_h == INVALID_HANDLE_VALUE) {
        printf("FAIL: cannot open GPU driver (err=%lu)\n", GetLastError());
        return FALSE;
    }
    /* INIT_HARDWARE with NBIO_MAP flag — required on Win11 26100 WDM
     * fallback to map BAR5 before any register/SMU access. */
    AMDBC250_IOCTL_INIT_HARDWARE ih;
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL;
    ih.MmioSize         = 0x80000;
    ih.Flags            = AMDBC250_INIT_FLAG_NBIO_MAP;
    DWORD br = 0;
    if (!DeviceIoControl(g_h, IOCTL_AMDBC250_INIT_HARDWARE,
                         &ih, sizeof(ih), &ih, sizeof(ih), &br, NULL)) {
        printf("FAIL: INIT_HARDWARE (err=%lu)\n", GetLastError());
        CloseHandle(g_h);
        g_h = INVALID_HANDLE_VALUE;
        return FALSE;
    }
    return TRUE;
}

static int fetch_telemetry(AMDBC250_IOCTL_SMU_TELEMETRY *t) {
    ZeroMemory(t, sizeof(*t));
    DWORD br = 0;
    if (DeviceIoControl(g_h, IOCTL_AMDBC250_GET_SMU_TELEMETRY,
                        NULL, 0, t, sizeof(*t), &br, NULL)) {
        return br >= sizeof(*t) ? 1 : 0;
    }
    return 0;
}

static const char *feat_name(int bit) {
    switch (bit) {
        case 0: return "GFXCLK_DPM"; case 1: return "SOCLK_DPM"; case 2: return "GFXOFF";
        case 3: return "CG"; case 4: return "PG"; case 5: return "DS_GFXCLK";
        case 6: return "DS_SOCLK"; case 7: return "DS_LCLK"; case 8: return "DS_FCLK";
        case 9: return "SOCCLK_DPM"; case 10: return "LCLK_DPM"; case 11: return "FCLK_DPM";
        case 16: return "FSM"; default: return NULL;
    }
}

static void print_snapshot(const AMDBC250_IOCTL_SMU_TELEMETRY *t) {
    printf("SMU v%u.%u.%u  driver_if=%u  (msg_status=%s)\n",
           (t->SmuVersion >> 16) & 0xFF, (t->SmuVersion >> 8) & 0xFF, t->SmuVersion & 0xFF,
           t->DriverIfVersion, t->MsgStatus == 1 ? "OK" : "TIMEOUT");
    printf("  GfxFreq    : %u MHz", t->GfxFreqMhz);
    if (t->QueryGfxclkMhz && t->QueryGfxclkMhz != t->GfxFreqMhz)
        printf("  (QueryGfxclk=%u MHz)", t->QueryGfxclkMhz);
    printf("\n");
    if (t->GfxVid <= 255)
        printf("  GfxVid     : %u (%u mV)\n", t->GfxVid, t->GfxMillivolts);
    else
        printf("  GfxVid     : %u (unsupported)\n", t->GfxVid);
    printf("  ActiveWgps : %u%s\n", t->ActiveWgps, t->ActiveWgps ? "" : "  (GFXOFF / gated)");
    printf("  CpuCore    : mask=0x%08X (%u cores)  CpuV=%u mV  GpuV=%u mV\n",
           t->CpuCoreMask,
           (t->CpuCoreMask & 0xFF) == 0xFF ? 8 : ((t->CpuCoreMask & 0xFF) == 0x77 ? 6 : 0),
           t->CpuVoltageMv, t->GpuVoltageMv);
    printf("  Features   : 0x%08X\n", t->EnabledSmuFeatures);
    for (int b = 0; b < 32; b++) {
        if (t->EnabledSmuFeatures & (1u << b)) {
            const char *n = feat_name(b);
            if (n) printf("               bit %2d (%s)\n", b, n);
        }
    }
    printf("  Sensors    : edge=0x%08X junct=0x%08X mem=0x%08X fanRPM=0x%08X fanPWM=0x%08X\n",
           t->SmnEdgeTemp, t->SmnJunctionTemp, t->SmnMemTemp, t->SmnFanRpm, t->SmnFanPwm);
    printf("  Result     : %u\n", t->Result);
}

int main(int argc, char **argv) {
    int once = 0, delay = 1000;
    const char *csvpath = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--once")) once = 1;
        else if (!strcmp(argv[i], "--csv") && i + 1 < argc) csvpath = argv[++i];
        else if (!strcmp(argv[i], "--delay") && i + 1 < argc) delay = atoi(argv[++i]);
        else {
            printf("Unknown arg: %s\n", argv[i]);
            printf("Usage: smu-telemetry-cli [--once] [--csv file.csv] [--delay N]\n");
            return 1;
        }
    }

    if (!init_driver()) return 1;
    printf("=== BC-250 Real SMU Telemetry (IOCTL 0x77) ===\n\n");

    FILE *csv = NULL;
    if (csvpath) {
        csv = fopen(csvpath, "w");
        if (csv) {
            fprintf(csv, "Time_s,Version,GfxFreqMhz,QueryGfxclk,GfxVid,mV,ActiveWgps,Features,"
                         "CoreMask,CpuV_mV,GpuV_mV,EdgeTemp,JunctionTemp,MemTemp,FanRpm,FanPwm\n");
        } else {
            printf("WARN: cannot open %s, CSV disabled\n", csvpath);
        }
    }

    AMDBC250_IOCTL_SMU_TELEMETRY t;
    int n = 0;
    while (1) {
        if (fetch_telemetry(&t)) {
            print_snapshot(&t);
        } else {
            printf("FAIL: telemetry IOCTL (err=%lu)\n", GetLastError());
            if (once) { if (csv) fclose(csv); CloseHandle(g_h); return 1; }
        }
        if (csv) {
            fprintf(csv, "%d,0x%08X,%u,%u,%u,%u,%u,0x%08X,0x%08X,%u,%u,0x%08X,0x%08X,0x%08X,0x%08X,0x%08X\n",
                    n, t.SmuVersion, t.GfxFreqMhz, t.QueryGfxclkMhz, t.GfxVid, t.GfxMillivolts,
                    t.ActiveWgps, t.EnabledSmuFeatures,
                    t.CpuCoreMask, t.CpuVoltageMv, t.GpuVoltageMv,
                    t.SmnEdgeTemp, t.SmnJunctionTemp, t.SmnMemTemp, t.SmnFanRpm, t.SmnFanPwm);
            fflush(csv);
        }
        n++;
        if (once) break;
        Sleep((DWORD)delay);
        printf("\n");
    }

    if (csv) fclose(csv);
    CloseHandle(g_h);
    printf("=== Done (%d samples) ===\n", n);
    return 0;
}
