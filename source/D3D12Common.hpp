#pragma once
#if DX12_AVAILABLE
#include <cassert>
#define DX_CHECK(hr) (assert(!FAILED(hr)))

#include <d3d12.h>  // For ID3D12CommandQueue, ID3D12Device2, and ID3D12Fence
#include <wrl.h> 

void TransitionResource(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> commandList,
    Microsoft::WRL::ComPtr<ID3D12Resource> resource,
    D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState);

// Clear a render target view.
void ClearRTV(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> commandList,
    D3D12_CPU_DESCRIPTOR_HANDLE rtv, FLOAT* clearColor);

// Clear the depth of a depth-stencil view.
void ClearDepth(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> commandList,
    D3D12_CPU_DESCRIPTOR_HANDLE dsv, FLOAT depth = 1.0f);

// Create a GPU buffer.
void UpdateBufferResource(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> commandList,
    ID3D12Resource** pDestinationResource, ID3D12Resource** pIntermediateResource,
    size_t numElements, size_t elementSize, const void* bufferData, Microsoft::WRL::ComPtr<ID3D12Device2> device,
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE);

// Resize the depth buffer to match the size of the client area.
void ResizeDepthBuffer(int width, int height);
#endif
