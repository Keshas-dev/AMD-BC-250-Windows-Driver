/* smu-unlock-staged.c - staged BC-250 SMU secure-access diagnostics.
 *
 * SCOPE, READ THIS FIRST
 *
 * The kernel side of this work is deliberately thin: the driver exposes a
 * whitelisted 6-argument Q2/Q3 passthrough plus two read-only diagnostics, and
 * nothing else. The destructive stages of the unlock chain (ring overflow ->
 * fake transfer table -> clear the gate byte) are NOT implemented here.
 *
 * That is a deliberate omission, not an oversight. The chain is a
 * memory-corruption exploit whose success depends on reproducing the SMU ring's
 * internal counter layout and subqueue command-type encoding exactly. A
 * transcription error does not fail cleanly - it corrupts SMU ring state, which
 * wedges the SMU and needs an AC power cycle to recover. Writing that sequence
 * from a quick reading would be reckless, so it is left out until it can be
 * transcribed carefully and reviewed.
 *
 * What this tool DOES give you is the part that is safe and the part that is
 * genuinely new: a way to read the SMU's secure-access gate state, and - once
 * that gate is open - a way to read arbitrary SMN addresses through the
 * firmware's own privileged path. That read primitive is what would let a GC
 * or WGP register alias be located deliberately, one known address at a time,
 * rather than by a blind sweep.
 *
 * Every mutated byte lives in volatile SMU SRAM; a normal reboot restores the
 * SMU regardless. Nothing here touches flash, CMOS or the SPI flash.
 *
 * Usage (run as Administrator):
 *   smu-unlock-staged.exe probe [addr]
 *       Read-only. SMU version, the secure-access gate byte, and confirmation
 *       that the Q2 transfer engine can read SMU SRAM at all. Run this first;
 *       everything else depends on it succeeding. With an address it reads four
 *       consecutive dwords from there instead of the gate, which is how the
 *       read path itself gets checked: four zeros are ambiguous between "gate
 *       is open" and "this always returns zero", and only a known non-zero
 *       address tells those apart.
 *
 *   smu-unlock-staged.exe verify
 *       Read-only. Reports the gate byte and probes one benign SMN address
 *       through the secure window (Q3 0x2A). Before any chain runs, that probe
 *       answers 0xFD (rejected-prerequisite). This is how you confirm the
 *       starting state, and later the end state.
 *
 *   smu-unlock-staged.exe smnread <addr>
 *       Read-only, requires the gate to be open (verify says so). Reads one
 *       32-bit SMN address through the firmware's secure window. This is the
 *       new capability and it is read-only by construction: the whitelist has
 *       no entry for the matching SMN *write* message.
 *
 *   smu-unlock-staged.exe secprobe
 *       Read-only. Reads a handful of addresses whose correct values are
 *       already known from other measurements on this board, and reports
 *       match / differ. This is the check that distinguishes a real secure
 *       read from one that merely returns a plausible-looking default.
 *
 *   smu-unlock-staged.exe secprobe <addr> [<addr>...]
 *       Read-only. Reads the given addresses and prints them raw, with no
 *       interpretation, for when the value is not already known.
 *
 *   smu-unlock-staged.exe q2 <msg> <w0> <w1> <w2> <w3>
 *       Raw whitelisted Q2 send, up to four words. The kernel still validates
 *       the message, the argument count, the ring command type and the address
 *       ranges, so an out-of-range call is refused rather than executed.
 *
 *   smu-unlock-staged.exe q3 <msg> <arg>
 *       Raw whitelisted Q3 send, single word.
 *
 *   smu-unlock-staged.exe fstatus
 *       Read-only. Prints the SMU feature mask and the active compute-unit
 *       count. Use this first: it is where the board actually stands.
 *
 *   smu-unlock-staged.exe feature6 status|on|off
 *       Sets or clears SMU feature bit 6, the GPU compute-unit power policy,
 *       via Q2 0x05 / 0x06. Only that one bit is reachable. Feature 6 gates the
 *       RequestActiveWgp message: the SMU answers 0xFF while it is clear, which
 *       is why WGP requests came back rejected before. "off" puts it back.
 *
 *   smu-unlock-staged.exe wgp <0..18>
 *       Sets the active compute-unit count via Q0 0x18. Reversible with any
 *       other value; 0 is the normal idle state. Prints the resulting feature
 *       mask and count either way, so the effect is visible rather than assumed.
 *
 *   smu-unlock-staged.exe bankprobe
 *       Read-mostly. SPI_PG is a per-bank register, so whether our driver can
 *       even reach it depends on the GRBM_GFX_INDEX encoding. This writes 0x00
 *       and 0x07 (0x07 being stock) under each candidate encoding and reads
 *       back, restoring everything afterwards. It never writes 0x1F, so it
 *       cannot enable a WGP or hard-lock the display.
 *
 *   smu-unlock-staged.exe sramdiff [count]
 *       Read-only apart from one RequestActiveWgp. Snapshots a few SRAM
 *       windows, requests a WGP, snapshots again, and prints every dword that
 *       moved. This locates the variable that actually tracks the WGP count by
 *       measurement, instead of guessing which reading of an ambiguous
 *       decompile note is correct. Deliberately avoids the secure-SMN path.
 *       Confirms the whitelist refuses what it should: a non-whitelisted
 *       message, a non-whitelisted queue, and a secure-SMN *write*. The write
 *       refusal is the important one - it is the check that keeps this tool
 *       from becoming an arbitrary SMN write primitive.
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

/* SMU-local SRAM address of the secure-access gate byte. SMU 88.6.0 / our
 * BIOS 3.00, from bc250-smu-unlock\unlock.py. Not an SMN address. The driver
 * owns the same constant as AMDBC250_SMU_DBG_DISABLE; the kernel reports the
 * gate byte for us, so nothing here has to read it directly. */
