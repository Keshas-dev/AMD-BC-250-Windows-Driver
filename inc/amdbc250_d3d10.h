/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    amdbc250_d3d10.h

Abstract:
    Definitions for the Microsoft Direct3D version 10 DDI.

    This header defines the structures and function prototypes required for the
    User-Mode Display Driver (UMD) to implement Direct3D 10 functionality.

Environment:
    User mode (loaded by dxgkrnl.sys thunks)

--*/

#ifndef _AMDBC250_D3D10_H_
#define _AMDBC250_D3D10_H_

#include <d3d10umddi.h>

// Forward declaration of the UMD device context
typedef struct _AMDBC250_UMD_DEVICE *PAMDBC250_UMD_DEVICE;

//=============================================================================
// D3D10 DDI Functions
//=============================================================================

// DDI_D3D10DDI_CREATEDEVICE
// Creates a Direct3D 10 device.
HRESULT APIENTRY
Bc250D3D10CreateDevice(
    _In_ CONST HANDLE hAdapter,
    _Inout_ D3D10DDIARG_CREATEDEVICE* pCreateDevice
    );

// DDI_D3D10DDI_DESTROYDEVICE
// Destroys a Direct3D 10 device.
VOID APIENTRY
Bc250D3D10DestroyDevice(
    _In_ CONST HANDLE hDevice
    );

// DDI_D3D10DDI_CREATEBLENDSTATE
// Creates a blend state.
VOID APIENTRY
Bc250D3D10CreateBlendState(
    _In_ CONST HANDLE hDevice,
    _In_ CONST D3D10DDIARG_CREATEBLENDSTATE* pCreateBlendState,
    _In_ D3D10DDI_HBLENDSTATE hBlendState,
    _In_ D3D10DDI_HRTBLENDSTATE hRTBlendState
    );

// DDI_D3D10DDI_DESTROYBLENDSTATE
// Destroys a blend state.
VOID APIENTRY
Bc250D3D10DestroyBlendState(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HBLENDSTATE hBlendState
    );

// DDI_D3D10DDI_CREATERASTERIZERSTATE
// Creates a rasterizer state.
VOID APIENTRY
Bc250D3D10CreateRasterizerState(
    _In_ CONST HANDLE hDevice,
    _In_ CONST D3D10DDIARG_CREATERASTERIZERSTATE* pCreateRasterizerState,
    _In_ D3D10DDI_HRASTERIZERSTATE hRasterizerState,
    _In_ D3D10DDI_HRTRASTERIZERSTATE hRTRasterizerState
    );

// DDI_D3D10DDI_DESTROYRASTERIZERSTATE
// Destroys a rasterizer state.
VOID APIENTRY
Bc250D3D10DestroyRasterizerState(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HRASTERIZERSTATE hRasterizerState
    );

// DDI_D3D10DDI_CREATEDEPTHSTENCILSTATE
// Creates a depth stencil state.
VOID APIENTRY
Bc250D3D10CreateDepthStencilState(
    _In_ CONST HANDLE hDevice,
    _In_ CONST D3D10DDIARG_CREATEDEPTHSTENCILSTATE* pCreateDepthStencilState,
    _In_ D3D10DDI_HDEPTHSTENCILSTATE hDepthStencilState,
    _In_ D3D10DDI_HRTDEPTHSTENCILSTATE hRTDepthStencilState
    );

// DDI_D3D10DDI_DESTROYDEPTHSTENCILSTATE
// Destroys a depth stencil state.
VOID APIENTRY
Bc250D3D10DestroyDepthStencilState(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HDEPTHSTENCILSTATE hDepthStencilState
    );

// DDI_D3D10DDI_CREATEGEOMETRYSHADER
// Creates a geometry shader.
VOID APIENTRY
Bc250D3D10CreateGeometryShader(
    _In_ CONST HANDLE hDevice,
    _In_ CONST UINT* pShaderCode,
    _In_ D3D10DDI_HSHADER hShader,
    _In_ D3D10DDI_HRTSHADER hRTShader,
    _In_ CONST D3D10DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT* pCreateGeometryShaderWithStreamOutput
    );

// DDI_D3D10DDI_DESTROYSHADER
// Destroys a shader.
VOID APIENTRY
Bc250D3D10DestroyShader(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HSHADER hShader
    );

// DDI_D3D10DDI_CREATEVERTEXSHADER
// Creates a vertex shader.
VOID APIENTRY
Bc250D3D10CreateVertexShader(
    _In_ CONST HANDLE hDevice,
    _In_ CONST UINT* pShaderCode,
    _In_ D3D10DDI_HSHADER hShader,
    _In_ D3D10DDI_HRTSHADER hRTShader
    );

// DDI_D3D10DDI_CREATEPIXELSHADER
// Creates a pixel shader.
VOID APIENTRY
Bc250D3D10CreatePixelShader(
    _In_ CONST HANDLE hDevice,
    _In_ CONST UINT* pShaderCode,
    _In_ D3D10DDI_HSHADER hShader,
    _In_ D3D10DDI_HRTSHADER hRTShader
    );

