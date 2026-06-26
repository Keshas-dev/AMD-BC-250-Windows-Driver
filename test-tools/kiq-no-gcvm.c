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
    PVOID ringVa = VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!ringVa) {
        printf("Ring alloc FAILED\n");
        CloseHandle(h);
        return 1;
    }
    ZeroMemory(ringVa, 0x1000);

    printf("\nRing VA=%p\n", ringVa);

    /* Use SEND_PM4 IOCTL instead of direct ring programming */
    UCHAR pm4Buf[272] = {0};
    /* PM4 WRITE_DATA: header=0xC0370003, control=0x10100000, addr_lo=0x32D4, addr_hi=0, data=CAFEBABE */
    *(UINT32*)(pm4Buf + 0)   = 0xC0370003;  /* HEADER: IT_WRITE_DATA count=3 */
    *(UINT32*)(pm4Buf + 4)   = 0x10100000;  /* CONTROL: DST_SEL=register, WR_CONFIRM */
    *(UINT32*)(pm4Buf + 8)   = 0x000032D4;  /* ADDR_LO = SCRATCH */
    *(UINT32*)(pm4Buf + 12)  = 0x00000000;  /* ADDR_HI */
    *(UINT32*)(pm4Buf + 16)  = 0xCAFEBABE;  /* DATA */
    *(UINT32*)(pm4Buf + 256) = 5;            /* CommandCount */
    DWORD br2 = 0;
    BOOL sent = DeviceIoControl(h, 0x80000B84, pm4Buf, sizeof(pm4Buf), NULL, 0, &br2, NULL);
    printf("SEND_PM4 (no GCVM): %s (err=%lu)\n", sent ? "OK" : "FAIL", sent ? 0 : GetLastError());

    /* Wait for GPU */
    Sleep(200);

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

    VirtualFree(ringVa, 0, MEM_RELEASE);
    CloseHandle(h);
    printf("\n=== Done ===\n");
    return 0;
}
