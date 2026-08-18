// smu-governor-test.c
// BC-250 SMU frequency/voltage governor via the KMDOD Escape DDI.
// Implements the PROVEN SAFE governor sequence from cyan-skillfish-governor:
//   1. Q3(0x8C, 80)      - Set GPU max temp 80C
//   2. Q0(0x3A, 0)       - Unforce frequency
//   3. Q0(0x3C, 0)       - Unforce voltage (ignore failure)
//   4. Look up safe point >= target
//   5. Q3(0x1E, profile) - Set perf profile (1=low, 3=high)
//   6. Q0(0x3B, vid)     - Force voltage
//   7. Q0(0x39, freq)    - Force frequency
//
// Usage:
//   smu-governor-test.exe status              - show freq/vid/features/temp
//   smu-governor-test.exe set <freq_mhz>      - set frequency (governor sequence)
//   smu-governor-test.exe unforce             - undo force (unforce freq+vid)
//   smu-governor-test.exe table               - show safe points

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <d3dkmthk.h>

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

#define BC250_ESC_MAGIC 0xBC2501CA
#define BC250_ESC_READ_REG        1
#define BC250_ESC_WRITE_REG       2
#define BC250_ESC_READ_SMN        3
#define BC250_ESC_WRITE_SMN       4
#define BC250_ESC_SMU_QUERY       5
#define BC250_ESC_SMU_QUERY_PARAM 6
#define BC250_ESC_GET_GPU_ID      7
#define BC250_ESC_GET_STATUS      8
#define BC250_ESC_SMU_SEND        9

typedef struct _BC250_ESC_BUFFER {
    ULONG Magic;
    ULONG Command;
    ULONG Arg1;
    ULONG Arg2;
    ULONG Result;
    NTSTATUS Status;
} BC250_ESC_BUFFER;

// Safe points from cyan-skillfish-governor default-config.toml
typedef struct _SAFE_POINT {
    ULONG FreqMhz;
    ULONG Mv;
    ULONG Profile;
} SAFE_POINT;

static SAFE_POINT g_points[] = {
    {  500,  700, 1 },
    {  800,  750, 1 },
    { 1000,  800, 1 },
    { 1175,  850, 3 },
    { 1400,  900, 3 },
    { 1600,  950, 3 },
    { 1800, 1000, 3 },
    { 2000, 1050, 3 },
};
#define NUM_POINTS (sizeof(g_points)/sizeof(g_points[0]))

static D3DKMT_HANDLE g_hAdapter = 0;

// vid = round((1.55 - mv/1000) / 0.00625)
static ULONG mv_to_vid(ULONG mv)
{
    return (ULONG)lround((1.55 - mv / 1000.0) / 0.00625);
}

static int esc_buf(BC250_ESC_BUFFER* buf)
{
    D3DKMT_ESCAPE escape;
    NTSTATUS status;

    memset(&escape, 0, sizeof(escape));
    escape.hAdapter = g_hAdapter;
    escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    escape.pPrivateDriverData = buf;
    escape.PrivateDriverDataSize = sizeof(*buf);

    status = D3DKMTEscape(&escape);
    if (!NT_SUCCESS(status))
    {
        printf("  D3DKMTEscape FAILED: 0x%08X (%lu)\n", status, status);
        return -1;
    }
    if (!NT_SUCCESS(buf->Status))
    {
        printf("  Escape handler error: 0x%08X\n", buf->Status);
        return -1;
    }
    return 0;
}

static int smu_q0(ULONG msg, ULONG param, ULONG* result)
{
    BC250_ESC_BUFFER buf;
    buf.Magic = BC250_ESC_MAGIC;
    buf.Command = BC250_ESC_SMU_QUERY_PARAM;
    buf.Arg1 = msg;
    buf.Arg2 = param;
    if (esc_buf(&buf) != 0)
        return -1;
    if (result) *result = buf.Result;
    return 0;
}

static int smu_q3(ULONG msg, ULONG param, ULONG* result)
{
    BC250_ESC_BUFFER buf;
    buf.Magic = BC250_ESC_MAGIC;
    buf.Command = BC250_ESC_SMU_SEND;
    buf.Arg1 = (3 << 16) | msg;
    buf.Arg2 = param;
    if (esc_buf(&buf) != 0)
        return -1;
    if (result) *result = buf.Result;
    return 0;
}

static int open_adapter(void)
{
    D3DKMT_OPENADAPTERFROMHDC open;
    HDC hdc;
    NTSTATUS status;

    hdc = GetDC(NULL);
    if (hdc == NULL)
    {
        printf("GetDC(NULL) failed\n");
        return -1;
    }

    memset(&open, 0, sizeof(open));
    open.hDc = hdc;

    status = D3DKMTOpenAdapterFromHdc(&open);
    ReleaseDC(NULL, hdc);

    if (!NT_SUCCESS(status))
    {
        printf("D3DKMTOpenAdapterFromHdc FAILED: 0x%08X (%lu)\n", status, status);
        return -1;
    }

    g_hAdapter = open.hAdapter;
    printf("Adapter LUID: %08lx-%08lx  VidPnSourceId: %u\n",
        open.AdapterLuid.HighPart, open.AdapterLuid.LowPart, open.VidPnSourceId);
    return 0;
}

