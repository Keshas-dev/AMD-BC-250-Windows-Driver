/* psp-smu-reg-test.c - try PSP mailbox to write SPI_PG via SOS */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "..\inc\amdbc250_ioctl.h"

/* PSP-specific IOCTLs (from PspIoctl.h) */
#define IOCTL_PSP_INIT_HW      CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PSP_READ_REG     CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PSP_WRITE_REG    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PSP_LOAD_FW      CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PSP_SEND_CMD     CTL_CODE(FILE_DEVICE_UNKNOWN, 0x805, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PSP_REG_PROG     CTL_CODE(FILE_DEVICE_UNKNOWN, 0x816, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct { ULONG64 PhysicalAddress; ULONG Size; } PSP_INIT_HW_REQUEST;
typedef struct { ULONG Offset; } PSP_READ_REQUEST;
typedef struct { ULONG Offset; ULONG Value; } PSP_READ_RESPONSE;
typedef struct { ULONG Offset; ULONG Value; } PSP_WRITE_REQUEST;

static HANDLE hPsp;

static uint32_t ReadSpiPg(void) {
    PSP_READ_REQUEST req = {0x5C3C};
    PSP_READ_RESPONSE resp = {0};
    DWORD br;
    if (DeviceIoControl(hPsp, IOCTL_PSP_READ_REG, &req, sizeof(req), &resp, sizeof(resp), &br, NULL))
        return resp.Value;
    return 0xFFFFFFFF;
}

static int WriteSpiPgViaPsp(ULONG value) {
    PSP_WRITE_REQUEST req = {0x5C3C, value};
    DWORD br;
    return DeviceIoControl(hPsp, IOCTL_PSP_WRITE_REG, &req, sizeof(req), NULL, 0, &br, NULL) ? 0 : -1;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    hPsp = CreateFileA("\\\\.\\AmdBcPsp", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hPsp == INVALID_HANDLE_VALUE) {
        printf("FAIL: open PSP driver err=%lu\n", GetLastError());
        return 1;
    }
    printf("PSP driver opened\n");

    /* Init PSP HW */
    PSP_INIT_HW_REQUEST initReq;
    initReq.PhysicalAddress = 0xFE800000ULL;
    initReq.Size = 0x80000;
    DWORD br;
    if (!DeviceIoControl(hPsp, IOCTL_PSP_INIT_HW, &initReq, sizeof(initReq), NULL, 0, &br, NULL)) {
        printf("PSP INIT_HW failed err=%lu\n", GetLastError());
    } else {
        printf("PSP INIT_HW OK\n");
    }

    printf("Baseline SPI_PG = 0x%08X\n", ReadSpiPg());

    /* Try writing via PSP driver IOCTL */
    printf("\n=== Write SPI_PG = 0x1F via PSP IOCTL ===\n");
    if (WriteSpiPgViaPsp(0x1F) == 0) {
        printf("Write: OK\n");
    } else {
        printf("Write: FAIL err=%lu\n", GetLastError());
    }
    printf("SPI_PG after write = 0x%08X\n", ReadSpiPg());

    /* Try REG_PROG IOCTL (0x816) - register programming via ring */
    printf("\n=== Try IOCTL_PSP_REG_PROG (0x816) ===\n");
    ULONG regProgData[3] = {0x5C3C, 0x1F, 0};  /* offset, value, reserved */
    if (DeviceIoControl(hPsp, IOCTL_PSP_REG_PROG, regProgData, sizeof(regProgData), NULL, 0, &br, NULL)) {
        printf("REG_PROG: OK\n");
    } else {
        printf("REG_PROG: FAIL err=%lu\n", GetLastError());
    }
    printf("SPI_PG after REG_PROG = 0x%08X\n", ReadSpiPg());

    CloseHandle(hPsp);
    printf("\n=== DONE ===\n");
    return 0;
}
