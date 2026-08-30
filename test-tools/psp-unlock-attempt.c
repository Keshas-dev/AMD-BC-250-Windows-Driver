/* psp-unlock-attempt.c
 *
 * Fresh (2026-08-21) WGP unlock attempts through the PSP driver \\.\AmdBcPsp.
 * Old attempts were pre-C2PMSG-fix era — re-testing everything with current builds.
 *
 * Paths tried:
 *   A) NBIO_UNLOCK signature writes (0xC100/0xC180 -> 0xFEDCBAEF/0xFEDCBADF)
 *   B) SMU Q0 msg 0x18 RequestActiveWgp (fresh retry; old run was pre-fix)
 *   C) SMU Q0 msg 0x1E QueryActiveWgp baseline + after each step
 *   D) REG_PROG direct write to SPI_PG (same BAR5 path, control group)
 *
 * SPI_PG checked after EVERY step. READ-ONLY except the explicit attempts.
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

#define DEV_PATH L"\\\\.\\AmdBcPsp"

#define PSP_READ_REG    0x222000UL  /* fn 0x800 */
#define PSP_NBIO_UNLOCK 0x222010UL  /* fn 0x804 */
#define PSP_REG_PROG    0x222058UL  /* fn 0x816 */
#define PSP_SMU_WAKE    0x222084UL  /* fn 0x821 */

#define SPI_PG_OFFSET   0x5C3C
#define CC_ARRAY_OFFSET 0x9C1C

typedef struct { ULONG Offset; ULONG Reserved; } RD_REQ;
typedef struct { ULONG Value;  ULONG Status;  } RD_RSP;

typedef struct {                     /* PSP_SMU_WAKE_REQUEST */
    ULONG Message; ULONG Argument; ULONG Reserved[2];
} SMU_REQ;
typedef struct {                     /* PSP_SMU_WAKE_RESPONSE */
    ULONG Message; ULONG Argument; ULONG Response; ULONG Status;
} SMU_RSP;

static HANDLE h = INVALID_HANDLE_VALUE;

static int ReadReg(ULONG off, ULONG* out) {
    RD_REQ req; RD_RSP rsp; DWORD ret = 0;
    req.Offset = off; req.Reserved = 0;
    if (!DeviceIoControl(h, PSP_READ_REG, &req, sizeof(req), &rsp, sizeof(rsp), &ret, NULL)) return -1;
    *out = rsp.Value; return 0;
}

/* returns 0 on OK(1), prints status otherwise */
static int SmuMsg(ULONG msg, ULONG arg, ULONG* respVal, ULONG* statusCode) {
    SMU_REQ req; SMU_RSP rsp; DWORD ret = 0;
    ZeroMemory(&req, sizeof(req)); ZeroMemory(&rsp, sizeof(rsp));
    req.Message = msg; req.Argument = arg;
    if (!DeviceIoControl(h, PSP_SMU_WAKE, &req, sizeof(req), &rsp, sizeof(rsp), &ret, NULL))
        return -1;
    if (respVal) *respVal = rsp.Response;
    if (statusCode) *statusCode = rsp.Status;
    return (rsp.Status == 1) ? 0 : 1;
}

static void CheckState(const char* label) {
    ULONG spi = 0, cc = 0, resp = 0, st = 0;
    ReadReg(SPI_PG_OFFSET, &spi);
    ReadReg(CC_ARRAY_OFFSET, &cc);
    SmuMsg(0x1E, 0, &resp, &st);
    printf("[%s] SPI_PG=0x%08X CC=0x%08X ActiveWgp=%u (smuSt=%u)\n",
           label, spi, cc, (st == 1) ? resp : 0xFFFFFFFF, st);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    h = CreateFileW(DEV_PATH, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { printf("FAIL open AmdBcPsp gle=%lu\n", GetLastError()); return 1; }
    printf("Opened \\\\.\\AmdBcPsp\n\n");

    /* SMU alive check */
    ULONG resp = 0, st = 0;
    int rc = SmuMsg(0x01, 0xAA, &resp, &st);
    printf("SMU TestMessage: st=%u resp=0x%X %s\n\n", st, resp, (rc == 0) ? "(alive)" : "(DEAD)");

    CheckState("BASELINE");

    /* --- Path A: NBIO_UNLOCK signatures --- */
    DWORD ret = 0; ULONG out[3] = {0,0,0};
    BOOL ok = DeviceIoControl(h, PSP_NBIO_UNLOCK, NULL, 0, out, sizeof(out), &ret, NULL);
    printf("\n[A] NBIO_UNLOCK: ok=%d sig1=0x%08X sig2=0x%08X mmhubAfter=0x%08X\n",
           ok, out[0], out[1], out[2]);
    CheckState("after NBIO_UNLOCK");

    /* --- Path B: RequestActiveWgp (Q0 0x18) --- */
    rc = SmuMsg(0x18, 0, &resp, &st);
    printf("\n[B] RequestActiveWgp(0x18): st=%u resp=0x%08X %s\n", st, resp,
           rc == 0 ? "(ACCEPTED)" : (rc == -1 ? "(IOCTL FAIL)" : "(rejected/unknown)"));
    CheckState("after ReqActiveWgp");

    /* --- Path C: wake GFX then query again --- */
    SmuMsg(0x3B, 99, &resp, &st);   /* ForceGfxVid vid=99 (~932mV) */
    printf("\n[C] ForceGfxVid(99): st=%u resp=0x%X\n", st, resp);
    SmuMsg(0x39, 1500, &resp, &st); /* ForceGfxFreq 1500MHz */
    printf("    ForceGfxFreq(1500): st=%u resp=0x%X\n", st, resp);
    Sleep(200);
    CheckState("after GFX wake");

    /* --- Path D: direct SPI_PG write via driver (control group) --- */
    RD_REQ wreq; DWORD wret = 0;
    wreq.Offset = SPI_PG_OFFSET; wreq.Reserved = 0x1F;
    /* NOTE: WRITE_REG IOCTL is fn 0x801 -> 0x222004; reuse struct {off,val} */
    ULONG wr[2] = { SPI_PG_OFFSET, 0x0000001F };
    ok = DeviceIoControl(h, 0x222004UL /* WRITE_REG */, wr, sizeof(wr), wr, sizeof(wr), &wret, NULL);
    printf("\n[D] Direct SPI_PG<-0x1F via PSP driver: ok=%d\n", ok);
    CheckState("after direct write");

    printf("\n=== VERDICT ===\n");
    ULONG finalSpi = 0; ReadReg(SPI_PG_OFFSET, &finalSpi);
    if ((finalSpi & 0x1F) == 0x1F)
        printf("SPI_PG=0x1F — UNLOCK WORKED THROUGH PSP DRIVER PATH!\n");
    else
        printf("SPI_PG still locked via all PSP-driver paths (fresh confirmation).\n");

    CloseHandle(h);
    return 0;
}
