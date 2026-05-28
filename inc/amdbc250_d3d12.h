/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    amdbc250_d3d12.h

Abstract:
    Definitions for the Microsoft Direct3D version 12 DDI (Driver Development Interface).

    This header defines the structures and function prototypes required for the
    User-Mode Display Driver (UMD) to implement Direct3D 12 functionality.
    
    Includes support for:
    - Adapter and device management
    - Resource and heap management
    - Command queue and command list execution
    - Pipeline state and root signatures
    - Descriptor heaps (CBV, SRV, RTV, DSV, Sampler)
    - Rendering and compute operations
    - Synchronization via fences

Environment:
    User mode (UMD)

--*/

#ifndef _AMDBC250_D3D12_H_
#define _AMDBC250_D3D12_H_

#include <d3d12umddi.h>

/* Forward declarations */
typedef struct _AMDBC250_UMD_DEVICE *PAMDBC250_UMD_DEVICE;

/* ============================================================================
   Adapter Management
   ============================================================================ */

HRESULT APIENTRY Bc250D3D12GetSupportedVersions(
    D3D12DDI_HADAPTER hAdapter,
    _Inout_ UINT32* puEntries,
    _Out_writes_opt_(*puEntries) UINT64* pSupportedDDIInterfaceVersions
    );

HRESULT APIENTRY Bc250D3D12GetCaps(
    D3D12DDI_HADAPTER hAdapter,
    _In_ CONST D3D12DDIARG_GETCAPS* pArgs
    );

HRESULT APIENTRY Bc250D3D12GetOptionalDDITables(
    D3D12DDI_HADAPTER hAdapter,
    _Inout_ UINT32* puEntries,
    _Out_writes_opt_(*puEntries) D3D12DDI_TABLE_REQUEST* pRequests
    );

HRESULT APIENTRY Bc250D3D12FillDDITable(
    D3D12DDI_HADAPTER hAdapter,
    D3D12DDI_TABLE_TYPE TableType,
    _Inout_ VOID* pTable,
    SIZE_T TableSize,
    UINT FeatureVersion,
    _In_opt_ D3D12DDI_HRTTABLE hRTTable
    );

/* ============================================================================
   Device Management
   ============================================================================ */

SIZE_T APIENTRY Bc250D3D12CalcPrivateDeviceSize(
    D3D12DDI_HADAPTER hAdapter,
    _In_ CONST D3D12DDIARG_CALCPRIVATEDEVICESIZE* pArgs
    );

HRESULT APIENTRY Bc250D3D12CreateDevice(
    D3D12DDI_HADAPTER hAdapter,
    _In_ CONST D3D12DDIARG_CREATEDEVICE_0003* pArgs
    );

VOID APIENTRY Bc250D3D12DestroyDevice(
    D3D12DDI_HDEVICE hDevice
    );

/* ============================================================================
   Heap Management (VRAM Allocation)
   ============================================================================ */

SIZE_T APIENTRY Bc250D3D12CalcPrivateHeapSize(
    D3D12DDI_HDEVICE hDevice,
    _In_ CONST D3D12DDIARG_CREATEHEAP* pArgs
    );

HRESULT APIENTRY Bc250D3D12CreateHeap(
    D3D12DDI_HDEVICE hDevice,
    _In_ CONST D3D12DDIARG_CREATEHEAP* pArgs,
    D3D12DDI_HHEAP hHeap,
    D3D12DDI_HRTHEAP hHeapRT
    );

VOID APIENTRY Bc250D3D12DestroyHeap(
    D3D12DDI_HDEVICE hDevice,
    D3D12DDI_HHEAP hHeap
    );

/* ============================================================================
   Resource Management (GPU Memory)
   ============================================================================ */

SIZE_T APIENTRY Bc250D3D12CalcPrivateResourceSize(
    D3D12DDI_HDEVICE hDevice,
    _In_ CONST D3D12DDIARG_CREATERESOURCE* pArgs
    );

