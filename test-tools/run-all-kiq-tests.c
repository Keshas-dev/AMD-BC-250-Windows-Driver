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

    /* Init HW (NBIO_MAP) */
    AMDBC250_IOCTL_INIT_HARDWARE ih;
    memset(&ih, 0, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL; ih.MmioSize = 0x80000; ih.Flags = 1;
    if (!DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &ret, NULL)) {
        printf("Init FAILED err=%lu\n", GetLastError());
        CloseHandle(h);
        return 1;
    }
    printf("Init OK (FB=0x%llX)\n\n", ih.FbPhysicalBase);

    /* 1) GPU_IB_TEST (0x89) — same struct as GPU_KIQ_TEST */
    printf("=== IB TEST ===\n");
    AMDBC250_IOCTL_GPU_KIQ_TEST ibt;
    memset(&ibt, 0, sizeof(ibt));
    if (DeviceIoControl(h, IOCTL_AMDBC250_GPU_IB_TEST, &ibt, sizeof(ibt), &ibt, sizeof(ibt), &ret, NULL)) {
        printf("Result=0x%08X Scratch:0x%08X->0x%08X HqdProg=%u FwLoad=%u\n",
            ibt.Result, ibt.ScratchBefore, ibt.ScratchAfter, ibt.HqdProgrammed, ibt.FwLoaded);
    } else {
        printf("IB_TEST IOCTL FAILED err=%lu\n", GetLastError());
    }

    /* 2) EXECUTE_RING_PM4 (0x8A) */
    printf("\n=== EXECUTE_RING_PM4 ===\n");
    AMDBC250_IOCTL_EXECUTE_RING_PM4 pm4;
    memset(&pm4, 0, sizeof(pm4));
    /* Minimal PM4: WRITE_DATA to scratch */
    pm4.Commands[0] = 0xC0033700;  /* IT_WRITE_DATA, count=3 */
    pm4.Commands[1] = 0x00100000;  /* reg + WR_CONFIRM */
    pm4.Commands[2] = 0x000032D4;  /* SCRATCH addr */
    pm4.Commands[3] = 0x5AFEBABE;  /* data */
    pm4.CommandCount = 4;
    pm4.TimeoutMs = 100;
    if (DeviceIoControl(h, IOCTL_AMDBC250_EXECUTE_RING_PM4, &pm4, sizeof(pm4), &pm4, sizeof(pm4), &ret, NULL)) {
        printf("Result=%u Scratch:0x%08X->0x%08X HqdActive=0x%08X\n",
            pm4.Result, pm4.ScratchBefore, pm4.ScratchAfter, pm4.HqdActive);
        printf("Wptr:0x%08X->0x%08X Rptr:0x%08X->0x%08X\n",
            pm4.WptrBefore, pm4.WptrAfter, pm4.RptrBefore, pm4.RptrAfter);
        printf("RingPa=0x%llX MqdPa=0x%llX\n", pm4.RingPa, pm4.MqdPa);
        printf("PQ_Ctrl:0x%08X->0x%08X PQ_BaseRb=0x%08X\n",
            pm4.PqCtrlBefore, pm4.PqCtrlAfter, pm4.PqBaseReadback);
        printf("SwResult=%u SmuFeatures=0x%08X GfxFreq=%u\n",
            pm4.SwResult, pm4.SmuFeaturesMask, pm4.SmuGfxFreqMhz);
        printf("Dispatch: res=%u Grbm:0x%08X->0x%08X\n",
            pm4.DispatchResult, pm4.GrbmStatusBefore, pm4.GrbmStatusAfter);
        printf("MQD load: PGM_LO=0x%08X PgmRb=0x%08X PgmHiRb=0x%08X TmgRb=0x%08X\n",
            pm4.MqdLoadPgmLo, pm4.PgmLoReadback, pm4.PgmHiReadback, pm4.TmgMaskReadback);
        printf("RingDwords[0..3]: 0x%08X 0x%08X 0x%08X 0x%08X\n",
            pm4.RingDwords[0], pm4.RingDwords[1], pm4.RingDwords[2], pm4.RingDwords[3]);
    } else {
        printf("EXECUTE_RING_PM4 FAILED err=%lu\n", GetLastError());
    }

    CloseHandle(h);
    return 0;
}