static void show_status(void)
{
    ULONG r;

    printf("== SMU status ==\n");
    if (smu_q0(0x02, 0, &r) == 0)       // GetSmuVersion
        printf("SMU version    : 0x%08X\n", r);
    if (smu_q0(0x37, 0, &r) == 0)       // GetGfxFrequency (MHz direct)
        printf("GFX freq       : %lu MHz\n", r);
    if (smu_q0(0x38, 0, &r) == 0)       // GetGfxVid
        printf("GFX VID        : 0x%lX (VID %lu, ~%.0f mV)\n", r, r, (1.55 - r * 0.00625) * 1000.0);
    if (smu_q0(0x3D, 0, &r) == 0)       // GetEnabledSmuFeatures
        printf("Features       : 0x%08X\n", r);
    if (smu_q0(0x1E, 0, &r) == 0)       // QueryActiveWgp
        printf("Active WGP     : %lu\n", r);
    if (smu_q3(0x40, 0, &r) == 0)       // GetCpuTempMax
        printf("CPU temp max   : %lu C\n", r);
    if (smu_q3(0x37, 0, &r) == 0)       // GetCurrentGpuVoltage
        printf("GPU voltage    : %lu mV\n", r);
    if (smu_q3(0x36, 0, &r) == 0)       // GetCurrentCpuVoltage
        printf("CPU voltage    : %lu mV\n", r);
}

static void show_table(void)
{
    ULONG i;

    printf("== Safe points ==\n");
    printf(" Freq(MHz)   mV   VID  Profile\n");
    for (i = 0; i < NUM_POINTS; i++)
    {
        printf("  %5lu    %4lu  0x%02lX   %lu\n",
            g_points[i].FreqMhz, g_points[i].Mv, mv_to_vid(g_points[i].Mv), g_points[i].Profile);
    }
}

// Find nearest safe point at or above target freq.
static SAFE_POINT* find_safe_point(ULONG target_mhz)
{
    ULONG i;

    for (i = 0; i < NUM_POINTS; i++)
    {
        if (g_points[i].FreqMhz >= target_mhz)
        {
            return &g_points[i];
        }
    }
    return &g_points[NUM_POINTS - 1];
}

static int set_frequency(ULONG target_mhz)
{
    SAFE_POINT* pt;
    ULONG vid, r;

    if (target_mhz < 500)
    {
        printf("Minimum safe frequency is 500 MHz\n");
        return -1;
    }

    pt = find_safe_point(target_mhz);
    vid = mv_to_vid(pt->Mv);

    printf("Target: %lu MHz -> safe point %lu MHz @ %lu mV (VID 0x%02lX, profile %lu)\n",
        target_mhz, pt->FreqMhz, pt->Mv, vid, pt->Profile);

    // 1. Set GPU max temperature to 80C
    printf("  [1/7] Q3 SetGpuMaxTemperature(0x8C, 80) ...\n");
    if (smu_q3(0x8C, 80, &r) != 0) { printf("  FAILED\n"); return -1; }

    // 2. Unforce any previous frequency
    printf("  [2/7] Q0 UnforceGfxFreq(0x3A, 0) ...\n");
    if (smu_q0(0x3A, 0, &r) != 0) { printf("  FAILED\n"); return -1; }

    // 3. Unforce any previous voltage (ignore failure)
    printf("  [3/7] Q0 UnforceGfxVid(0x3C, 0) ...\n");
    (VOID)smu_q0(0x3C, 0, &r);

    // 4. Set perf profile
    printf("  [4/7] Q3 SetPerfProfileIndex(0x1E, %lu) ...\n", pt->Profile);
    if (smu_q3(0x1E, pt->Profile, &r) != 0) { printf("  FAILED\n"); return -1; }

    // 5. Force voltage
    printf("  [5/7] Q0 ForceGfxVid(0x3B, 0x%02lX) ...\n", vid);
    if (smu_q0(0x3B, vid, &r) != 0) { printf("  FAILED\n"); return -1; }

    // 6. Force frequency (SAFE after voltage+profile)
    printf("  [6/7] Q0 ForceGfxFreq(0x39, %lu) ...\n", pt->FreqMhz);
    if (smu_q0(0x39, pt->FreqMhz, &r) != 0) { printf("  FAILED\n"); return -1; }

    // 7. Verify
    printf("  [7/7] Verify ...\n");
    if (smu_q0(0x37, 0, &r) == 0)
        printf("  New GFX freq: %lu MHz\n", r);
    if (smu_q0(0x38, 0, &r) == 0)
        printf("  New GFX VID : 0x%lX\n", r);

    printf("DONE\n");
    return 0;
}

static int unforce(void)
{
    ULONG r;

    printf("== Unforce ==\n");
    printf("  Q0 UnforceGfxFreq(0x3A) ...\n");
    if (smu_q0(0x3A, 0, &r) != 0) { printf("  FAILED\n"); return -1; }
    printf("  Q0 UnforceGfxVid(0x3C) ...\n");
    (VOID)smu_q0(0x3C, 0, &r);

    if (smu_q0(0x37, 0, &r) == 0)
        printf("  GFX freq now: %lu MHz\n", r);
    return 0;
}

int main(int argc, char** argv)
{
    ULONG target;

    printf("BC-250 SMU governor (via KMDOD Escape)\n");

    if (open_adapter() != 0)
    {
        printf("Cannot open display adapter. Is SampleDisplay v1.0.116+ installed?\n");
        return 1;
    }

    if (argc < 2 || strcmp(argv[1], "status") == 0)
    {
        show_status();
        return 0;
    }

    if (strcmp(argv[1], "table") == 0)
    {
        show_table();
        return 0;
    }

    if (strcmp(argv[1], "unforce") == 0)
    {
        return unforce();
    }

    if (strcmp(argv[1], "set") == 0 && argc >= 3)
    {
        target = (ULONG)strtoul(argv[2], NULL, 10);
        return set_frequency(target);
    }

    printf("Usage: %s [status | table | unforce | set <freq_mhz>]\n", argv[0]);
    return 1;
}