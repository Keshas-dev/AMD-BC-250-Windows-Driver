/*
 * bc250-vitals.c - AMD BC-250 read-only system vitals (Faza 1).
 *
 * Reads everything currently reachable through the SMU mailbox. No writes, no
 * MMIO, no state changes - the only IOCTLs used are GET_SMU_TELEMETRY,
 * SMU_CPU_MSG with read-only message IDs, GET_VRAM_INFO and a single
 * allow-listed PCI_SMN read (0x0115A870, core presence mask).
 *
 * Deliberately NOT used:
 *   - IOCTL_AMDBC250_CORE_UNLOCK     (writes SMN 0x0115A870 via Q3 0x98)
 *   - IOCTL_AMDBC250_GET_TEMP_INFO   (returns synthetic stubs, not real temps)
 *   - IOCTL_AMDBC250_SMU_MSG_ARGS    (Q2 whitelist is writes only)
 *   - direct reads of SMU C2PMSG regs (self-referential read wedged the SMU)
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "..\inc\amdbc250_ioctl.h"

#define DEV_PATH "\\\\.\\AMDBC250DreamV43"

/* Only SMN address this tool is ever allowed to touch. */
#define SMN_CORE_MASK 0x0115A870u

/* Q0 read-only message IDs */
#define Q0_GET_SMU_VERSION   0x02u
#define Q0_GET_DRIVER_IF     0x03u
#define Q0_QUERY_CORE_PSTATE 0x0Cu
#define Q0_QUERY_GFXCLK      0x0Fu
#define Q0_QUERY_SOC_CLOCK   0x11u   /* QueryVddcrSocClock = DRAM/SoC clock */
#define Q0_QUERY_DF_PSTATE   0x13u
#define Q0_QUERY_ACTIVE_WGP  0x1Eu
#define Q0_GET_GFX_FREQ      0x37u
#define Q0_GET_GFX_VID       0x38u
#define Q0_GET_ENABLED_FEAT  0x3Du

/* Q3 read-only message IDs */
#define Q3_GET_CPU_VOLTAGE   0x36u
#define Q3_GET_CORE_FREQ     0x43u

static HANDLE               g_h = INVALID_HANDLE_VALUE;
static volatile LONG        g_stop = 0;
static int                  g_csv = 0;
static int                  g_csv_header = 1;

/* ABI guards. The driver checks minimum lengths, and this repo has a history of
 * a too-small INIT_HARDWARE buffer overflowing the stack (0xC0000409).
 * Note VRAM_INFO is 28 bytes of members but 32 with 8-byte alignment. */
typedef char assert_smu_cpu_msg[(sizeof(AMDBC250_IOCTL_SMU_CPU_MSG) == 24) ? 1 : -1];
typedef char assert_init_hw[(sizeof(AMDBC250_IOCTL_INIT_HARDWARE) == 32) ? 1 : -1];
typedef char assert_pci_smn[(sizeof(AMDBC250_IOCTL_PCI_SMN_ACCESS) == 36) ? 1 : -1];
typedef char assert_smu_tel[(sizeof(AMDBC250_IOCTL_SMU_TELEMETRY) == 72) ? 1 : -1];
typedef char assert_vram[(sizeof(AMDBC250_IOCTL_VRAM_INFO) == 32) ? 1 : -1];

static BOOL WINAPI ctrl_handler(DWORD type)
{
    (void)type;
    g_stop = 1;
    return TRUE;
}

static const char *st_name(UINT32 st)
{
    switch (st) {
    case 0x00: return "TIMEOUT";
    case 0x01: return "OK";
    case 0xFC: return "BUSY";
    case 0xFD: return "REJECTED";
    case 0xFE: return "UNKNOWN_CMD";
    case 0xFF: return "FAILED";
    default:   return "?";
    }
}

/* mV = round((-vid * 0.00625 + 1.55) * 1000) */
static UINT32 vid_to_mv(UINT32 vid)
{
    double mv;
    if (vid > 255u) return 0;
    mv = (-((double)vid) * 0.00625 + 1.55) * 1000.0;
    return (UINT32)(mv + 0.5);
}

/* SMU version word 0x00MMmmBB -> "MM.mm.BB" (0x00580600 -> 88.6.0) */
static void smu_ver_str(UINT32 v, char *out, size_t n)
{
    _snprintf_s(out, n, _TRUNCATE, "%u.%u.%u",
                (v >> 16) & 0xFFu, (v >> 8) & 0xFFu, v & 0xFFu);
}

