#include "App.hpp"

#if DX12_AVAILABLE
#include <GLFW/glfw3.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h> // For CommandLineToArgvW
#include <wrl.h>	// for comptr
#include <algorithm>
#include <cassert>
#include <chrono>

// DirectX 12 specific headers.
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>	
#include <directx/d3dx12.h>			// D3D12 extension library.

#include "D3D12Common.hpp"

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#undef min
#undef max

#include "D3D12CommandQueue.hpp"

using namespace std;
using namespace Microsoft::WRL;

// The number of swap chain back buffers.
constexpr uint8_t g_NumFrames = 3;
// Use WARP adapter (the software rasterizer for debugging purposes)
bool g_UseWarp = false;

// Set to true once the DX12 objects have been initialized.
bool g_IsInitialized = false;

// Window handle.
HWND g_hWnd;
// Window rectangle (used to toggle fullscreen state).
RECT g_WindowRect;

// DirectX 12 Objects
ComPtr<ID3D12Device2> g_Device;
std::shared_ptr<CommandQueue> m_DirectCommandQueue, m_ComputeCommandQueue, m_CopyCommandQueue;
ComPtr<IDXGISwapChain4> g_SwapChain;
ComPtr<ID3D12Resource> g_BackBuffers[g_NumFrames];
ComPtr<ID3D12GraphicsCommandList> g_CommandList;
ComPtr<ID3D12DescriptorHeap> g_RTVDescriptorHeap;		// contains render target views
UINT g_RTVDescriptorSize;			// vendor-specific
UINT g_CurrentBackBufferIndex;		// what texture we are on -- may not be sequential on every vendor

// Synchronization objects
ComPtr<ID3D12Fence> g_Fence;
uint64_t g_FenceValue = 0;
uint64_t g_FrameFenceValues[g_NumFrames] = {};	// need one per frame
HANDLE g_FenceEvent;

// swapchain present controls
// By default, enable V-Sync.
// Can be toggled with the V key.
bool g_VSync = true;
bool g_TearingSupported = false;
// By default, use windowed mode.
// Can be toggled with the Alt+Enter or F11
bool g_Fullscreen = false;

// Vertex buffer for the cube.
Microsoft::WRL::ComPtr<ID3D12Resource> m_VertexBuffer;
D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView;
// Index buffer for the cube.
Microsoft::WRL::ComPtr<ID3D12Resource> m_IndexBuffer;
D3D12_INDEX_BUFFER_VIEW m_IndexBufferView;

// Depth buffer.
Microsoft::WRL::ComPtr<ID3D12Resource> m_DepthBuffer;
// Descriptor heap for depth buffer.
Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_DSVHeap;

// Root signature
Microsoft::WRL::ComPtr<ID3D12RootSignature> m_RootSignature;

// Pipeline state object.
Microsoft::WRL::ComPtr<ID3D12PipelineState> m_PipelineState;

D3D12_VIEWPORT m_Viewport;
D3D12_RECT m_ScissorRect{ CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX) };

float m_FoV = 45;

DirectX::XMMATRIX m_ModelMatrix;
DirectX::XMMATRIX m_ViewMatrix;
DirectX::XMMATRIX m_ProjectionMatrix;

bool m_ContentLoaded = false;

using namespace DirectX;

uint64_t m_FenceValues[g_NumFrames] = {};


// Vertex data for a colored cube.
// matches VertexPosColor in the HLSL file
struct VertexPosColor
{
    XMFLOAT3 Position;
    XMFLOAT3 Color;
};

// cube vertex data
static constexpr VertexPosColor g_Vertices[8] = {
    { XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(0.0f, 0.0f, 0.0f) }, // 0
    { XMFLOAT3(-1.0f,  1.0f, -1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f) }, // 1
    { XMFLOAT3(1.0f,  1.0f, -1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f) }, // 2
    { XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) }, // 3
    { XMFLOAT3(-1.0f, -1.0f,  1.0f), XMFLOAT3(0.0f, 0.0f, 1.0f) }, // 4
    { XMFLOAT3(-1.0f,  1.0f,  1.0f), XMFLOAT3(0.0f, 1.0f, 1.0f) }, // 5
    { XMFLOAT3(1.0f,  1.0f,  1.0f), XMFLOAT3(1.0f, 1.0f, 1.0f) }, // 6
    { XMFLOAT3(1.0f, -1.0f,  1.0f), XMFLOAT3(1.0f, 0.0f, 1.0f) }  // 7
};
// cube index data
static constexpr WORD g_Indicies[36] =
{
    0, 1, 2, 0, 2, 3,
    4, 6, 5, 4, 7, 6,
    4, 5, 1, 4, 1, 0,
    3, 2, 6, 3, 6, 7,
    1, 5, 6, 1, 6, 2,
    4, 0, 3, 4, 3, 7
};