HRESULT APIENTRY Bc250D3D12CreateResource(
    D3D12DDI_HDEVICE hDevice,
    _In_ CONST D3D12DDIARG_CREATERESOURCE* pArgs,
    D3D12DDI_HRESOURCE hResource,
    D3D12DDI_HRTRESOURCE hResourceRT
    );

VOID APIENTRY Bc250D3D12DestroyResource(
    D3D12DDI_HDEVICE hDevice,
    D3D12DDI_HRESOURCE hResource
    );

HRESULT APIENTRY Bc250D3D12MapResource(
    D3D12DDI_HDEVICE hDevice,
    D3D12DDI_HRESOURCE hResource,
    UINT Subresource,
    _In_opt_ CONST D3D12DDI_BOX* pBox,
    _Out_ VOID** ppData
    );

VOID APIENTRY Bc250D3D12UnmapResource(
    D3D12DDI_HDEVICE hDevice,
    D3D12DDI_HRESOURCE hResource,
    UINT Subresource
    );

/* ============================================================================
   Command Queue Management
   ============================================================================ */

SIZE_T APIENTRY Bc250D3D12CalcPrivateCommandQueueSize(
    D3D12DDI_HDEVICE hDevice,
    _In_ CONST D3D12DDIARG_CREATECOMMANDQUEUE* pArgs
    );

HRESULT APIENTRY Bc250D3D12CreateCommandQueue(
    D3D12DDI_HDEVICE hDevice,
    _In_ CONST D3D12DDIARG_CREATECOMMANDQUEUE* pArgs,
    D3D12DDI_HCOMMANDQUEUE hCommandQueue,
    D3D12DDI_HRTCOMMANDQUEUE hCommandQueueRT
    );

VOID APIENTRY Bc250D3D12DestroyCommandQueue(
    D3D12DDI_HDEVICE hDevice,
    D3D12DDI_HCOMMANDQUEUE hCommandQueue
    );

/* ============================================================================
   Command List Management
   ============================================================================ */

SIZE_T APIENTRY Bc250D3D12CalcPrivateCommandListSize(
    D3D12DDI_HDEVICE hDevice,
    _In_ CONST D3D12DDIARG_CREATECOMMANDLIST* pArgs
    );

HRESULT APIENTRY Bc250D3D12CreateCommandList(
    D3D12DDI_HDEVICE hDevice,
    _In_ CONST D3D12DDIARG_CREATECOMMANDLIST* pArgs,
    D3D12DDI_HCOMMANDLIST hCommandList,
    D3D12DDI_HRTCOMMANDLIST hCommandListRT
    );

VOID APIENTRY Bc250D3D12DestroyCommandList(
    D3D12DDI_HDEVICE hDevice,
    D3D12DDI_HCOMMANDLIST hCommandList
    );

VOID APIENTRY Bc250D3D12CloseCommandList(
    D3D12DDI_HDEVICE hDevice,
    D3D12DDI_HCOMMANDLIST hCommandList
    );

HRESULT APIENTRY Bc250D3D12ExecuteCommandLists(
    D3D12DDI_HDEVICE hDevice,
    UINT NumCommandLists,
    _In_reads_(NumCommandLists) CONST D3D12DDI_HCOMMANDLIST* phCommandLists
    );

/* ============================================================================
   Fence Synchronization
   ============================================================================ */

SIZE_T APIENTRY Bc250D3D12CalcPrivateFenceSize(
    D3D12DDI_HDEVICE hDevice,
    _In_ CONST D3D12DDIARG_CREATEFENCE* pArgs
    );

HRESULT APIENTRY Bc250D3D12CreateFence(
    D3D12DDI_HDEVICE hDevice,
    _In_ CONST D3D12DDIARG_CREATEFENCE* pArgs,
    D3D12DDI_HFENCE hFence,
    D3D12DDI_HRTFENCE hFenceRT
    );

VOID APIENTRY Bc250D3D12DestroyFence(
    D3D12DDI_HDEVICE hDevice,
    D3D12DDI_HFENCE hFence
    );

