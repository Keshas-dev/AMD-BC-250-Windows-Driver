#define INITGUID
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

static FILE *g = NULL;
static void Log(const char *fmt, ...) {
    va_list a; va_start(a, fmt);
    vfprintf(stdout, fmt, a); va_end(a); fflush(stdout);
    if (g) { va_start(a, fmt); vfprintf(g, fmt, a); va_end(a); fflush(g); }
}

static HANDLE OpenKmd(void) {
    return CreateFileW(L"\\\\.\\AMDBC250DreamV43",
        GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
}

int main(void) {
    g = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\psp-test.log", "w");
    if (!g) { printf("Cannot open log\n"); return 1; }
    Log("=== PSP Status + Mailbox Test ===\n\n"); fflush(g);

    HANDLE h = OpenKmd();
    if (h == INVALID_HANDLE_VALUE) {
        Log("KMD NOT FOUND\n"); fclose(g); return 1;
    }
    DWORD br = 0;

    /* PSP GET_STATUS (0x80000BA4) */
    UCHAR outBuf[256] = {0};
    BOOL ok = DeviceIoControl(h, 0x80000BA4, NULL, 0, outBuf, sizeof(outBuf), &br, NULL);
    Log("PSP_GET_STATUS: %s\n", ok ? "OK" : "FAIL");
    if (ok) {
        /* struct: Initialized(4) SosAlive(4) FirmwareLoaded(4) MmioBase(4) SolRegister(4) C2pmsg64(4) */
        UINT32 init    = *(UINT32*)(outBuf + 0);
        UINT32 sos     = *(UINT32*)(outBuf + 4);
        UINT32 fw      = *(UINT32*)(outBuf + 8);
        UINT32 mmio    = *(UINT32*)(outBuf + 12);
        UINT32 sol     = *(UINT32*)(outBuf + 16);
        UINT32 c2pmsg64 = *(UINT32*)(outBuf + 20);
        Log("  Initialized:  %u\n", init);
        Log("  SosAlive:     %u\n", sos);
        Log("  FirmwareLoaded: %u\n", fw);
        Log("  MmioBase:     0x%08X\n", mmio);
        Log("  SolRegister:  0x%08X\n", sol);
        Log("  C2PMSG_64:    0x%08X\n", c2pmsg64);
    }
    fflush(g);

    /* PSP TEST_MAILBOX (0x80000BA8) */
    Log("\nPSP_TEST_MAILBOX:\n"); fflush(g);
    {
        UCHAR inBuf[16] = {0};
        UCHAR outBuf2[16] = {0};
        *(UINT32*)(inBuf + 0) = 0xDEADBEEF; /* WriteValue */
        ok = DeviceIoControl(h, 0x80000BA8, inBuf, sizeof(inBuf), outBuf2, sizeof(outBuf2), &br, NULL);
        Log("  Result: %s\n", ok ? "OK" : "FAIL");
        if (ok) {
            Log("  WriteValue: 0x%08X\n", *(UINT32*)(outBuf2 + 0));
            Log("  ReadValue:  0x%08X\n", *(UINT32*)(outBuf2 + 4));
            Log("  SolValue:   0x%08X\n", *(UINT32*)(outBuf2 + 8));
        }
        fflush(g);
    }

    /* PSP SEND_COMMAND (0x80000BA0) — try common commands */
    Log("\nPSP_SEND_COMMAND tests:\n"); fflush(g);
    {
        UCHAR cmdBuf[280] = {0};
        /* struct: Command(4) DataSize(4) Data(256) */
        UINT32 commands[] = {
            0x00000001,  /* LOAD_FW */
            0x00000002,  /* LOAD_TOS */
            0x00000003,  /* LOAD_ASD */
            0x00000004,  /* LOAD_SYSDRV */
            0x00000005,  /* DESTROY_RINGS */
            0x00000010,  /* ? */
            0x00010001,  /* ? */
            0x00020000,  /* GFX_CTRL */
            0x10000001,  /* ? */
            0x20000001,  /* ? */
        };
        int i;
        for (i = 0; i < 10; i++) {
            RtlZeroMemory(cmdBuf, sizeof(cmdBuf));
            *(UINT32*)(cmdBuf + 0) = commands[i];
            *(UINT32*)(cmdBuf + 4) = 0;
            UCHAR outCmd[256] = {0};
            ok = DeviceIoControl(h, 0x80000BA0, cmdBuf, sizeof(cmdBuf), outCmd, sizeof(outCmd), &br, NULL);
            Log("  CMD 0x%08X: %s\n", commands[i], ok ? "OK" : "FAIL");
        }
        fflush(g);
    }

    CloseHandle(h);
    Log("\n=== PSP Test Complete ===\n"); fflush(g);
    if (g) fclose(g);
    printf("Done. Check output\\psp-test.log\n");
    return 0;
}