#define DBG_DISABLE_ADDR   AMDBC250_SMU_DBG_DISABLE

static HANDLE g_h = INVALID_HANDLE_VALUE;

static const char *
st_name(uint32_t s)
{
    switch (s) {
    case 0x01: return "OK";
    case 0xFF: return "FAILED";
    case 0xFE: return "UNKNOWN_CMD";
    case 0xFD: return "REJECTED_PREREQ";
    case 0xFC: return "BUSY";
    case 0x00: return "TIMEOUT";
    default:   return "?";
    }
}

static int
open_dev(void)
{
    DWORD br = 0;
    AMDBC250_IOCTL_INIT_HARDWARE ih;

    g_h = CreateFileA("\\\\.\\AMDBC250DreamV43",
        GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, 0, NULL);
    if (g_h == INVALID_HANDLE_VALUE) {
        printf("FAIL: CreateFile gle=%lu\n", GetLastError());
        return 0;
    }

    /* Map BAR5 so MmioVirtualBase exists; every SMN mailbox path needs it.
     * NBIO_MAP is the safe flag: it maps the BAR and enables memory decoding
     * without running the full hardware init sequence. */
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL;
    ih.MmioSize = 0x80000;
    ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
    if (!DeviceIoControl(g_h, IOCTL_AMDBC250_INIT_HARDWARE,
                         &ih, sizeof(ih), &ih, sizeof(ih), &br, NULL)) {
        printf("FAIL: INIT_HARDWARE gle=%lu\n", GetLastError());
        CloseHandle(g_h);
        g_h = INVALID_HANDLE_VALUE;
        return 0;
    }
    return 1;
}

/* --- whitelisted raw sends ---------------------------------------------- */

static int
q2(uint32_t msg, const uint32_t *w, uint32_t count, const char *what)
{
    AMDBC250_IOCTL_SMU_MSG_ARGS ma;
    DWORD br = 0;
    ZeroMemory(&ma, sizeof(ma));
    ma.Queue = 2;
    ma.Message = msg;
    ma.ArgCount = count;
    for (uint32_t i = 0; i < count && i < AMDBC250_SMU_MAX_ARGS; i++) {
        ma.Arg[i] = w[i];
    }
    if (!DeviceIoControl(g_h, IOCTL_AMDBC250_SMU_MSG_ARGS,
                         &ma, sizeof(ma), &ma, sizeof(ma), &br, NULL)) {
        printf("  %-22s REFUSED gle=%lu%s\n", what, GetLastError(),
               (GetLastError() == ERROR_INVALID_PARAMETER) ? " (whitelist)" : "");
        return 0;
    }
    printf("  %-22s resp=0x%08X st=0x%02X (%s)\n", what, ma.Response,
           ma.ResponseStatus, st_name(ma.ResponseStatus));
    return ma.ResponseStatus == 0x01;
}

static int
q3(uint32_t msg, uint32_t arg, uint32_t *outResp, const char *what)
{
    AMDBC250_IOCTL_SMU_MSG_ARGS ma;
    DWORD br = 0;
    ZeroMemory(&ma, sizeof(ma));
    ma.Queue = 3;
    ma.Message = msg;
    ma.ArgCount = 1;
    ma.Arg[0] = arg;
    if (!DeviceIoControl(g_h, IOCTL_AMDBC250_SMU_MSG_ARGS,
                         &ma, sizeof(ma), &ma, sizeof(ma), &br, NULL)) {
        printf("  %-22s REFUSED gle=%lu%s\n", what, GetLastError(),
               (GetLastError() == ERROR_INVALID_PARAMETER) ? " (whitelist)" : "");
        return 0;
    }
    if (outResp) *outResp = ma.Response;
    printf("  %-22s resp=0x%08X st=0x%02X (%s)\n", what, ma.Response,
           ma.ResponseStatus, st_name(ma.ResponseStatus));
    return 1;
}

/* --- read-only diagnostics ---------------------------------------------- */

