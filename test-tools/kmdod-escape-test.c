// kmdod-escape-test.c
// User-mode test for the BC-250 Escape DDI in the KMDOD (SampleDisplay) driver.
// Opens the display adapter via D3DKMTOpenAdapterFromHdc and issues
// D3DKMT_ESCAPE_DRIVERPRIVATE escapes (BAR5 R/W, SMN R/W, SMU mailbox).
//
// Usage:
//   kmdod-escape-test.exe                        -- run all probes
//   kmdod-escape-test.exe readreg <off_hex>
//   kmdod-escape-test.exe writereg <off_hex> <val_hex>
//   kmdod-escape-test.exe readsmn <addr_hex>
//   kmdod-escape-test.exe writesmn <addr_hex> <val_hex>
//   kmdod-escape-test.exe smuq <msg_hex> [param_hex]
//   kmdod-escape-test.exe gpuinfo

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <d3dkmthk.h>

// Minimal NTSTATUS helper (user-mode compile of kernel header uses these)
#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif
#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS 0
#endif
#ifndef STATUS_NOT_SUPPORTED
#define STATUS_NOT_SUPPORTED ((NTSTATUS)0xC00000BBL)
#endif
#ifndef STATUS_INVALID_PARAMETER
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000DL)
#endif
#ifndef STATUS_INVALID_BUFFER_SIZE
#define STATUS_INVALID_BUFFER_SIZE ((NTSTATUS)0xC0000206L)
#endif

// BC-250 Escape protocol (must match bdd_ddi.cxx)
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
#define BC250_ESC_CORE_UNLOCK    10

typedef struct _BC250_ESC_BUFFER {
    ULONG Magic;
    ULONG Command;
    ULONG Arg1;
    ULONG Arg2;
    ULONG Result;
    NTSTATUS Status;
} BC250_ESC_BUFFER;

static D3DKMT_HANDLE g_hAdapter = 0;