// DDI_D3D10DDI_CREATEINPUTLAYOUT
// Creates an input layout.
VOID APIENTRY
Bc250D3D10CreateInputLayout(
    _In_ CONST HANDLE hDevice,
    _In_ CONST D3D10DDIARG_CREATEINPUTLAYOUT* pCreateInputLayout,
    _In_ D3D10DDI_HINPUTLAYOUT hInputLayout,
    _In_ D3D10DDI_HRTINPUTLAYOUT hRTInputLayout
    );

// DDI_D3D10DDI_DESTROYINPUTLAYOUT
// Destroys an input layout.
VOID APIENTRY
Bc250D3D10DestroyInputLayout(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HINPUTLAYOUT hInputLayout
    );

// DDI_D3D10DDI_CREATEBUFFER
// Creates a buffer.
VOID APIENTRY
Bc250D3D10CreateBuffer(
    _In_ CONST HANDLE hDevice,
    _In_ CONST D3D10DDIARG_CREATEBUFFER* pCreateBuffer,
    _In_ D3D10DDI_HRESOURCE hResource,
    _In_ D3D10DDI_HRTRESOURCE hRTResource
    );

// DDI_D3D10DDI_CREATETEXTURE2D
// Creates a 2D texture.
VOID APIENTRY
Bc250D3D10CreateTexture2D(
    _In_ CONST HANDLE hDevice,
    _In_ CONST D3D10DDIARG_CREATETEXTURE2D* pCreateTexture2D,
    _In_ D3D10DDI_HRESOURCE hResource,
    _In_ D3D10DDI_HRTRESOURCE hRTResource
    );

// DDI_D3D10DDI_DESTROYRESOURCE
// Destroys a resource.
VOID APIENTRY
Bc250D3D10DestroyResource(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HRESOURCE hResource
    );

// DDI_D3D10DDI_SETVIEWPORTS
// Sets viewports.
VOID APIENTRY
Bc250D3D10SetViewports(
    _In_ CONST HANDLE hDevice,
    _In_ UINT NumViewports,
    _In_ UINT FirstViewport,
    _In_ CONST D3D10_DDI_VIEWPORT* pViewports
    );

// DDI_D3D10DDI_SETSCISSORRECTS
// Sets scissor rectangles.
VOID APIENTRY
Bc250D3D10SetScissorRects(
    _In_ CONST HANDLE hDevice,
    _In_ UINT NumRects,
    _In_ UINT FirstRect,
    _In_ CONST RECT* pRects
    );

// DDI_D3D10DDI_SETBLENDSTATE
// Sets the blend state.
VOID APIENTRY
Bc250D3D10SetBlendState(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HBLENDSTATE hBlendState,
    _In_ CONST FLOAT BlendFactor[4],
    _In_ UINT SampleMask
    );

// DDI_D3D10DDI_SETRASTERIZERSTATE
// Sets the rasterizer state.
VOID APIENTRY
Bc250D3D10SetRasterizerState(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HRASTERIZERSTATE hRasterizerState
    );

// DDI_D3D10DDI_SETDEPTHSTENCILSTATE
// Sets the depth stencil state.
VOID APIENTRY
Bc250D3D10SetDepthStencilState(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HDEPTHSTENCILSTATE hDepthStencilState,
    _In_ UINT StencilRef
    );

// DDI_D3D10DDI_DRAWINDEXED
// Draws indexed primitives.
VOID APIENTRY
Bc250D3D10DrawIndexed(
    _In_ CONST HANDLE hDevice,
    _In_ UINT IndexCount,
    _In_ UINT StartIndexLocation,
    _In_ INT BaseVertexLocation
    );

// DDI_D3D10DDI_DRAW
// Draws non-indexed primitives.
VOID APIENTRY
Bc250D3D10Draw(
    _In_ CONST HANDLE hDevice,
    _In_ UINT VertexCount,
    _In_ UINT StartVertexLocation
    );

// DDI_D3D10DDI_SETVERTEXBUFFERS
// Sets vertex buffers.
VOID APIENTRY
Bc250D3D10SetVertexBuffers(
    _In_ CONST HANDLE hDevice,
    _In_ UINT StartBuffer,
    _In_ UINT NumBuffers,
    _In_ CONST D3D10DDI_HRESOURCE* phVertexBuffers,
    _In_ CONST UINT* pStrides,
    _In_ CONST UINT* pOffsets
    );

// DDI_D3D10DDI_SETINDEXBUFFER
// Sets the index buffer.
VOID APIENTRY
Bc250D3D10SetIndexBuffer(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HRESOURCE hIndexBuffer,
    _In_ DXGI_FORMAT Format,
    _In_ UINT Offset
    );