std::shared_ptr<CommandQueue> GetCommandQueue(D3D12_COMMAND_LIST_TYPE type)
{
    std::shared_ptr<CommandQueue> commandQueue;
    switch (type)
    {
    case D3D12_COMMAND_LIST_TYPE_DIRECT:
        commandQueue = m_DirectCommandQueue;
        break;
    case D3D12_COMMAND_LIST_TYPE_COMPUTE:
        commandQueue = m_ComputeCommandQueue;
        break;
    case D3D12_COMMAND_LIST_TYPE_COPY:
        commandQueue = m_CopyCommandQueue;
        break;
    default:
        assert(false && "Invalid command queue type.");
    }

    return commandQueue;
}

D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRenderTargetView()
{
    return CD3DX12_CPU_DESCRIPTOR_HANDLE(g_RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
        g_CurrentBackBufferIndex, g_RTVDescriptorSize);
}


bool LoadContent() {
    auto device = g_Device;
    auto commandQueue = GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY);
    auto commandList = commandQueue->GetCommandList();

    // Upload vertex buffer data.
    ComPtr<ID3D12Resource> intermediateVertexBuffer;
    UpdateBufferResource(commandList.Get(), &m_VertexBuffer, &intermediateVertexBuffer, _countof(g_Vertices), sizeof(VertexPosColor), g_Vertices, g_Device);

    // Create the vertex buffer view.
    m_VertexBufferView.BufferLocation = m_VertexBuffer->GetGPUVirtualAddress();
    m_VertexBufferView.SizeInBytes = sizeof(g_Vertices);
    m_VertexBufferView.StrideInBytes = sizeof(VertexPosColor);

    // Upload index buffer data.
    ComPtr<ID3D12Resource> intermediateIndexBuffer;
    UpdateBufferResource(commandList.Get(), &m_IndexBuffer, &intermediateIndexBuffer, _countof(g_Indicies), sizeof(WORD), g_Indicies, device);

    // Create index buffer view.
    m_IndexBufferView.BufferLocation = m_IndexBuffer->GetGPUVirtualAddress();
    m_IndexBufferView.Format = DXGI_FORMAT_R16_UINT;
    m_IndexBufferView.SizeInBytes = sizeof(g_Indicies);

    // Create the descriptor heap for the depth-stencil view.
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    DX_CHECK(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_DSVHeap)));

    // Load the shaders.
    ComPtr<ID3DBlob> vertShaderblob;
    DX_CHECK(D3DReadFileToBlob(L"dx_vs.cso", &vertShaderblob));

    ComPtr<ID3DBlob> fragShaderblob;
    DX_CHECK(D3DReadFileToBlob(L"dx_fs.cso", &fragShaderblob));

    // Create the vertex input layout
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    // Create a root signature.
    // version 1.1 is preferred, but we fall back to 1.0 if it is not available
    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
    {
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    }

    // Allow input layout and deny unnecessary access to certain pipeline stages.
    D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;

    // A single 32-bit constant root parameter that is used by the vertex shader.
    CD3DX12_ROOT_PARAMETER1 rootParameters[1];
    rootParameters[0].InitAsConstants(sizeof(XMMATRIX) / 4, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
    rootSignatureDescription.Init_1_1(_countof(rootParameters), rootParameters, 0, nullptr, rootSignatureFlags);

    // Serialize the root signature.
    // it becomes a binary object which can be used to create the actual root signature
    ComPtr<ID3DBlob> rootSignatureBlob;
    ComPtr<ID3DBlob> errorBlob;
    DX_CHECK(D3DX12SerializeVersionedRootSignature(&rootSignatureDescription, featureData.HighestVersion, &rootSignatureBlob, &errorBlob));
    // Create the root signature.
    DX_CHECK(device->CreateRootSignature(0, rootSignatureBlob->GetBufferPointer(), rootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&m_RootSignature)));

    // define render targets
    D3D12_RT_FORMAT_ARRAY rtvFormats = {};
    rtvFormats.NumRenderTargets = 1;
    rtvFormats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

    struct PipelineStateStream
    {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE pRootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT InputLayout;
        CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimitiveTopologyType;
        CD3DX12_PIPELINE_STATE_STREAM_VS VS;
        CD3DX12_PIPELINE_STATE_STREAM_PS PS;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
        CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
    } pipelineStateStream;

    // describe the pipeline state object
    pipelineStateStream.pRootSignature = m_RootSignature.Get();
    pipelineStateStream.InputLayout = { inputLayout, _countof(inputLayout) };
    pipelineStateStream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipelineStateStream.VS = CD3DX12_SHADER_BYTECODE(vertShaderblob.Get());
    pipelineStateStream.PS = CD3DX12_SHADER_BYTECODE(fragShaderblob.Get());
    pipelineStateStream.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pipelineStateStream.RTVFormats = rtvFormats;

    // create the PSO
    D3D12_PIPELINE_STATE_STREAM_DESC pipelineStateStreamDesc = {
       sizeof(PipelineStateStream), &pipelineStateStream
    };
    DX_CHECK(device->CreatePipelineState(&pipelineStateStreamDesc, IID_PPV_ARGS(&m_PipelineState)));

    // we still haven't actually done anything yet
    // execute this command list to put everything on the GPU
    auto fenceValue = commandQueue->ExecuteCommandList(commandList);
    commandQueue->WaitForFenceValue(fenceValue);

    m_ContentLoaded = true;

    // Resize/Create the depth buffer.
    ResizeDepthBuffer(WIDTH, HEIGHT);

    return true;
}

