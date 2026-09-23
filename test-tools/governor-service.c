/* governor-service.c — BC-250 GPU governor as a long-running console service.
 *
 * Applies the proven cyan-skillfish-governor change_freq sequence (Q3 temp ->
 * Q0 unforce -> Q3 profile -> Q0 force_vid -> Q0 force_freq) and holds the
 * target by re-applying every INTERVAL_MS. Safe-point table for clamping.
 *
 * Usage:
 *   governor-service.exe                     # 1500 MHz / 900 mV / profile 3
 *   governor-service.exe 1600 950 3 5000     # freq mv profile interval_ms
 *   governor-service.exe unforce             # one-shot restore (unforce freq+vid)
 *   governor-service.exe status              # one-shot readback
 *
 * Console tool — start minimized / via Task Scheduler for "service" use.
 * Ctrl+C forces unforce on exit (safe).
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "..\inc\amdbc250_ioctl.h"

static HANDLE h = INVALID_HANDLE_VALUE;
static volatile BOOL g_Stop = FALSE;

static BOOL W32(uint32_t o, uint32_t v)
{
    AMDBC250_IOCTL_REG_ACCESS r;
    DWORD b;
    r.RegisterOffset = o;
    r.Value = v;
    return DeviceIoControl(h, IOCTL_AMDBC250_WRITE_REG, &r, sizeof(r), &r, sizeof(r), &b, NULL);
}
static uint32_t R32(uint32_t o)
{
    AMDBC250_IOCTL_REG_ACCESS r;
    DWORD b;
    r.RegisterOffset = o;
    r.Value = 0;
    if (DeviceIoControl(h, IOCTL_AMDBC250_READ_REG, &r, sizeof(r), &r, sizeof(r), &b, NULL))
        return r.Value;
    return 0xFFFFFFFF;
}
static void smnW(uint32_t a, uint32_t v) { W32(0x38, a); W32(0x3C, v); }
static uint32_t smnR(uint32_t a) { W32(0x38, a); R32(0x38); return R32(0x3C); }

/* Q0: cmd=0x03B10A08 rsp=0x03B10A68 arg=0x03B10A48
 * Q3: cmd=0x03B10A20 rsp=0x03B10A80 arg=0x03B10A88 */
static int q0(uint32_t msg, uint32_t arg)
{
    smnW(0x03B10A68, 0);
    smnW(0x03B10A48, arg);
    smnW(0x03B10A08, msg);
    for (int i = 0; i < 500; i++) {
        uint32_t st = smnR(0x03B10A68);
        if (st == 1) return 1;
        if (st == 0xFF) return -1;
        if (st == 0xFE) return -2;
        if (st == 0xFD) return -3;
        if (st == 0xFC) return -4;
        Sleep(1);
    }
    return -100;
}
static uint32_t q0_arg(void) { return smnR(0x03B10A48); }

static int q3(uint32_t msg, uint32_t arg)
{
    smnW(0x03B10A80, 0);
    smnW(0x03B10A88, arg);
    smnW(0x03B10A20, msg);
    for (int i = 0; i < 500; i++) {
        uint32_t st = smnR(0x03B10A80);
        if (st == 1) return 1;
        if (st == 0xFF) return -1;
        if (st == 0xFE) return -2;
        if (st == 0xFD) return -3;
        if (st == 0xFC) return -4;
        Sleep(1);
    }
    return -100;
}

static int mv_to_vid(int mv)
{
    return (int)((1.55 - (double)mv / 1000.0) / 0.00625 + 0.5);
}

/* Governor safe points (cyan-skillfish-governor default-config.toml subset). */
static const struct { int freq; int mv; int profile; } SAFE_POINTS[] = {
    {  500,  700, 1 }, { 1000,  800, 1 }, { 1175,  850, 3 },
    { 1500,  900, 3 }, { 1600,  910, 3 }, { 1850,  930, 3 },
    { 2000, 1000, 3 }, { 2230, 1085, 3 },
};
#define N_SAFE (int)(sizeof(SAFE_POINTS) / sizeof(SAFE_POINTS[0]))

static void clamp_to_safe(int *freq, int *mv, int *profile)
{
    /* First safe point with freq >= requested (voltage rises with freq on curve). */
    int best = N_SAFE - 1;
    for (int i = 0; i < N_SAFE; i++) {
        if (SAFE_POINTS[i].freq >= *freq) {
            best = i;
            break;
        }
    }
    *freq = SAFE_POINTS[best].freq;
    *mv = SAFE_POINTS[best].mv;
    *profile = SAFE_POINTS[best].profile;
}