// DDI_D3D10DDI_SETINPUTLAYOUT
// Sets the input layout.
VOID APIENTRY
Bc250D3D10SetInputLayout(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HINPUTLAYOUT hInputLayout
    );

// DDI_D3D10DDI_SETVERTEXSHADER
// Sets the vertex shader.
VOID APIENTRY
Bc250D3D10SetVertexShader(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HSHADER hShader
    );

// DDI_D3D10DDI_SETPIXELSHADER
// Sets the pixel shader.
VOID APIENTRY
Bc250D3D10SetPixelShader(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HSHADER hShader
    );

// DDI_D3D10DDI_SETGEOMETRYSHADER
// Sets the geometry shader.
VOID APIENTRY
Bc250D3D10SetGeometryShader(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HSHADER hShader
    );

// DDI_D3D10DDI_SETCONSTANTBUFFERS
// Sets constant buffers.
VOID APIENTRY
Bc250D3D10SetConstantBuffers(
    _In_ CONST HANDLE hDevice,
    _In_ UINT StartBuffer,
    _In_ UINT NumBuffers,
    _In_ CONST D3D10DDI_HRESOURCE* phConstantBuffers
    );

// DDI_D3D10DDI_SETSHADERRESOURCES
// Sets shader resources.
VOID APIENTRY
Bc250D3D10SetShaderResources(
    _In_ CONST HANDLE hDevice,
    _In_ UINT StartView,
    _In_ UINT NumViews,
    _In_ CONST D3D10DDI_HSHADERRESOURCEVIEW* phShaderResourceViews
    );

// DDI_D3D10DDI_SETSAMPLERS
// Sets samplers.
VOID APIENTRY
Bc250D3D10SetSamplers(
    _In_ CONST HANDLE hDevice,
    _In_ UINT StartSampler,
    _In_ UINT NumSamplers,
    _In_ CONST D3D10DDI_HSAMPLER* phSamplers
    );

// DDI_D3D10DDI_SETRENDERTARGETS
// Sets render targets.
VOID APIENTRY
Bc250D3D10SetRenderTargets(
    _In_ CONST HANDLE hDevice,
    _In_ CONST D3D10DDI_HRENDERTARGETVIEW* phRenderTargetViews,
    _In_ UINT RTVCount,
    _In_ D3D10DDI_HDEPTHSTENCILVIEW hDepthStencilView
    );

// DDI_D3D10DDI_CLEARRENDERTARGETVIEW
// Clears a render target view.
VOID APIENTRY
Bc250D3D10ClearRenderTargetView(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HRENDERTARGETVIEW hRenderTargetView,
    _In_ CONST FLOAT ColorRGBA[4]
    );

// DDI_D3D10DDI_CLEARDEPTHSTENCILVIEW
// Clears a depth stencil view.
VOID APIENTRY
Bc250D3D10ClearDepthStencilView(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HDEPTHSTENCILVIEW hDepthStencilView,
    _In_ UINT ClearFlags,
    _In_ FLOAT Depth,
    _In_ UINT8 Stencil
    );

// DDI_D3D10DDI_FLUSH
// Flushes outstanding commands.
VOID APIENTRY
Bc250D3D10Flush(
    _In_ CONST HANDLE hDevice
    );

// DDI_D3D10DDI_GENMIPS
// Generates mipmaps.
VOID APIENTRY
Bc250D3D10GenMips(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HSHADERRESOURCEVIEW hShaderResourceView
    );

// DDI_D3D10DDI_RESOURCEMAP
// Maps a resource for CPU access.
VOID APIENTRY
Bc250D3D10ResourceMap(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HRESOURCE hResource,
    _In_ UINT Subresource,
    _In_ D3D10_DDI_MAP MapType,
    _In_ UINT MapFlags,
    _Inout_ D3D10DDIARG_MAP* pMap
    );

// DDI_D3D10DDI_RESOURCEUNMAP
// Unmaps a resource.
VOID APIENTRY
Bc250D3D10ResourceUnmap(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HRESOURCE hResource,
    _In_ UINT Subresource
    );

// DDI_D3D10DDI_SETCONSTANTBUFFEROFFSET
// Sets constant buffer offset.
VOID APIENTRY
Bc250D3D10SetConstantBufferOffset(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HRESOURCE hConstantBuffer,
    _In_ UINT Offset
    );

// DDI_D3D10DDI_SETCONSTANTBUFFERS_SCALED
// Sets scaled constant buffers.
VOID APIENTRY
Bc250D3D10SetConstantBuffersScaled(
    _In_ CONST HANDLE hDevice,
    _In_ UINT StartBuffer,
    _In_ UINT NumBuffers,
    _In_ CONST D3D10DDI_HRESOURCE* phConstantBuffers,
    _In_ CONST UINT* pOffsets,
    _In_ CONST UINT* pSizes
    );

#endif // _AMDBC250_D3D10_H_
