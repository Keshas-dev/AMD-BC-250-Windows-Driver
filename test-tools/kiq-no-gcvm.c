/* kiq-no-gcvm.c — KIQ submit WITHOUT replacing PT_BASE, use BIOS page tables */
#include <windows.h>
#include <stdio.h>

static BOOL ReadReg(HANDLE h, unsigned offset, unsigned *val) {
    unsigned ra[2] = {offset, 0xDEADBEEF};
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, 0x80000B88, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
    if (ok) *val = ra[1]; else *val = 0xDEADBEEF;
    return ok;
}

static BOOL WriteReg(HANDLE h, unsigned offset, unsigned val) {
    unsigned ra[2] = {offset, val};
    DWORD br = 0;
    return DeviceIoControl(h, 0x80000B8C, ra, sizeof(ra), ra, sizeof(ra), &br, NULL);
}

int main(void) {
    printf("=== KIQ No-GCVM Test ===\n");
    printf("Using BIOS page tables (PT_BASE=0x7E510000)\n");

    HANDLE h = CreateFileW(L"\\\\.\\AMDBC250DreamV43",
        GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("Cannot open device error=%lu\n", GetLastError());
        return 1;
    }

    /* INIT_HARDWARE NBIO_MAP */
    UCHAR initIn[32] = {0}; DWORD br = 0;
    *(unsigned __int64*)(initIn + 0) = 0xFE800000ULL;
    *(unsigned*)(initIn + 8) = 0x00080000;
    *(unsigned*)(initIn + 12) = 1;
    *(unsigned __int64*)(initIn + 16) = 0xC0000000ULL;
    *(unsigned*)(initIn + 24) = 0x10000000;
    DeviceIoControl(h, 0x80000B80, initIn, sizeof(initIn), NULL, 0, &br, NULL);
    printf("INIT_HARDWARE OK\n");

    unsigned val;

    /* Read BIOS state */
    ReadReg(h, 0x32D4, &val); printf("SCRATCH before: 0x%08X\n", val);
    ReadReg(h, 0x6C8C, &val); printf("PT_BASE_LO: 0x%08X\n", val);
    ReadReg(h, 0x6C90, &val); printf("PT_BASE_HI: 0x%08X\n", val);
    ReadReg(h, 0x0B460, &val); printf("CONTEXT0_CNTL: 0x%08X\n", val);
    ReadReg(h, 0x0B360, &val); printf("L2_CNTL: 0x%08X\n", val);
    ReadReg(h, 0xE060, &val); printf("KIQ_BASE_LO: 0x%08X\n", val);
    ReadReg(h, 0xE064, &val); printf("KIQ_BASE_HI: 0x%08X\n", val);
    ReadReg(h, 0x4A74, &val); printf("ME_CNTL: 0x%08X\n", val);
    ReadReg(h, 0xDAC0, &val); printf("HQD_ACTIVE: 0x%08X\n", val);
    ReadReg(h, 0xDAD8, &val); printf("HQD_PQ_BASE_LO: 0x%08X\n", val);
    ReadReg(h, 0xDADC, &val); printf("HQD_PQ_BASE_HI: 0x%08X\n", val);

    /* Allocate ring buffer */
    PVOID ringVa = ExAllocatePool2(POOL_FLAG_NON_PAGED, 0x1000, 'KIQ');
    if (!ringVa) {
        printf("Ring alloc FAILED\n");
        CloseHandle(h);
        return 1;
    }
    ZeroMemory(ringVa, 0x1000);

    PHYSICAL_ADDRESS ringPa = MmGetPhysicalAddress(ringVa);
    printf("\nRing VA=%p PA=0x%llX\n", ringVa, ringPa.QuadPart);

    /* Write PM4 NOP + WRITE_DATA to SCRATCH into ring */
    volatile PULONG ring = (volatile PULONG)ringVa;
    ring[0] = 0xC0023700;  /* PM4: IT_WRITE_DATA */
    ring[1] = 0x000032D4;  /* SCRATCH register offset */
    ring[2] = 0xCAFEBABE;  /* value to write */
    ring[3] = 0x30000000;  /* NOP */
    ring[4] = 0x30000000;  /* NOP */
    ring[5] = 0x30000000;  /* NOP */
    ring[6] = 0x30000000;  /* NOP */
    ring[7] = 0x30000000;  /* NOP */
    KeMemoryBarrier();

    /* Halt ME+PFP */
    ReadReg(h, 0x4A74, &val);
    WriteReg(h, 0x4A74, val | (1<<28) | (1<<30));
    printf("\nME Halted\n");

    /* Deactivate KIQ queue */
    WriteReg(h, 0x34D0, 0x00010000);  /* GRBM_INDEX = ME=1 */
    WriteReg(h, 0xDAC0, 0);           /* HQD_ACTIVE = 0 */

    /* Program KIQ_BASE with ring PA */
    WriteReg(h, 0xE060, (ULONG)(ringPa.QuadPart & 0xFFFFFFFF));
    WriteReg(h, 0xE064, (ULONG)(ringPa.QuadPart >> 32));
    printf("KIQ_BASE set to PA=0x%llX\n", ringPa.QuadPart);

    /* Verify */
    ReadReg(h, 0xE060, &val); printf("KIQ_BASE_LO readback: 0x%08X\n", val);
    ReadReg(h, 0xE064, &val); printf("KIQ_BASE_HI readback: 0x%08X\n", val);

    /* Set WPTR = 0 */
    WriteReg(h, 0xE078, 0);
    WriteReg(h, 0xE06C, 0);  /* RPTR = 0 */

    /* Activate queue */
    WriteReg(h, 0xDAC0, 1);

    /* Resume ME+PFP */
    ReadReg(h, 0x4A74, &val);
    WriteReg(h, 0x4A74, val & ~((1<<28) | (1<<30)));
    printf("ME Resumed\n");

    /* Wait */
    Sleep(100);

    /* Kick WPTR = 8 DWORDs */
    WriteReg(h, 0xE078, 8);
    printf("WPTR kicked to 8\n");

    /* Wait for GPU */
    Sleep(100);

    /* Read results */
    ReadReg(h, 0x32D4, &val); printf("\nSCRATCH after: 0x%08X\n", val);
    ReadReg(h, 0xE078, &val); printf("KIQ_WPTR after: 0x%08X\n", val);
    ReadReg(h, 0xE06C, &val); printf("KIQ_RPTR after: 0x%08X\n", val);

    if (val == 0xCAFEBABE)
        printf("\n*** PM4 EXECUTED! ***\n");
    else if (val != 0x4D585042)
        printf("\nSCRATCH changed from 0x4D585042 to 0x%08X\n", val);
    else
        printf("\nSCRATCH unchanged - PM4 did not execute\n");

    /* Cleanup: halt and free */
    WriteReg(h, 0x34D0, 0x00010000);
    WriteReg(h, 0xDAC0, 0);
    ReadReg(h, 0x4A74, &val);
    WriteReg(h, 0x4A74, val | (1<<28) | (1<<30));
    WriteReg(h, 0x34D0, 0xE0000000);

    ExFreePool(ringVa);
    CloseHandle(h);
    printf("\n=== Done ===\n");
    return 0;
}
