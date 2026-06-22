/* psp-pm4-submit-test.c — Test GPU PM4 execution via PSP KIQ ring */
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

static FILE *g = NULL;
static void Log(const char *fmt, ...) {
    va_list a; va_start(a, fmt); vfprintf(stdout, fmt, a); va_end(a);
    if (g) { va_start(a, fmt); vfprintf(g, fmt, a); va_end(a); }
}

/* PSP driver IOCTLs — CTL_CODE(FILE_DEVICE_UNKNOWN=0x22, func, 0, 0) */
#define PSP_DEVICE_NAME         L"\\\\.\\AmdBcPsp"
#define IOCTL_PSP_INIT_HW       0x0022200C
#define IOCTL_PSP_KIQ_SUBMIT    0x00222060
#define IOCTL_PSP_GPU_PM4_SUBMIT CTL_CODE(FILE_DEVICE_UNKNOWN, 0x825, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PSP_READ_REG      CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PSP_WRITE_REG     CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* GPU driver IOCTLs — CTL_CODE_AMDBC250: CTL_CODE(0x8000, 0x270+func, 0, 0) */
#define GPU_DEVICE_NAME         L"\\\\.\\AMDBC250DreamV43"
#define IOCTL_GPU_INIT_HW       0x80000B80
#define IOCTL_GPU_READ_REG      0x80000B88

typedef struct {
    ULONG CommandCount;
    ULONG Reserved;
    ULONG WaitMs;
    ULONG Commands[64];
} PM4_SUBMIT_REQ;

typedef struct {
    ULONG Status;
    ULONG ScratchBefore;
    ULONG ScratchAfter;
    ULONG WptrReadback;
    ULONG KiqRingWptr;
    ULONG KiqRingSize;
    ULONG KiqRingPa;
    ULONG HqdPqWptrBefore;
    ULONG HqdPqWptrAfter;
    ULONG HqdActive;
    ULONG Pm4Dwords;
} PM4_SUBMIT_RESP;

static BOOL GpuReadReg(HANDLE h, unsigned offset, unsigned *val) {
    unsigned ra[2] = {offset, 0xDEADBEEF};
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_GPU_READ_REG, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
    if (ok) *val = ra[1]; else *val = 0xDEADBEEF;
    return ok;
}

static BOOL PspReadReg(HANDLE h, unsigned offset, unsigned *val) {
    unsigned ra[2] = {offset, 0};
    unsigned resp[2] = {0, 0};
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_PSP_READ_REG, ra, sizeof(ra), resp, sizeof(resp), &br, NULL);
    if (ok && val) *val = resp[0];
    return ok;
}

