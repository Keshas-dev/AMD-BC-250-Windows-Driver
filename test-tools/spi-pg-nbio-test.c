#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>

/* PSP driver path (separate device) */
#define PSP_DEVICE_NAME_W   L"\\\\.\\AmdBcPsp"
#define IOCTL_PSP_READ_REG    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PSP_WRITE_REG   CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PSP_INIT_HW     CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PSP_NBIO_UNLOCK CTL_CODE(FILE_DEVICE_UNKNOWN, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PSP_GET_STATUS  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x808, METHOD_BUFFERED, FILE_ANY_ACCESS)

#pragma pack(push, 1)
typedef struct { ULONG Offset; ULONG Reserved; } PSP_READ_REQ;
typedef struct { ULONG Value;   ULONG Status;    } PSP_READ_RESP;
typedef struct { ULONG Offset;  ULONG Value;     } PSP_WRITE_REQ;
typedef struct { ULONG Status;  ULONG Reserved;  } PSP_WRITE_RESP;
typedef struct { ULONG64 PhysicalAddress; ULONG Size; } PSP_INIT_REQ;
typedef struct {
    ULONG C2PMSG_81; ULONG C2PMSG_35; ULONG C2PMSG_36;
    ULONG PspAlive;  ULONG FwLoaded;  ULONG FwSize; ULONG FwPaShifted;
    ULONG NbioSig1;  ULONG NbioSig2;
    ULONG GrbmStatus; ULONG MmhubCheck;
    ULONG MmioVA;    ULONG MmioSize;  ULONG RingCreated;
    ULONG C2PMSG_37; ULONG C2PMSG_64;
    ULONG GcCheck;   ULONG HdpCheck;
    ULONG MeCntl;    ULONG GrbmGfxIndex;
} PSP_STATUS_INFO;
#pragma pack(pop)

/* GPU driver */
#include "..\inc\amdbc250_ioctl.h"

/* Registers (GC_BASE = 0x1260; Linux gc_10_1_0_offset.h mmSPI=0x1277 -> 0x5C3C) */
#define REG_SPI_PG     0x5C3C   /* CORRECT: mmSPI_PG_ENABLE_STATIC_WGP_MASK = 0x1277*4+0x1260 */
#define REG_SPI_OLD    0x34FC   /* WRONG offset used by PSP docs (different register) */
#define REG_CC_ARRAY   0x9C1C
#define REG_RLC_PG     0x3D64
#define REG_GRBM       0x3260
#define REG_GFX_INDEX  0x34D0   /* READ-ONLY in this test — write was BSOD cause */
#define REG_SCRATCH    0x32D4

static HANDLE g_gpu = INVALID_HANDLE_VALUE;
static HANDLE g_psp = INVALID_HANDLE_VALUE;
static FILE  *g_log = NULL;

static void xlog(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    if (g_log) {
        va_start(ap, fmt);
        vfprintf(g_log, fmt, ap);
        va_end(ap);
        fflush(g_log);
    }
    fflush(stdout);
}

static void phase(const char *tag) {
    xlog("\n========== PHASE: %s ==========\n", tag);
}

static BOOL GpuRead(uint32_t off, uint32_t *val) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD br = 0;
    memset(&r, 0, sizeof(r));
    r.RegisterOffset = off;
    if (!DeviceIoControl(g_gpu, IOCTL_AMDBC250_READ_REG, &r, sizeof(r), &r, sizeof(r), &br, NULL))
        return FALSE;
    if (val) *val = r.Value;
    return TRUE;
}
static BOOL GpuWrite(uint32_t off, uint32_t val) {
    AMDBC250_IOCTL_REG_ACCESS r; DWORD br = 0;
    memset(&r, 0, sizeof(r));
    r.RegisterOffset = off;
    r.Value = val;
    return DeviceIoControl(g_gpu, IOCTL_AMDBC250_WRITE_REG, &r, sizeof(r), &r, sizeof(r), &br, NULL);
}

static BOOL PspRead(uint32_t off, uint32_t *val) {
    PSP_READ_REQ req = { off, 0 }; PSP_READ_RESP resp = { 0 }; DWORD br = 0;
    if (!DeviceIoControl(g_psp, IOCTL_PSP_READ_REG, &req, sizeof(req), &resp, sizeof(resp), &br, NULL))
        return FALSE;
    if (val) *val = resp.Value;
    return TRUE;
}
static BOOL PspWrite(uint32_t off, uint32_t val) {
    PSP_WRITE_REQ req = { off, val }; PSP_WRITE_RESP resp = { 0 }; DWORD br = 0;
    return DeviceIoControl(g_psp, IOCTL_PSP_WRITE_REG, &req, sizeof(req), &resp, sizeof(resp), &br, NULL);
}

