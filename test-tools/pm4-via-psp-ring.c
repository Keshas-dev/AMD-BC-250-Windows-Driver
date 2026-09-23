/* pm4-via-psp-ring.c -- Definitive test: can PSP GPCOM ring relay PM4 to CP?
 * SCRATCH is cleared to 0x12345678 via CPU BEFORE each PSP submit.
 * Each cmd_id uses a UNIQUE marker. Only a change to that marker = proof. */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static HANDLE g_hDev = INVALID_HANDLE_VALUE;

#define IOCTL_GPU_INIT       0x80000B80
#define IOCTL_GPU_READ       0x80000B88
#define IOCTL_GPU_WRITE      0x80000B8C
#define IOCTL_PSP_RING_INIT  0x80000C18
#define IOCTL_PSP_RING_SUBMIT 0x80000C1C

#define REG_SCRATCH     0x32D4
#define REG_CP_ME_CNTL  0x4A74
#define REG_SPI_PG      0x5C3C
#define REG_GRBM_STATUS 0x3260
#define C2PMSG_64       0x58200
#define C2PMSG_67       0x5820C
#define C2PMSG_81       0x58244

#define SEED 0x12345678

typedef struct { uint32_t Offset; uint32_t Value; } REG_IO;

static BOOL Ioctl(uint32_t code, const void* in, uint32_t inSz, void* out, uint32_t outSz) {
    DWORD br = 0;
    return DeviceIoControl(g_hDev, code, (LPVOID)in, inSz, out, outSz, &br, NULL);
}
static uint32_t ReadReg(uint32_t off) {
    REG_IO r = { off, 0 };
    Ioctl(IOCTL_GPU_READ, &r, sizeof(r), &r, sizeof(r));
    return r.Value;
}
static void WriteReg(uint32_t off, uint32_t val) {
    REG_IO r = { off, val };
    Ioctl(IOCTL_GPU_WRITE, &r, sizeof(r), NULL, 0);
}
static BOOL InitHardware(void) {
    UCHAR buf[32] = {0};
    *(uint64_t*)(buf + 0)  = 0xFE800000ULL;
    *(uint32_t*)(buf + 8)  = 0x80000;
    *(uint32_t*)(buf + 12) = 1;
    return Ioctl(IOCTL_GPU_INIT, buf, sizeof(buf), NULL, 0);
}
static uint32_t SeedScratch(void) {
    WriteReg(REG_SCRATCH, SEED);
    return ReadReg(REG_SCRATCH);
}
static const char* Verdict(uint32_t marker, uint32_t actual) {
    if (actual == marker) return "MATCH! PM4 DID EXECUTE!";
    if (actual != SEED)   return "CHANGED (unexpected value)";
    return "NO CHANGE (PSP did NOT forward PM4)";
}

#pragma pack(push, 1)
typedef struct {
    uint32_t Result, FenceStatus, RespStatus;
    uint32_t RespFwAddrLo, RespFwAddrHi, RespTmrSize;
} SUBMIT_OUT;
#pragma pack(pop)

static int PspSubmit(uint32_t cmdId, const void* data, uint32_t dataSize, SUBMIT_OUT* out) {
    UCHAR in[8 + 512];
    memset(in, 0, sizeof(in));
    *(uint32_t*)(in + 0) = cmdId;
    *(uint32_t*)(in + 4) = dataSize;
    if (data && dataSize > 0) memcpy(in + 8, data, dataSize);
    BOOL ok = Ioctl(IOCTL_PSP_RING_SUBMIT, in, 8 + dataSize, out, sizeof(*out));
    return ok ? 0 : -1;
}

#define PM4_HDR(op, cnt) ((3u << 30) | (((cnt)-1) << 16) | ((op) << 8))
#define PM4_NOP2         0x30000000
#define IT_WRITE_DATA    0x37

static void Ppm4(uint32_t marker) {
    uint32_t pm4[7];
    pm4[0] = PM4_NOP2;
    pm4[1] = PM4_HDR(IT_WRITE_DATA, 5);
    pm4[2] = 0x00000102;
    pm4[3] = REG_SCRATCH;
    pm4[4] = 0x00000000;
    pm4[5] = marker;
    pm4[6] = PM4_NOP2;
    uint32_t got = SeedScratch();
    SUBMIT_OUT so;
    PspSubmit(/* filled by caller */ 0, NULL, 0, &so); /* dummy - use below */
    (void)pm4; (void)got; (void)so;
}

