/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    amdbc250_d3d11.h

Abstract:
    Definitions for the Microsoft Direct3D version 11 DDI.

    This header defines the structures and function prototypes required for the
    User-Mode Display Driver (UMD) to implement Direct3D 11 functionality.

Environment:
    User mode (loaded by dxgkrnl.sys thunks)

--*/

#ifndef _AMDBC250_D3D11_H_
#define _AMDBC250_D3D11_H_

#include <d3d10umddi.h>
#include <d3d11umddi.h>

// Forward declaration of the UMD device context
typedef struct _AMDBC250_UMD_DEVICE *PAMDBC250_UMD_DEVICE;

//=============================================================================
// D3D11 DDI Functions
//=============================================================================

// DDI_D3D11DDI_CREATEDEVICE
// Creates a Direct3D 11 device.
HRESULT APIENTRY
Bc250D3D11CreateDevice(
    _In_ CONST HANDLE hAdapter,
    _Inout_ D3D11DDIARG_CREATEDEVICE* pCreateDevice
    );

// DDI_D3D11DDI_DESTROYDEVICE
// Destroys a Direct3D 11 device.
VOID APIENTRY
Bc250D3D11DestroyDevice(
    _In_ CONST HANDLE hDevice
    );

// DDI_D3D11DDI_CREATEBLENDSTATE
// Creates a blend state.
VOID APIENTRY
Bc250D3D11CreateBlendState(
    _In_ CONST HANDLE hDevice,
    _In_ CONST D3D11DDIARG_CREATEBLENDSTATE* pCreateBlendState,
    _In_ D3D10DDI_HBLENDSTATE hBlendState,
    _In_ D3D10DDI_HRTBLENDSTATE hRTBlendState
    );

// DDI_D3D11DDI_DESTROYBLENDSTATE
// Destroys a blend state.
VOID APIENTRY
Bc250D3D11DestroyBlendState(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HBLENDSTATE hBlendState
    );

// DDI_D3D11DDI_CREATERASTERIZERSTATE
// Creates a rasterizer state.
VOID APIENTRY
Bc250D3D11CreateRasterizerState(
    _In_ CONST HANDLE hDevice,
    _In_ CONST D3D11DDIARG_CREATERASTERIZERSTATE* pCreateRasterizerState,
    _In_ D3D10DDI_HRASTERIZERSTATE hRasterizerState,
    _In_ D3D10DDI_HRTRASTERIZERSTATE hRTRasterizerState
    );

// DDI_D3D11DDI_DESTROYRASTERIZERSTATE
// Destroys a rasterizer state.
VOID APIENTRY
Bc250D3D11DestroyRasterizerState(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HRASTERIZERSTATE hRasterizerState
    );

// DDI_D3D11DDI_CREATEDEPTHSTENCILSTATE
// Creates a depth stencil state.
VOID APIENTRY
Bc250D3D11CreateDepthStencilState(
    _In_ CONST HANDLE hDevice,
    _In_ CONST D3D11DDIARG_CREATEDEPTHSTENCILSTATE* pCreateDepthStencilState,
    _In_ D3D10DDI_HDEPTHSTENCILSTATE hDepthStencilState,
    _In_ D3D10DDI_HRTDEPTHSTENCILSTATE hRTDepthStencilState
    );

// DDI_D3D11DDI_DESTROYDEPTHSTENCILSTATE
// Destroys a depth stencil state.
VOID APIENTRY
Bc250D3D11DestroyDepthStencilState(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HDEPTHSTENCILSTATE hDepthStencilState
    );

// DDI_D3D11DDI_CREATEGEOMETRYSHADER
// Creates a geometry shader.
VOID APIENTRY
Bc250D3D11CreateGeometryShader(
    _In_ CONST HANDLE hDevice,
    _In_ CONST UINT* pShaderCode,
    _In_ D3D10DDI_HSHADER hShader,
    _In_ D3D10DDI_HRTSHADER hRTShader,
    _In_ CONST D3D11DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT* pCreateGeometryShaderWithStreamOutput
    );

// DDI_D3D11DDI_DESTROYSHADER
// Destroys a shader.
VOID APIENTRY
Bc250D3D11DestroyShader(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HSHADER hShader
    );

// DDI_D3D11DDI_CREATEVERTEXSHADER
// Creates a vertex shader.
VOID APIENTRY
Bc250D3D11CreateVertexShader(
    _In_ CONST HANDLE hDevice,
    _In_ CONST UINT* pShaderCode,
    _In_ D3D10DDI_HSHADER hShader,
    _In_ D3D10DDI_HRTSHADER hRTShader
    );