static const char *st_name(uint32_t s) {
    switch (s) {
    case 1: return "OK"; case 0xFF: return "FAIL";
    case 0xFE: return "UNKNOWN"; case 0xFD: return "REJECTED";
    case 0xFC: return "BUSY"; case 0: return "TIMEOUT"; default: return "?";
    }
}

static int SmuQ0(uint32_t msg, uint32_t arg, uint32_t *out) {
    AMDBC250_IOCTL_SMU_CPU_MSG sm; DWORD br = 0;
    memset(&sm, 0, sizeof(sm));
    sm.Queue = 0; sm.Message = msg; sm.Argument = arg;
    if (!DeviceIoControl(g_gpu, IOCTL_AMDBC250_SMU_CPU_MSG, &sm, sizeof(sm), &sm, sizeof(sm), &br, NULL)) {
        xlog("  SMU_CPU_MSG q0 msg=0x%X FAIL gle=%lu\n", msg, GetLastError());
        return 0;
    }
    xlog("  Q0 0x%02X -> resp=0x%08X st=0x%02X (%s) res=%u\n",
         msg, sm.Response, sm.ResponseStatus, st_name(sm.ResponseStatus), sm.Result);
    if (out) *out = sm.Response;
    return sm.ResponseStatus == 1;
}

static void dump_regs(const char *tag) {
    uint32_t v;
    xlog("--- %s ---\n", tag);
    if (GpuRead(REG_GRBM, &v))       xlog("  GPU GRBM     (0x3260): 0x%08X\n", v);
    if (GpuRead(REG_GFX_INDEX, &v))  xlog("  GPU GFX_IDX  (0x34D0) RO: 0x%08X\n", v);
    if (GpuRead(REG_SCRATCH, &v))    xlog("  GPU SCRATCH  (0x32D4): 0x%08X\n", v);
    if (GpuRead(REG_SPI_PG, &v))
        xlog("  GPU SPI_PG   (0x5C3C) CORRECT: 0x%08X %s\n", v,
             (v == 0x1F) ? "*** UNLOCKED ***" : (v == 0 ? "[0 = gated]" : "[value]"));
    if (GpuRead(REG_SPI_OLD, &v))
        xlog("  GPU SPI_34FC (0x34FC) old/wrong: 0x%08X\n", v);
    if (GpuRead(REG_CC_ARRAY, &v))   xlog("  GPU CC_ARRAY (0x9C1C): 0x%08X\n", v);
    if (GpuRead(REG_RLC_PG, &v))     xlog("  GPU RLC_PG   (0x3D64): 0x%08X\n", v);

    if (g_psp != INVALID_HANDLE_VALUE) {
        if (PspRead(REG_SPI_PG, &v))
            xlog("  PSP SPI_PG   (0x5C3C): 0x%08X\n", v);
        if (PspRead(REG_SPI_OLD, &v))
            xlog("  PSP SPI_34FC (0x34FC): 0x%08X\n", v);
        if (PspRead(REG_CC_ARRAY, &v))  xlog("  PSP CC_ARRAY (0x9C1C): 0x%08X\n", v);
        if (PspRead(REG_RLC_PG, &v))    xlog("  PSP RLC_PG   (0x3D64): 0x%08X\n", v);
        if (PspRead(REG_GRBM, &v))      xlog("  PSP GRBM     (0x3260): 0x%08X\n", v);
    }
}