static void RunTest(const char* label, uint32_t cmdId, uint32_t marker) {
    SUBMIT_OUT so;
    printf("\n  %s: cmd_id=0x%02X marker=0x%08X\n", label, cmdId, marker);

    /* Reset SCRATCH to SEED before this test */
    uint32_t before = SeedScratch();
    printf("  SCRATCH before: 0x%08X (expect 0x%08X)\n", before, SEED);

    /* Build PM4 WRITE_DATA */
    uint32_t pm4[7];
    pm4[0] = PM4_NOP2;
    pm4[1] = PM4_HDR(IT_WRITE_DATA, 5);
    pm4[2] = 0x00000102;
    pm4[3] = REG_SCRATCH;
    pm4[4] = 0x00000000;
    pm4[5] = marker;
    pm4[6] = PM4_NOP2;

    int r = PspSubmit(cmdId, pm4, sizeof(pm4), &so);
    const char* respName =
        so.RespStatus == 0x00000000 ? "SUCCESS" :
        so.RespStatus == 0x00000006 ? "BAD_PARAMS" :
        so.RespStatus == 0x00000100 ? "UNKNOWN_CMD" :
        so.RespStatus == 0x00000117 ? "NOT_SUPPORTED" : "other";
    printf("  PSP Result=%u Fence=%u RespStatus=0x%08X (%s)\n",
           so.Result, so.FenceStatus, so.RespStatus, respName);
    printf("  WPTR: 0x%08X\n", ReadReg(C2PMSG_67));

    uint32_t after = ReadReg(REG_SCRATCH);
    printf("  SCRATCH after:  0x%08X\n", after);
    printf("  VERDICT: %s\n", Verdict(marker, after));
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== PM4 via PSP Ring — DEFINITIVE TEST ===\n");
    printf("Method: CPU writes SCRATCH=0x%08X, PSP submits PM4 WRITE_DATA,\n", SEED);
    printf("        if SCRATCH changes to marker, PM4 executed on GPU.\n\n");

    g_hDev = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
                          0, NULL, OPEN_EXISTING, 0, NULL);
    if (g_hDev == INVALID_HANDLE_VALUE) {
        printf("FAIL: CreateFile gle=%lu\n", GetLastError()); return 1;
    }
    printf("[1] InitHardware: %s\n", InitHardware() ? "OK" : "FAIL");
    printf("[2] GPU_ID=0x%08X SCRATCH=0x%08X\n", ReadReg(0x0000), ReadReg(REG_SCRATCH));

    /* Verify CPU write path works */
    printf("\n[3] Verify CPU WRITE_REG path\n");
    uint32_t v = SeedScratch();
    printf("  SCRATCH = 0x%08X (%s)\n", v, v == SEED ? "OK" : "FAIL!");

    /* Unhalt CP */
    printf("\n[4] Unhalt CP: ME_CNTL 0x%08X -> ", ReadReg(REG_CP_ME_CNTL));
    WriteReg(REG_CP_ME_CNTL, 0);
    printf("0x%08X (%s)\n", ReadReg(REG_CP_ME_CNTL),
           ReadReg(REG_CP_ME_CNTL) == 0 ? "UNHALTED" : "STILL HALTED");

    /* PSP Ring Init */
    printf("\n[5] PSP_RING_INIT\n");
    uint32_t flags = 0;
    UCHAR ri_out[24] = {0};
    BOOL ok = Ioctl(IOCTL_PSP_RING_INIT, &flags, sizeof(flags), ri_out, sizeof(ri_out));
    printf("  Result=%u RingPa=0x%llX Size=0x%X C2PMSG_64=0x%08X C2PMSG_81=0x%08X\n",
           *(uint32_t*)(ri_out+0), *(uint64_t*)(ri_out+4),
           *(uint32_t*)(ri_out+12), *(uint32_t*)(ri_out+16), *(uint32_t*)(ri_out+20));
    printf("  WPTR: 0x%08X\n", ReadReg(C2PMSG_67));

    /* Control: GET_FW_ATTESTATION */
    printf("\n[6] Control: GET_FW_ATTESTATION (must succeed)\n");
    SUBMIT_OUT so;
    PspSubmit(0x0F, NULL, 0, &so);
    printf("  RespStatus=0x%08X (%s)\n", so.RespStatus,
           so.RespStatus == 0 ? "SUCCESS" : "FAIL");

    /* --- The actual tests --- */
    RunTest("cmd_id=0x01 INIT_GPCOM",      0x01, 0xAA55AA55);
    RunTest("cmd_id=0x02 INIT_GPCOM_RESP",  0x02, 0xDEADBEEF);
    RunTest("cmd_id=0x03 LOAD_IP_FW",       0x03, 0xBB66BB66);

    /* Final state */
    printf("\n[9] Final state\n");
    printf("  GRBM_STATUS = 0x%08X\n", ReadReg(REG_GRBM_STATUS));
    printf("  SCRATCH     = 0x%08X\n", ReadReg(REG_SCRATCH));
    printf("  CP_ME_CNTL  = 0x%08X\n", ReadReg(REG_CP_ME_CNTL));
    printf("  SPI_PG      = 0x%08X\n", ReadReg(REG_SPI_PG));

    CloseHandle(g_hDev);
    printf("\n=== Done ===\n");
    return 0;
}