static int esc(ULONG cmd, ULONG arg1, ULONG arg2, ULONG* pResult)
{
    BC250_ESC_BUFFER buf;
    D3DKMT_ESCAPE escape;
    NTSTATUS status;

    memset(&buf, 0, sizeof(buf));
    buf.Magic = BC250_ESC_MAGIC;
    buf.Command = cmd;
    buf.Arg1 = arg1;
    buf.Arg2 = arg2;

    memset(&escape, 0, sizeof(escape));
    escape.hAdapter = g_hAdapter;
    escape.hDevice = 0;
    escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    escape.Flags.Value = 0;
    escape.pPrivateDriverData = &buf;
    escape.PrivateDriverDataSize = sizeof(buf);
    escape.hContext = 0;

    status = D3DKMTEscape(&escape);
    if (!NT_SUCCESS(status))
    {
        printf("  D3DKMTEscape FAILED: 0x%08X (%lu)\n", status, status);
        return -1;
    }
    if (!NT_SUCCESS(buf.Status))
    {
        printf("  Escape handler error: 0x%08X\n", buf.Status);
        return -1;
    }
    if (pResult)
    {
        *pResult = buf.Result;
    }
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

static void run_all(void)
{
    ULONG r;

    printf("== BC-250 KMDOD Escape probes ==\n");

    if (esc(BC250_ESC_GET_GPU_ID, 0, 0, &r) == 0)
        printf("GPU_ID            (BAR5 0x0000): 0x%08X\n", r);

    // GET_STATUS: Result=GPU_ID, Arg2=GRBM_STATUS, Arg1=SMU version
    {
        BC250_ESC_BUFFER buf;
        D3DKMT_ESCAPE escape;
        memset(&buf, 0, sizeof(buf));
        buf.Magic = BC250_ESC_MAGIC;
        buf.Command = BC250_ESC_GET_STATUS;
        memset(&escape, 0, sizeof(escape));
        escape.hAdapter = g_hAdapter;
        escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
        escape.pPrivateDriverData = &buf;
        escape.PrivateDriverDataSize = sizeof(buf);
        if (NT_SUCCESS(D3DKMTEscape(&escape)))
            printf("GET_STATUS: GPU_ID=0x%08X GRBM_STATUS=0x%08X SMUver=0x%08X\n",
                buf.Result, buf.Arg2, buf.Arg1);
    }

    if (esc(BC250_ESC_READ_REG, 0x3260, 0, &r) == 0)   // GRBM_STATUS
        printf("GRBM_STATUS       (BAR5 0x3260): 0x%08X\n", r);
    if (esc(BC250_ESC_READ_REG, 0x32D4, 0, &r) == 0)   // SCRATCH
        printf("SCRATCH           (BAR5 0x32D4): 0x%08X\n", r);
    if (esc(BC250_ESC_READ_REG, 0x34D0, 0, &r) == 0)   // GRBM_GFX_INDEX
        printf("GRBM_GFX_INDEX    (BAR5 0x34D0): 0x%08X\n", r);
    if (esc(BC250_ESC_READ_REG, 0x5C3C, 0, &r) == 0)   // SPI_PG
        printf("SPI_PG_STATIC_MASK(BAR5 0x5C3C): 0x%08X\n", r);
    if (esc(BC250_ESC_READ_REG, 0x9C1C, 0, &r) == 0)   // CC_ARRAY
        printf("CC_GC_ARRAY_CONFIG(BAR5 0x9C1C): 0x%08X\n", r);

    if (esc(BC250_ESC_SMU_QUERY, 0x02, 0, &r) == 0)    // GetSmuVersion
        printf("SMU GetSmuVersion (Q0 msg 0x02): 0x%08X\n", r);
    if (esc(BC250_ESC_SMU_QUERY, 0x03, 0, &r) == 0)    // GetDriverIfVersion
        printf("SMU GetDriverIfVer(Q0 msg 0x03): 0x%08X\n", r);
    if (esc(BC250_ESC_SMU_QUERY, 0x37, 0, &r) == 0)    // GetGfxFrequency
        printf("SMU GetGfxFreq    (Q0 msg 0x37): %lu MHz\n", r);
    if (esc(BC250_ESC_SMU_QUERY, 0x3D, 0, &r) == 0)    // GetEnabledSmuFeatures
        printf("SMU Features      (Q0 msg 0x3D): 0x%08X\n", r);
    if (esc(BC250_ESC_SMU_QUERY, 0x1E, 0, &r) == 0)    // QueryActiveWgp
        printf("SMU ActiveWgp     (Q0 msg 0x1E): %lu\n", r);

    if (esc(BC250_ESC_READ_SMN, 0x03B10024, 0, &r) == 0)   // FW_FLAGS
        printf("SMN FW_FLAGS      (0x03B10024): 0x%08X\n", r);
    if (esc(BC250_ESC_READ_SMN, 0x03B10B14, 0, &r) == 0)   // PUB_CTRL
        printf("SMN PUB_CTRL      (0x03B10B14): 0x%08X\n", r);
    if (esc(BC250_ESC_READ_SMN, 0x03B10A08, 0, &r) == 0)   // C2PMSG_66
        printf("SMN C2PMSG_66     (0x03B10A08): 0x%08X\n", r);
    if (esc(BC250_ESC_READ_SMN, 0x03B10A48, 0, &r) == 0)   // C2PMSG_82
        printf("SMN C2PMSG_82     (0x03B10A48): 0x%08X\n", r);
    if (esc(BC250_ESC_READ_SMN, 0x03B10A68, 0, &r) == 0)   // C2PMSG_90
        printf("SMN C2PMSG_90     (0x03B10A68): 0x%08X\n", r);
}

int main(int argc, char** argv)
{
    ULONG r;
    unsigned long v1, v2;

    printf("BC-250 KMDOD Escape test tool\n");

    if (open_adapter() != 0)
    {
        printf("Cannot open display adapter. Is SampleDisplay v1.0.115+ installed?\n");
        return 1;
    }

    if (argc < 2)
    {
        run_all();
        return 0;
    }

    if (strcmp(argv[1], "readreg") == 0 && argc >= 3)
    {
        v1 = (unsigned long)strtoul(argv[2], NULL, 16);
        if (esc(BC250_ESC_READ_REG, v1, 0, &r) == 0)
            printf("0x%08X\n", r);
    }
    else if (strcmp(argv[1], "writereg") == 0 && argc >= 4)
    {
        v1 = (unsigned long)strtoul(argv[2], NULL, 16);
        v2 = (unsigned long)strtoul(argv[3], NULL, 16);
        if (esc(BC250_ESC_WRITE_REG, v1, v2, &r) == 0)
            printf("write 0x%08X=0x%08X -> readback 0x%08X\n", v1, v2, r);
    }
    else if (strcmp(argv[1], "readsmn") == 0 && argc >= 3)
    {
        v1 = (unsigned long)strtoul(argv[2], NULL, 16);
        if (esc(BC250_ESC_READ_SMN, v1, 0, &r) == 0)
            printf("SMN[0x%08X] = 0x%08X\n", v1, r);
    }
    else if (strcmp(argv[1], "writesmn") == 0 && argc >= 4)
    {
        v1 = (unsigned long)strtoul(argv[2], NULL, 16);
        v2 = (unsigned long)strtoul(argv[3], NULL, 16);
        if (esc(BC250_ESC_WRITE_SMN, v1, v2, &r) == 0)
            printf("SMN[0x%08X] <- 0x%08X (readback 0x%08X)\n", v1, v2, r);
    }
    else if (strcmp(argv[1], "smuq") == 0 && argc >= 3)
    {
        v1 = (unsigned long)strtoul(argv[2], NULL, 16);
        if (argc >= 4)
        {
            v2 = (unsigned long)strtoul(argv[3], NULL, 16);
            if (esc(BC250_ESC_SMU_QUERY_PARAM, v1, v2, &r) == 0)
                printf("SMU msg 0x%lX param 0x%lX -> 0x%08X\n", v1, v2, r);
        }
        else
        {
            if (esc(BC250_ESC_SMU_QUERY, v1, 0, &r) == 0)
                printf("SMU msg 0x%lX -> 0x%08X\n", v1, r);
        }
    }
    else if (strcmp(argv[1], "gpuinfo") == 0)
    {
        if (esc(BC250_ESC_GET_STATUS, 0, 0, &r) == 0)
            printf("GPU_ID=0x%08X\n", r);
    }
    else if (strcmp(argv[1], "smusend") == 0 && argc >= 4)
    {
        // smusend <queue> <msg_hex> <param_hex>
        unsigned long q = strtoul(argv[2], NULL, 10);
        v1 = (unsigned long)strtoul(argv[3], NULL, 16);
        v2 = (unsigned long)strtoul(argv[4], NULL, 16);
        if (esc(BC250_ESC_SMU_SEND, (q << 16) | v1, v2, &r) == 0)
            printf("SMU Q%lu msg 0x%lX param 0x%lX -> 0x%08X\n", q, v1, v2, r);
    }
    else if (strcmp(argv[1], "coreunlock") == 0)
    {
        BC250_ESC_BUFFER buf;
        D3DKMT_ESCAPE escape;
        memset(&buf, 0, sizeof(buf));
        buf.Magic = BC250_ESC_MAGIC;
        buf.Command = BC250_ESC_CORE_UNLOCK;
        memset(&escape, 0, sizeof(escape));
        escape.hAdapter = g_hAdapter;
        escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
        escape.pPrivateDriverData = &buf;
        escape.PrivateDriverDataSize = sizeof(buf);
        if (NT_SUCCESS(D3DKMTEscape(&escape)))
        {
            printf("CORE_UNLOCK: Status=0x%08X mask=0x%08X\n", buf.Status, buf.Result);
        }
        else
        {
            printf("CORE_UNLOCK D3DKMTEscape FAILED\n");
        }
    }
    else
    {
        printf("Unknown command: %s\n", argv[1]);
        printf("Usage: %s [readreg <off> | writereg <off> <val> | readsmn <addr> | writesmn <addr> <val> | smuq <msg> [param] | smusend <queue> <msg> <param> | coreunlock]\n", argv[0]);
    }

    return 0;
}