/* psp-kIQ-pm4-test.c — Test GPU PM4 execution via PSP KIQ ring using existing IOCTL_PSP_KIQ_SUBMIT */
/* Uses the existing IOCTL_PSP_KIQ_SUBMIT (0x818) with PSP_KIQ_SUBMIT_REQUEST structure */

#include <windows.h>
#include <stdio.h>

#define PSP_DEVICE_NAME         L"\\\\.\\AmdBcPsp"
#define PSP_IOCTL_READ_REG      CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define PSP_IOCTL_WRITE_REG     CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define PSP_IOCTL_INIT_HW       CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define PSP_IOCTL_KIQ_SUBMIT    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x818, METHOD_BUFFERED, FILE_ANY_ACCESS) /* KIQ ring submit */

// Structure from PspIoctl.h
typedef struct _PSP_KIQ_SUBMIT_REQUEST {
    ULONG CommandCount;      // Number of PM4 DWORDs (max 64)
    ULONG Reserved[3];       // Alignment padding
    ULONG Commands[64];      // PM4 commands
} PSP_KIQ_SUBMIT_REQUEST, *PPSP_KIQ_SUBMIT_REQUEST;

static BOOL PspReadReg(HANDLE h, unsigned offset, unsigned *val) {
    unsigned ra[2] = {offset, 0};
    unsigned resp[2] = {0, 0};
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, PSP_IOCTL_READ_REG, ra, sizeof(ra), resp, sizeof(resp), &br, NULL);
    if (ok && val) *val = resp[0];
    return ok;
}