static int
do_probe(uint32_t addr)
{
    AMDBC250_IOCTL_SMU_UNLOCK_STEP us;
    DWORD br = 0;

    ZeroMemory(&us, sizeof(us));
    us.Step = AMDBC250_SMU_UNLOCK_STEP_PROBE;
    us.Param[0] = addr;   /* 0 => driver defaults to the gate byte */

    printf("PROBE (read-only)\n");
    if (addr) printf("  reading SRAM at 0x%08X\n", addr);
    if (!DeviceIoControl(g_h, IOCTL_AMDBC250_SMU_UNLOCK_STEP,
                         &us, sizeof(us), &us, sizeof(us), &br, NULL)) {
        printf("  FAIL gle=%lu\n", GetLastError());
        return 0;
    }
    printf("  result        : %u\n", us.Result);
    printf("  SMU version   : 0x%08X   (expect 0x00580600 = 88.6.0)\n", us.Detail[0]);
    printf("  DMA page PA   : 0x%08X\n", us.Detail[1]);
    printf("  prior state   : %u\n", us.Detail[2]);
    printf("  read at       : 0x%08X\n", us.Detail[3]);
    for (uint32_t i = 0; i < AMDBC250_SMU_PROBE_WORDS; i++) {
        printf("  SRAM[0x%08X] = 0x%08X%s\n",
               us.Detail[3] + i * 4, us.Detail[4 + i],
               (i == 0 ? "   <- gate byte (0 = secure access OPEN)" : ""));
    }
    if (us.Result) {
        printf("  -> SMU answered and the Q2 transfer engine read SRAM. OK.\n");
        if (us.Detail[4] == 0 && us.Detail[5] == 0 && us.Detail[6] == 0 &&
            us.Detail[7] == 0) {
            printf("  -> WARNING: all four words are zero. That is consistent with\n"
                   "     an already-open gate, but ALSO with a read path that\n"
                   "     always returns zero. Re-run against a known non-zero\n"
                   "     address before concluding anything.\n");
        } else {
            printf("  -> Non-zero data present, so the read path returns real data.\n");
        }
        return 1;
    }
    printf("  -> transfer engine did not answer. Stop and report this.\n");
    return 0;
}

static int
do_verify(void)
{
    AMDBC250_IOCTL_SMU_UNLOCK_STEP us;
    DWORD br = 0;

    ZeroMemory(&us, sizeof(us));
    us.Step = AMDBC250_SMU_UNLOCK_STEP_VERIFY;

    printf("VERIFY (read-only)\n");
    if (!DeviceIoControl(g_h, IOCTL_AMDBC250_SMU_UNLOCK_STEP,
                         &us, sizeof(us), &us, sizeof(us), &br, NULL)) {
        printf("  FAIL gle=%lu\n", GetLastError());
        return 0;
    }
    printf("  result        : %u\n", us.Result);
    printf("  read at       : 0x%08X\n", us.Detail[3]);
    for (uint32_t i = 0; i < AMDBC250_SMU_PROBE_WORDS; i++) {
        printf("  SRAM[0x%08X] = 0x%08X%s\n",
               us.Detail[3] + i * 4, us.Detail[4 + i],
               (i == 0 ? "   <- gate byte (0 = secure access OPEN)" : ""));
    }
    printf("  SMN probe     : 0x%08X   (SMN 0x%08X, benign)\n",
           us.Detail[6], AMDBC250_SMU_UNLOCK_PROBE_SMN);
    printf("  probe status  : 0x%02X (%s)\n", us.Detail[7], st_name(us.Detail[7]));
    if (us.Detail[7] == 0xFD) {
        printf("  -> STILL GATED (0xFD). Expected until the chain is implemented.\n");
        return 0;
    }
    if (us.Detail[4] == 0) {
        printf("  -> GATE CLEAR. Arbitrary secure SMN read is available.\n");
        return 1;
    }
    printf("  -> gate byte still non-zero.\n");
    return 0;
}

static int
do_smnread(uint32_t addr)
{
    uint32_t resp = 0;
    printf("secure SMN read\n");
    if (!q3(AMDBC250_SMU_Q3_SEC_SMN_READ32, addr, &resp, "Q3 0x2A")) {
        return 0;
    }
    printf("  SMN[0x%08X] = 0x%08X\n", addr, resp);
    return 1;
}

/* --- secprobe: cross-check the secure read against values already known ----
 *
 * A single read proves nothing: an all-ones result looks equally like real data
 * and like a default. What distinguishes them is a read of an address whose
 * correct value is already known independently. The table below comes from
 * earlier verified readings on this board (core-unlock runs and the SMU version
 * query), so agreement is real evidence and disagreement is a hard error.
 *
 * Note on the two call forms of msg 0x2A: the reference uses the same message
 * both as a gate probe and as a read, and because the mailbox always transmits
 * all six argument words, "no argument" and "argument zero" are the same thing
 * on the wire. So the gate probe is just a read of SMN address 0, and there is
 * no separate form to implement.
 */
struct { uint32_t addr; uint32_t expect; const char *why; } g_known[] = {
    { 0x0115A870u, 0x000000FFu, "core presence mask: 0xFF = 8 cores (BIOS unlock on)" },
    { 0x0005A870u, 0x000000FFu, "low alias of the same core mask" },
    { 0x03B10024u, 0x00000001u, "SMU FW_FLAGS: bit0 = interrupts enabled" },
    { 0x03B10B14u, 0x00000000u, "SMU PUB_CTRL: 0 = reset not asserted" },
};

