#define INITGUID
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <guiddef.h>
#include <d3dkmthk.h>
#include <dxgi.h>
#include <d3d11.h>
#include <d3d9.h>

static FILE *g = NULL;
static void Log(const char *fmt, ...) {
    va_list a; va_start(a, fmt);
    vfprintf(stdout, fmt, a); va_end(a); fflush(stdout);
    if (g) { va_start(a, fmt); vfprintf(g, fmt, a); va_end(a); fflush(g); }
}

int main(void) {
    g = fopen("C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\wddm-probe.log", "w");
    if (!g) { printf("Cannot open log\n"); return 1; }
    Log("=== WDDM Probe via BasicDisplay ===\n\n");

    /* ============================================== */
    /* S1: D3DKMTEnumAdapters                        */
    /* ============================================== */
    Log("=== S1: D3DKMTEnumAdapters ===\n");
    {
        D3DKMT_ENUMADAPTERS ea = {0};
        NTSTATUS st = D3DKMTEnumAdapters(&ea);
        Log("D3DKMTEnumAdapters: 0x%08X  NumAdapters=%u\n", st, ea.NumAdapters);
        if (st == 0) {
            for (UINT i = 0; i < ea.NumAdapters && i < MAX_ENUM_ADAPTERS; i++) {
                D3DKMT_ADAPTERINFO *ai = &ea.Adapters[i];
                Log("  Adapter[%u]: hAdapter=0x%08X Luid=%u.%d NumSources=%u PrecisePresent=%d\n",
                    i, ai->hAdapter, ai->AdapterLuid.HighPart, ai->AdapterLuid.LowPart,
                    ai->NumOfSources, ai->bPrecisePresentRegionsPreferred);
            }
        }
    }

    /* ============================================== */
    /* S2: D3DKMTOpenAdapterFromHdc (primary)        */
    /* ============================================== */
    Log("\n=== S2: D3DKMTOpenAdapterFromHdc ===\n");
    D3DKMT_HANDLE hAdapter = 0;
    LUID adapterLuid = {0};
    {
        HDC hdc = GetDC(NULL);
        if (!hdc) { Log("GetDC(NULL) failed\n"); }
        else {
            D3DKMT_OPENADAPTERFROMHDC oah = {0};
            oah.hDc = hdc;
            NTSTATUS st = D3DKMTOpenAdapterFromHdc(&oah);
            Log("D3DKMTOpenAdapterFromHdc: 0x%08X\n", st);
            if (st == 0) {
                hAdapter = oah.hAdapter;
                adapterLuid = oah.AdapterLuid;
                Log("  hAdapter=0x%08X  Luid=%u.%d  VidPnSourceId=%d\n",
                    hAdapter, adapterLuid.HighPart, adapterLuid.LowPart, oah.VidPnSourceId);
            }
            ReleaseDC(NULL, hdc);
        }
    }

    /* ============================================== */
    /* S3: QueryAdapterInfo - KMTQAITYPE_UMDRIVERNAME */
    /* ============================================== */
    Log("\n=== S3: QueryAdapterInfo (UMDRIVERNAME) ===\n");
    if (hAdapter) {
        WCHAR umName[256] = {0};
        D3DKMT_QUERYADAPTERINFO qa = {0};
        qa.hAdapter = hAdapter;
        qa.Type = KMTQAITYPE_UMDRIVERNAME;
        qa.pPrivateDriverData = umName;
        qa.PrivateDriverDataSize = sizeof(umName);
        NTSTATUS st = D3DKMTQueryAdapterInfo(&qa);
        Log("  UMDRIVERNAME: 0x%08X\n", st);
        if (st == 0) {
            Log("  UMD Name: %ls\n", umName);
        }
    }

    /* ============================================== */
    /* S4: QueryAdapterInfo - KMTQAITYPE_DRIVERVERSION */
    /* ============================================== */
    Log("\n=== S4: QueryAdapterInfo (DRIVERVERSION) ===\n");
    if (hAdapter) {
        UINT64 ver = 0;
        D3DKMT_QUERYADAPTERINFO qa = {0};
        qa.hAdapter = hAdapter;
        qa.Type = KMTQAITYPE_DRIVERVERSION;
        qa.pPrivateDriverData = &ver;
        qa.PrivateDriverDataSize = sizeof(ver);
        NTSTATUS st = D3DKMTQueryAdapterInfo(&qa);
        Log("  DRIVERVERSION: 0x%08X  value=0x%llX\n", st, ver);
    }

    /* ============================================== */
    /* S5: QueryAdapterInfo - KMTQAITYPE_GETSEGMENTSIZE */
    /* ============================================== */
    Log("\n=== S5: QueryAdapterInfo (GETSEGMENTSIZE) ===\n");
    if (hAdapter) {
        D3DKMT_SEGMENTSIZEINFO si = {0};
        D3DKMT_QUERYADAPTERINFO qa = {0};
        qa.hAdapter = hAdapter;
        qa.Type = KMTQAITYPE_GETSEGMENTSIZE;
        qa.pPrivateDriverData = &si;
        qa.PrivateDriverDataSize = sizeof(si);
        NTSTATUS st = D3DKMTQueryAdapterInfo(&qa);
        Log("  GETSEGMENTSIZE: 0x%08X\n", st);
        if (st == 0) {
            Log("  DedicatedVideoMemorySize: %llu MB\n", si.DedicatedVideoMemorySize / (1024*1024));
            Log("  DedicatedSystemMemorySize: %llu MB\n", si.DedicatedSystemMemorySize / (1024*1024));
            Log("  SharedSystemMemorySize:   %llu MB\n", si.SharedSystemMemorySize / (1024*1024));
        }
    }

    /* ============================================== */
    /* S6: QueryAdapterInfo - KMTQAITYPE_ADAPTERTYPE */
    /* ============================================== */
    Log("\n=== S6: QueryAdapterInfo (ADAPTERTYPE) ===\n");
    if (hAdapter) {
        UINT64 atype = 0;
        D3DKMT_QUERYADAPTERINFO qa = {0};
        qa.hAdapter = hAdapter;
        qa.Type = KMTQAITYPE_ADAPTERTYPE;
        qa.pPrivateDriverData = &atype;
        qa.PrivateDriverDataSize = sizeof(atype);
        NTSTATUS st = D3DKMTQueryAdapterInfo(&qa);
        Log("  ADAPTERTYPE: 0x%08X  value=0x%llX\n", st, atype);
    }

    /* ============================================== */
    /* S7: QueryAdapterInfo - KMTQAITYPE_ADAPTERADDRESS */
    /* ============================================== */
    Log("\n=== S7: QueryAdapterInfo (ADAPTERADDRESS) ===\n");
    if (hAdapter) {
        UINT64 addr[2] = {0};
        D3DKMT_QUERYADAPTERINFO qa = {0};
        qa.hAdapter = hAdapter;
        qa.Type = KMTQAITYPE_ADAPTERADDRESS;
        qa.pPrivateDriverData = addr;
        qa.PrivateDriverDataSize = sizeof(addr);
        NTSTATUS st = D3DKMTQueryAdapterInfo(&qa);
        Log("  ADAPTERADDRESS: 0x%08X\n", st);
        if (st == 0) {
            Log("  Bus: 0x%llX  Slot: 0x%llX\n", addr[0], addr[1]);
        }
    }

    /* ============================================== */
    /* S8: QueryAdapterInfo - KMTQAITYPE_ADAPTERREGISTRYINFO */
    /* ============================================== */
    Log("\n=== S8: QueryAdapterInfo (ADAPTERREGISTRYINFO) ===\n");
    if (hAdapter) {
        WCHAR regInfo[512] = {0};
        D3DKMT_QUERYADAPTERINFO qa = {0};
        qa.hAdapter = hAdapter;
        qa.Type = KMTQAITYPE_ADAPTERREGISTRYINFO;
        qa.pPrivateDriverData = regInfo;
        qa.PrivateDriverDataSize = sizeof(regInfo);
        NTSTATUS st = D3DKMTQueryAdapterInfo(&qa);
        Log("  ADAPTERREGISTRYINFO: 0x%08X\n", st);
        if (st == 0) {
            Log("  RegistryString: %ls\n", regInfo);
        }
    }

    /* ============================================== */
    /* S9: QueryAdapterInfo - KMTQAITYPE_WDDM_1_2_CAPS */
    /* ============================================== */
    Log("\n=== S9: QueryAdapterInfo (WDDM_1_2_CAPS) ===\n");
    if (hAdapter) {
        UINT64 caps = 0;
        D3DKMT_QUERYADAPTERINFO qa = {0};
        qa.hAdapter = hAdapter;
        qa.Type = KMTQAITYPE_WDDM_1_2_CAPS;
        qa.pPrivateDriverData = &caps;
        qa.PrivateDriverDataSize = sizeof(caps);
        NTSTATUS st = D3DKMTQueryAdapterInfo(&qa);
        Log("  WDDM_1_2_CAPS: 0x%08X  value=0x%llX\n", st, caps);
    }

    /* ============================================== */
    /* S10: QueryAdapterInfo - KMTQAITYPE_WDDM_2_0_CAPS */
    /* ============================================== */
    Log("\n=== S10: QueryAdapterInfo (WDDM_2_0_CAPS) ===\n");
    if (hAdapter) {
        UINT64 caps = 0;
        D3DKMT_QUERYADAPTERINFO qa = {0};
        qa.hAdapter = hAdapter;
        qa.Type = KMTQAITYPE_WDDM_2_0_CAPS;
        qa.pPrivateDriverData = &caps;
        qa.PrivateDriverDataSize = sizeof(caps);
        NTSTATUS st = D3DKMTQueryAdapterInfo(&qa);
        Log("  WDDM_2_0_CAPS: 0x%08X  value=0x%llX\n", st, caps);
    }

    /* ============================================== */
    /* S11: QueryAdapterInfo - KMTQAITYPE_NODEMETADATA */
    /* ============================================== */
    Log("\n=== S11: QueryAdapterInfo (NODEMETADATA) ===\n");
    if (hAdapter) {
        UCHAR buf[4096] = {0};
        D3DKMT_QUERYADAPTERINFO qa = {0};
        qa.hAdapter = hAdapter;
        qa.Type = KMTQAITYPE_NODEMETADATA;
        qa.pPrivateDriverData = buf;
        qa.PrivateDriverDataSize = sizeof(buf);
        NTSTATUS st = D3DKMTQueryAdapterInfo(&qa);
        Log("  NODEMETADATA: 0x%08X\n", st);
        if (st == 0) {
            UINT *u = (UINT*)buf;
            Log("  NumNodes: %u\n", u[0]);
        }
    }

    /* ============================================== */
    /* S12: QueryClockCalibration                     */
    /* ============================================== */
    Log("\n=== S12: QueryClockCalibration ===\n");
    if (hAdapter) {
        D3DKMT_QUERYCLOCKCALIBRATION qc = {0};
        qc.hAdapter = hAdapter;
        qc.NodeOrdinal = 0;
        qc.PhysicalAdapterIndex = 0;
        NTSTATUS st = D3DKMTQueryClockCalibration(&qc);
        Log("  QueryClockCalibration: 0x%08X\n", st);
        if (st == 0) {
            Log("  GpuFrequency:      %llu\n", qc.ClockData.GpuFrequency);
            Log("  GpuClockCounter:   %llu\n", qc.ClockData.GpuClockCounter);
            Log("  CpuClockCounter:   %llu\n", qc.ClockData.CpuClockCounter);
        }
    }

    /* ============================================== */
    /* S13: QueryStatistics (ADAPTER)                 */
    /* ============================================== */
    Log("\n=== S13: QueryStatistics (ADAPTER) ===\n");
    if (hAdapter) {
        D3DKMT_QUERYSTATISTICS qs = {0};
        qs.Type = D3DKMT_QUERYSTATISTICS_ADAPTER;
        qs.AdapterLuid = adapterLuid;
        NTSTATUS st = D3DKMTQueryStatistics(&qs);
        Log("  QueryStatistics(ADAPTER): 0x%08X\n", st);
        if (st == 0) {
            D3DKMT_QUERYSTATISTICS_ADAPTER_INFORMATION *ai = &qs.QueryResult.AdapterInformation;
            Log("  AdapterInformation:\n");
            Log("    NbSegments: %u\n", ai->NbSegments);
            Log("    NodeCount: %u\n", ai->NodeCount);
            Log("    VidPnSourceCount: %u\n", ai->VidPnSourceCount);
            Log("    VSyncEnabled: %u\n", ai->VSyncEnabled);
            Log("    TdrDetectedCount: %u\n", ai->TdrDetectedCount);
        }
    }

    /* ============================================== */
    /* S14: CloseAdapter                              */
    /* ============================================== */
    Log("\n=== S14: CloseAdapter ===\n");
    if (hAdapter) {
        D3DKMT_CLOSEADAPTER ca = {0};
        ca.hAdapter = hAdapter;
        NTSTATUS st = D3DKMTCloseAdapter(&ca);
        Log("  CloseAdapter: 0x%08X\n", st);
    }

    /* ============================================== */
    /* S15: EnumDisplayDevices (Win32 API)           */
    /* ============================================== */
    Log("\n=== S15: EnumDisplayDevices ===\n");
    {
        DISPLAY_DEVICEA dd = {0};
        dd.cb = sizeof(dd);
        for (int i = 0; EnumDisplayDevicesA(NULL, i, &dd, 0); i++) {
            Log("  Display[%d]: %s  (%s)\n", i, dd.DeviceName, dd.DeviceString);
            Log("    StateFlags: 0x%08X\n", dd.StateFlags);
            if (dd.StateFlags & DISPLAY_DEVICE_ATTACHED) {
                DEVMODEA dm = {0};
                dm.dmSize = sizeof(dm);
                if (EnumDisplaySettingsA(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm)) {
                    Log("    Current: %dx%d @ %dHz  BPP=%d\n",
                        dm.dmPelsWidth, dm.dmPelsHeight, dm.dmDisplayFrequency, dm.dmBitsPerPel);
                }
            }
            memset(&dd, 0, sizeof(dd)); dd.cb = sizeof(dd);
        }
    }

    /* ============================================== */
    /* S16: DXGI EnumAdapters                         */
    /* ============================================== */
    Log("\n=== S16: DXGI EnumAdapters ===\n");
    {
        IDXGIFactory *factory = NULL;
        HRESULT hr = CreateDXGIFactory(&IID_IDXGIFactory, (void**)&factory);
        Log("CreateDXGIFactory: 0x%08X\n", hr);
        if (SUCCEEDED(hr)) {
            for (UINT i = 0; ; i++) {
                IDXGIAdapter *adapter = NULL;
                hr = factory->lpVtbl->EnumAdapters(factory, i, &adapter);
                if (hr != S_OK) break;
                DXGI_ADAPTER_DESC desc = {0};
                adapter->lpVtbl->GetDesc(adapter, &desc);
                Log("  DXGI Adapter[%u]:\n", i);
                Log("    Desc: %ls\n", desc.Description);
                Log("    VendorId=0x%04X  DeviceId=0x%04X  SubSys=0x%08X  Rev=%u\n",
                    desc.VendorId, desc.DeviceId, desc.SubSysId, desc.Revision);
                Log("    DedicatedVideo: %llu MB\n", desc.DedicatedVideoMemory / (1024*1024));
                Log("    SharedSystem:   %llu MB\n", desc.SharedSystemMemory / (1024*1024));
                Log("    Luid=%u.%d\n",
                    desc.AdapterLuid.HighPart, desc.AdapterLuid.LowPart);

                for (UINT o = 0; ; o++) {
                    IDXGIOutput *out = NULL;
                    hr = adapter->lpVtbl->EnumOutputs(adapter, o, &out);
                    if (hr != S_OK) break;
                    DXGI_OUTPUT_DESC od = {0};
                    out->lpVtbl->GetDesc(out, &od);
                    Log("    Output[%u]: %s  Bounds=(%ld,%ld)-(%ld,%ld)  Attached=%d  Rotation=%d\n",
                        o, od.DeviceName,
                        od.DesktopCoordinates.left, od.DesktopCoordinates.top,
                        od.DesktopCoordinates.right, od.DesktopCoordinates.bottom,
                        od.AttachedToDesktop, od.Rotation);
                    out->lpVtbl->Release(out);
                }
                adapter->lpVtbl->Release(adapter);
            }
            factory->lpVtbl->Release(factory);
        }
    }

    /* ============================================== */
    /* S17: Check our custom KMD device status        */
    /* ============================================== */
    Log("\n=== S17: Check AMDBC250 KMD device ===\n");
    {
        HANDLE hKmd = CreateFileW(L"\\\\.\\AMDBC250DreamV43",
            GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (hKmd != INVALID_HANDLE_VALUE) {
            DWORD bytesReturned = 0;
            DWORD caps[8] = {0};
            BOOL ok = DeviceIoControl(hKmd, 0x80000800, NULL, 0, caps, sizeof(caps), &bytesReturned, NULL);
            Log("  KMD device: OPEN  IOCTL_GET_CAPS: %s  bytesReturned=%lu\n",
                ok ? "OK" : "FAIL", bytesReturned);
            if (ok && bytesReturned >= 28) {
                Log("    Version=%lu.%lu.%lu  CUs=%lu  GPUCLK=%lu MHz  MEMCLK=%lu MHz  Temp=%lu  Throttle=%lu  ThrCount=%lu\n",
                    caps[0]/100, (caps[0]/10)%10, caps[0]%10,
                    caps[1], caps[2], caps[3], caps[4], caps[5], caps[6]);
            }
            DWORD vram[4] = {0};
            ok = DeviceIoControl(hKmd, 0x80000804, NULL, 0, vram, sizeof(vram), &bytesReturned, NULL);
            Log("  IOCTL_GET_VRAM_INFO: %s  bytesReturned=%lu\n", ok ? "OK" : "FAIL", bytesReturned);
            if (ok && bytesReturned >= 12) {
                Log("    Total=%lu MB  Visible=%lu MB  Used=%lu MB\n", vram[0], vram[1], vram[2]);
            }

            DWORD bars[16] = {0};
            ok = DeviceIoControl(hKmd, 0x80000BB8, NULL, 0, bars, sizeof(bars), &bytesReturned, NULL);
            Log("  IOCTL_GET_RESOURCE_BARS: %s  bytesReturned=%lu\n", ok ? "OK" : "FAIL", bytesReturned);
            if (ok && bytesReturned >= 48) {
                Log("    DeviceStarted=%lu  MmioMapped=%lu  MmioPA=0x%08lX%08lX  MmioSize=0x%X\n",
                    bars[0], bars[1], bars[3], bars[2], bars[4]);
                Log("    FbPA=0x%08lX%08lX  FbSize=0x%X\n", bars[6], bars[5], bars[7]);
            }

            CloseHandle(hKmd);
        } else {
            Log("  KMD device: NOT FOUND (error=%lu) - driver not installed\n", GetLastError());
        }
    }

    /* ============================================== */
    /* S18: Try to create D3D11 device via BasicDisplay */
    /* ============================================== */
    Log("\n=== S18: D3D11CreateDevice (BasicDisplay) ===\n");
    {
        HMODULE hD3d11 = LoadLibraryW(L"d3d11.dll");
        if (hD3d11) {
            typedef HRESULT (WINAPI *PFN_D3D11_CREATE)(
                void*, D3D_DRIVER_TYPE, HMODULE, UINT,
                const D3D_FEATURE_LEVEL*, UINT, UINT,
                void**, D3D_FEATURE_LEVEL*, void**);
            PFN_D3D11_CREATE pCreate = (PFN_D3D11_CREATE)GetProcAddress(hD3d11, "D3D11CreateDevice");
            if (pCreate) {
                D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
                D3D_FEATURE_LEVEL got = {0};
                HRESULT hr = pCreate(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
                    levels, 3, D3D11_SDK_VERSION, NULL, &got, NULL);
                Log("  D3D11CreateDevice(HARDWARE): 0x%08X  FeatureLevel=0x%08X\n", hr, got);
                if (SUCCEEDED(hr)) {
                    Log("  SUCCESS: D3D11 device created via BasicDisplay!\n");
                }
                hr = pCreate(NULL, D3D_DRIVER_TYPE_WARP, NULL, 0,
                    levels, 3, D3D11_SDK_VERSION, NULL, &got, NULL);
                Log("  D3D11CreateDevice(WARP): 0x%08X  FeatureLevel=0x%08X\n", hr, got);
            }
            FreeLibrary(hD3d11);
        } else {
            Log("  d3d11.dll not loadable\n");
        }
    }

    /* ============================================== */
    /* S19: Try to create D3D9 via BasicDisplay       */
    /* ============================================== */
    Log("\n=== S19: Direct3DCreate9 (BasicDisplay) ===\n");
    {
        HMODULE hD3d9 = LoadLibraryW(L"d3d9.dll");
        if (hD3d9) {
            typedef IDirect3D9* (WINAPI *PFN_D3DCREATE9)(UINT);
            PFN_D3DCREATE9 pCreate = (PFN_D3DCREATE9)GetProcAddress(hD3d9, "Direct3DCreate9");
            if (pCreate) {
                IDirect3D9 *d3d9 = pCreate(D3D_SDK_VERSION);
                Log("  Direct3DCreate9(32): %p\n", d3d9);
                if (d3d9) {
                    UINT count = d3d9->lpVtbl->GetAdapterCount(d3d9);
                    Log("  AdapterCount: %u\n", count);
                    for (UINT a = 0; a < count; a++) {
                        D3DADAPTER_IDENTIFIER9 ident = {0};
                        HRESULT hr = d3d9->lpVtbl->GetAdapterIdentifier(d3d9, a, 0, &ident);
                        if (SUCCEEDED(hr)) {
                            Log("  Adapter[%u]: %s  VendorId=0x%04X  DeviceId=0x%04X  Driver=%s\n",
                                a, ident.Description, ident.VendorId, ident.DeviceId, ident.Driver);
                        }
                        D3DCAPS9 caps = {0};
                        hr = d3d9->lpVtbl->GetDeviceCaps(d3d9, a, D3DDEVTYPE_HAL, &caps);
                        Log("    HAL caps: 0x%08X (hr=0x%08X)\n", caps.DevCaps, hr);
                        D3DDISPLAYMODE mode = {0};
                        hr = d3d9->lpVtbl->GetAdapterDisplayMode(d3d9, a, &mode);
                        Log("    Current mode: %dx%d @ %dHz fmt=%d (hr=0x%08X)\n",
                            mode.Width, mode.Height, mode.RefreshRate, mode.Format, hr);
                    }
                    d3d9->lpVtbl->Release(d3d9);
                }
            }
            FreeLibrary(hD3d9);
        }
    }

    /* ============================================== */
    /* S20: DxgkDdiEscape — UMD→KMD via D3DKMTEscape */
    /* ============================================== */
    Log("\n=== S20: DxgkDdiEscape ===\n");
    {
        NTSTATUS st2;
        /* Re-open adapter for escape tests */
        HDC hdc2 = GetDC(NULL);
        D3DKMT_OPENADAPTERFROMHDC oah2 = {0};
        oah2.hDc = hdc2;
        st2 = D3DKMTOpenAdapterFromHdc(&oah2);
        Log("  OpenAdapter: 0x%08X h=0x%08X\n", st2, oah2.hAdapter);
        ReleaseDC(NULL, hdc2);

        if (st2 == 0) {
            /* CreateDevice */
            D3DKMT_CREATEDEVICE cd2 = {0};
            cd2.hAdapter = oah2.hAdapter;
            st2 = D3DKMTCreateDevice(&cd2);
            Log("  CreateDevice: 0x%08X hDevice=0x%08X\n", st2, cd2.hDevice);

            /* S20a: GET_CAPS (cmd=0x01) */
            Log("\n--- S20a: ESCAPE GET_CAPS (cmd=0x01) ---\n");
            {
                UCHAR ebuf[512] = {0};
                typedef struct { ULONG CommandId; NTSTATUS Status; ULONG OutputSize; } EH;
                EH *hdr = (EH*)ebuf;
                hdr->CommandId = 0x01;
                D3DKMT_ESCAPE esc = {0};
                esc.hAdapter = oah2.hAdapter;
                esc.hDevice = cd2.hDevice;
                esc.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
                esc.pPrivateDriverData = ebuf;
                esc.PrivateDriverDataSize = sizeof(ebuf);
                NTSTATUS est = D3DKMTEscape(&esc);
                Log("  D3DKMTEscape: 0x%08X  DriverStatus: 0x%08X  OutputSize: %lu\n",
                    est, hdr->Status, hdr->OutputSize);
                if (est == 0 && hdr->Status == 0) {
                    PULONG d = (PULONG)(hdr + 1);
                    Log("  VendorId=0x%08X  DeviceId=0x%08X\n", d[0], d[1]);
                    Log("  CUs=%lu  Shaders=%lu  Pipes=%lu\n", d[2], d[3], d[4]);
                    Log("  GPUclk=%lu MHz  MemClk=%lu MHz\n", d[5], d[6]);
                }
            }

            /* S20b: GET_VRAM_INFO (cmd=0x02) */
            Log("\n--- S20b: ESCAPE GET_VRAM_INFO (cmd=0x02) ---\n");
            {
                UCHAR ebuf[512] = {0};
                typedef struct { ULONG CommandId; NTSTATUS Status; ULONG OutputSize; } EH;
                EH *hdr = (EH*)ebuf;
                hdr->CommandId = 0x02;
                D3DKMT_ESCAPE esc = {0};
                esc.hAdapter = oah2.hAdapter;
                esc.hDevice = cd2.hDevice;
                esc.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
                esc.pPrivateDriverData = ebuf;
                esc.PrivateDriverDataSize = sizeof(ebuf);
                NTSTATUS est = D3DKMTEscape(&esc);
                Log("  D3DKMTEscape: 0x%08X  DriverStatus: 0x%08X  OutputSize: %lu\n",
                    est, hdr->Status, hdr->OutputSize);
                if (est == 0 && hdr->Status == 0) {
                    PULONG d = (PULONG)(hdr + 1);
                    Log("  TotalVRAM=%lu MB  Visible=%lu MB\n", d[0], d[1]);
                    Log("  VramPA=0x%08X_%08X\n", d[3], d[2]);
                }
            }

            /* S20c: GET_FW_VERSION (cmd=0x05) */
            Log("\n--- S20c: ESCAPE GET_FW_VERSION (cmd=0x05) ---\n");
            {
                UCHAR ebuf[512] = {0};
                typedef struct { ULONG CommandId; NTSTATUS Status; ULONG OutputSize; } EH;
                EH *hdr = (EH*)ebuf;
                hdr->CommandId = 0x05;
                D3DKMT_ESCAPE esc = {0};
                esc.hAdapter = oah2.hAdapter;
                esc.hDevice = cd2.hDevice;
                esc.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
                esc.pPrivateDriverData = ebuf;
                esc.PrivateDriverDataSize = sizeof(ebuf);
                NTSTATUS est = D3DKMTEscape(&esc);
                Log("  D3DKMTEscape: 0x%08X  DriverStatus: 0x%08X\n", est, hdr->Status);
                if (est == 0 && hdr->Status == 0) {
                    Log("  FW Version: %s\n", (const char*)(hdr + 1));
                }
            }

            /* Cleanup */
            if (cd2.hDevice) {
                D3DKMT_DESTROYDEVICE dd2 = {0};
                dd2.hDevice = cd2.hDevice;
                D3DKMTDestroyDevice(&dd2);
            }
            D3DKMT_CLOSEADAPTER ca2 = {0};
            ca2.hAdapter = oah2.hAdapter;
            D3DKMTCloseAdapter(&ca2);
        }
    }

    /* ============================================== */
    /* S21: INIT_HARDWARE + READ_REG via IOCTL        */
    /* ============================================== */
    Log("\n=== S21: INIT_HARDWARE + READ_REG ===\n");
    {
        HANDLE hKmd2 = CreateFileW(L"\\\\.\\AMDBC250DreamV43",
            GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (hKmd2 != INVALID_HANDLE_VALUE) {
            DWORD br2 = 0;

            /* Step 1: Check current resource bars */
            DWORD bars[16] = {0};
            BOOL ok2 = DeviceIoControl(hKmd2, 0x80000BB8, NULL, 0, bars, sizeof(bars), &br2, NULL);
            Log("  Before INIT: ResourceBars %s  MmioMapped=%lu  DeviceStarted=%lu\n",
                ok2 ? "OK" : "FAIL", bars[1], bars[0]);

            /* Step 2: INIT_HARDWARE with BAR5 (0xFE800000, 256KB) */
            /* BAR5 is safe for reads, known from Linux probe */
            typedef struct { UINT64 MmioPA; UINT32 MmioSize; UINT32 Flags; UINT64 FbPA; UINT32 FbSize; } INIT_HW;
            INIT_HW ihw = {0};
            ihw.MmioPA   = 0xFE800000ULL;  /* BAR5 — GPU registers (read-only safe) */
            ihw.MmioSize = 0x40000;         /* 256 KB */
            ihw.Flags    = 1;               /* NBIO_MAP flag — map without GPU init */
            ihw.FbPA     = 0xC0000000ULL;   /* BAR0 — VRAM framebuffer */
            ihw.FbSize   = 0x10000000;      /* 256 MB */

            ok2 = DeviceIoControl(hKmd2, 0x80000B80, &ihw, sizeof(ihw), &ihw, sizeof(ihw), &br2, NULL);
            Log("  INIT_HARDWARE: %s  bytesReturned=%lu\n", ok2 ? "OK" : "FAIL", br2);

            if (ok2) {
                /* Step 3: Verify resource bars after init */
                RtlZeroMemory(bars, sizeof(bars));
                ok2 = DeviceIoControl(hKmd2, 0x80000BB8, NULL, 0, bars, sizeof(bars), &br2, NULL);
                Log("  After INIT: ResourceBars %s  MmioMapped=%lu  MmioPA=0x%08lX%08lX  MmioSize=0x%X\n",
                    ok2 ? "OK" : "FAIL", bars[1], bars[3], bars[2], bars[4]);

                /* Step 4: READ_REG — GPU_ID (offset 0x0) */
                typedef struct { UINT32 Offset; UINT32 Value; } REG_ACC;
                REG_ACC ra = {0};
                ra.Offset = 0x0000;  /* GPU_ID */
                ok2 = DeviceIoControl(hKmd2, 0x80000B88, &ra, sizeof(ra), &ra, sizeof(ra), &br2, NULL);
                Log("  READ_REG GPU_ID (0x0000): %s  Value=0x%08X\n", ok2 ? "OK" : "FAIL", ra.Value);

                /* Step 5: READ_REG — GRBM_STATUS (offset 0x2004) */
                ra.Offset = 0x2004;  /* GRBM_STATUS */
                ok2 = DeviceIoControl(hKmd2, 0x80000B88, &ra, sizeof(ra), &ra, sizeof(ra), &br2, NULL);
                Log("  READ_REG GRBM_STATUS (0x2004): %s  Value=0x%08X\n", ok2 ? "OK" : "FAIL", ra.Value);

                /* Step 6: READ_REG — Clock register (0xD00 + 4 = GPU_CLK) */
                ra.Offset = 0x0D04;
                ok2 = DeviceIoControl(hKmd2, 0x80000B88, &ra, sizeof(ra), &ra, sizeof(ra), &br2, NULL);
                Log("  READ_REG CLK (0x0D04): %s  Value=0x%08X\n", ok2 ? "OK" : "FAIL", ra.Value);

                /* Step 7: READ_REG — HDP registers (0x05A0) */
                ra.Offset = 0x05A0;
                ok2 = DeviceIoControl(hKmd2, 0x80000B88, &ra, sizeof(ra), &ra, sizeof(ra), &br2, NULL);
                Log("  READ_REG HDP (0x05A0): %s  Value=0x%08X\n", ok2 ? "OK" : "FAIL", ra.Value);

                /* Step 8: READ_REG — Scratch test (0x2074) */
                ra.Offset = 0x2074;
                ok2 = DeviceIoControl(hKmd2, 0x80000B88, &ra, sizeof(ra), &ra, sizeof(ra), &br2, NULL);
                Log("  READ_REG SCRATCH (0x2074): %s  Value=0x%08X\n", ok2 ? "OK" : "FAIL", ra.Value);
            }

            CloseHandle(hKmd2);
        } else {
            Log("  KMD device: NOT FOUND (error=%lu)\n", GetLastError());
        }
    }

    /* ============================================== */
    /* S22: ALLOC_VIDMEM via IOCTL                    */
    /* ============================================== */
    Log("\n=== S22: ALLOC_VIDMEM ===\n");
    {
        HANDLE hKmd3 = CreateFileW(L"\\\\.\\AMDBC250DreamV43",
            GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (hKmd3 != INVALID_HANDLE_VALUE) {
            DWORD br3 = 0;

            /* Allocate 64KB contiguous memory */
            ULONG allocInput[3] = { 64 * 1024, 0, 0 }; /* Size=64KB, Alignment=0, Flags=0 */
            ULONG64 allocOutput[2] = {0}; /* PA + VA */
            BOOL ok3 = DeviceIoControl(hKmd3, 0x80000840,
                allocInput, sizeof(allocInput),
                allocOutput, sizeof(allocOutput),
                &br3, NULL);
            Log("  ALLOC_VIDMEM (64KB): %s  bytesReturned=%lu\n", ok3 ? "OK" : "FAIL", br3);
            if (ok3 && br3 >= 16) {
                Log("    PhysicalAddr=0x%llX  VirtualAddr=0x%llX\n", allocOutput[0], allocOutput[1]);

                if (allocOutput[1] != 0) {
                    /* Write test pattern to the allocated buffer */
                    PUCHAR buf = (PUCHAR)(ULONG_PTR)allocOutput[1];
                    ULONG i;
                    for (i = 0; i < 256 && i < 64*1024; i++) {
                        buf[i] = (UCHAR)(i & 0xFF);
                    }
                    /* Read back and verify */
                    UCHAR first = buf[0];
                    UCHAR last = buf[255];
                    Log("    Write/Read test: buf[0]=0x%02X (expect 0x00)  buf[255]=0x%02X (expect 0xFF)\n",
                        first, last);

                    /* Free the buffer */
                    PVOID freeInput[1] = { (PVOID)(ULONG_PTR)allocOutput[1] };
                    DeviceIoControl(hKmd3, 0x80000934, freeInput, sizeof(freeInput), NULL, 0, &br3, NULL);
                    Log("    FREED buffer OK\n");
                }
            }

            CloseHandle(hKmd3);
        } else {
            Log("  KMD device: NOT FOUND (error=%lu)\n", GetLastError());
        }
    }

    Log("\n=== WDDM Probe Complete ===\n");
    fclose(g);
    printf("Done. Check output\\wddm-probe.log\n");
    return 0;
}
