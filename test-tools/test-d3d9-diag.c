#include <windows.h>
#include <d3d9.h>
#include <stdio.h>

#pragma comment(lib, "d3d9.lib")

int main() {
    FILE *f = fopen("output\\d3d9-diag.txt", "w");
    if (!f) return 1;

    fprintf(f, "=== D3D9 Diagnostic ===\n");

    IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) {
        fprintf(f, "Direct3DCreate9: FAILED (NULL)\n");
        fclose(f);
        return 1;
    }
    fprintf(f, "Direct3DCreate9: OK\n");

    UINT count = IDirect3D9_GetAdapterCount(d3d);
    fprintf(f, "Adapter count: %u\n", count);

    for (UINT i = 0; i < count; i++) {
        D3DADAPTER_IDENTIFIER9 id;
        HRESULT hr = IDirect3D9_GetAdapterIdentifier(d3d, i, 0, &id);
        if (SUCCEEDED(hr)) {
            fprintf(f, "Adapter[%u]: %s (Vendor=0x%04X Device=0x%04X)\n",
                i, id.Description, id.VendorId, id.DeviceId);
        } else {
            fprintf(f, "Adapter[%u]: GetIdentifier FAILED 0x%08X\n", i, hr);
        }

        D3DCAPS9 caps;
        hr = IDirect3D9_GetDeviceCaps(d3d, i, D3DDEVTYPE_HAL, &caps);
        fprintf(f, "  HAL caps: %s (0x%08X)\n", SUCCEEDED(hr) ? "OK" : "FAIL", hr);

        hr = IDirect3D9_GetDeviceCaps(d3d, i, D3DDEVTYPE_REF, &caps);
        fprintf(f, "  REF caps: %s (0x%08X)\n", SUCCEEDED(hr) ? "OK" : "FAIL", hr);
    }

    D3DPRESENT_PARAMETERS pp = {0};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.BackBufferWidth = 800;
    pp.BackBufferHeight = 600;
    pp.BackBufferCount = 1;

    HWND hWnd = CreateWindowA("STATIC", "test", WS_OVERLAPPEDWINDOW,
        0, 0, 100, 100, NULL, NULL, NULL, NULL);

    for (UINT i = 0; i < count; i++) {
        IDirect3DDevice9 *dev = NULL;
        pp.BackBufferFormat = D3DFMT_X8R8G8B8;
        HRESULT hr = IDirect3D9_CreateDevice(d3d, i, D3DDEVTYPE_HAL, hWnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev);
        fprintf(f, "CreateDevice[%u] HAL: %s (0x%08X)\n", i,
            SUCCEEDED(hr) ? "OK" : "FAIL", hr);
        if (SUCCEEDED(hr)) {
            IDirect3DDevice9_Release(dev);
        }

        pp.BackBufferFormat = D3DFMT_UNKNOWN;
    }

    if (hWnd) DestroyWindow(hWnd);
    IDirect3D9_Release(d3d);
    fclose(f);
    return 0;
}