VOID APIENTRY Bc250D3D12SetFenceValue(
    D3D12DDI_HDEVICE hDevice,
    D3D12DDI_HFENCE hFence,
    UINT64 Value
    );

VOID APIENTRY Bc250D3D12WaitForFence(
    D3D12DDI_HDEVICE hDevice,
    D3D12DDI_HFENCE hFence,
    UINT64 Value
    );

/* ============================================================================
   Memory Residency Management
   ============================================================================ */

VOID APIENTRY Bc250D3D12MakeResident(
    D3D12DDI_HDEVICE hDevice,
    UINT NumObjects,
    _In_reads_(NumObjects) CONST D3D12DDI_HANDLE* phObjects
    );

VOID APIENTRY Bc250D3D12Evict(
    D3D12DDI_HDEVICE hDevice,
    UINT NumObjects,
    _In_reads_(NumObjects) CONST D3D12DDI_HANDLE* phObjects
    );

/* ============================================================================
   Command Signature Management
   ============================================================================ */

SIZE_T APIENTRY Bc250D3D12CalcPrivateCommandSignatureSize(
    D3D12DDI_HDEVICE hDevice,
    _In_ CONST D3D12DDIARG_CREATECOMMANDSIGNATURE* pArgs
    );

HRESULT APIENTRY Bc250D3D12CreateCommandSignature(
    D3D12DDI_HDEVICE hDevice,
    _In_ CONST D3D12DDIARG_CREATECOMMANDSIGNATURE* pArgs,
    D3D12DDI_HCOMMANDSIGNATURE hCommandSignature,
    D3D12DDI_HRTCOMMANDSIGNATURE hCommandSignatureRT
    );

VOID APIENTRY Bc250D3D12DestroyCommandSignature(
    D3D12DDI_HDEVICE hDevice,
    D3D12DDI_HCOMMANDSIGNATURE hCommandSignature
    );

/* ============================================================================
   Pipeline State and Root Signatures
   ============================================================================ */

SIZE_T APIENTRY Bc250D3D12CalcPrivatePipelineStateSize(
    D3D12DDI_HDEVICE hDevice,
    _In_ CONST D3D12DDIARG_CREATEPIPELINESTATE* pArgs
    );

HRESULT APIENTRY Bc250D3D12CreatePipelineState(
    D3D12DDI_HDEVICE hDevice,
    _In_ CONST D3D12DDIARG_CREATEPIPELINESTATE* pArgs,
    D3D12DDI_HPIPELINESTATE hPipelineState,
    D3D12DDI_HRTPIPELINESTATE hPipelineStateRT
    );

VOID APIENTRY Bc250D3D12DestroyPipelineState(
    D3D12DDI_HDEVICE hDevice,
    D3D12DDI_HPIPELINESTATE hPipelineState
    );

SIZE_T APIENTRY Bc250D3D12CalcPrivateRootSignatureSize(
    D3D12DDI_HDEVICE hDevice,
    _In_ CONST D3D12DDIARG_CREATEROOTSIGNATURE* pArgs
    );

HRESULT APIENTRY Bc250D3D12CreateRootSignature(
    D3D12DDI_HDEVICE hDevice,
    _In_ CONST D3D12DDIARG_CREATEROOTSIGNATURE* pArgs,
    D3D12DDI_HROOTSIGNATURE hRootSignature,
    D3D12DDI_HRTROOTSIGNATURE hRootSignatureRT
    );

VOID APIENTRY Bc250D3D12DestroyRootSignature(
    D3D12DDI_HDEVICE hDevice,
    D3D12DDI_HROOTSIGNATURE hRootSignature
    );

/* ============================================================================
   Descriptor Heap Management
   ============================================================================ */

SIZE_T APIENTRY Bc250D3D12CalcPrivateDescriptorHeapSize(
    D3D12DDI_HDEVICE hDevice,
    _In_ CONST D3D12DDIARG_CREATE_DESCRIPTOR_HEAP* pArgs
    );