// DDI_D3D11DDI_CREATEPIXELSHADER
// Creates a pixel shader.
VOID APIENTRY
Bc250D3D11CreatePixelShader(
    _In_ CONST HANDLE hDevice,
    _In_ CONST UINT* pShaderCode,
    _In_ D3D10DDI_HSHADER hShader,
    _In_ D3D10DDI_HRTSHADER hRTShader
    );

// DDI_D3D11DDI_CREATEINPUTLAYOUT
// Creates an input layout.
VOID APIENTRY
Bc250D3D11CreateInputLayout(
    _In_ CONST HANDLE hDevice,
    _In_ CONST D3D10DDIARG_CREATEINPUTLAYOUT* pCreateInputLayout,
    _In_ D3D10DDI_HINPUTLAYOUT hInputLayout,
    _In_ D3D10DDI_HRTINPUTLAYOUT hRTInputLayout
    );

// DDI_D3D11DDI_DESTROYINPUTLAYOUT
// Destroys an input layout.
VOID APIENTRY
Bc250D3D11DestroyInputLayout(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HINPUTLAYOUT hInputLayout
    );

// DDI_D3D11DDI_CREATEBUFFER
// Creates a buffer.
VOID APIENTRY
Bc250D3D11CreateBuffer(
    _In_ CONST HANDLE hDevice,
    _In_ CONST D3D11DDIARG_CREATEBUFFER* pCreateBuffer,
    _In_ D3D10DDI_HRESOURCE hResource,
    _In_ D3D10DDI_HRTRESOURCE hRTResource
    );

// DDI_D3D11DDI_CREATETEXTURE2D
// Creates a 2D texture.
VOID APIENTRY
Bc250D3D11CreateTexture2D(
    _In_ CONST HANDLE hDevice,
    _In_ CONST D3D11DDIARG_CREATETEXTURE2D* pCreateTexture2D,
    _In_ D3D10DDI_HRESOURCE hResource,
    _In_ D3D10DDI_HRTRESOURCE hRTResource
    );

// DDI_D3D11DDI_DESTROYRESOURCE
// Destroys a resource.
VOID APIENTRY
Bc250D3D11DestroyResource(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HRESOURCE hResource
    );

// DDI_D3D11DDI_SETVIEWPORTS
// Sets viewports.
VOID APIENTRY
Bc250D3D11SetViewports(
    _In_ CONST HANDLE hDevice,
    _In_ UINT NumViewports,
    _In_ UINT FirstViewport,
    _In_ CONST D3D10_DDI_VIEWPORT* pViewports
    );

// DDI_D3D11DDI_SETSCISSORRECTS
// Sets scissor rectangles.
VOID APIENTRY
Bc250D3D11SetScissorRects(
    _In_ CONST HANDLE hDevice,
    _In_ UINT NumRects,
    _In_ UINT FirstRect,
    _In_ CONST RECT* pRects
    );

// DDI_D3D11DDI_SETBLENDSTATE
// Sets the blend state.
VOID APIENTRY
Bc250D3D11SetBlendState(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HBLENDSTATE hBlendState,
    _In_ CONST FLOAT BlendFactor[4],
    _In_ UINT SampleMask
    );

// DDI_D3D11DDI_SETRASTERIZERSTATE
// Sets the rasterizer state.
VOID APIENTRY
Bc250D3D11SetRasterizerState(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HRASTERIZERSTATE hRasterizerState
    );

// DDI_D3D11DDI_SETDEPTHSTENCILSTATE
// Sets the depth stencil state.
VOID APIENTRY
Bc250D3D11SetDepthStencilState(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HDEPTHSTENCILSTATE hDepthStencilState,
    _In_ UINT StencilRef
    );

// DDI_D3D11DDI_DRAWINDEXED
// Draws indexed primitives.
VOID APIENTRY
Bc250D3D11DrawIndexed(
    _In_ CONST HANDLE hDevice,
    _In_ UINT IndexCount,
    _In_ UINT StartIndexLocation,
    _In_ INT BaseVertexLocation
    );

// DDI_D3D11DDI_DRAW
// Draws non-indexed primitives.
VOID APIENTRY
Bc250D3D11Draw(
    _In_ CONST HANDLE hDevice,
    _In_ UINT VertexCount,
    _In_ UINT StartVertexLocation
    );

// DDI_D3D11DDI_SETVERTEXBUFFERS
// Sets vertex buffers.
VOID APIENTRY
Bc250D3D11SetVertexBuffers(
    _In_ CONST HANDLE hDevice,
    _In_ UINT StartBuffer,
    _In_ UINT NumBuffers,
    _In_ CONST D3D10DDI_HRESOURCE* phVertexBuffers,
    _In_ CONST UINT* pStrides,
    _In_ CONST UINT* pOffsets
    );

