/*
 * Simple GPU Test - BC-250 D3D12 Test
 * Tests if UMD DLL can be loaded and D3D12 device created
 */

#define COBJMACROS
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <stdio.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

int main()
{
    HRESULT hr;
    IDXGIFactory4* pFactory = NULL;
    IDXGIAdapter1* pAdapter = NULL;
    ID3D12Device* pDevice = NULL;
    UINT adapterIndex = 0;
    DXGI_ADAPTER_DESC1 adapterDesc;

    printf("=== BC-250 GPU Simple Test ===\n\n");

    /* Step 1: Create DXGI Factory */
    hr = CreateDXGIFactory2(0, &IID_IDXGIFactory4, (void**)&pFactory);
    if (FAILED(hr)) {
        printf("[FAIL] CreateDXGIFactory2 failed: 0x%08X\n", hr);
        return 1;
    }
    printf("[OK] DXGI Factory created\n");

    /* Step 2: Enumerate adapters */
    while (IDXGIFactory4_EnumAdapters1(pFactory, adapterIndex, &pAdapter) != DXGI_ERROR_NOT_FOUND) {
        IDXGIAdapter1_GetDesc1(pAdapter, &adapterDesc);
        
        printf("\nAdapter %u: %ls\n", adapterIndex, adapterDesc.Description);
        printf("  Vendor ID: 0x%04X\n", adapterDesc.VendorId);
        printf("  Device ID: 0x%04X\n", adapterDesc.DeviceId);
        printf("  Dedicated VRAM: %llu MB\n", adapterDesc.DedicatedVideoMemory / (1024 * 1024));
        
        /* Check if it's our AMD GPU */
        if (adapterDesc.VendorId == 0x1002 && adapterDesc.DeviceId == 0x13FE) {
            printf("  >>> Found BC-250! Testing D3D12...\n");
            
            /* Step 3: Create D3D12 device */
            hr = D3D12CreateDevice(
                (IUnknown*)pAdapter,
                D3D_FEATURE_LEVEL_12_0,
                &IID_ID3D12Device,
                (void**)&pDevice
            );
            
            if (SUCCEEDED(hr)) {
                printf("[SUCCESS] D3D12 Device created!\n");
                
                D3D_FEATURE_LEVEL featureLevel;
                hr = ID3D12Device_CheckFeatureSupport(pDevice, D3D12_FEATURE_FEATURE_LEVELS, 
                    &featureLevel, sizeof(featureLevel));
                
                if (SUCCEEDED(hr)) {
                    printf("  Feature Level: 0x%04X\n", featureLevel);
                }
                
                ID3D12Device_Release(pDevice);
            } else {
                printf("[FAIL] D3D12CreateDevice failed: 0x%08X\n", hr);
                printf("  This is EXPECTED - UMD is a stub!\n");
            }
            
            break;
        }
        
        IDXGIAdapter1_Release(pAdapter);
        pAdapter = NULL;
        adapterIndex++;
    }

    if (pAdapter) IDXGIAdapter1_Release(pAdapter);
    if (pFactory) IDXGIFactory4_Release(pFactory);

    printf("\n=== Test Complete ===\n");
    return 0;
}