HRESULT APIENTRY Bc250D3D12CreateDescriptorHeap(
    D3D12DDI_HDEVICE hDevice,
    _In_ CONST D3D12DDIARG_CREATE_DESCRIPTOR_HEAP* pArgs,
    D3D12DDI_HDESCRIPTORHEAP hHeap,
    D3D12DDI_HRTDESCRIPTORHEAP hHeapRT
    );

VOID APIENTRY Bc250D3D12DestroyDescriptorHeap(
    D3D12DDI_HDEVICE hDevice,
    D3D12DDI_HDESCRIPTORHEAP hHeap
    );

/* ============================================================================
   Descriptor Creation (Shader Resource View, RTV, DSV, CBV, Sampler)
   ============================================================================ */

VOID APIENTRY Bc250D3D12CreateShaderResourceView(
    D3D12DDI_HDEVICE hDevice,
    _In_ CONST D3D12DDIARG_CREATESHADERRESOURCEVIEW* pArgs,
    D3D12DDI_CPU_DESCRIPTOR_HANDLE DescriptorHandle
    );

VOID APIENTRY Bc250D3D12CreateConstantBufferView(
    D3D12DDI_HDEVICE hDevice,
    _In_ CONST D3D12DDIARG_CREATECONSTANTBUFFERVIEW* pArgs,
    D3D12DDI_CPU_DESCRIPTOR_HANDLE DescriptorHandle
    );

VOID APIENTRY Bc250D3D12CreateSampler(
    D3D12DDI_HDEVICE hDevice,
    _In_ CONST D3D12DDIARG_CREATESAMPLER* pArgs,
    D3D12DDI_CPU_DESCRIPTOR_HANDLE DescriptorHandle
    );

VOID APIENTRY Bc250D3D12CreateUnorderedAccessView(
    D3D12DDI_HDEVICE hDevice,
    _In_ CONST D3D12DDIARG_CREATEUNORDEREDACCESSVIEW* pArgs,
    D3D12DDI_CPU_DESCRIPTOR_HANDLE DescriptorHandle
    );

VOID APIENTRY Bc250D3D12CreateRenderTargetView(
    D3D12DDI_HDEVICE hDevice,
    _In_ CONST D3D12DDIARG_CREATERENDERTARGETVIEW* pArgs,
    D3D12DDI_CPU_DESCRIPTOR_HANDLE DescriptorHandle
    );

VOID APIENTRY Bc250D3D12CreateDepthStencilView(
    D3D12DDI_HDEVICE hDevice,
    _In_ CONST D3D12DDIARG_CREATEDEPTHSTENCILVIEW* pArgs,
    D3D12DDI_CPU_DESCRIPTOR_HANDLE DescriptorHandle
    );

VOID APIENTRY Bc250D3D12CopyDescriptors(
    D3D12DDI_HDEVICE hDevice,
    _In_ CONST D3D12DDIARG_COPY_DESCRIPTORS* pArgs
    );

/* ============================================================================
   Descriptor Heap Binding
   ============================================================================ */

VOID APIENTRY Bc250D3D12SetDescriptorHeaps(
    D3D12DDI_HDEVICE hDevice,
    UINT NumDescriptorHeaps,
    _In_reads_(NumDescriptorHeaps) CONST D3D12DDI_HDESCRIPTORHEAP* phDescriptorHeaps
    );

/* ============================================================================
   Root Signature Binding
   ============================================================================ */

VOID APIENTRY Bc250D3D12SetGraphicsRootSignature(
    D3D12DDI_HDEVICE hDevice,
    D3D12DDI_HROOTSIGNATURE hRootSignature
    );

VOID APIENTRY Bc250D3D12SetComputeRootSignature(
    D3D12DDI_HDEVICE hDevice,
    D3D12DDI_HROOTSIGNATURE hRootSignature
    );

/* ============================================================================
   Pipeline State Binding
   ============================================================================ */