static void
do_secprobe(int argc, char **argv)
{
    printf("SECPROBE - secure SMN read cross-check (read-only)\n\n");

    if (argc >= 3) {
        /* Caller-supplied addresses: print raw, interpret nothing. */
        for (int i = 2; i < argc; i++) {
            uint32_t a = (uint32_t)strtoul(argv[i], NULL, 0);
            uint32_t r = 0;
            printf("SMN[0x%08X]:\n", a);
            q3(AMDBC250_SMU_Q3_SEC_SMN_READ32, a, &r, "  Q3 0x2A");
            printf("  -> 0x%08X\n", r);
        }
        return;
    }

    {
        unsigned agrees = 0, disagrees = 0, nodata = 0;
        printf("known-value cross-check:\n\n");
        for (unsigned i = 0; i < sizeof(g_known) / sizeof(g_known[0]); i++) {
            uint32_t r = 0;
            AMDBC250_IOCTL_SMU_MSG_ARGS ma;
            DWORD br = 0;
            ZeroMemory(&ma, sizeof(ma));
            ma.Queue = 3;
            ma.Message = AMDBC250_SMU_Q3_SEC_SMN_READ32;
            ma.ArgCount = 1;
            ma.Arg[0] = g_known[i].addr;
            if (!DeviceIoControl(g_h, IOCTL_AMDBC250_SMU_MSG_ARGS,
                                 &ma, sizeof(ma), &ma, sizeof(ma), &br, NULL)) {
                printf("  SMN[0x%08X]  refused (gle=%lu)\n",
                       g_known[i].addr, GetLastError());
                nodata++;
                continue;
            }
            r = ma.Response;
            if (ma.ResponseStatus != 0x01) {
                /* A timeout leaves the old argument value in the response
                   register, so this is NOT a reading. Say so explicitly rather
                   than printing the leftover as if it were data. */
                printf("  SMN[0x%08X]  st=0x%02X (%s)  resp=0x%08X"
                       "  [NO READING - response register held the argument]\n",
                       g_known[i].addr, ma.ResponseStatus,
                       st_name(ma.ResponseStatus), r);
                nodata++;
                continue;
            }
            if (r == g_known[i].expect) {
                printf("  SMN[0x%08X]  = 0x%08X  MATCH  (%s)\n",
                       g_known[i].addr, r, g_known[i].why);
                agrees++;
            } else {
                printf("  SMN[0x%08X]  = 0x%08X  differ (expected 0x%08X: %s)\n",
                       g_known[i].addr, r, g_known[i].expect, g_known[i].why);
                disagrees++;
            }
        }
        printf("\n%d match, %d differ, %d gave no reading\n",
               agrees, disagrees, nodata);
        if (agrees > 0 && disagrees == 0) {
            printf("-> The secure read returns REAL data, not a default: the\n"
                   "   values agree with readings taken by other means.\n");
        } else if (agrees == 0) {
            printf("-> Nothing matched. Do NOT treat these as register values.\n");
        }
    }
}

/* --- negative tests: prove the whitelist actually holds ------------------ */

static void
expect_refused(const char *what, AMDBC250_IOCTL_SMU_MSG_ARGS *ma)
{
    DWORD br = 0;
    if (DeviceIoControl(g_h, IOCTL_AMDBC250_SMU_MSG_ARGS,
                        ma, sizeof(*ma), ma, sizeof(*ma), &br, NULL)) {
        printf("  [BAD] %-34s was ACCEPTED - whitelist is too loose\n", what);
    } else {
        printf("  [ok]  %-34s refused (gle=%lu)\n", what, GetLastError());
    }
}

static void
do_selftest(void)
{
    AMDBC250_IOCTL_SMU_MSG_ARGS ma;

    printf("SELFTEST: the whitelist must refuse the following\n\n");

    /* Secure SMN WRITE. If this is ever accepted, the tool has become an
     * arbitrary SMN write primitive and must not be run again. */
    ZeroMemory(&ma, sizeof(ma));
    ma.Queue = 3; ma.Message = AMDBC250_SMU_Q3_SEC_SMN_WRITE32; ma.ArgCount = 1;
    ma.Arg[0] = 0xDEADBEEF;
    expect_refused("Q3 0x2C secure SMN write", &ma);

    /* Secure SMN write-address setup, the other half of a write. */
    ZeroMemory(&ma, sizeof(ma));
    ma.Queue = 3; ma.Message = AMDBC250_SMU_Q3_SEC_SET_SMN_WR_ADDR; ma.ArgCount = 1;
    ma.Arg[0] = 0x0115A870;
    expect_refused("Q3 0x2B set SMN write addr", &ma);

    /* The rpc trigger, which is arbitrary SMU function call. */
    ZeroMemory(&ma, sizeof(ma));
    ma.Queue = 3; ma.Message = AMDBC250_SMU_Q3_RPC_TRIGGER; ma.ArgCount = 1;
    ma.Arg[0] = 0x7F;
    expect_refused("Q3 0x22 rpc trigger (not unlocked)", &ma);

    /* A non-whitelisted message on a whitelisted queue. */
    ZeroMemory(&ma, sizeof(ma));
    ma.Queue = 2; ma.Message = 0x7F; ma.ArgCount = 2;
    expect_refused("Q2 msg 0x7F (not whitelisted)", &ma);

    /* The ungated SMN write, which is a fixed-value primitive. */
    ZeroMemory(&ma, sizeof(ma));
    ma.Queue = 3; ma.Message = AMDBC250_SMU_Q3_UNGATED_SMN_WRITE; ma.ArgCount = 1;
    ma.Arg[0] = 0x0115A870;
    expect_refused("Q3 0x98 ungated SMN write", &ma);

    /* Too many arguments. */
    ZeroMemory(&ma, sizeof(ma));
    ma.Queue = 3; ma.Message = AMDBC250_SMU_Q3_SEC_SMN_READ32;
    ma.ArgCount = AMDBC250_SMU_MAX_ARGS;
    expect_refused("Q3 0x2A with max arg count", &ma);

    printf("\nIf every line says [ok], the read-only claim holds.\n");
}

