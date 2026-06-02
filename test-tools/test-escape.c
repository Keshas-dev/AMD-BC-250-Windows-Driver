#include <windows.h>
#include <stdio.h>
#include <d3dkmthk.h>

typedef struct {
    ULONG CommandId;
    NTSTATUS Status;
    ULONG OutputSize;
} ESCAPE_HEADER;

static FILE *g = NULL;
static void Log(const char *fmt, ...) {
    va_list a; va_start(a, fmt);
    vfprintf(stdout, fmt, a); va_end(a); fflush(stdout);
    if (g) { va_start(a, fmt); vfprintf(g, fmt, a); va_end(a); fflush(g); }
}

int main(void) {
    /* Write a marker file immediately to prove we got here */
    {
        HANDLE hf = CreateFileA("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\escape-started.txt",
            GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hf != INVALID_HANDLE_VALUE) {
            DWORD w; WriteFile(hf, "started", 7, &w, NULL); CloseHandle(hf);
        }
    }
    g = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\escape-test.log", "w");
    Log("=== DxgkDdiEscape Test ===\n\n");

    /* Open adapter */
    HDC hdc = GetDC(NULL);
    D3DKMT_OPENADAPTERFROMHDC oah = {0};
    oah.hDc = hdc;
    NTSTATUS st = D3DKMTOpenAdapterFromHdc(&oah);
    Log("S1 OpenAdapter: 0x%08X h=0x%08X\n", st, oah.hAdapter);
    ReleaseDC(NULL, hdc);
    if (st != 0) { Log("FATAL\n"); fclose(g); return 1; }

    /* Create device */
    D3DKMT_CREATEDEVICE cd = {0};
    cd.hAdapter = oah.hAdapter;
    st = D3DKMTCreateDevice(&cd);
    Log("S2 CreateDevice: 0x%08X hDevice=0x%08X\n", st, cd.hDevice);

    /* S3: GET_CAPS via D3DKMTEscape */
    Log("\n--- S3: GET_CAPS (cmd=0x01) ---\n");
    {
        UCHAR buf[256] = {0};
        ESCAPE_HEADER *hdr = (ESCAPE_HEADER*)buf;
        hdr->CommandId = 0x01;
        D3DKMT_ESCAPE esc = {0};
        esc.hAdapter = oah.hAdapter;
        esc.hDevice = cd.hDevice;
        esc.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
        esc.Flags.Value = 0;
        esc.pPrivateDriverData = buf;
        esc.PrivateDriverDataSize = sizeof(buf);
        st = D3DKMTEscape(&esc);
        Log("  D3DKMTEscape: 0x%08X\n", st);
        if (st == 0 && hdr->Status == 0) {
            PULONG d = (PULONG)(hdr + 1);
            Log("  Status: 0x%08X  OutputSize: %lu\n", hdr->Status, hdr->OutputSize);
            Log("  VendorId=0x%08X DeviceId=0x%08X\n", d[0], d[1]);
            Log("  CUs=%lu  Shaders=%lu  Pipes=%lu\n", d[2], d[3], d[4]);
            Log("  GPUclk=%lu MHz  MemClk=%lu MHz\n", d[5], d[6]);
        } else {
            Log("  Status: 0x%08X (driver returned error)\n", hdr->Status);
        }
    }

    /* S4: GET_VRAM_INFO via D3DKMTEscape */
    Log("\n--- S4: GET_VRAM_INFO (cmd=0x02) ---\n");
    {
        UCHAR buf[256] = {0};
        ESCAPE_HEADER *hdr = (ESCAPE_HEADER*)buf;
        hdr->CommandId = 0x02;
        D3DKMT_ESCAPE esc = {0};
        esc.hAdapter = oah.hAdapter;
        esc.hDevice = cd.hDevice;
        esc.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
        esc.Flags.Value = 0;
        esc.pPrivateDriverData = buf;
        esc.PrivateDriverDataSize = sizeof(buf);
        st = D3DKMTEscape(&esc);
        Log("  D3DKMTEscape: 0x%08X\n", st);
        if (st == 0 && hdr->Status == 0) {
            PULONG d = (PULONG)(hdr + 1);
            Log("  TotalVRAM=%lu MB  Visible=%lu MB\n", d[0], d[1]);
            Log("  VramPA=0x%08X%08X\n", d[3], d[2]);
        } else {
            Log("  Status: 0x%08X\n", hdr->Status);
        }
    }

    /* S5: GET_FW_VERSION via D3DKMTEscape */
    Log("\n--- S5: GET_FW_VERSION (cmd=0x05) ---\n");
    {
        UCHAR buf[256] = {0};
        ESCAPE_HEADER *hdr = (ESCAPE_HEADER*)buf;
        hdr->CommandId = 0x05;
        D3DKMT_ESCAPE esc = {0};
        esc.hAdapter = oah.hAdapter;
        esc.hDevice = cd.hDevice;
        esc.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
        esc.Flags.Value = 0;
        esc.pPrivateDriverData = buf;
        esc.PrivateDriverDataSize = sizeof(buf);
        st = D3DKMTEscape(&esc);
        Log("  D3DKMTEscape: 0x%08X\n", st);
        if (st == 0 && hdr->Status == 0) {
            const char *ver = (const char*)(hdr + 1);
            Log("  FW Version: %s\n", ver);
        } else {
            Log("  Status: 0x%08X\n", hdr->Status);
        }
    }

    /* Cleanup */
    if (cd.hDevice) {
        D3DKMT_DESTROYDEVICE dd = {0};
        dd.hDevice = cd.hDevice;
        D3DKMTDestroyDevice(&dd);
    }
    {
        D3DKMT_CLOSEADAPTER ca = {0};
        ca.hAdapter = oah.hAdapter;
        D3DKMTCloseAdapter(&ca);
    }

    Log("\n=== Test Complete ===\n");
    fclose(g);
    printf("Done. Check output\\escape-test.log\n");
    return 0;
}