void EnableDebugLayer()
{
#if defined(_DEBUG)
    // Always enable the debug layer before doing anything DX12 related
    // so all possible errors generated while creating DX12 objects
    // are caught by the debug layer.
    // Enabling the debug layer after creating the ID3D12Device will cause the runtime to remove the device. 
    ComPtr<ID3D12Debug> debugInterface;
    DX_CHECK(D3D12GetDebugInterface(IID_PPV_ARGS(&debugInterface)));
    debugInterface->EnableDebugLayer();
#endif
}

// picks the adapter with the most VRAM
// more intelligent heurists can be added here instead
ComPtr<IDXGIAdapter4> GetAdapter(bool useWarp)
{
    ComPtr<IDXGIFactory4> dxgiFactory;
    UINT createFactoryFlags = 0;
#if defined(_DEBUG)
    createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

    DX_CHECK(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory)));

    ComPtr<IDXGIAdapter1> dxgiAdapter1;
    ComPtr<IDXGIAdapter4> dxgiAdapter4;

    // the software renderer
    if (useWarp)
    {
        DX_CHECK(dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&dxgiAdapter1)));
        DX_CHECK(dxgiAdapter1.As(&dxgiAdapter4));
    }
    else
    {
        SIZE_T maxDedicatedVideoMemory = 0;
        for (UINT i = 0; dxgiFactory->EnumAdapters1(i, &dxgiAdapter1) != DXGI_ERROR_NOT_FOUND; ++i)
        {
            DXGI_ADAPTER_DESC1 dxgiAdapterDesc1;
            dxgiAdapter1->GetDesc1(&dxgiAdapterDesc1);

            // Check to see if the adapter can create a D3D12 device without actually 
            // creating it. The adapter with the largest dedicated video memory
            // is favored.
            // use DXGI_ADAPTER_FLAG_SOFTWARE to only select hardware devices
            if ((dxgiAdapterDesc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
                SUCCEEDED(D3D12CreateDevice(dxgiAdapter1.Get(),
                    D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)) &&
                dxgiAdapterDesc1.DedicatedVideoMemory > maxDedicatedVideoMemory)
            {
                maxDedicatedVideoMemory = dxgiAdapterDesc1.DedicatedVideoMemory;
                DX_CHECK(dxgiAdapter1.As(&dxgiAdapter4));
            }
        }
    }

    return dxgiAdapter4;
}