VOID APIENTRY Bc250D3D12SetPipelineState(
    D3D12DDI_HDEVICE hDevice,
    D3D12DDI_HPIPELINESTATE hPipelineState
    );

/* ============================================================================
   Root Parameter Binding (Graphics)
   ============================================================================ */

VOID APIENTRY Bc250D3D12SetGraphicsRootDescriptorTable(
    D3D12DDI_HDEVICE hDevice,
    UINT RootParameterIndex,
    D3D12DDI_GPU_DESCRIPTOR_HANDLE BaseDescriptor
    );

VOID APIENTRY Bc250D3D12SetGraphicsRoot32BitConstant(
    D3D12DDI_HDEVICE hDevice,
    UINT RootParameterIndex,
    UINT Value,
    UINT DestOffsetIn32BitValues
    );

VOID APIENTRY Bc250D3D12SetGraphicsRoot32BitConstants(
    D3D12DDI_HDEVICE hDevice,
    UINT RootParameterIndex,
    UINT Num32BitValuesToSet,
    _In_reads_(Num32BitValuesToSet) CONST VOID* pSrcData,
    UINT DestOffsetIn32BitValues
    );

VOID APIENTRY Bc250D3D12SetGraphicsRootConstantBufferView(
    D3D12DDI_HDEVICE hDevice,
    UINT RootParameterIndex,
    D3D12DDI_GPU_VIRTUAL_ADDRESS BufferLocation
    );

VOID APIENTRY Bc250D3D12SetGraphicsRootShaderResourceView(
    D3D12DDI_HDEVICE hDevice,
    UINT RootParameterIndex,
    D3D12DDI_GPU_VIRTUAL_ADDRESS BufferLocation
    );

VOID APIENTRY Bc250D3D12SetGraphicsRootUnorderedAccessView(
    D3D12DDI_HDEVICE hDevice,
    UINT RootParameterIndex,
    D3D12DDI_GPU_VIRTUAL_ADDRESS BufferLocation
    );

/* ============================================================================
   Root Parameter Binding (Compute)
   ============================================================================ */

VOID APIENTRY Bc250D3D12SetComputeRootDescriptorTable(
    D3D12DDI_HDEVICE hDevice,
    UINT RootParameterIndex,
    D3D12DDI_GPU_DESCRIPTOR_HANDLE BaseDescriptor
    );

VOID APIENTRY Bc250D3D12SetComputeRoot32BitConstant(
    D3D12DDI_HDEVICE hDevice,
    UINT RootParameterIndex,
    UINT Value,
    UINT DestOffsetIn32BitValues
    );

VOID APIENTRY Bc250D3D12SetComputeRoot32BitConstants(
    D3D12DDI_HDEVICE hDevice,
    UINT RootParameterIndex,
    UINT Num32BitValuesToSet,
    _In_reads_(Num32BitValuesToSet) CONST VOID* pSrcData,
    UINT DestOffsetIn32BitValues
    );

VOID APIENTRY Bc250D3D12SetComputeRootConstantBufferView(
    D3D12DDI_HDEVICE hDevice,
    UINT RootParameterIndex,
    D3D12DDI_GPU_VIRTUAL_ADDRESS BufferLocation
    );

VOID APIENTRY Bc250D3D12SetComputeRootShaderResourceView(
    D3D12DDI_HDEVICE hDevice,
    UINT RootParameterIndex,
    D3D12DDI_GPU_VIRTUAL_ADDRESS BufferLocation
    );

VOID APIENTRY Bc250D3D12SetComputeRootUnorderedAccessView(
    D3D12DDI_HDEVICE hDevice,
    UINT RootParameterIndex,
    D3D12DDI_GPU_VIRTUAL_ADDRESS BufferLocation
    );

/* ============================================================================
   Input Assembler (IA) Commands
   ============================================================================ */

VOID APIENTRY Bc250D3D12IASetPrimitiveTopology(
    D3D12DDI_HDEVICE hDevice,
    D3D12DDI_PRIMITIVE_TOPOLOGY PrimitiveTopology
    );

