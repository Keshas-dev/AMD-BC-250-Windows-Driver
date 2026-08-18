/* psp-proxy-smu-test.c - send SMU messages via GPU driver PSP proxy */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"
#pragma warning(disable: 4996)

/* CTL_CODE(dev, func, method, access)  WDK: (dev<<16)|(access<<14)|(func<<2)|(method) */
#define IOCTL_AMDBC250_PSP_SMU_MSG   CTL_CODE(FILE_DEVICE_AMDBC250, 0x924, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AMDBC250_PSP_LOAD_IP_FW CTL_CODE(FILE_DEVICE_AMDBC250, 0x920, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct {
    UINT32 Message;
    UINT32 Argument;
    UINT32 Response;
    UINT32 ResponseStatus;
    UINT32 Result;
} PSP_SMU_MSG;

static HANDLE h;
static BOOL W32(uint32_t o, uint32_t v) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=v; return DeviceIoControl(h,IOCTL_AMDBC250_WRITE_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL); }
static uint32_t R32(uint32_t o) { AMDBC250_IOCTL_REG_ACCESS r; DWORD b; r.RegisterOffset=o; r.Value=0; if(DeviceIoControl(h,IOCTL_AMDBC250_READ_REG,&r,sizeof(r),&r,sizeof(r),&b,NULL)) return r.Value; return 0xFFFFFFFF; }

static int sendPspSmu(uint32_t msg, uint32_t arg, uint32_t *rsp, uint32_t *status) {
    PSP_SMU_MSG m;
    DWORD br = 0;
    memset(&m, 0, sizeof(m));
    m.Message = msg;
    m.Argument = arg;
    BOOL r = DeviceIoControl(h, IOCTL_AMDBC250_PSP_SMU_MSG, &m, sizeof(m), &m, sizeof(m), &br, NULL);
    if (rsp) *rsp = m.Response;
    if (status) *status = m.ResponseStatus;
    return r ? 0 : -1;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    h = CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { printf("open FAIL gle=%lu\n", GetLastError()); return 1; }
    printf("device OK\n");

    AMDBC250_IOCTL_INIT_HARDWARE ih;
    DWORD br = 0;
    memset(&ih, 0, sizeof(ih));
    ih.MmioPhysicalBase = 0xFE800000ULL;
    ih.MmioSize = 0x80000;
    ih.Flags = AMDBC250_INIT_FLAG_NBIO_MAP;
    if (!DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &br, NULL)) {
        printf("INIT fail\n"); return 1;
    }
    printf("INIT OK\n");

    /* Direct BAR5 read for comparison */
    uint32_t spi_direct = R32(0x5C3C);
    printf("\nDirect BAR5 SPI_PG = 0x%08X\n", spi_direct);

    /* Test 1: GetSmuVersion via PSP proxy (sanity) */
    uint32_t rsp, status;
    int r = sendPspSmu(0x02, 0, &rsp, &status);
    printf("\nPSP proxy GetSmuVersion(0x02): r=%d rsp=0x%08X status=0x%08X\n", r, rsp, status);

    /* Test 2: QueryActiveWgp via PSP proxy */
    r = sendPspSmu(0x1E, 0, &rsp, &status);
    printf("PSP proxy QueryActiveWgp(0x1E): r=%d Wgp=%u status=0x%08X\n", r, rsp, status);

    /* Test 3: SMU 0x98 (ungated SMN write) via PSP proxy to candidate SPI_PG alias */
    /* Try 0x0115B000 - responded to 0x98 in earlier test */
    printf("\n=== PSP proxy SMU 0x98 (ungated SMN write) ===\n");
    uint32_t addrs[] = { 0x0115B000, 0x0115A870, 0x0115A874, 0x01160000, 0x01100000 };
    for (int i = 0; i < 5; i++) {
        r = sendPspSmu(0x98, addrs[i], &rsp, &status);
        printf("  0x98 -> 0x%08X: r=%d status=0x%08X\n", addrs[i], r, status);
    }

    /* Test 4: After 0x98, check SPI_PG via direct BAR5 */
    printf("\nSPI_PG after PSP 0x98 tests: 0x%08X\n", R32(0x5C3C));

    /* Test 5: Try 0x18 RequestActiveWgp via PSP proxy */
    printf("\n=== RequestActiveWgp via PSP proxy ===\n");
    r = sendPspSmu(0x18, 0x1F, &rsp, &status);
    printf("  0x18(0x1F): r=%d status=0x%08X\n", r, status);
    r = sendPspSmu(0x1E, 0, &rsp, &status);
    printf("  QueryActiveWgp after: Wgp=%u\n", rsp);
    printf("  SPI_PG after 0x18: 0x%08X\n", R32(0x5C3C));

    printf("\n=== DONE ===\n");
    CloseHandle(h);
    return 0;
}
