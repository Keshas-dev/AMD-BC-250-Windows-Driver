/* psp-gpu-pm4-submit-test.c — Test GPU PM4 execution via PSP KIQ ring */
/* Uses the correct IOCTL_PSP_GPU_PM4_SUBMIT (0x825) with proper request/response structures */

#include <windows.h>
#include <stdio.h>
#include <stdint.h>

#define PSP_DEVICE_NAME         L"\\\\.\\AmdBcPsp"
#define PSP_IOCTL_READ_REG      CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define PSP_IOCTL_WRITE_REG     CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define PSP_IOCTL_INIT_HW       CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define PSP_IOCTL_GPU_PM4_SUBMIT CTL_CODE(FILE_DEVICE_UNKNOWN, 0x825, METHOD_BUFFERED, FILE_ANY_ACCESS)

// Structures from PspIoctl.h
typedef struct _PSP_GPU_PM4_SUBMIT_REQUEST {
    ULONG CommandCount;      // Number of PM4 DWORDs (max 64)
    ULONG Reserved;          // Alignment padding
    ULONG WaitMs;            // Wait time in ms after kick (0 = no wait)
    ULONG Commands[64];      // PM4 commands
} PSP_GPU_PM4_SUBMIT_REQUEST, *PPSP_GPU_PM4_SUBMIT_REQUEST;

typedef struct _PSP_GPU_PM4_SUBMIT_RESPONSE {
    ULONG Status;            // NTSTATUS
    ULONG ScratchBefore;     // SCRATCH (0x32D4) before PM4
    ULONG ScratchAfter;      // SCRATCH (0x32D4) after wait
    ULONG WptrReadback;      // WPTR readback after kick
    ULONG KiqRingWptr;       // PSP KIQ ring WPTR
    ULONG KiqRingSize;       // PSP KIQ ring size
    ULONG KiqRingPa;         // PSP KIQ ring PA (low 32 bits)
    ULONG HqdPqWptrBefore;   // HQD_PQ_WPTR_LO before kick
    ULONG HqdPqWptrAfter;    // HQD_PQ_WPTR_LO after wait
    ULONG HqdActive;         // HQD_ACTIVE readback
    ULONG Pm4Dwords;         // Number of PM4 DWORDs written
} PSP_GPU_PM4_SUBMIT_RESPONSE, *PPSP_GPU_PM4_SUBMIT_RESPONSE;

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
    printf("=== PSP GPU PM4 Submit Test ===\n\n");

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
    PspReadReg(hPsp, 0xE06C, &val); printf("  KIQ_RPTR (0xE06C)        = %u\n", val);
    unsigned kiqRptrBefore = val;

    /* Step 4: Prepare PM4 commands */
    printf("\n--- Preparing PM4 Commands ---\n");
    PSP_GPU_PM4_SUBMIT_REQUEST req;
    PSP_GPU_PM4_SUBMIT_RESPONSE resp;
    
    // PM4 WRITE_DATA to SCRATCH (0x32D4) = 0xCAFEBABE
    // Correct format (Type-3): header(TYPE=3, OPCODE=0x37, DST_SEL=Register=0,
    //   COUNT=3) + DWORD1=register offset + DWORD2=data + DWORD3=NOP(pad)
    //   COUNT = number of DWORDs after the header = addr(1) + data(2)
    req.CommandCount = 4;     // 4 DWORDs: header + addr + data + NOP
    req.WaitMs = 100;         // Wait 100ms after kick
    req.Commands[0] = 0xC0033700;   // WRITE_DATA, dst=Register, count=3
    req.Commands[1] = 0x000032D4;   // SCRATCH register offset (DWORD1 = address)
    req.Commands[2] = 0xCAFEBABE;   // DATA
    req.Commands[3] = 0xC0001000;   // NOP (padding data DWORD)

    printf("Prepared %d DWORDs of PM4 commands\n", req.CommandCount);
    printf("  CMD[0] = 0x%08X (WRITE_DATA, Register, count=3)\n", req.Commands[0]);
    printf("  CMD[1] = 0x%08X (SCRATCH offset = 0x32D4)\n", req.Commands[1]);
    printf("  CMD[2] = 0x%08X (DATA = 0xCAFEBABE)\n", req.Commands[2]);
    printf("  CMD[3] = 0x%08X (NOP pad)\n", req.Commands[3]);

    /* Step 5: Submit via PSP GPU PM4 SUBMIT */
    printf("\n--- Submitting via PSP GPU PM4 SUBMIT ---\n");
    DWORD br = 0;
    BOOL ok = DeviceIoControl(hPsp, PSP_IOCTL_GPU_PM4_SUBMIT, &req, sizeof(req), &resp, sizeof(resp), &br, NULL);
    printf("PSP_IOCTL_GPU_PM4_SUBMIT: ok=%d error=%lu\n", ok, GetLastError());

    /* Step 6: Check results */
    printf("\n--- Results ---\n");
    printf("  Status            = 0x%08X\n", resp.Status);
    printf("  ScratchBefore     = 0x%08X\n", resp.ScratchBefore);
    printf("  ScratchAfter      = 0x%08X\n", resp.ScratchAfter);
    printf("  WptrReadback      = 0x%08X\n", resp.WptrReadback);
    printf("  KiqRingWptr       = %u\n", resp.KiqRingWptr);
    printf("  KiqRingSize       = %u bytes\n", resp.KiqRingSize);
    printf("  KiqRingPa         = 0x%08X\n", resp.KiqRingPa);
    printf("  HqdPqWptrBefore   = %u\n", resp.HqdPqWptrBefore);
    printf("  HqdPqWptrAfter    = %u\n", resp.HqdPqWptrAfter);
    printf("  HqdActive         = 0x%08X\n", resp.HqdActive);
    printf("  Pm4Dwords         = %u\n", resp.Pm4Dwords);

    /* Step 6b: Read KIQ_RPTR after to see if the CP CONSUMED the ring */
    unsigned kiqRptrAfter;
    PspReadReg(hPsp, 0xE06C, &kiqRptrAfter);
    printf("  KIQ_RPTR (0xE06C)    = %u  (before=%u; advanced => CP consumed ring)\n",
        kiqRptrAfter, kiqRptrBefore);

    if (resp.ScratchAfter == 0xCAFEBABE) {
        printf("\n  *** SUCCESS: GPU executed PM4 via PSP KIQ! SCRATCH=0xCAFEBABE ***\n");
    } else if (resp.ScratchAfter != resp.ScratchBefore) {
        printf("\n  SCRATCH changed (0x%08X -> 0x%08X) but not to CAFEBABE\n",
               resp.ScratchBefore, resp.ScratchAfter);
    } else {
        printf("\n  SCRATCH unchanged — GPU did NOT execute PM4\n");
    }

    /* Step 7: Cleanup */
    CloseHandle(hPsp);
    printf("\n=== Done ===\n");
    return 0;
}