// create device from adapter
// destroying a device causes all resources allocated on it to become invalid
// should be destroyed after all resources have been destroyed (the validation layer will complain otherwise)
ComPtr<ID3D12Device2> CreateDevice(ComPtr<IDXGIAdapter4> adapter)
{
    ComPtr<ID3D12Device2> d3d12Device2;
    DX_CHECK(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3d12Device2)));

    // Enable debug messages in debug mode.
#if defined(_DEBUG)
    ComPtr<ID3D12InfoQueue> pInfoQueue;
    if (SUCCEEDED(d3d12Device2.As(&pInfoQueue)))
    {
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);
        // Suppress whole categories of messages
        // uncomment to provide some
        //D3D12_MESSAGE_CATEGORY Categories[] = {};

        // Suppress messages based on their severity level
        D3D12_MESSAGE_SEVERITY Severities[] =
        {
            D3D12_MESSAGE_SEVERITY_INFO     // these don't indicate misuse of the API so we ignore them
        };

        // Suppress individual messages by their ID
        D3D12_MESSAGE_ID DenyIds[] = {
            D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,   // I'm really not sure how to avoid this message. Happens when clearing using a color other than the optimized clear color. If arbitrary clear colors are not desired, dont' suppress this message
            D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,                         // This warning occurs when using capture frame while graphics debugging.
            D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,                       // This warning occurs when using capture frame while graphics debugging.
        };

        D3D12_INFO_QUEUE_FILTER NewFilter = {};
        //NewFilter.DenyList.NumCategories = _countof(Categories);
        //NewFilter.DenyList.pCategoryList = Categories;
        NewFilter.DenyList.NumSeverities = _countof(Severities);
        NewFilter.DenyList.pSeverityList = Severities;
        NewFilter.DenyList.NumIDs = _countof(DenyIds);
        NewFilter.DenyList.pIDList = DenyIds;

        DX_CHECK(pInfoQueue->PushStorageFilter(&NewFilter));
    }
#endif

    return d3d12Device2;
}

// from a device, make a command queue
ComPtr<ID3D12CommandQueue> CreateCommandQueue(ComPtr<ID3D12Device2> device, D3D12_COMMAND_LIST_TYPE type)
{
    ComPtr<ID3D12CommandQueue> d3d12CommandQueue;

    D3D12_COMMAND_QUEUE_DESC desc = {};
    desc.Type = type;
    desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    desc.NodeMask = 0;

    DX_CHECK(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&d3d12CommandQueue)));

    return d3d12CommandQueue;
}

// gsync, freesync, etc
bool CheckTearingSupport()
{
    BOOL allowTearing = FALSE;

    // Rather than create the DXGI 1.5 factory interface directly, we create the
    // DXGI 1.4 interface and query for the 1.5 interface. This is to enable the 
    // graphics debugging tools which will not support the 1.5 factory interface 
    // until a future update.
    ComPtr<IDXGIFactory4> factory4;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory4))))
    {
        ComPtr<IDXGIFactory5> factory5;
        if (SUCCEEDED(factory4.As(&factory5)))
        {
            if (FAILED(factory5->CheckFeatureSupport(
                DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                &allowTearing, sizeof(allowTearing))))
            {
                allowTearing = FALSE;
            }
        }
    }

    return allowTearing == TRUE;
}

ComPtr<IDXGISwapChain4> CreateSwapChain(HWND hWnd,
    ComPtr<ID3D12CommandQueue> commandQueue,
    uint32_t width, uint32_t height, uint32_t bufferCount)
{
    ComPtr<IDXGISwapChain4> dxgiSwapChain4;
    ComPtr<IDXGIFactory4> dxgiFactory4;
    UINT createFactoryFlags = 0;
#if defined(_DEBUG)
    createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

    DX_CHECK(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory4)));

    // now describe how we want it to work
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width = width;
    swapChainDesc.Height = height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.Stereo = FALSE;
    swapChainDesc.SampleDesc = { 1, 0 };
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = bufferCount;
    swapChainDesc.Scaling = DXGI_SCALING_STRETCH;               // what hapens when the backbuffer is not the size of the target
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;   // for going as fast as possible (no vsync), discard frames in-flight to reduce latency
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    // It is recommended to always allow tearing if tearing support is available.
    swapChainDesc.Flags = CheckTearingSupport() ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    ComPtr<IDXGISwapChain1> swapChain1;
    DX_CHECK(dxgiFactory4->CreateSwapChainForHwnd(
        commandQueue.Get(),
        hWnd,
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain1));

    // Disable the Alt+Enter fullscreen toggle feature. Switching to fullscreen
    // will be handled manually.
    DX_CHECK(dxgiFactory4->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER));

    DX_CHECK(swapChain1.As(&dxgiSwapChain4));

    return dxgiSwapChain4;
}

ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(ComPtr<ID3D12Device2> device,
    D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors)
{
    ComPtr<ID3D12DescriptorHeap> descriptorHeap;

    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.NumDescriptors = numDescriptors;
    desc.Type = type;

    DX_CHECK(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptorHeap)));

    return descriptorHeap;
}