/* --- Feature 6 / WGP power ----------------------------------------------
 *
 * Q2 0x05 enable_smu_features and 0x06 disable_smu_features both take the mask
 * as ARG0 with ARG1 zero. The kernel admits only bit 6 through these two
 * messages, so the widest thing a caller can do here is flip the GPU
 * compute-unit power policy and nothing else.
 *
 * The status readout is Q0 0x3D GetEnabledSmuFeatures, which the kernel already
 * whitelists, and Q0 0x1E QueryActiveWgp. Both are queries, so the sequence
 * below is safe to run before touching anything: it prints where the board
 * actually stands.
 */
static void
print_status(const char *tag)
{
    AMDBC250_IOCTL_SMU_CPU_MSG sm;
    DWORD br = 0;

    printf("--- status%s\n", tag ? tag : "");
    ZeroMemory(&sm, sizeof(sm));
    sm.Queue = 0; sm.Message = AMDBC250_SMU_Q0_GET_ENABLED_FEATURES;
    if (DeviceIoControl(g_h, IOCTL_AMDBC250_SMU_CPU_MSG,
                        &sm, sizeof(sm), &sm, sizeof(sm), &br, NULL)) {
        printf("    features        = 0x%08X%s\n", sm.Response,
               (sm.Response & AMDBC250_SMU_FEATURE_GFX_WGP_POWER)
                   ? "   <- feature 6 (WGP power) SET" : "   <- feature 6 CLEAR");
    }
    ZeroMemory(&sm, sizeof(sm));
    sm.Queue = 0; sm.Message = AMDBC250_SMU_Q0_QUERY_ACTIVE_WGP;
    if (DeviceIoControl(g_h, IOCTL_AMDBC250_SMU_CPU_MSG,
                        &sm, sizeof(sm), &sm, sizeof(sm), &br, NULL)) {
        printf("    active WGP      = %u   st=0x%02X (%s)\n",
               sm.Response, sm.ResponseStatus, st_name(sm.ResponseStatus));
    }
}

static int
q2_features(uint32_t msg, const char *what)
{
    AMDBC250_IOCTL_SMU_MSG_ARGS ma;
    DWORD br = 0;
    ZeroMemory(&ma, sizeof(ma));
    ma.Queue = 2;
    ma.Message = msg;
    ma.ArgCount = 2;   /* ARG0 = mask, ARG1 = 0 */
    ma.Arg[0] = AMDBC250_SMU_FEATURE_GFX_WGP_POWER;
    ma.Arg[1] = 0;
    if (!DeviceIoControl(g_h, IOCTL_AMDBC250_SMU_MSG_ARGS,
                         &ma, sizeof(ma), &ma, sizeof(ma), &br, NULL)) {
        printf("  %s REFUSED gle=%lu%s\n", what, GetLastError(),
               (GetLastError() == ERROR_INVALID_PARAMETER) ? " (whitelist)" : "");
        return 0;
    }
    printf("  %-28s resp=0x%08X st=0x%02X (%s)\n", what, ma.Response,
           ma.ResponseStatus, st_name(ma.ResponseStatus));
    return ma.ResponseStatus == 0x01;
}

static int
q0_wgp(uint32_t count)
{
    AMDBC250_IOCTL_SMU_CPU_MSG sm;
    DWORD br = 0;
    ZeroMemory(&sm, sizeof(sm));
    sm.Queue = 0; sm.Message = AMDBC250_SMU_Q0_REQUEST_ACTIVE_WGP;
    sm.Argument = count;
    if (!DeviceIoControl(g_h, IOCTL_AMDBC250_SMU_CPU_MSG,
                         &sm, sizeof(sm), &sm, sizeof(sm), &br, NULL)) {
        printf("  RequestActiveWgp(%u) REFUSED gle=%lu%s\n", count, GetLastError(),
               (GetLastError() == ERROR_INVALID_PARAMETER) ? " (whitelist)" : "");
        return 0;
    }
    printf("  RequestActiveWgp(%u)        st=0x%02X (%s) resp=0x%08X\n",
           count, sm.ResponseStatus, st_name(sm.ResponseStatus), sm.Response);
    return 1;
}

static void
do_feature6(const char *what)
{
    print_status(" (before)");
    if (!strcmp(what, "on")) {
        printf("--- enabling feature 6 (GPU compute-unit power policy)\n");
        q2_features(AMDBC250_SMU_Q2_ENABLE_FEATURES, "Q2 0x05 enable bit6");
    } else if (!strcmp(what, "off")) {
        printf("--- disabling feature 6\n");
        q2_features(AMDBC250_SMU_Q2_DISABLE_FEATURES, "Q2 0x06 disable bit6");
    }
    print_status(" (after)");
    if (!strcmp(what, "off")) {
        printf("NOTE: feature 6 gates RequestActiveWgp, so with it clear the\n"
               "      WGP request will be rejected again. That is expected.\n");
    }
}

static void
do_wgp(uint32_t count)
{
    print_status(" (before)");
    printf("--- requesting %u active WGP\n", count);
    q0_wgp(count);
    print_status(" (after)");
}

static void
do_feature_status(void)
{
    print_status("");
}

