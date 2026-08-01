#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include "..\inc\amdbc250_ioctl.h"

int main(void) {
    HANDLE h = CreateFileW(L"\\\\.\\AMDBC250DreamV43", GENERIC_READ|GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("Cannot open GPU (err=%lu)\n", GetLastError());
        return 1;
    }
    DWORD ret;
    /* Init HW first */
    AMDBC250_IOCTL_INIT_HARDWARE ih;
    memset(&ih, 0, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL;
    ih.MmioSize = 0x80000;
    ih.Flags = 1;
    if (!DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &ret, NULL)) {
        printf("InitHw FAILED err=%lu\n", GetLastError());
        CloseHandle(h);
        return 1;
    }
    printf("=== KIQ TEST ===\n");

    /* KIQ mode (UseIB=0) */
    AMDBC250_IOCTL_GPU_KIQ_TEST kiq;
    memset(&kiq, 0, sizeof(kiq));
    if (DeviceIoControl(h, IOCTL_AMDBC250_GPU_KIQ_TEST, &kiq, sizeof(kiq), &kiq, sizeof(kiq), &ret, NULL)) {
        printf("KIQ: Result=0x%08X FwLoaded=%u RingAlloc=%u HqdProg=%u MecUnhalt=%u\n",
            kiq.Result, kiq.FwLoaded, kiq.RingAllocated, kiq.HqdProgrammed, kiq.MecUnhalted);
        printf("    DoorKick=%u Pm4Sub=%u Scratch:0x%08X->0x%08X->0x%08X\n",
            kiq.DoorKicked, kiq.Pm4Submitted, kiq.ScratchBefore, kiq.ScratchAfter, kiq.ScratchAfter2);
        printf("    HqdRptr=0x%08X KiQ_RP=0x%08X GrbmStat=0x%08X\n",
            kiq.HqdRptr, kiq.KiQ_RP, kiq.GrbmStat);
        printf("    HqdRptr2=0x%08X KiQ_RP2=0x%08X GrbmStat2=0x%08X\n",
            kiq.HqdRptr2, kiq.KiQ_RP2, kiq.GrbmStat2);
        printf("    RingGpuVa=0x%llX FbBase=0x%08X\n", kiq.RingGpuVa, kiq.FbLocationBase);
        printf("    HqdPqWptrRb=0x%08X DbLo=0x%08X DbHi=0x%08X DbCtl=0x%08X\n",
            kiq.HqdPqWptrRb, kiq.DbLoRb, kiq.DbHiRb, kiq.DbCtlRb);
    } else {
        printf("KIQ IOCTL FAILED err=%lu\n", GetLastError());
    }

    /* IB mode (UseIB=1) */
    printf("\n--- IB mode ---\n");
    AMDBC250_IOCTL_GPU_KIQ_TEST ib;
    memset(&ib, 0, sizeof(ib));
    ib.UseIB = 1;
    if (DeviceIoControl(h, IOCTL_AMDBC250_GPU_KIQ_TEST, &ib, sizeof(ib), &ib, sizeof(ib), &ret, NULL)) {
        printf("IB:  Result=0x%08X Scratch:0x%08X->0x%08X HqdProg=%u FwLoaded=%u\n",
            ib.Result, ib.ScratchBefore, ib.ScratchAfter, ib.HqdProgrammed, ib.FwLoaded);
    } else {
        printf("IB IOCTL FAILED err=%lu\n", GetLastError());
    }

    CloseHandle(h);
    return 0;
}
