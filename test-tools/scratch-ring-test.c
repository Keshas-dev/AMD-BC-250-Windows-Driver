/*++
   scratch-ring-test.c — CP-level SCRATCH ring test (user-mode, SAFE)

   Directly answers: "Can the Command Processor submit + execute a PM4
   packet on BC-250?" This is UPSTREAM of the WGP shader fuse
   (SET_UCONFIG_REG -> SCRATCH is executed by the CP, not shader cores).

   Reuses the existing kernel IOCTL_GPU_KIQ_TEST path (which already
   does the full KIQ HQD setup + a SCRATCH write + poll). ZERO new
   kernel MMIO — no freeze/hang risk from this tool.

   Build: cl.exe /nologo scratch-ring-test.c /Fe=..\output\scratch-ring-test.exe
--*/

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "..\\inc\\amdbc250_ioctl.h"

static HANDLE OpenGpu(void) {
    return CreateFileW(L"\\\\.\\AMDBC250DreamV43",
        GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
}

int main(void) {
    HANDLE h = OpenGpu();
    if (h == INVALID_HANDLE_VALUE) {
        printf("cannot open GPU driver \\\\.\\AMDBC250DreamV43 (err=%lu)\n", GetLastError());
        return 1;
    }

    /* 1. INIT_HARDWARE (NBIO_MAP) — required on Win11 26100 WDM fallback */
    AMDBC250_IOCTL_INIT_HARDWARE ih;
    DWORD br = 0;
    ZeroMemory(&ih, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL;
    ih.MmioSize        = 0x80000;
    ih.Flags             = AMDBC250_INIT_FLAG_NBIO_MAP;
    if (!DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE,
                         &ih, sizeof(ih), &ih, sizeof(ih), &br, NULL)) {
        printf("INIT_HARDWARE failed (err=%lu) — register access may not work\n", GetLastError());
    } else {
        printf("INIT_HARDWARE OK (MmioBase=0x%llx size=0x%x)\n", ih.MmioPhysicalBase, ih.MmioSize);
    }

    /* 2. GPU KIQ / CP-level SCRATCH ring test */
    AMDBC250_IOCTL_GPU_KIQ_TEST kt;
    ZeroMemory(&kt, sizeof(kt));
    kt.UseIB = 0;
    if (!DeviceIoControl(h, IOCTL_AMDBC250_GPU_KIQ_TEST,
                         &kt, sizeof(kt), &kt, sizeof(kt), &br, NULL)) {
        printf("GPU_KIQ_TEST failed (err=%lu)\n", GetLastError());
        CloseHandle(h);
        return 1;
    }

    printf("\n=== GPU KIQ / CP-level SCRATCH ring test ===\n");
    printf("  Result          = %u   (0 = fail)\n", kt.Result);
    printf("  MmioMapped     = %u\n", kt.MmioMapped);
    printf("  FwLoaded       = %u\n", kt.FwLoaded);
    printf("  RingAllocated  = %u\n", kt.RingAllocated);
    printf("  HqdProgrammed = %u\n", kt.HqdProgrammed);
    printf("  Pm4Submitted   = %u\n", kt.Pm4Submitted);
    printf("  MecUnhalted   = %u\n", kt.MecUnhalted);
    printf("  ScratchBefore  = 0x%08X\n", kt.ScratchBefore);
    printf("  ScratchAfter   = 0x%08X  <-- CP executes SCRATCH write if != Before\n", kt.ScratchAfter);
    printf("  ScratchAfter2  = 0x%08X\n", kt.ScratchAfter2);
    printf("  HqdRptr        = 0x%08X (HW-consumed ptr)\n", kt.HqdRptr);
    printf("  KiQ_RP         = 0x%08X\n", kt.KiQ_RP);
    printf("  GrbmStat       = 0x%08X\n", kt.GrbmStat);
    printf("  HqdPqWptrRb   = 0x%08X (CP view of WPTR)\n", kt.HqdPqWptrRb);
    printf("  DoorKicked     = %u\n", kt.DoorKicked);

    if (kt.ScratchBefore != kt.ScratchAfter) {
        printf("\n  >>> SCRATCH CHANGED: CP/PM4 submission WORKS (ring executes).\n");
        printf("      Shader COMPUTE still needs WGPs, but this proves the CP is alive.\n");
    } else {
        printf("\n  >>> SCRATCH UNCHANGED: CP did NOT execute the ring.\n");
        printf("      If RlcResumeEnabled=0, set it=1 + rebuild (see notes). Else CP is dead on this unit.\n");
    }
    CloseHandle(h);
    return 0;
}