/* --- SRAM diff: find the variable that actually tracks the WGP count ----
 *
 * The decompile note says the active count lives "at 0x18018[0x7f]", which is
 * ambiguous, and reading there showed float telemetry rather than a count, so
 * the obvious reading of that note is wrong. Rather than guess at an address,
 * observe the effect: snapshot a few regions, request a WGP, snapshot again,
 * and report every dword that moved.
 *
 * A word that changes across the request is a real candidate, and it is found by
 * measurement rather than by interpreting a notation whose meaning is unclear.
 *
 * This is read-only apart from the single RequestActiveWgp message it sends to
 * provoke the change. It deliberately does NOT touch the secure-SMN path, which
 * is the one that can wedge the SMU on this board: reading the SMU's own
 * mailbox block through it is what killed the mailbox earlier.
 */
#define DIFF_WIN_MAX   64u   /* dwords per window */

struct diff_win { const char *name; uint32_t base; uint32_t nwords; };

static const struct diff_win g_windows[] = {
    { "0x18000 (doc: 0x18018 area, metrics+WGP)", 0x18000u, 64u },
    { "0x18800 (Q2 append ring area)",            0x18800u, 64u },
    { "0x07B00 (gate area)",                      0x07B00u, 16u },
};

static int
read_window(uint32_t base, uint32_t nwords, uint32_t *out)
{
    uint32_t got = 0;
    while (got < nwords) {
        AMDBC250_IOCTL_SMU_UNLOCK_STEP us;
        DWORD br = 0;
        uint32_t batch = nwords - got;
        if (batch > AMDBC250_SMU_PROBE_WORDS) batch = AMDBC250_SMU_PROBE_WORDS;
        if (batch < AMDBC250_SMU_PROBE_WORDS) batch = AMDBC250_SMU_PROBE_WORDS;
        ZeroMemory(&us, sizeof(us));
        us.Step = AMDBC250_SMU_UNLOCK_STEP_PROBE;
        us.Param[0] = base + got * 4u;
        if (!DeviceIoControl(g_h, IOCTL_AMDBC250_SMU_UNLOCK_STEP,
                             &us, sizeof(us), &us, sizeof(us), &br, NULL)) {
            printf("    read 0x%08X FAILED gle=%lu\n", base + got * 4u,
                   GetLastError());
            return 0;
        }
        if (us.Result != 1) {
            printf("    read 0x%08X reported failure\n", base + got * 4u);
            return 0;
        }
        for (uint32_t i = 0; i < AMDBC250_SMU_PROBE_WORDS; i++) {
            out[got + i] = us.Detail[4 + i];
        }
        got += AMDBC250_SMU_PROBE_WORDS;
    }
    return 1;
}

static void
do_sramdiff(uint32_t count)
{
    const unsigned nwin = sizeof(g_windows) / sizeof(g_windows[0]);
    static uint32_t before[sizeof(g_windows) / sizeof(g_windows[0])][DIFF_WIN_MAX];
    static uint32_t after[sizeof(g_windows) / sizeof(g_windows[0])][DIFF_WIN_MAX];
    unsigned total = 0;

    printf("SRAM DIFF (read-only except one RequestActiveWgp)\n\n");

    printf("snapshot BEFORE:\n");
    for (unsigned w = 0; w < nwin; w++) {
        if (!read_window(g_windows[w].base, g_windows[w].nwords, before[w])) {
            printf("  aborting: cannot read baseline\n");
            return;
        }
        printf("  ok  %-46s %u words\n", g_windows[w].name, g_windows[w].nwords);
    }

    printf("\nrequesting %u active WGP:\n", count);
    q0_wgp(count);
    Sleep(200);
    print_status(" (after)");

    printf("\nsnapshot AFTER:\n");
    for (unsigned w = 0; w < nwin; w++) {
        if (!read_window(g_windows[w].base, g_windows[w].nwords, after[w])) {
            printf("  aborting: cannot read post-state\n");
            return;
        }
    }
    printf("  ok  all windows\n");

    printf("\ndifferences:\n");
    for (unsigned w = 0; w < nwin; w++) {
        unsigned n = 0;
        for (uint32_t i = 0; i < g_windows[w].nwords; i++) {
            if (before[w][i] != after[w][i]) {
                uint32_t a = g_windows[w].base + i * 4u;
                if (n < 12) {
                    printf("  0x%08X : 0x%08X -> 0x%08X   (byte@+3 = 0x%02X -> 0x%02X)\n",
                           a, before[w][i], after[w][i],
                           (before[w][i] >> 24) & 0xFF, (after[w][i] >> 24) & 0xFF);
                }
                n++;
            }
        }
        if (n == 0) {
            printf("  %-46s no change\n", g_windows[w].name);
        } else {
            printf("  %-46s %u word(s) changed\n", g_windows[w].name, n);
        }
        total += n;
    }
    printf("\n%u word(s) changed in total.\n", total);
    if (total == 0) {
        printf("-> Nothing moved, so the request never reached SMU state that\n"
               "   lives in these windows. Either the SMU recomputes the count\n"
               "   from a different place, or it refused and never stored it.\n");
    } else {
        printf("-> Candidates above are real SMU state that the request touched.\n");
    }
}