static BOOL PspWriteReg(HANDLE h, unsigned offset, unsigned val) {
    unsigned ra[2] = {offset, val};
    DWORD br = 0;
    return DeviceIoControl(h, PSP_IOCTL_WRITE_REG, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
}

int main(void) {
    printf("=== PSP KIQ PM4 Submit Test (detailed diagnostics) ===\n\n");

    /* Step 1: Open PSP device */
    HANDLE hPsp = CreateFileW(PSP_DEVICE_NAME, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hPsp == INVALID_HANDLE_VALUE) {
        printf("Cannot open PSP device error=%lu\n", GetLastError());
        return 1;
    }
    printf("PSP device opened (handle=%p)\n", hPsp);

    /* Step 2: Init PSP HW (map GPU BAR5) */
    {
        struct { unsigned __int64 PA; unsigned size; } req;
        req.PA = 0xFE800000ULL;
        req.size = 0x00080000;
        DWORD br = 0;
        BOOL ok = DeviceIoControl(hPsp, PSP_IOCTL_INIT_HW, &req, sizeof(req), NULL, 0, &br, NULL);
        printf("PSP INIT_HW (GPU BAR5): ok=%d error=%lu\n", ok, GetLastError());
    }

    /* Step 3: Read initial state */
    printf("\n--- Initial State ---\n");
    unsigned val;
    PspReadReg(hPsp, 0x32D4, &val); printf("  SCRATCH (0x32D4)        = 0x%08X\n", val);
    PspReadReg(hPsp, 0x4A74, &val); printf("  ME_CNTL (0x4A74)        = 0x%08X\n", val);
    PspReadReg(hPsp, 0xE060, &val); printf("  KIQ_BASE_LO (0xE060)    = 0x%08X\n", val);
    PspReadReg(hPsp, 0xE064, &val); printf("  KIQ_BASE_HI (0xE064)    = 0x%08X\n", val);
    PspReadReg(hPsp, 0xE06C, &val); printf("  KIQ_RPTR (0xE06C)       = 0x%08X\n", val);
    PspReadReg(hPsp, 0xE078, &val); printf("  KIQ_WPTR (0xE078)       = 0x%08X\n", val);
    PspReadReg(hPsp, 0xDAC0, &val); printf("  HQD_ACTIVE (0xDAC0)     = 0x%08X\n", val);
    PspReadReg(hPsp, 0xDAD8, &val); printf("  HQD_PQ_BASE_LO (0xDAD8) = 0x%08X\n", val);
    PspReadReg(hPsp, 0xDADC, &val); printf("  HQD_PQ_BASE_HI (0xDADC) = 0x%08X\n", val);

    /* Step 4: Unhalt GPU */
    printf("\n--- Unhalt GPU ---\n");
    PspReadReg(hPsp, 0x4A74, &val); printf("  ME_CNTL before unhalt = 0x%08X\n", val);
    // Clear halt bits: ME_HALT(bit28), PFP_HALT(bit30)
    val &= ~((1u << 28) | (1u << 30));
    PspWriteReg(hPsp, 0x4A74, val);
    PspReadReg(hPsp, 0x4A74, &val); printf("  ME_CNTL after unhalt  = 0x%08X\n", val);

    /* Step 5: Prepare PM4 commands for KIQ submit */
    printf("\n--- Preparing PM4 Commands for KIQ Submit ---\n");
    PSP_KIQ_SUBMIT_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.CommandCount = 5;     // 5 DWORDs: header + control + addr_lo + addr_hi + data
    
    // PM4 IT_WRITE_DATA to SCRATCH (0x32D4) = 0xCAFEBABE
    // Header: TYPE=3(11), COUNT=3, OPCODE=0x37 -> 0xC0370003
    req.Commands[0] = 0xC0370003;   // PM4: IT_WRITE_DATA (count=3)
    req.Commands[1] = 0x10100000;   // CONTROL: DST_SEL=register, WR_CONFIRM
    req.Commands[2] = 0x000032D4;   // ADDR_LO = SCRATCH register byte offset
    req.Commands[3] = 0x00000000;   // ADDR_HI
    req.Commands[4] = 0xCAFEBABE;   // DATA
    
    printf("Prepared %d DWORDs of PM4 commands\n", req.CommandCount);
    printf("  CMD[0] = 0x%08X (IT_WRITE_DATA)\n", req.Commands[0]);
    printf("  CMD[1] = 0x%08X (CONTROL)\n", req.Commands[1]);
    printf("  CMD[2] = 0x%08X (SCRATCH offset = 0x32D4)\n", req.Commands[2]);
    printf("  CMD[3] = 0x%08X (ADDR_HI = 0)\n", req.Commands[3]);
    printf("  CMD[4] = 0x%08X (DATA = 0xCAFEBABE)\n", req.Commands[4]);

    /* Step 6: Submit via PSP KIQ */
    printf("\n--- Submitting via PSP KIQ (IOCTL_PSP_KIQ_SUBMIT) ---\n");
    DWORD br = 0;
    BOOL ok = DeviceIoControl(hPsp, PSP_IOCTL_KIQ_SUBMIT, &req, sizeof(req), NULL, 0, &br, NULL);
    printf("PSP_IOCTL_KIQ_SUBMIT: ok=%d error=%lu\n", ok, GetLastError());

    /* Step 7: Check results */
    printf("\n--- Results ---\n");
    PspReadReg(hPsp, 0x32D4, &val); printf("  SCRATCH (0x32D4)        = 0x%08X\n", val);
    PspReadReg(hPsp, 0xE06C, &val); printf("  KIQ_RPTR (0xE06C)       = 0x%08X\n", val);
    PspReadReg(hPsp, 0xE078, &val); printf("  KIQ_WPTR (0xE078)       = 0x%08X\n", val);
    PspReadReg(hPsp, 0xDAC0, &val); printf("  HQD_ACTIVE (0xDAC0)     = 0x%08X\n", val);
    PspReadReg(hPsp, 0xDAD8, &val); printf("  HQD_PQ_BASE_LO (0xDAD8) = 0x%08X\n", val);
    PspReadReg(hPsp, 0xDADC, &val); printf("  HQD_PQ_BASE_HI (0xDADC) = 0x%08X\n", val);
    PspReadReg(hPsp, 0xDB90, &val); printf("  HQD_PQ_WPTR_LO (0xDB90) = 0x%08X\n", val);
    PspReadReg(hPsp, 0xDB94, &val); printf("  HQD_PQ_WPTR_HI (0xDB94) = 0x%08X\n", val);
    
    if (val == 0xCAFEBABE) {
        printf("  *** SUCCESS: GPU executed PM4 via PSP KIQ! SCRATCH=0xCAFEBABE ***\n");
    } else {
        printf("  GPU did not execute PM4 (SCRATCH unchanged)\n");
    }

    /* Step 8: Cleanup */
    CloseHandle(hPsp);
    printf("\n=== Done ===\n");
    return 0;
}