static int smu_msg(UINT32 q, UINT32 m, UINT32 a, UINT32 *resp, UINT32 *st)
{
    AMDBC250_IOCTL_SMU_CPU_MSG sm;
    DWORD br = 0;

    if (st) *st = 0xFD;
    if (g_h == INVALID_HANDLE_VALUE) return 0;

    ZeroMemory(&sm, sizeof(sm));
    sm.Queue    = q;
    sm.Message  = m;
    sm.Argument = a;

    if (!DeviceIoControl(g_h, IOCTL_AMDBC250_SMU_CPU_MSG,
                         &sm, sizeof(sm), &sm, sizeof(sm), &br, NULL))
        return 0;

    if (resp) *resp = sm.Response;
    if (st)   *st   = sm.ResponseStatus;
    return sm.Result ? 1 : 0;
}

/* Read-only, single allow-listed SMN address.
 *
 * CF8/CFC (00:00.0 0xB8/0xBC) is the AUTHORITATIVE transport - it is the same
 * path the -8core status check uses and it is verified on this board. The
 * driver additionally returns a BAR5 NBIO 0x38/0x3C reading for comparison, but
 * the BAR5 aperture does not decode non-GC SMN addresses on this part, so it
 * routinely disagrees. It is therefore advisory, not a cross-check gate. */
static int smn_read(UINT32 addr, UINT32 *val, UINT32 *bar5_out)
{
    AMDBC250_IOCTL_PCI_SMN_ACCESS p;
    DWORD br = 0;

    if (g_h == INVALID_HANDLE_VALUE) return 0;

    ZeroMemory(&p, sizeof(p));
    p.SmnAddress = addr;
    p.IsWrite    = 0;

    if (!DeviceIoControl(g_h, IOCTL_AMDBC250_PCI_SMN_ACCESS,
                         &p, sizeof(p), &p, sizeof(p), &br, NULL))
        return 0;
    if (!p.Result) return 0;
    /* Dead-bus sentinels. A failed fabric read returns one of these while the
     * driver still reports Result=1, so they must be rejected here or a dead
     * read would be indistinguishable from data. */
    if (p.SmnData == 0xFFFFFFFFu || p.SmnData == 0x00000000u) return 0;

    if (bar5_out) *bar5_out = p.Bar5SmnData;
    *val = p.SmnData;
    return 1;
}

static int popcount8(UINT32 v)
{
    int n = 0, i;
    for (i = 0; i < 8; i++) {
        if (v & (1u << i)) n++;
    }
    return n;
}

/* Bounded copy helper - avoids the strcat_s/strcpy_s overload ambiguity. */
static void set_str(char *out, size_t n, const char *s)
{
    size_t l = strlen(s);
    if (n == 0) return;
    if (l >= n) l = n - 1;
    memcpy(out, s, l);
    out[l] = '\0';
}

/* Small rotating static buffers so several num_or_dash() results can appear in
 * one printf argument list. 0 MHz and p-state 0 are both plausible readings, so
 * a failed query must be distinguishable from a real zero. */
#define NUMBUF_SLOTS 16
static char g_numbuf[NUMBUF_SLOTS][12];
static int  g_numbuf_next = 0;

static const char *num_or_dash(UINT32 v)
{
    char *b = g_numbuf[g_numbuf_next];
    g_numbuf_next = (g_numbuf_next + 1) % NUMBUF_SLOTS;
    _snprintf_s(b, 12, _TRUNCATE, "%u", v);
    return b;
}

static void decode_features(UINT32 f, char *out, size_t n)
{
    static const struct { UINT32 bit; const char *name; } tab[] = {
        { 0, "GFXCLK_DPM" },
        { 2, "GFXOFF" },
        { 3, "CLOCK_GATING" },
        { 4, "POWER_GATING" },
        { 6, "GFX_WGP_POWER" },
    };
    size_t i, used = 0;

    if (n == 0) return;
    out[0] = '\0';
    for (i = 0; i < sizeof(tab) / sizeof(tab[0]); i++) {
        size_t nl;
        if (!(f & (1u << tab[i].bit))) continue;
        nl = strlen(tab[i].name);
        /* need: separator + name + NUL terminator must fit */
        if (used + (used ? 1u : 0u) + nl + 1u >= n) break;
        if (used) out[used++] = '|';
        memcpy(out + used, tab[i].name, nl);
        used += nl;
        out[used] = '\0';
    }
    if (!out[0]) set_str(out, n, "(none set)");
}