void UpdateRenderTargetViews(ComPtr<ID3D12Device2> device,
    ComPtr<IDXGISwapChain4> swapChain, ComPtr<ID3D12DescriptorHeap> descriptorHeap)
{
    auto rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(descriptorHeap->GetCPUDescriptorHandleForHeapStart());

    for (int i = 0; i < g_NumFrames; ++i)
    {
        ComPtr<ID3D12Resource> backBuffer;
        DX_CHECK(swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));

        device->CreateRenderTargetView(backBuffer.Get(), nullptr, rtvHandle);

        g_BackBuffers[i] = backBuffer;

        rtvHandle.Offset(rtvDescriptorSize);
    }
}

ComPtr<ID3D12CommandAllocator> CreateCommandAllocator(ComPtr<ID3D12Device2> device,
    D3D12_COMMAND_LIST_TYPE type)
{
    ComPtr<ID3D12CommandAllocator> commandAllocator;
    DX_CHECK(device->CreateCommandAllocator(type, IID_PPV_ARGS(&commandAllocator)));

    return commandAllocator;
}

ComPtr<ID3D12GraphicsCommandList> CreateCommandList(ComPtr<ID3D12Device2> device,
    ComPtr<ID3D12CommandAllocator> commandAllocator, D3D12_COMMAND_LIST_TYPE type)
{
    ComPtr<ID3D12GraphicsCommandList> commandList;
    DX_CHECK(device->CreateCommandList(0, type, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList)));

    DX_CHECK(commandList->Close());

    return commandList;
}

ComPtr<ID3D12Fence> CreateFence(ComPtr<ID3D12Device2> device)
{
    ComPtr<ID3D12Fence> fence;

    DX_CHECK(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));

    return fence;
}

HANDLE CreateEventHandle()
{
    HANDLE fenceEvent;

    fenceEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
    assert(fenceEvent && "Failed to create fence event.");

    return fenceEvent;
}

uint64_t Signal(ComPtr<ID3D12CommandQueue> commandQueue, ComPtr<ID3D12Fence> fence,
    uint64_t& fenceValue)
{
    uint64_t fenceValueForSignal = ++fenceValue;
    DX_CHECK(commandQueue->Signal(fence.Get(), fenceValueForSignal));

    return fenceValueForSignal;
}

void WaitForFenceValue(ComPtr<ID3D12Fence> fence, uint64_t fenceValue, HANDLE fenceEvent,
    std::chrono::milliseconds duration = std::chrono::milliseconds::max())
{
    if (fence->GetCompletedValue() < fenceValue)
    {
        DX_CHECK(fence->SetEventOnCompletion(fenceValue, fenceEvent));
        ::WaitForSingleObject(fenceEvent, static_cast<DWORD>(duration.count()));
    }
}

// block until all GPU commands have completed
void Flush(ComPtr<ID3D12CommandQueue> commandQueue, ComPtr<ID3D12Fence> fence,
    uint64_t& fenceValue, HANDLE fenceEvent)
{
    uint64_t fenceValueForSignal = Signal(commandQueue, fence, fenceValue);
    WaitForFenceValue(fence, fenceValueForSignal, fenceEvent);
}