VOID APIENTRY Bc250D3D12IASetVertexBuffers(
    D3D12DDI_HDEVICE hDevice,
    UINT StartSlot,
    UINT NumViews,
    _In_reads_(NumViews) CONST D3D12DDI_VERTEX_BUFFER_VIEW* pVertexBufferViews
    );

VOID APIENTRY Bc250D3D12IASetIndexBuffer(
    D3D12DDI_HDEVICE hDevice,
    _In_ CONST D3D12DDI_INDEX_BUFFER_VIEW* pIndexBufferView
    );

/* ============================================================================
   Draw Commands
   ============================================================================ */

VOID APIENTRY Bc250D3D12DrawInstanced(
    D3D12DDI_HDEVICE hDevice,
    UINT VertexCountPerInstance,
    UINT InstanceCount,
    UINT StartVertexLocation,
    UINT StartInstanceLocation
    );

VOID APIENTRY Bc250D3D12DrawIndexedInstanced(
    D3D12DDI_HDEVICE hDevice,
    UINT IndexCountPerInstance,
    UINT InstanceCount,
    UINT StartIndexLocation,
    INT BaseVertexLocation,
    UINT StartInstanceLocation
    );

VOID APIENTRY Bc250D3D12Dispatch(
    D3D12DDI_HDEVICE hDevice,
    UINT ThreadGroupCountX,
    UINT ThreadGroupCountY,
    UINT ThreadGroupCountZ
    );

/* ============================================================================
   Resource State Transitions
   ============================================================================ */

VOID APIENTRY Bc250D3D12ResourceBarrier(
    D3D12DDI_HDEVICE hDevice,
    UINT NumBarriers,
    _In_reads_(NumBarriers) CONST D3D12DDI_RESOURCE_BARRIER* pBarriers
    );

/* ============================================================================
   Clear Operations
   ============================================================================ */

VOID APIENTRY Bc250D3D12ClearRenderTargetView(
    D3D12DDI_HDEVICE hDevice,
    D3D12DDI_CPU_DESCRIPTOR_HANDLE RenderTargetView,
    _In_reads_(4) CONST FLOAT ColorRGBA[4],
    UINT NumRects,
    _In_reads_opt_(NumRects) CONST D3D12DDI_RECT* pRects
    );

VOID APIENTRY Bc250D3D12ClearDepthStencilView(
    D3D12DDI_HDEVICE hDevice,
    D3D12DDI_CPU_DESCRIPTOR_HANDLE DepthStencilView,
    D3D12DDI_CLEAR_FLAGS ClearFlags,
    FLOAT Depth,
    UINT8 Stencil,
    UINT NumRects,
    _In_reads_opt_(NumRects) CONST D3D12DDI_RECT* pRects
    );

/* ============================================================================
   Output/Presentation
   ============================================================================ */

HRESULT APIENTRY Bc250D3D12Present(
    D3D12DDI_HDEVICE hDevice,
    _In_ CONST D3D12DDI_PRESENT_ARGS* pArgs
    );

/* ============================================================================
   Rasterizer State (Viewport, Scissor)
   ============================================================================ */

VOID APIENTRY Bc250D3D12SetViewport(
    D3D12DDI_HDEVICE hDevice,
    _In_ CONST D3D12DDI_VIEWPORT* pViewport
    );

VOID APIENTRY Bc250D3D12SetScissorRect(
    D3D12DDI_HDEVICE hDevice,
    _In_ CONST D3D12DDI_RECT* pRect
    );

VOID APIENTRY Bc250D3D12SetBlendFactor(
    D3D12DDI_HDEVICE hDevice,
    _In_reads_(4) CONST FLOAT BlendFactor[4]
    );

VOID APIENTRY Bc250D3D12SetStencilRef(
    D3D12DDI_HDEVICE hDevice,
    UINT StencilRef
    );

#endif /* _AMDBC250_D3D12_H_ */