static void sample(void)
{
    AMDBC250_IOCTL_SMU_TELEMETRY  tel;
    AMDBC250_IOCTL_VRAM_INFO      vram;
    char     ver[32], feat[128];
    UINT32   tel_st = 0, gfx_st = 0xFD, v = 0, coremask = 0;
    UINT32   cpu_mv = 0, gfx_mhz = 0, gfx_vid = 0, gfx_alt = 0;
    UINT32   active_wgp = 0, features = 0, smu_ver = 0, drvif = 0;
    UINT32   core_mhz[8];
    UINT32   core_pstate[8];
    UINT32   soc_mhz = 0, df_pstate = 0, bar5_mask = 0xFFFFFFFFu;
    int      i, cores = 0, have_tel = 0, have_mask = 0;
    int      have_cpu_mv = 0, have_mhz = 0, have_alt = 0, have_vid = 0;
    int      have_wgp = 0, have_feat = 0, have_vram = 0;
    int      have_soc = 0, have_df = 0, pstate_cores = 0;
    SYSTEMTIME lt;

    for (i = 0; i < 8; i++) { core_mhz[i] = 0; core_pstate[i] = 0; }

    /* ---- bulk SMU telemetry ---- */
    ZeroMemory(&tel, sizeof(tel));
    {
        DWORD br = 0;
        if (DeviceIoControl(g_h, IOCTL_AMDBC250_GET_SMU_TELEMETRY,
                            NULL, 0, &tel, sizeof(tel), &br, NULL) &&
            tel.Result) {
            have_tel = 1;
            smu_ver   = tel.SmuVersion;
            drvif     = tel.DriverIfVersion;
            gfx_mhz   = tel.GfxFreqMhz;
            gfx_vid   = tel.GfxVid;
            active_wgp = tel.ActiveWgps;
            features  = tel.EnabledSmuFeatures;
            tel_st    = tel.MsgStatus;
        }
    }

    /* ---- individual Q0 reads (fills gaps, cross-checks the bulk call) ---- */
    if (smu_msg(0, Q0_GET_SMU_VERSION, 0, &v, NULL)) smu_ver = v;
    if (smu_msg(0, Q0_GET_DRIVER_IF,   0, &v, NULL)) drvif   = v;
    /* gfx_st is tracked separately so a GPU-clock timeout never makes the SMU
     * version line report [TIMEOUT] (that would read as "SMU is dead"). */
    if (smu_msg(0, Q0_GET_GFX_FREQ,    0, &v, &gfx_st)) { gfx_mhz = v; have_mhz = 1; }
    if (smu_msg(0, Q0_QUERY_GFXCLK,    0, &v, NULL)) { gfx_alt = v; have_alt = 1; }
    if (smu_msg(0, Q0_GET_GFX_VID,     0, &v, NULL)) { gfx_vid = v; have_vid = 1; }
    if (smu_msg(0, Q0_QUERY_ACTIVE_WGP,0, &v, NULL)) { active_wgp = v; have_wgp = 1; }
    if (smu_msg(0, Q0_GET_ENABLED_FEAT,0, &v, NULL)) { features = v; have_feat = 1; }

    /* ---- Q0 read-only state queries ---- */
    if (smu_msg(0, Q0_QUERY_SOC_CLOCK,  0, &v, NULL)) { soc_mhz   = v; have_soc = 1; }
    if (smu_msg(0, Q0_QUERY_DF_PSTATE,  0, &v, NULL)) { df_pstate = v; have_df  = 1; }
    for (i = 0; i < 8; i++) {
        if (smu_msg(0, Q0_QUERY_CORE_PSTATE, (UINT32)i, &v, NULL)) {
            core_pstate[i] = v;
            pstate_cores++;
        }
    }

    /* ---- Q3 read-only: CPU voltage + per-core clocks ---- */
    if (smu_msg(3, Q3_GET_CPU_VOLTAGE, 0, &v, NULL)) { cpu_mv = v; have_cpu_mv = 1; }
    for (i = 0; i < 8; i++) {
        if (smu_msg(3, Q3_GET_CORE_FREQ, (UINT32)i, &v, NULL)) {
            core_mhz[i] = v;
            cores++;
        }
    }

    /* ---- core presence mask via the one allow-listed SMN read ---- */
    bar5_mask = 0xFFFFFFFFu;
    if (smn_read(SMN_CORE_MASK, &coremask, &bar5_mask)) have_mask = 1;

    /* ---- VRAM totals ---- */
    ZeroMemory(&vram, sizeof(vram));
    {
        DWORD br = 0;
        if (DeviceIoControl(g_h, IOCTL_AMDBC250_GET_VRAM_INFO,
                            &vram, sizeof(vram), &vram, sizeof(vram), &br, NULL))
            have_vram = 1;
    }

    smu_ver_str(smu_ver, ver, sizeof(ver));
    decode_features(features, feat, sizeof(feat));
    GetLocalTime(&lt);

    if (g_csv) {
        char feat_hex[16], mask_hex[16];

        _snprintf_s(feat_hex, sizeof(feat_hex), _TRUNCATE, "0x%08X", features);
        if (have_mask)
            _snprintf_s(mask_hex, sizeof(mask_hex), _TRUNCATE, "0x%02X", coremask);
        else
            strcpy_s(mask_hex, sizeof(mask_hex), "n/a");

        if (g_csv_header) {
            printf("time,smu,drvif,cpu_mv,cores_mhz,cores_pstate,"
                   "gfx_mhz,gfx_alt_mhz,gfx_vid,gfx_mv,active_wgp,"
                   "soc_mhz,df_pstate,features,core_mask,vram_total_mb\n");
            g_csv_header = 0;
        }
        printf("%04u-%02u-%02u %02u:%02u:%02u,%s,%u,%s,",
               lt.wYear, lt.wMonth, lt.wDay, lt.wHour, lt.wMinute, lt.wSecond,
               ver, drvif, have_cpu_mv ? num_or_dash(cpu_mv) : "-");
        for (i = 0; i < 8; i++) {
            if (core_mhz[i])  printf("%s%u",  i ? "/" : "", core_mhz[i]);
            else              printf("%s-",  i ? "/" : "");
        }
        printf(",");
        for (i = 0; i < 8; i++) {
            if (core_pstate[i] != 0xFFFFFFFFu) printf("%s%u", i ? "/" : "", core_pstate[i]);
            else                               printf("%s-", i ? "/" : "");
        }
        printf(",%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n",
               have_mhz ? num_or_dash(gfx_mhz)   : "-",
               have_alt ? num_or_dash(gfx_alt)   : "-",
               have_vid ? num_or_dash(gfx_vid)   : "-",
               have_vid ? num_or_dash(vid_to_mv(gfx_vid)) : "-",
               have_wgp ? num_or_dash(active_wgp) : "-",
               have_soc ? num_or_dash(soc_mhz)   : "-",
               have_df  ? num_or_dash(df_pstate) : "-",
               feat_hex, mask_hex,
               have_vram ? num_or_dash((UINT32)((unsigned long long)vram.TotalVramBytes
                                                / (1024ull * 1024ull))) : "-");
        return;
    }

    printf("========================================================\n");
    printf(" AMD BC-250 Vitals   %04u-%02u-%02u %02u:%02u:%02u\n",
           lt.wYear, lt.wMonth, lt.wDay, lt.wHour, lt.wMinute, lt.wSecond);
    printf("========================================================\n");

    printf("SMU          %s   driver-if %u   [telemetry: %s]\n",
           ver, drvif, tel_st == 0x01 ? "ok" : "timeout/unknown");

    printf("CPU          %s", have_cpu_mv ? "" : "(unavailable)  ");
    if (have_cpu_mv) printf("%u mV", cpu_mv);
    printf("\n");

    if (cores) {
        int printed = 0;
        printf("Cores        ");
        for (i = 0; i < 8; i++) {
            if (!core_mhz[i]) continue;
            printf("%sc%d=%u MHz", printed ? "  " : "", i, core_mhz[i]);
            printed++;
        }
        printf("\n");
    }

    if (have_mask) {
        printf("Core mask    0x%02X = %d core(s)%s   (CF8/CFC)\n", coremask,
               popcount8(coremask), (coremask == 0xFFu) ? "  [unlocked]" : "");
        if (bar5_mask != 0xFFFFFFFFu && bar5_mask != coremask)
            printf("             advisory: BAR5 NBIO transport read 0x%08X (not authoritative)\n",
                   bar5_mask);
    } else {
        printf("Core mask    (unavailable - SMN read failed or returned a dead-bus value)\n");
    }

    printf("GPU          ");
    if (have_mhz || have_alt) {
        printf("%u MHz", have_mhz ? gfx_mhz : gfx_alt);
        if (have_alt && have_mhz && gfx_alt != gfx_mhz)
            printf("  (query %u MHz)", gfx_alt);
    } else {
        printf("(unavailable)");
    }
    if (gfx_st != 0xFD)
        printf("  [%s]", st_name(gfx_st));
    if (have_vid)
        printf("   VID %u = %u mV", gfx_vid, vid_to_mv(gfx_vid));
    printf("\n");

    printf("WGP          %u active%s\n", have_wgp ? active_wgp : 0u,
           have_wgp && active_wgp == 0 ? "   [GFXOFF / deep sleep]" : "");

    /* Q0 0x11 / 0x13 - SoC/DRAM clock and power state. 0x11 is the first
     * readout of the memory clock this board exposes through the mailbox; the
     * alternative (the SMU metrics table) needs a DRAM DMA transfer. */
    printf("SoC/DRAM     ");
    if (have_soc) printf("%u MHz", soc_mhz); else printf("(unavailable)");
    if (have_df)  printf("   DF p-state %u", df_pstate);
    printf("\n");

    if (pstate_cores) {
        int printed = 0;
        printf("Core P-state ");
        for (i = 0; i < 8; i++) {
            if (core_pstate[i] == 0xFFu) continue;
            printf("%sc%d=%u", printed ? "  " : "", i, core_pstate[i]);
            printed++;
        }
        printf("\n");
    }

    if (have_feat) printf("Features     0x%08X  %s\n", features, feat);

    /* VRAM: the driver returns compile-time defaults, not a measurement.
     * TotalVramBytes is set once to 16 GB and only corrected by the FULL init
     * path reading CMOS UMA_SIZE, which NBIO_MAP never reaches. UsedVramBytes
     * is hardcoded 0 and SegmentCount is the literal 2. So only the total is
     * shown, explicitly labelled. Use cmos-memcfg-test.exe for the real UMA. */
    if (have_vram) {
        printf("VRAM         %llu MB reported by driver (NOT the CMOS UMA size; "
               "use cmos-memcfg-test.exe for that)\n",
               (unsigned long long)vram.TotalVramBytes / (1024ull * 1024ull));
    }

    if (have_tel) {
        printf("SMN raw      edge=0x%08X junction=0x%08X mem=0x%08X\n",
               tel.SmnEdgeTemp, tel.SmnJunctionTemp, tel.SmnMemTemp);
        printf("             fan=0x%08X fanpwm=0x%08X  (raw - no decode table for cyan_skillfish)\n",
               tel.SmnFanRpm, tel.SmnFanPwm);
    }

    printf("--------------------------------------------------------\n");
}