// print the frame rate every second
void Update()
{
    static uint64_t frameCounter = 0;
    static double elapsedSeconds = 0.0;
    static std::chrono::high_resolution_clock clock;
    static auto t0 = clock.now();

    frameCounter++;
    auto t1 = clock.now();
    auto deltaTime = t1 - t0;
    t0 = t1;

    elapsedSeconds += deltaTime.count() * 1e-9;
    if (elapsedSeconds > 1.0)
    {
        char buffer[500];
        auto fps = frameCounter / elapsedSeconds;
        sprintf_s(buffer, 500, "FPS: %f\n", fps);
        OutputDebugString(buffer);

        frameCounter = 0;
        elapsedSeconds = 0.0;
    }

    // Update the model matrix.
    float angle = static_cast<float>(elapsedSeconds * 90.0);
    const XMVECTOR rotationAxis = XMVectorSet(0, 1, 1, 0);
    m_ModelMatrix = XMMatrixRotationAxis(rotationAxis, XMConvertToRadians(angle));

    // Update the view matrix.
    const XMVECTOR eyePosition = XMVectorSet(0, 0, -10, 1);
    const XMVECTOR focusPoint = XMVectorSet(0, 0, 0, 1);
    const XMVECTOR upDirection = XMVectorSet(0, 1, 0, 0);
    m_ViewMatrix = XMMatrixLookAtLH(eyePosition, focusPoint, upDirection);

    // Update the projection matrix.
    float aspectRatio = WIDTH / static_cast<float>(HEIGHT);
    m_ProjectionMatrix = XMMatrixPerspectiveFovLH(XMConvertToRadians(m_FoV), aspectRatio, 0.1f, 100.0f);
}

UINT  Present()
{
    UINT syncInterval = g_VSync ? 1 : 0;
    UINT presentFlags = g_TearingSupported && !g_VSync ? DXGI_PRESENT_ALLOW_TEARING : 0;
    DX_CHECK(g_SwapChain->Present(syncInterval, presentFlags));
    g_CurrentBackBufferIndex = g_SwapChain->GetCurrentBackBufferIndex();

    return g_CurrentBackBufferIndex;
}

// 2 steps
// clear the backbuffer
// present the rendered frame
void Render()
{

    auto commandQueue = GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
    auto commandList = commandQueue->GetCommandList();

    UINT currentBackBufferIndex = g_CurrentBackBufferIndex;
    auto backBuffer = g_BackBuffers[g_CurrentBackBufferIndex];
    auto rtv = GetCurrentRenderTargetView();
    auto dsv = m_DSVHeap->GetCPUDescriptorHandleForHeapStart();

    // Clear the render targets.
    {
        TransitionResource(commandList, backBuffer,
            D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

        FLOAT clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };

        ClearRTV(commandList, rtv, clearColor);
        ClearDepth(commandList, dsv);
    }

    // prepare pipeline for rendering
    commandList->SetPipelineState(m_PipelineState.Get());
    commandList->SetGraphicsRootSignature(m_RootSignature.Get());

    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &m_VertexBufferView);
    commandList->IASetIndexBuffer(&m_IndexBufferView);

    commandList->RSSetViewports(1, &m_Viewport);
    commandList->RSSetScissorRects(1, &m_ScissorRect);

    commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    // Update the MVP matrix
    XMMATRIX mvpMatrix = XMMatrixMultiply(m_ModelMatrix, m_ViewMatrix);
    mvpMatrix = XMMatrixMultiply(mvpMatrix, m_ProjectionMatrix);
    commandList->SetGraphicsRoot32BitConstants(0, sizeof(XMMATRIX) / 4, &mvpMatrix, 0);

    // draw call
    commandList->DrawIndexedInstanced(_countof(g_Indicies), 1, 0, 0, 0);

    // Present
    {
        TransitionResource(commandList, backBuffer,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

        m_FenceValues[currentBackBufferIndex] = commandQueue->ExecuteCommandList(commandList);

        currentBackBufferIndex = Present();

        commandQueue->WaitForFenceValue(m_FenceValues[currentBackBufferIndex]);
    }
}

void Flush()
{
    m_DirectCommandQueue->Flush();
    //m_ComputeCommandQueue->Flush();
    m_CopyCommandQueue->Flush();
}