int main(int argc, char **argv) {
    int do_writes = 1;
    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-r") == 0) do_writes = 0;  /* read-only */
    }

    setvbuf(stdout, NULL, _IONBF, 0);
    g_log = fopen("C:\\AMD-BC-250\\spi-pg-nbio-test.log", "w");
    xlog("=== BC-250 SPI_PG after PSP NBIO unlock (v2, no GFX_INDEX write) ===\n");
    xlog("mode: %s, argv=%d\n", do_writes ? "read+write" : "read-only", argc);

    /* --- Open GPU --- */
    phase("OPEN GPU");
    g_gpu = CreateFileA("\\\\.\\AMDBC250DreamV43",
        GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, 0, NULL);
    if (g_gpu == INVALID_HANDLE_VALUE) {
        xlog("FAIL: GPU device gle=%lu\n", GetLastError());
        return 1;
    }
    xlog("GPU device opened\n");

    /* Probe READ first — INIT only if needed (remap can race display) */
    phase("GPU READ PROBE (skip INIT if already mapped)");
    {
        uint32_t probe = 0;
        if (GpuRead(REG_GRBM, &probe)) {
            xlog("GPU already mapped, GRBM=0x%08X — skip INIT_HARDWARE\n", probe);
        } else {
            AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br = 0;
            xlog("GPU not ready (gle=%lu), calling INIT_HARDWARE NBIO_MAP...\n", GetLastError());
            memset(&ih, 0, sizeof(ih));
            ih.MmioPhysicalBase = 0xFE800000ULL;
            ih.MmioSize = 0x80000;
            ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
            if (!DeviceIoControl(g_gpu, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &br, NULL)) {
                xlog("FAIL: GPU INIT_HARDWARE gle=%lu\n", GetLastError());
                return 1;
            }
            xlog("GPU INIT_HARDWARE OK (BAR5 0xFE800000)\n");
        }
    }

    /* --- Open PSP --- */
    phase("OPEN PSP");
    g_psp = CreateFileW(PSP_DEVICE_NAME_W,
        GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (g_psp == INVALID_HANDLE_VALUE) {
        xlog("WARN: PSP device open failed gle=%lu (GPU-only path)\n", GetLastError());
        g_psp = INVALID_HANDLE_VALUE;
    } else {
        xlog("PSP device opened — INIT_HW only if NBIO needs MmioBase\n");
        /* Try GET_STATUS first; if MmioVA=0, INIT_HW needed for NBIO_UNLOCK */
        PSP_STATUS_INFO si; DWORD br = 0;
        memset(&si, 0, sizeof(si));
        if (DeviceIoControl(g_psp, IOCTL_PSP_GET_STATUS, NULL, 0, &si, sizeof(si), &br, NULL)) {
            xlog("PSP status: MmioVA=0x%08X Size=0x%X SIG1=0x%08X\n",
                 si.MmioVA, si.MmioSize, si.NbioSig1);
            if (si.MmioVA == 0) {
                PSP_INIT_REQ ireq;
                xlog("MmioVA=0, calling PSP INIT_HW (BAR5) for NBIO...\n");
                memset(&ireq, 0, sizeof(ireq));
                ireq.PhysicalAddress = 0xFE800000ULL;
                ireq.Size = 0x80000;
                if (DeviceIoControl(g_psp, IOCTL_PSP_INIT_HW, &ireq, sizeof(ireq), NULL, 0, &br, NULL))
                    xlog("PSP INIT_HW (BAR5) OK\n");
                else
                    xlog("WARN: PSP INIT_HW gle=%lu (NBIO may fail)\n", GetLastError());
            } else {
                xlog("PSP already has MmioBase — skip INIT_HW (avoid dual map)\n");
            }
        } else {
            xlog("WARN: PSP GET_STATUS gle=%lu\n", GetLastError());
        }
    }

    /* --- Phase 1: baseline BEFORE NBIO unlock --- */
    phase("1 BASELINE (before NBIO unlock)");
    dump_regs("BEFORE unlock");

    uint32_t wgp0 = 0;
    xlog("\nSMU QueryActiveWgp baseline:\n");
    SmuQ0(AMDBC250_SMU_Q0_QUERY_ACTIVE_WGP, 0, &wgp0);

    /* --- Phase 2: PSP NBIO unlock --- */
    phase("2 PSP NBIO UNLOCK");
    if (g_psp != INVALID_HANDLE_VALUE) {
        ULONG resp[4] = {0}; DWORD br = 0;
        if (DeviceIoControl(g_psp, IOCTL_PSP_NBIO_UNLOCK, NULL, 0, resp, sizeof(resp), &br, NULL)) {
            xlog("NBIO_UNLOCK: SIG1=0x%08X SIG2=0x%08X extra=0x%08X 0x%08X\n",
                 resp[0], resp[1],
                 (br >= 12) ? resp[2] : 0, (br >= 16) ? resp[3] : 0);
            if (resp[0] == 0xFEDCBAEF || resp[1] == 0xFEDCBADF)
                xlog("*** NBIO signatures written OK ***\n");
        } else {
            xlog("NBIO_UNLOCK FAILED gle=%lu\n", GetLastError());
        }
        PSP_STATUS_INFO si; memset(&si, 0, sizeof(si));
        if (DeviceIoControl(g_psp, IOCTL_PSP_GET_STATUS, NULL, 0, &si, sizeof(si), &br, NULL)) {
            xlog("PSP status: SIG1=0x%08X SIG2=0x%08X GRBM=0x%08X MMHUB=0x%08X GC=0x%08X HDP=0x%08X\n",
                 si.NbioSig1, si.NbioSig2, si.GrbmStatus, si.MmhubCheck, si.GcCheck, si.HdpCheck);
        }
    } else {
        xlog("SKIP: no PSP device\n");
    }

    /* --- Phase 3: re-read after unlock --- */
    phase("3 AFTER NBIO unlock");
    dump_regs("AFTER unlock");

    /* --- Phase 4: SPI_PG write (NO GRBM_GFX_INDEX write — known BSOD) --- */
    if (do_writes) {
        phase("4 WRITE SPI_PG 0x5C3C = 0x1F (no GFX_INDEX)");
        {
            uint32_t spi_b = 0, spi_a = 0;
            GpuRead(REG_SPI_PG, &spi_b);
            xlog("SPI_PG pre-write: 0x%08X\n", spi_b);
            GpuWrite(REG_SPI_PG, 0x1F);
            Sleep(10);
            GpuRead(REG_SPI_PG, &spi_a);
            xlog("SPI_PG 0x5C3C: before=0x%08X wrote=0x1F read=0x%08X %s\n",
                 spi_b, spi_a,
                 (spi_a == 0x1F) ? "*** UNLOCKED! ***" :
                 (spi_a == spi_b) ? "[LOCKED — write dropped]" : "[CHANGED partial]");

            /* also try via PSP path */
            if (g_psp != INVALID_HANDLE_VALUE) {
                uint32_t pspi_b = 0, pspi_a = 0;
                PspRead(REG_SPI_PG, &pspi_b);
                PspWrite(REG_SPI_PG, 0x1F);
                Sleep(10);
                PspRead(REG_SPI_PG, &pspi_a);
                xlog("PSP SPI_PG 0x5C3C: before=0x%08X wrote=0x1F read=0x%08X %s\n",
                     pspi_b, pspi_a,
                     (pspi_a == 0x1F) ? "*** UNLOCKED via PSP! ***" : "[not via PSP]");
            }

            /* Linux recipe: CC=0 + SPI=0x1F together */
            uint32_t cc_b = 0, cc_a = 0;
            GpuRead(REG_CC_ARRAY, &cc_b);
            GpuWrite(REG_CC_ARRAY, 0x0);
            Sleep(10);
            GpuRead(REG_CC_ARRAY, &cc_a);
            xlog("CC_ARRAY 0x9C1C: before=0x%08X wrote=0x0 read=0x%08X %s\n",
                 cc_b, cc_a,
                 (cc_a == 0) ? "[CLEARED]" :
                 (cc_a == 0x1F000000) ? "[PARTIAL bits 24-28 only — old behavior]" : "[CHANGED]");

            uint32_t rlc_b = 0, rlc_a = 0;
            GpuRead(REG_RLC_PG, &rlc_b);
            GpuWrite(REG_RLC_PG, 0x1F);
            Sleep(10);
            GpuRead(REG_RLC_PG, &rlc_a);
            xlog("RLC_PG 0x3D64: before=0x%08X wrote=0x1F read=0x%08X %s\n",
                 rlc_b, rlc_a,
                 (rlc_a == 0x1F) ? "[POWERED]" : "[LOCKED]");
        }
    } else {
        phase("4 SKIPPED (read-only mode -r)");
    }

    /* --- Phase 5: re-read + QueryActiveWgp --- */
    phase("5 FINAL READBACK + SMU");
    dump_regs("FINAL");

    uint32_t wgp1 = 0;
    xlog("\nSMU QueryActiveWgp after write:\n");
    SmuQ0(AMDBC250_SMU_Q0_QUERY_ACTIVE_WGP, 0, &wgp1);
    SmuQ0(AMDBC250_SMU_Q0_GET_ENABLED_FEATURES, 0, NULL);
    SmuQ0(AMDBC250_SMU_Q0_GET_GFX_FREQUENCY, 0, NULL);

    /* --- Verdict --- */
    uint32_t spi_final = 0xFFFFFFFF;
    GpuRead(REG_SPI_PG, &spi_final);
    phase("VERDICT");
    if (spi_final == 0x1F && wgp1 > 0) {
        xlog("*** SUCCESS: SPI_PG=0x1F, ActiveWgp=%u — WGP unlock works after NBIO unlock! ***\n", wgp1);
    } else if (spi_final == 0x1F) {
        xlog("*** SPI_PG write STUCK (0x1F) but ActiveWgp still %u — try force GFX freq / GFXOFF off ***\n", wgp1);
    } else {
        xlog("*** FAIL: SPI_PG=0x%08X after NBIO unlock — hypothesis rejected OR unlock incomplete ***\n", spi_final);
    }

    if (g_psp != INVALID_HANDLE_VALUE) CloseHandle(g_psp);
    CloseHandle(g_gpu);
    if (g_log) fclose(g_log);
    return (spi_final == 0x1F) ? 0 : 2;
}