/* --- bankprobe: is our per-bank GRBM_GFX_INDEX encoding even right? ------
 *
 * SPI_PG_ENABLE_STATIC_WGP_MASK (0x5C3C) is a per-bank register, so it only
 * reads and writes after GRBM_GFX_INDEX selects the right bank. Our driver has
 * been using an encoding that the working Linux call never produces: Linux
 * passes instance=0xffffffff, which sets INSTANCE_BROADCAST_WRITES (bit 24),
 * while our selects leave that bit clear. If the encoding is wrong, writes land
 * on the wrong instance and every readback comes back 0 - which is exactly the
 * "register is locked" signature we have been treating as a hardware verdict
 * for months.
 *
 * The experiment is deliberately built so it cannot hurt the board. It never
 * writes 0x1F. It writes only 0x00 and 0x07, and 0x07 is the stock value, so
 * the register is left in a state the hardware already ships in. Everything is
 * restored at the end, including GRBM_GFX_INDEX.
 *
 * Reading 0x07 back proves the encoding is right and writes land. Reading 0
 * after a 0x07 write means either the encoding is wrong or the write is
 * genuinely blocked - and the two are told apart by the 0x00 step, which must
 * also read 0, so a bank where nothing ever changes is one where the index
 * never selected anything.
 */
/* GRBM_GFX_INDEX is defined in amdbc250_dream_hw.h, which is kernel-side and
   not part of the shared IOCTL header this tool includes. The offset is
   hardware-verified (GC_BASE + 0x2270), so it is named here rather than
   pulling a kernel header into a user-mode build. */
#define REG_GRBM_GFX_INDEX  0x000034D0u
#define REG_SPI_PG_MASK     0x00005C3Cu

static int
reg_read(uint32_t off, uint32_t *val)
{
    AMDBC250_IOCTL_REG_ACCESS a;
    DWORD br = 0;
    ZeroMemory(&a, sizeof(a));
    a.RegisterOffset = off;
    if (!DeviceIoControl(g_h, IOCTL_AMDBC250_READ_REG,
                         &a, sizeof(a), &a, sizeof(a), &br, NULL)) {
        printf("      read 0x%04X FAILED gle=%lu\n", off, GetLastError());
        return 0;
    }
    *val = a.Value;
    return 1;
}

static int
reg_write(uint32_t off, uint32_t val)
{
    AMDBC250_IOCTL_REG_ACCESS a;
    DWORD br = 0;
    ZeroMemory(&a, sizeof(a));
    a.RegisterOffset = off;
    a.Value = val;
    if (!DeviceIoControl(g_h, IOCTL_AMDBC250_WRITE_REG,
                         &a, sizeof(a), &a, sizeof(a), &br, NULL)) {
        printf("      write 0x%04X FAILED gle=%lu\n", off, GetLastError());
        return 0;
    }
    return 1;
}

struct bank_cand { uint32_t index; const char *label; };

static void
do_bankprobe(void)
{
    /* Both encodings, so the result says which one is right rather than only
       whether the one we currently use happens to work. */
    static const struct bank_cand cands[] = {
        { 0x00000000u, "SE0/SH0  instance=0     (what our driver uses)" },
        { 0x01000000u, "SE0/SH0  instance=0xFF  (what Linux uses)"     },
        { 0x00000100u, "SE0/SH1  instance=0     (ours)"                },
        { 0x01000100u, "SE0/SH1  instance=0xFF  (Linux)"                },
        { 0x00010000u, "SE1/SH0  instance=0     (ours)"                },
        { 0x01010000u, "SE1/SH0  instance=0xFF  (Linux)"                },
        { 0x00010100u, "SE1/SH1  instance=0     (ours)"                },
        { 0x01010100u, "SE1/SH1  instance=0xFF  (Linux)"                },
        { 0x15000000u, "broadcast 0x15000000   (gfx10 all-SA)"         },
    };
    const unsigned n = sizeof(cands) / sizeof(cands[0]);

    printf("BANKPROBE - does our GRBM_GFX_INDEX encoding select a real bank?\n");
    printf("Only 0x00 and 0x07 are ever written; 0x07 is the stock value.\n");
    printf("0x1F is NEVER written here.\n\n");
    printf("%-10s  %-42s %8s %8s %8s\n",
           "index", "bank", "before", "w0->rd", "w7->rd");

    for (unsigned i = 0; i < n; i++) {
        uint32_t before = 0, rd0 = 0xDEAD, rd7 = 0xDEAD;
        if (!reg_write(REG_GRBM_GFX_INDEX, cands[i].index)) continue;
        if (!reg_read(REG_SPI_PG_MASK, &before)) continue;

        if (reg_write(REG_SPI_PG_MASK, 0x00000000u) &&
            reg_read(REG_SPI_PG_MASK, &rd0) &&
            reg_write(REG_SPI_PG_MASK, 0x00000007u) &&
            reg_read(REG_SPI_PG_MASK, &rd7)) {
            printf("0x%08X  %-42s %8X %8X %8X  %s\n",
                   cands[i].index, cands[i].label, before, rd0, rd7,
                   (rd7 == 0x00000007u) ? "<== ENCODING CORRECT" : "");
        }
        /* restore */
        reg_write(REG_SPI_PG_MASK, before);
    }

    reg_write(REG_GRBM_GFX_INDEX, 0x15000000u);   /* leave gfx10 broadcast */
    printf("\nGRBM_GFX_INDEX restored to 0x15000000, SPI_PG restored.\n");
    printf("If any row says ENCODING CORRECT, the write path works and the\n"
           "  earlier \"SPI_PG is locked\" readings were the wrong bank.\n");
    printf("If no row does, every readback is 0 and the register is not being\n"
           "  reached at all through this path.\n");

    /*
     * Positive control. "Every readback is 0" is ambiguous between "this one
     * register is blocked" and "MMIO writes do not stick at all", and those
     * need completely different conclusions. Read back a register that is
     * known to hold a value, then write GRBM_GFX_INDEX and read it again. A
     * register that does not come back with the value just written proves the
     * write path itself is dead, which no amount of index fiddling can fix.
     */
    {
        uint32_t scratch = 0, idx = 0, scratch2 = 0;
        printf("\nPOSITIVE CONTROL:\n");
        if (reg_read(0x000032D4u, &scratch)) {
            printf("  SCRATCH[0x32D4]          = 0x%08X%s\n", scratch,
                   (scratch == 0x4D585042u) ? "  (beacon intact, MMIO read works)" : "");
        }
        if (reg_read(REG_GRBM_GFX_INDEX, &idx)) {
            printf("  GRBM_GFX_INDEX          = 0x%08X%s\n", idx,
                   (idx == 0x15000000u) ? "  (matches what we wrote: write path works)" : "");
        }
        reg_write(0x000032D4u, scratch);      /* rewrite same value */
        if (reg_read(0x000032D4u, &scratch2)) {
            printf("  SCRATCH after rewrite   = 0x%08X\n", scratch2);
        }
        printf("  If SCRATCH reads a sane value and GRBM_GFX_INDEX echoes, then\n"
               "  MMIO writes DO land and SPI_PG is specifically blocked.\n"
               "  If everything is 0, MMIO writes are not landing at all.\n");
    }
}

