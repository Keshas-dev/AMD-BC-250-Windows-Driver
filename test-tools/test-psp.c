#include <windows.h>
#include <stdio.h>
#include "amdbc250_ioctl.h"

static HANDLE OpenDevice(void) {
    return CreateFileA("\\\\.\\AMDBC250DreamV43", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
}

int main(int argc, char* argv[]) {
    printf("AMD BC-250 PSP Test Tool v1.0\n");
    printf("=============================\n\n");

    HANDLE hDevice = OpenDevice();
    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("ERROR: Cannot open device (error %lu)\n", GetLastError());
        return 1;
    }
    printf("[OK] Device opened\n\n");

    if (argc > 1 && strcmp(argv[1], "init") == 0) {
        printf("--- PSP Init ---\n");
        DWORD bytesReturned = 0;
        BOOL ok = DeviceIoControl(hDevice, 0x80000B98, NULL, 0, NULL, 0, &bytesReturned, NULL);
        printf("PSP Init: %s (error %lu)\n", ok ? "OK" : "FAIL", GetLastError());
    }
    else if (argc > 1 && strcmp(argv[1], "status") == 0) {
        printf("--- PSP Status ---\n");
        AMDBC250_IOCTL_PSP_STATUS status = {0};
        DWORD bytesReturned = 0;
        BOOL ok = DeviceIoControl(hDevice, 0x80000BA4, NULL, 0, &status, sizeof(status), &bytesReturned, NULL);
        if (ok) {
            printf("  Initialized:  %s\n", status.Initialized ? "YES" : "NO");
            printf("  SOS Alive:    %s\n", status.SosAlive ? "YES" : "NO");
            printf("  FW Loaded:    %s\n", status.FirmwareLoaded ? "YES" : "NO");
            printf("  MMIO Base:    0x%08X\n", status.MmioBase);
            printf("  SOL Register: 0x%08X\n", status.SolRegister);
            printf("  C2PMSG_64:    0x%08X\n", status.C2pmsg64);
        } else {
            printf("  Failed (error %lu)\n", GetLastError());
        }
    }
    else if (argc > 1 && strcmp(argv[1], "mailbox") == 0) {
        printf("--- PSP Mailbox Test ---\n");
        AMDBC250_IOCTL_PSP_TEST_MAILBOX test = {0};
        test.WriteValue = (argc > 2) ? strtoul(argv[2], NULL, 16) : 0xDEADBEEF;
        printf("  Writing 0x%08X to C2PMSG_0...\n", test.WriteValue);
        DWORD bytesReturned = 0;
        BOOL ok = DeviceIoControl(hDevice, 0x80000BA8, &test, sizeof(test), &test, sizeof(test), &bytesReturned, NULL);
        if (ok) {
            printf("  Read back:    0x%08X\n", test.ReadValue);
            printf("  SOL Register: 0x%08X\n", test.SolValue);
            printf("  %s\n", test.ReadValue == test.WriteValue ? "PASS: Read matches write" : "NOTE: Read differs from write (may be normal)");
        } else {
            printf("  Failed (error %lu)\n", GetLastError());
        }
    }
    else if (argc > 1 && strcmp(argv[1], "loadfw") == 0) {
        if (argc < 4) {
            printf("Usage: test-psp.exe loadfw <type> <file>\n");
            printf("  type: 0=SOS, 1=ASD, 2=TA\n");
            CloseHandle(hDevice);
            return 1;
        }

        DWORD fwType = atoi(argv[2]);
        const char* fwFile = argv[3];

        printf("--- PSP Load Firmware ---\n");
        printf("  Type: %u (%s)\n", fwType, fwType == 0 ? "SOS" : fwType == 1 ? "ASD" : "TA");
        printf("  File: %s\n", fwFile);

        HANDLE hFw = CreateFileA(fwFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (hFw == INVALID_HANDLE_VALUE) {
            printf("  ERROR: Cannot open firmware file (error %lu)\n", GetLastError());
            CloseHandle(hDevice);
            return 1;
        }

        DWORD fwSize = GetFileSize(hFw, NULL);
        if (fwSize == 0 || fwSize > 1024 * 1024) {
            printf("  ERROR: Invalid firmware size %u\n", fwSize);
            CloseHandle(hFw);
            CloseHandle(hDevice);
            return 1;
        }

        AMDBC250_IOCTL_PSP_LOAD_FIRMWARE pspFw = {0};
        pspFw.FirmwareType = fwType;
        pspFw.FirmwareSize = fwSize;
        DWORD bytesRead = 0;
        ReadFile(hFw, pspFw.FirmwareData, fwSize, &bytesRead, NULL);
        CloseHandle(hFw);

        printf("  Read %u bytes from file\n", bytesRead);

        DWORD bytesReturned = 0;
        BOOL ok = DeviceIoControl(hDevice, 0x80000B9C, &pspFw, sizeof(pspFw), NULL, 0, &bytesReturned, NULL);
        printf("  Load: %s (error %lu)\n", ok ? "OK" : "FAIL", GetLastError());
    }
    else {
        printf("Commands:\n");
        printf("  init          - Initialize PSP hardware\n");
        printf("  status        - Show PSP status\n");
        printf("  mailbox [hex] - Test PSP mailbox (optional hex value)\n");
        printf("  loadfw <type> <file> - Load firmware blob\n");
        printf("    type: 0=SOS, 1=ASD, 2=TA\n");
    }

    CloseHandle(hDevice);
    return 0;
}
