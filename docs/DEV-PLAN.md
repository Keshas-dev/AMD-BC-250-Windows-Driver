# BC-250 Driver Development Plan

## Priority 1: Vulkan ICD - Real Rendering Test
- Create a Vulkan test that actually renders (not just API calls)
- Test swapchain creation, command buffers, draw calls
- Verify GPU can output pixels to display

## Priority 2: GPGPU Support
- Vulkan Compute Shaders
- OpenCL via clover/rusticl
- Can enable BC-250 for AI/ML workloads

## Priority 3: D3D11/D3D12
- Requires WDDM miniport (blocked on Win11 26100+)
- Alternative: D3D11On12 or D3D12 via Vulkan (Vulkan interop)
- DXVK translation layer?