int
main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    if (argc < 2) {
        printf("usage: smu-unlock-staged <fstatus|feature6 <status|on|off>|wgp <0..18>"
               "|probe [addr]|verify|secprobe [addr...]|smnread|q2|q3|selftest>\n");
        return 2;
    }
    if (!open_dev()) return 1;

    if (!strcmp(argv[1], "probe")) {
        do_probe(argc >= 3 ? (uint32_t)strtoul(argv[2], NULL, 0) : 0);
    } else if (!strcmp(argv[1], "verify")) {
        do_verify();
    } else if (!strcmp(argv[1], "smnread")) {
        if (argc < 3) {
            printf("smnread needs an address, e.g. smnread 0x0005A870\n");
        } else {
            do_smnread((uint32_t)strtoul(argv[2], NULL, 0));
        }
    } else if (!strcmp(argv[1], "selftest")) {
        do_selftest();
    } else if (!strcmp(argv[1], "fstatus")) {
        do_feature_status();
    } else if (!strcmp(argv[1], "bankprobe")) {
        do_bankprobe();
    } else if (!strcmp(argv[1], "sramdiff")) {
        do_sramdiff(argc >= 3 ? (uint32_t)strtoul(argv[2], NULL, 0) : 1u);
    } else if (!strcmp(argv[1], "feature6")) {
        do_feature6(argc >= 3 ? argv[2] : "status");
    } else if (!strcmp(argv[1], "wgp")) {
        if (argc < 3) {
            printf("wgp needs a count 0..18\n");
            CloseHandle(g_h);
            return 2;
        }
        do_wgp((uint32_t)strtoul(argv[2], NULL, 0));
    } else if (!strcmp(argv[1], "secprobe")) {
        do_secprobe(argc, argv);
    } else if (!strcmp(argv[1], "q2")) {
        uint32_t w[4] = {0, 0, 0, 0};
        uint32_t msg;
        if (argc < 7) {
            printf("q2 needs: <msg> <w0> <w1> <w2> <w3>\n");
            CloseHandle(g_h);
            return 2;
        }
        msg = (uint32_t)strtoul(argv[2], NULL, 0);
        for (int i = 0; i < 4; i++) {
            w[i] = (uint32_t)strtoul(argv[3 + i], NULL, 0);
        }
        /* The ring append is refused here as well as in the kernel. Its entry
         * layout is {arg0 + base, arg2, arg1, 1}, so it is an arbitrary 16-byte
         * SMU-SRAM write with a caller-chosen destination, and the subqueue-4
         * command type overflows into the adjacent counter block. Two nested
         * guards is deliberate: this tool is the thing an operator reaches for
         * when something looks stuck, and one line of argument order should not
         * be able to wedge the SMU. */
        if (msg == AMDBC250_SMU_Q2_RING_APPEND) {
            printf("refusing: Q2 0x23 is the ring-hijack primitive and is not\n"
                   "          exposed by this tool. See the header comment.\n");
            CloseHandle(g_h);
            return 3;
        }
        printf("raw Q2 send\n");
        q2(msg, w, 4, "raw q2");
    } else if (!strcmp(argv[1], "q3")) {
        if (argc < 4) {
            printf("q3 needs: <msg> <arg>\n");
            CloseHandle(g_h);
            return 2;
        }
        printf("raw Q3 send\n");
        q3((uint32_t)strtoul(argv[2], NULL, 0),
           (uint32_t)strtoul(argv[3], NULL, 0), NULL, "raw q3");
    } else {
        printf("unknown command '%s'\n", argv[1]);
    }

    CloseHandle(g_h);
    return 0;
}