int main(int argc, char **argv)
{
    AMDBC250_IOCTL_INIT_HARDWARE ih;
    const char *device = DEV_PATH;
    int  watch = 0, delay = 2, i;
    DWORD br = 0;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--watch")) {
            watch = 1;
        } else if (!strcmp(argv[i], "--csv")) {
            g_csv = 1;
        } else if (!strcmp(argv[i], "--delay") && i + 1 < argc) {
            delay = atoi(argv[++i]);
            if (delay < 1)   delay = 1;
            if (delay > 3600) delay = 3600;   /* also stops delay*1000 overflowing int */
        } else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            printf("bc250-vitals - AMD BC-250 read-only system vitals\n\n"
                   "  bc250-vitals              one snapshot\n"
                   "  bc250-vitals --watch      loop, Ctrl+C to stop\n"
                   "  bc250-vitals --delay N    seconds between samples (default 2)\n"
                   "  bc250-vitals --csv        machine-readable output\n");
            return 0;
        } else if (!strncmp(argv[i], "--device=", 9)) {
            device = argv[i] + 9;
        } else {
            fprintf(stderr, "unknown argument: %s (try --help)\n", argv[i]);
            return 2;
        }
    }

    g_h = CreateFileA(device, GENERIC_READ | GENERIC_WRITE,
                      FILE_SHARE_READ | FILE_SHARE_WRITE,
                      NULL, OPEN_EXISTING, 0, NULL);
    if (g_h == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "cannot open %s (gle=%lu)\n",
                device, (unsigned long)GetLastError());
        return 3;
    }

    /* Map BAR5 only - Flags must be NBIO_MAP, never 0 (full init = TDR risk). */
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ull;
    ih.MmioSize         = 0x80000u;
    ih.Flags            = AMDBC250_INIT_FLAG_NBIO_MAP;

    if (!DeviceIoControl(g_h, IOCTL_AMDBC250_INIT_HARDWARE,
                         &ih, sizeof(ih), &ih, sizeof(ih), &br, NULL)) {
        fprintf(stderr, "INIT_HARDWARE failed (gle=%lu) - is atikmdag running?\n",
                (unsigned long)GetLastError());
        CloseHandle(g_h);
        return 4;
    }

    SetConsoleCtrlHandler(ctrl_handler, TRUE);

    do {
        sample();
        if (watch) {
            int slept = 0;
            fflush(stdout);
            while (!g_stop && slept < delay * 1000) {
                Sleep(100);
                slept += 100;
            }
        }
    } while (watch && !g_stop);

    CloseHandle(g_h);
    return 0;
}