// DDI_D3D11DDI_SETINDEXBUFFER
// Sets the index buffer.
VOID APIENTRY
Bc250D3D11SetIndexBuffer(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HRESOURCE hIndexBuffer,
    _In_ DXGI_FORMAT Format,
    _In_ UINT Offset
    );

// DDI_D3D11DDI_SETINPUTLAYOUT
// Sets the input layout.
VOID APIENTRY
Bc250D3D11SetInputLayout(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HINPUTLAYOUT hInputLayout
    );

// DDI_D3D11DDI_SETVERTEXSHADER
// Sets the vertex shader.
VOID APIENTRY
Bc250D3D11SetVertexShader(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HSHADER hShader
    );

// DDI_D3D11DDI_SETPIXELSHADER
// Sets the pixel shader.
VOID APIENTRY
Bc250D3D11SetPixelShader(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HSHADER hShader
    );

// DDI_D3D11DDI_SETGEOMETRYSHADER
// Sets the geometry shader.
VOID APIENTRY
Bc250D3D11SetGeometryShader(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HSHADER hShader
    );

// DDI_D3D11DDI_SETCONSTANTBUFFERS
// Sets constant buffers.
VOID APIENTRY
Bc250D3D11SetConstantBuffers(
    _In_ CONST HANDLE hDevice,
    _In_ UINT StartBuffer,
    _In_ UINT NumBuffers,
    _In_ CONST D3D10DDI_HRESOURCE* phConstantBuffers
    );

// DDI_D3D11DDI_SETSHADERRESOURCES
// Sets shader resources.
VOID APIENTRY
Bc250D3D11SetShaderResources(
    _In_ CONST HANDLE hDevice,
    _In_ UINT StartView,
    _In_ UINT NumViews,
    _In_ CONST D3D10DDI_HSHADERRESOURCEVIEW* phShaderResourceViews
    );

// DDI_D3D11DDI_SETSAMPLERS
// Sets samplers.
VOID APIENTRY
Bc250D3D11SetSamplers(
    _In_ CONST HANDLE hDevice,
    _In_ UINT StartSampler,
    _In_ UINT NumSamplers,
    _In_ CONST D3D10DDI_HSAMPLER* phSamplers
    );

// DDI_D3D11DDI_SETRENDERTARGETS
// Sets render targets.
VOID APIENTRY
Bc250D3D11SetRenderTargets(
    _In_ CONST HANDLE hDevice,
    _In_ CONST D3D10DDI_HRENDERTARGETVIEW* phRenderTargetViews,
    _In_ UINT RTVCount,
    _In_ D3D10DDI_HDEPTHSTENCILVIEW hDepthStencilView
    );

// DDI_D3D11DDI_CLEARRENDERTARGETVIEW
// Clears a render target view.
VOID APIENTRY
Bc250D3D11ClearRenderTargetView(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HRENDERTARGETVIEW hRenderTargetView,
    _In_ CONST FLOAT ColorRGBA[4]
    );

// DDI_D3D11DDI_CLEARDEPTHSTENCILVIEW
// Clears a depth stencil view.
VOID APIENTRY
Bc250D3D11ClearDepthStencilView(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HDEPTHSTENCILVIEW hDepthStencilView,
    _In_ UINT ClearFlags,
    _In_ FLOAT Depth,
    _In_ UINT8 Stencil
    );

// DDI_D3D11DDI_FLUSH
// Flushes outstanding commands.
VOID APIENTRY
Bc250D3D11Flush(
    _In_ CONST HANDLE hDevice
    );

// DDI_D3D11DDI_GENMIPS
// Generates mipmaps.
VOID APIENTRY
Bc250D3D11GenMips(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HSHADERRESOURCEVIEW hShaderResourceView
    );

// DDI_D3D11DDI_RESOURCEMAP
// Maps a resource for CPU access.
VOID APIENTRY
Bc250D3D11ResourceMap(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HRESOURCE hResource,
    _In_ UINT Subresource,
    _In_ D3D10DDI_MAP MapType,
    _In_ UINT MapFlags,
    _Inout_ D3D10DDIARG_MAP* pMap
    );

// DDI_D3D11DDI_RESOURCEUNMAP
// Unmaps a resource.
VOID APIENTRY
Bc250D3D11ResourceUnmap(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HRESOURCE hResource,
    _In_ UINT Subresource
    );