static int governor_change_freq(int freq, int mv, int profile)
{
    int r;
    r = q3(0x8C, 80);
    if (r != 1) printf("  warn: set_gpu_max_temp(80)=%d\n", r);
    r = q0(0x3A, 0); /* unforce freq */
    if (r != 1) printf("  warn: unforce_freq=%d\n", r);
    r = q0(0x3C, 0); /* unforce vid — failures OK */
    (void)r;

    r = q3(0x1E, (uint32_t)profile);
    if (r != 1) { printf("FAIL: set_perf_profile(%d)=%d\n", profile, r); return -1; }
    Sleep(50);

    int vid = mv_to_vid(mv);
    r = q0(0x3B, (uint32_t)vid);
    if (r != 1) { printf("FAIL: force_gfx_vid(%dmV->%d)=%d\n", mv, vid, r); return -1; }
    Sleep(50);

    r = q0(0x39, (uint32_t)freq);
    if (r != 1) {
        printf("FAIL: force_gfx_freq(%d)=%d\n", freq, r);
        return -1;
    }
    Sleep(200);
    return 0;
}

static void governor_unforce(void)
{
    q0(0x3A, 0);
    q0(0x3C, 0);
}

static void print_status(void)
{
    q0(0x37, 0); uint32_t freq = q0_arg();
    q0(0x1E, 0); uint32_t wgp  = q0_arg();
    q0(0x3D, 0); uint32_t feat = q0_arg();
    printf("  freq=%u MHz  activeWgp=%u  features=0x%08X  GFXOFF=%s\n",
           freq, wgp, feat, (feat & 4) ? "ON" : "OFF");
}

static BOOL WINAPI CtrlHandler(DWORD type)
{
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
        g_Stop = TRUE;
        return TRUE;
    }
    return FALSE;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    SetConsoleCtrlHandler(CtrlHandler, TRUE);

    int freq = 1500, mv = 900, profile = 3, interval = 5000;
    BOOL once = FALSE;

    if (argc >= 2 && _stricmp(argv[1], "unforce") == 0) { once = TRUE; }
    else if (argc >= 2 && _stricmp(argv[1], "status") == 0) { once = TRUE; }
    else {
        if (argc >= 2) freq = atoi(argv[1]);
        if (argc >= 3) mv = atoi(argv[2]);
        if (argc >= 4) profile = atoi(argv[3]);
        if (argc >= 5) interval = atoi(argv[4]);
        if (freq < 300 || freq > 2400) { printf("freq %d outside 300-2400\n", freq); return 1; }
        if (mv < 700 || mv > 1200) { printf("mv %d outside 700-1200\n", mv); return 1; }
        if (profile < 0 || profile > 3) profile = 3;
        if (interval < 1000) interval = 1000;
        clamp_to_safe(&freq, &mv, &profile);
    }

    h = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("FAIL: open AMDBC250DreamV43 gle=%lu\n", GetLastError());
        return 1;
    }

    AMDBC250_IOCTL_INIT_HARDWARE ih;
    DWORD br = 0;
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL;
    ih.MmioSize = 0x80000;
    ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
    if (!DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &br, NULL)) {
        printf("FAIL: INIT_HARDWARE gle=%lu\n", GetLastError());
        CloseHandle(h);
        return 1;
    }

    /* SMU alive check */
    int tr = q3(1, 123);
    uint32_t tst = smnR(0x03B10A88);
    if (tr != 1 || tst != 124) {
        printf("FAIL: SMU Q3 test status=%d resp=%u (expect 124)\n", tr, tst);
        CloseHandle(h);
        return 1;
    }

    if (argc >= 2 && _stricmp(argv[1], "unforce") == 0) {
        printf("=== governor unforce ===\n");
        governor_unforce();
        print_status();
        CloseHandle(h);
        return 0;
    }
    if (argc >= 2 && _stricmp(argv[1], "status") == 0) {
        printf("=== governor status ===\n");
        print_status();
        CloseHandle(h);
        return 0;
    }

    printf("=== governor-service: %d MHz @ %d mV profile=%d, re-apply every %d ms ===\n",
           freq, mv, profile, interval);
    printf("Ctrl+C to stop (unforce on exit)\n");

    int iter = 0;
    while (!g_Stop) {
        iter++;
        printf("[%d] change_freq(%d, %d, p=%d): ", iter, freq, mv, profile);
        int r = governor_change_freq(freq, mv, profile);
        if (r == 0) {
            q0(0x37, 0); uint32_t f = q0_arg();
            printf("OK (freq=%u MHz)\n", f);
        } else {
            printf("FAILED (%d)\n", r);
        }

        DWORD waited = 0;
        while (waited < (DWORD)interval && !g_Stop) {
            Sleep(250);
            waited += 250;
        }
    }

    printf("\nStopping: unforce...\n");
    governor_unforce();
    print_status();
    CloseHandle(h);
    return 0;
}
