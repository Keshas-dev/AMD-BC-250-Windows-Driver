/*
 * BC-250 D3D9 Test Application
 * Creates a window and renders a colored triangle using D3D9 C API.
 */

#include <windows.h>
#include <d3d9.h>
#include <stdio.h>

#pragma comment(lib, "d3d9.lib")

static IDirect3D9*       g_pD3D = NULL;
static IDirect3DDevice9* g_pd3dDevice = NULL;

HRESULT InitD3D(HWND hWnd)
{
    /* Create D3D9 object */
    g_pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!g_pD3D) {
        OutputDebugStringA("BC-250 Test: Failed to create D3D9\n");
        return E_FAIL;
    }

    /* Setup present parameters */
    D3DPRESENT_PARAMETERS d3dpp = {0};
    d3dpp.Windowed = TRUE;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.hDeviceWindow = hWnd;
    d3dpp.BackBufferFormat = D3DFMT_X8R8G8B8;
    d3dpp.BackBufferWidth = 800;
    d3dpp.BackBufferHeight = 600;
    d3dpp.BackBufferCount = 1;
    d3dpp.EnableAutoDepthStencil = FALSE;
    d3dpp.Flags = D3DPRESENTFLAG_DISCARD_DEPTHSTENCIL;
    d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;

    /* Create device using vtable */
    HRESULT hr = IDirect3D9_CreateDevice(
        g_pD3D,
        D3DADAPTER_DEFAULT,
        D3DDEVTYPE_HAL,
        hWnd,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING,
        &d3dpp,
        &g_pd3dDevice
    );

    if (FAILED(hr)) {
        char buf[128];
        snprintf(buf, sizeof(buf), "BC-250 Test: CreateDevice failed: 0x%08X\n", hr);
        OutputDebugStringA(buf);
        return hr;
    }

    OutputDebugStringA("BC-250 Test: D3D9 device created successfully\n");
    return S_OK;
}

void RenderFrame()
{
    if (!g_pd3dDevice) return;

    /* Clear screen to blue */
    IDirect3DDevice9_Clear(g_pd3dDevice, 0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 255), 1.0f, 0);

    /* Begin scene */
    IDirect3DDevice9_BeginScene(g_pd3dDevice);

    /* Draw a colored triangle using immediate mode */
    typedef struct {
        float x, y, z, rhw;
        DWORD color;
    } Vertex;

    Vertex vertices[] = {
        { 400.0f, 100.0f, 0.5f, 1.0f, D3DCOLOR_XRGB(255, 0, 0) },
        { 100.0f, 500.0f, 0.5f, 1.0f, D3DCOLOR_XRGB(0, 255, 0) },
        { 700.0f, 500.0f, 0.5f, 1.0f, D3DCOLOR_XRGB(0, 0, 255) },
    };

    IDirect3DDevice9_SetFVF(g_pd3dDevice, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    IDirect3DDevice9_DrawPrimitiveUP(g_pd3dDevice, D3DPT_TRIANGLELIST, 1, vertices, sizeof(Vertex));

    /* End scene */
    IDirect3DDevice9_EndScene(g_pd3dDevice);

    /* Present */
    IDirect3DDevice9_Present(g_pd3dDevice, NULL, NULL, NULL, NULL);
}

void Cleanup()
{
    if (g_pd3dDevice) {
        IDirect3DDevice9_Release(g_pd3dDevice);
        g_pd3dDevice = NULL;
    }
    if (g_pD3D) {
        IDirect3D9_Release(g_pD3D);
        g_pD3D = NULL;
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            PostQuitMessage(0);
        }
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    /* Register window class */
    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "BC250TestWindow";
    RegisterClassEx(&wc);

    /* Create window */
    HWND hWnd = CreateWindow(
        "BC250TestWindow",
        "BC-250 D3D9 Test - Press ESC to exit",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        800, 600,
        NULL, NULL, hInstance, NULL
    );

    if (!hWnd) {
        OutputDebugStringA("BC-250 Test: Failed to create window\n");
        return 1;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    /* Initialize D3D9 */
    HRESULT hr = InitD3D(hWnd);
    if (FAILED(hr)) {
        MessageBox(hWnd, "Failed to initialize Direct3D 9", "Error", MB_OK);
        return 1;
    }

    /* Message loop */
    MSG msg = {0};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            RenderFrame();
        }
    }

    Cleanup();
    return (int)msg.wParam;
}