// DDI_D3D11DDI_SETCONSTANTBUFFEROFFSET
// Sets constant buffer offset.
VOID APIENTRY
Bc250D3D11SetConstantBufferOffset(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HRESOURCE hConstantBuffer,
    _In_ UINT Offset
    );

// DDI_D3D11DDI_SETCONSTANTBUFFERS_SCALED
// Sets scaled constant buffers.
VOID APIENTRY
Bc250D3D11SetConstantBuffersScaled(
    _In_ CONST HANDLE hDevice,
    _In_ UINT StartBuffer,
    _In_ UINT NumBuffers,
    _In_ CONST D3D10DDI_HRESOURCE* phConstantBuffers,
    _In_ CONST UINT* pOffsets,
    _In_ CONST UINT* pSizes
    );

// DDI_D3D11DDI_CREATECOMPUTESHADER
// Creates a compute shader.
VOID APIENTRY
Bc250D3D11CreateComputeShader(
    _In_ CONST HANDLE hDevice,
    _In_ CONST UINT* pShaderCode,
    _In_ D3D10DDI_HSHADER hShader,
    _In_ D3D10DDI_HRTSHADER hRTShader
    );

// DDI_D3D11DDI_SETCOMPUTESHADER
// Sets the compute shader.
VOID APIENTRY
Bc250D3D11SetComputeShader(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HSHADER hShader
    );

// DDI_D3D11DDI_DISPATCH
// Dispatches compute shader workgroups.
VOID APIENTRY
Bc250D3D11Dispatch(
    _In_ CONST HANDLE hDevice,
    _In_ UINT ThreadGroupCountX,
    _In_ UINT ThreadGroupCountY,
    _In_ UINT ThreadGroupCountZ
    );

// DDI_D3D11DDI_CREATERESOURCEVIEW
// Creates a resource view.
VOID APIENTRY
Bc250D3D11CreateResourceView(
    _In_ CONST HANDLE hDevice,
    _In_ CONST D3D11DDIARG_CREATERESOURCEVIEW* pCreateResourceView,
    _In_ D3D10DDI_HSHADERRESOURCEVIEW hShaderResourceView,
    _In_ D3D10DDI_HRTSHADERRESOURCEVIEW hRTShaderResourceView
    );

// DDI_D3D11DDI_DESTROYRESOURCEVIEW
// Destroys a resource view.
VOID APIENTRY
Bc250D3D11DestroyResourceView(
    _In_ CONST HANDLE hDevice,
    _In_ D3D10DDI_HSHADERRESOURCEVIEW hShaderResourceView
    );

// DDI_D3D11DDI_CREATEUNORDEREDACCESSVIEW
// Creates an unordered access view.
VOID APIENTRY
Bc250D3D11CreateUnorderedAccessView(
    _In_ CONST HANDLE hDevice,
    _In_ CONST D3D11DDIARG_CREATEUNORDEREDACCESSVIEW* pCreateUnorderedAccessView,
    _In_ D3D11DDI_HUNORDEREDACCESSVIEW hUnorderedAccessView,
    _In_ D3D11DDI_HRTUNORDEREDACCESSVIEW hRTUnorderedAccessView
    );

// DDI_D3D11DDI_DESTROYUNORDEREDACCESSVIEW
// Destroys an unordered access view.
VOID APIENTRY
Bc250D3D11DestroyUnorderedAccessView(
    _In_ CONST HANDLE hDevice,
    _In_ D3D11DDI_HUNORDEREDACCESSVIEW hUnorderedAccessView
    );

// DDI_D3D11DDI_SETUNORDEREDACCESSVIEWS
// Sets unordered access views.
VOID APIENTRY
Bc250D3D11SetUnorderedAccessViews(
    _In_ CONST HANDLE hDevice,
    _In_ UINT StartSlot,
    _In_ UINT NumViews,
    _In_ CONST D3D11DDI_HUNORDEREDACCESSVIEW* phUnorderedAccessViews,
    _In_ CONST UINT* pUAVInitialCounts
    );

// DDI_D3D11DDI_SETRENDERTARGETS_SCALED
// Sets scaled render targets.
VOID APIENTRY
Bc250D3D11SetRenderTargetsScaled(
    _In_ CONST HANDLE hDevice,
    _In_ CONST D3D10DDI_HRENDERTARGETVIEW* phRenderTargetViews,
    _In_ UINT RTVCount,
    _In_ D3D10DDI_HDEPTHSTENCILVIEW hDepthStencilView,
    _In_ CONST D3D11DDIARG_SETRENDERTARGETS_SCALED* pSetRenderTargetsScaled
    );

#endif // _AMDBC250_D3D11_H_