static BOOL PspWriteReg(HANDLE h, unsigned offset, unsigned val) {
    unsigned ra[2] = {offset, val};
    DWORD br = 0;
    return DeviceIoControl(h, IOCTL_PSP_WRITE_REG, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
}

static void DumpRegs(HANDLE h, const char *tag) {
    unsigned val;
    Log("--- %s ---\n", tag);
    PspReadReg(h, 0x32D4, &val); Log("  SCRATCH (0x32D4)        = 0x%08X\n", val);
    PspReadReg(h, 0xDAC0, &val); Log("  HQD_ACTIVE (0xDAC0)     = 0x%08X\n", val);
    PspReadReg(h, 0xDAD8, &val); Log("  HQD_PQ_BASE (0xDAD8)    = 0x%08X\n", val);
    PspReadReg(h, 0xDADC, &val); Log("  HQD_PQ_BASE_HI (0xDADC) = 0x%08X\n", val);
    PspReadReg(h, 0xE060, &val); Log("  KIQ_BASE_LO (0xE060)    = 0x%08X\n", val);
    PspReadReg(h, 0xE064, &val); Log("  KIQ_BASE_HI (0xE064)    = 0x%08X\n", val);
    PspReadReg(h, 0xE078, &val); Log("  KIQ_WPTR (0xE078)       = 0x%08X\n", val);
    PspReadReg(h, 0x4A74, &val); Log("  ME_CNTL (0x4A74)        = 0x%08X\n", val);
    PspReadReg(h, 0x66C0, &val); Log("  GCVM_CTX0_CNTL (0x66C0) = 0x%08X\n", val);
    PspReadReg(h, 0x6C8C, &val); Log("  GCVM_PT_BASE_LO (0x6C8C)= 0x%08X\n", val);
    PspReadReg(h, 0x6C90, &val); Log("  GCVM_PT_BASE_HI (0x6C90)= 0x%08X\n", val);
    PspReadReg(h, 0xB360, &val); Log("  GCVM_L2_CNTL (0xB360)   = 0x%08X\n", val);
    PspReadReg(h, 0x3264, &val); Log("  GRBM_STATUS (0x3264)    = 0x%08X\n", val);
}

int main(void) {
    g = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\psp-pm4-submit.log", "w");
    Log("=== PSP GPU PM4 Submit Test ===\n\n");

    /* === Step 1: Open PSP device === */
    HANDLE hPsp = CreateFileW(PSP_DEVICE_NAME, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hPsp == INVALID_HANDLE_VALUE) {
        Log("Cannot open PSP device error=%lu\n", GetLastError());
        if (g) fclose(g); return 1;
    }
    Log("PSP device opened (handle=%p)\n", hPsp);

    /* === Step 2: Init PSP HW — map GPU BAR5 === */
    {
        struct { unsigned __int64 PA; unsigned size; } req;
        req.PA = 0xFE800000ULL;
        req.size = 0x00080000;
        DWORD br = 0;
        BOOL ok = DeviceIoControl(hPsp, IOCTL_PSP_INIT_HW, &req, sizeof(req), NULL, 0, &br, NULL);
        Log("PSP INIT_HW (GPU BAR5): ok=%d error=%lu\n", ok, GetLastError());
    }

    /* === Step 3: Read current state via PSP proxy === */
    Log("\n--- Step 3: Current state via PSP proxy ---\n");
    DumpRegs(hPsp, "Before PM4");

    /* === Step 4: Write test — can we write SCRATCH? === */
    Log("\n--- Step 4: SCRATCH write test ---\n");
    {
        unsigned before, after;
        PspReadReg(hPsp, 0x32D4, &before);
        Log("  SCRATCH before write = 0x%08X\n", before);
        PspWriteReg(hPsp, 0x32D4, 0x11111111);
        PspReadReg(hPsp, 0x32D4, &after);
        Log("  SCRATCH after write  = 0x%08X\n", after);
        if (after == 0x11111111) {
            Log("  SCRATCH is WRITABLE\n");
        } else {
            Log("  SCRATCH is NOT writable (hardware-locked or wrong)\n");
        }
        /* Restore */
        PspWriteReg(hPsp, 0x32D4, before);
    }

    /* === Step 5: Submit PM4 via PSP KIQ === */
    Log("\n--- Step 5: PM4 submit via PSP KIQ ---\n");
    {
        PM4_SUBMIT_REQ req;
        PM4_SUBMIT_RESP resp;
        DWORD br = 0;
        RtlZeroMemory(&req, sizeof(req));
        RtlZeroMemory(&resp, sizeof(resp));

        /* IT_WRITE_DATA to SCRATCH = 0xCAFEBABE */
        req.CommandCount = 4;
        req.WaitMs = 100;
        req.Commands[0] = 0xC0023700;  /* PM4 header: IT_WRITE_DATA */
        req.Commands[1] = 0x00000001;  /* control: ENGINE_ME, DST_MEMORY */
        req.Commands[2] = 0x000032D4;  /* SCRATCH register offset */
        req.Commands[3] = 0xCAFEBABE;  /* value to write */

        BOOL ok = DeviceIoControl(hPsp, IOCTL_PSP_GPU_PM4_SUBMIT, &req, sizeof(req), &resp, sizeof(resp), &br, NULL);
        Log("IOCTL_PSP_GPU_PM4_SUBMIT: ok=%d error=%lu\n", ok, GetLastError());

        if (ok) {
            Log("  Status          = 0x%08X\n", resp.Status);
            Log("  ScratchBefore   = 0x%08X\n", resp.ScratchBefore);
            Log("  ScratchAfter    = 0x%08X\n", resp.ScratchAfter);
            Log("  HqdActive       = 0x%08X\n", resp.HqdActive);
            Log("  HqdPqWptrBefore = %u\n", resp.HqdPqWptrBefore);
            Log("  HqdPqWptrAfter  = %u\n", resp.HqdPqWptrAfter);
            Log("  WptrReadback    = 0x%08X\n", resp.WptrReadback);
            Log("  KiqRingWptr     = %u\n", resp.KiqRingWptr);
            Log("  KiqRingSize     = %u bytes\n", resp.KiqRingSize);
            Log("  KiqRingPa       = 0x%08X\n", resp.KiqRingPa);
            Log("  Pm4Dwords       = %u\n", resp.Pm4Dwords);

            if (resp.ScratchAfter == 0xCAFEBABE) {
                Log("\n  *** SUCCESS: GPU executed PM4 via PSP KIQ! SCRATCH=0xCAFEBABE ***\n");
            } else if (resp.ScratchAfter != resp.ScratchBefore) {
                Log("\n  SCRATCH changed (0x%08X -> 0x%08X) but not to CAFEBABE\n",
                    resp.ScratchBefore, resp.ScratchAfter);
            } else {
                Log("\n  SCRATCH unchanged — GPU did NOT execute PM4\n");
            }
        } else {
            Log("  PM4 submit FAILED (err=%lu)\n", GetLastError());
        }
    }

    /* === Step 6: Try different PM4 commands === */
    Log("\n--- Step 6: PM4 with SELECT_EN=1 ---\n");
    {
        PM4_SUBMIT_REQ req;
        PM4_SUBMIT_RESP resp;
        DWORD br = 0;
        RtlZeroMemory(&req, sizeof(req));
        RtlZeroMemory(&resp, sizeof(resp));
        req.CommandCount = 4;
        req.WaitMs = 100;
        req.Commands[0] = 0xC0023700;  /* IT_WRITE_DATA */
        req.Commands[1] = 0x00010001;  /* control: ENGINE_ME=1, DST_MEMORY=1, SELECT_EN=1 */
        req.Commands[2] = 0x000032D4;  /* SCRATCH register offset */
        req.Commands[3] = 0xDEADBEEF;  /* value to write */

        BOOL ok = DeviceIoControl(hPsp, IOCTL_PSP_GPU_PM4_SUBMIT, &req, sizeof(req), &resp, sizeof(resp), &br, NULL);
        if (ok) {
            Log("  ScratchBefore=0x%08X ScratchAfter=0x%08X HqdActive=0x%08X\n",
                resp.ScratchBefore, resp.ScratchAfter, resp.HqdActive);
            if (resp.ScratchAfter == 0xDEADBEEF)
                Log("  *** SUCCESS ***\n");
            else
                Log("  SCRATCH unchanged\n");
        } else {
            Log("  FAILED (err=%lu)\n", GetLastError());
        }
    }

    /* === Step 7: Final state dump === */
    DumpRegs(hPsp, "After PM4 attempts");

    CloseHandle(hPsp);
    Log("\n=== Done ===\n");
    if (g) fclose(g);
    printf("Done. Check output\\psp-pm4-submit.log\n");
    return 0;
}