// when the window changes size, the swapchain needs to be updated
void Resize(uint32_t width, uint32_t height)
{
    if (WIDTH != width || WIDTH != height)
    {
        // Don't allow 0 size swap chain back buffers.
        WIDTH = std::max(1u, width);
        HEIGHT = std::max(1u, height);

        // Flush the GPU queue to make sure the swap chain's back buffers
        // are not being referenced by an in-flight command list.
        
        Flush();

        for (int i = 0; i < g_NumFrames; ++i)
        {
            // Any references to the back buffers must be released
            // before the swap chain can be resized.
            g_BackBuffers[i].Reset();
            g_FrameFenceValues[i] = g_FrameFenceValues[g_CurrentBackBufferIndex];
        }
        DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
        DX_CHECK(g_SwapChain->GetDesc(&swapChainDesc));
        DX_CHECK(g_SwapChain->ResizeBuffers(g_NumFrames, WIDTH, HEIGHT,
            swapChainDesc.BufferDesc.Format, swapChainDesc.Flags));

        g_CurrentBackBufferIndex = g_SwapChain->GetCurrentBackBufferIndex();

        UpdateRenderTargetViews(g_Device, g_SwapChain, g_RTVDescriptorHeap);
    }
}

void ResizeDepthBuffer(int width, int height)
{
    if (m_ContentLoaded)
    {
        // Flush any GPU commands that might be referencing the depth buffer.
        Flush();

        // if the window is minimized, the size may become 0.
        // so ensure the size is at least 1
        width = std::max(1, width);
        height = std::max(1, height);

        auto device = g_Device;

        // Resize screen dependent resources.
       // Create a depth buffer.
        D3D12_CLEAR_VALUE optimizedClearValue = {};
        optimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
        optimizedClearValue.DepthStencil = { 1.0f, 0 };
        auto v = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        auto t = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, width, height, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
        DX_CHECK(device->CreateCommittedResource(&v,D3D12_HEAP_FLAG_NONE, &t, D3D12_RESOURCE_STATE_DEPTH_WRITE, &optimizedClearValue, IID_PPV_ARGS(&m_DepthBuffer)
        ));

        // Update the depth-stencil view.
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {};
        dsv.Format = DXGI_FORMAT_D32_FLOAT;
        dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsv.Texture2D.MipSlice = 0;
        dsv.Flags = D3D12_DSV_FLAG_NONE;

        device->CreateDepthStencilView(m_DepthBuffer.Get(), &dsv,
            m_DSVHeap->GetCPUDescriptorHandleForHeapStart());
    }
}

void DxApp::inithook()
{
    g_hWnd = glfwGetWin32Window(window);
    g_TearingSupported = CheckTearingSupport();
    ::GetWindowRect(g_hWnd, &g_WindowRect);

    m_Viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<float>(WIDTH), static_cast<float>(HEIGHT));

    // create dx12 objects
    ComPtr<IDXGIAdapter4> dxgiAdapter4 = GetAdapter(g_UseWarp);
    g_Device = CreateDevice(dxgiAdapter4);
    m_DirectCommandQueue = std::make_shared<CommandQueue>(g_Device, D3D12_COMMAND_LIST_TYPE_DIRECT);
    m_CopyCommandQueue = std::make_shared<CommandQueue>(g_Device, D3D12_COMMAND_LIST_TYPE_COPY);
    g_SwapChain = CreateSwapChain(g_hWnd, m_DirectCommandQueue->GetD3D12CommandQueue(),
        WIDTH, HEIGHT, g_NumFrames);
    g_CurrentBackBufferIndex = g_SwapChain->GetCurrentBackBufferIndex();
    g_RTVDescriptorHeap = CreateDescriptorHeap(g_Device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, g_NumFrames);
    g_RTVDescriptorSize = g_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    UpdateRenderTargetViews(g_Device, g_SwapChain, g_RTVDescriptorHeap);

    g_Fence = CreateFence(g_Device);
    g_FenceEvent = CreateEventHandle();

    LoadContent();

    g_IsInitialized = true;
}

void DxApp::onresize(int width, int height) {
    m_Viewport = CD3DX12_VIEWPORT(0.0f, 0.0f,
        static_cast<float>(width), static_cast<float>(height));

    Resize(width, height);
    ResizeDepthBuffer(width, height);
}

void DxApp::tickhook()
{
    Update();
    Render();
}

void DxApp::cleanuphook()
{
    // ensure all commands are done before closing
    Flush();
}

#endif