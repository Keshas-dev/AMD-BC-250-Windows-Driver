#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AMDBC250_DEVICE_PATH L"\\\\.\\AMDBC250DreamV43"

#define IOCTL_AMDBC250_READ_REG   ((ULONG)0x80000B88)
#define IOCTL_AMDBC250_WRITE_REG  ((ULONG)0x80000B8C)
#define IOCTL_AMDBC250_INIT_HARDWARE ((ULONG)0x80000B80)

#define C2PMSG_35 0x1056C
#define C2PMSG_36 0x10570
#define C2PMSG_37 0x10574
#define C2PMSG_81 0x10614

typedef struct {
    ULONG Offset;
    ULONG Value;
    ULONG Status;
} REG_IOCTL;

typedef struct {
    ULONG64 MmioPhysicalBase;
    ULONG MmioSize;
    ULONG Flags;
    ULONG64 FbPhysicalBase;
    ULONG FbSize;
} INIT_HW;

static HANDLE g_h = INVALID_HANDLE_VALUE;

static int OpenGpu(void) {
    g_h = CreateFileW(AMDBC250_DEVICE_PATH, GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    return g_h != INVALID_HANDLE_VALUE ? 0 : -1;
}

static ULONG ReadReg(ULONG offset) {
    REG_IOCTL req = {offset, 0, 0};
    DWORD ret = 0;
    if (!DeviceIoControl(g_h, IOCTL_AMDBC250_READ_REG, &req, sizeof(req), &req, sizeof(req), &ret, NULL))
        return 0xFFFFFFFF;
    return req.Value;
}

static int WriteReg(ULONG offset, ULONG value) {
    REG_IOCTL req = {offset, value, 0};
    DWORD ret = 0;
    return DeviceIoControl(g_h, IOCTL_AMDBC250_WRITE_REG, &req, sizeof(req), &req, sizeof(req), &ret, NULL) ? 0 : -1;
}

static int InitHw(void) {
    INIT_HW ih = {0};
    ih.MmioPhysicalBase = 0xFE800000ULL;  /* hardcoded BAR5 */
    ih.MmioSize = 0x80000;
    ih.Flags = 1;
    DWORD ret = 0;
    return DeviceIoControl(g_h, IOCTL_AMDBC250_INIT_HARDWARE, &ih, sizeof(ih), &ih, sizeof(ih), &ret, NULL) ? 0 : -1;
}

int main(void) {
    printf("=== PSP Mailbox Command Scan ===\n");

    if (OpenGpu() != 0) {
        printf("ERROR: Cannot open GPU driver\n"); return 1;
    }
    /* Init HW may fail if driver already initialized — continue anyway */
    if (InitHw() != 0) {
        printf("Init HW: FAILED (err=%lu) — trying without it\n", GetLastError());
    }

    ULONG c81 = ReadReg(C2PMSG_81);
    printf("C2PMSG_81 = 0x%08X (before test)\n", c81);

    printf("\nScanning GFX_CMD_ID 0x00-0xFF...\n");
    printf("ID  | C2PMSG_35-after | C2PMSG_81-after | Status\n");
    printf("----+-----------------+-----------------+--------\n");

    int found = 0;
    for (ULONG cmd = 0; cmd <= 0xFF; cmd++) {
        ULONG c35_before = ReadReg(C2PMSG_35);
        if (c35_before != 0) {
            printf("WARN: C2PMSG_35 busy (0x%08X) at cmd=0x%02X, skipping\n", c35_before, cmd);
            continue;
        }

        WriteReg(C2PMSG_35, cmd);
        Sleep(200);
        ULONG c35_after = ReadReg(C2PMSG_35);
        ULONG c81_after = ReadReg(C2PMSG_81);

        const char *status = "no response";
        if (c35_after == 0) {
            status = "consumed";
            found++;
        } else if (c35_after != cmd) {
            status = "modified";
            found++;
        }

        if (c35_after != cmd || c81_after != c81) {
            printf("0x%02X | 0x%08X       | 0x%08X       | %s\n",
                cmd, c35_after, c81_after, status);
        }
    }

    printf("\nTotal commands that modified state: %d\n", found);
    printf("C2PMSG_81 = 0x%08X (after test)\n", ReadReg(C2PMSG_81));
    CloseHandle(g_h);
    return 0;
}
