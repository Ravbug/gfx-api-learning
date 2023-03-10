#if DX12_AVAILABLE
#include "D3D12CommandQueue.hpp"
#include "D3D12Common.hpp"

CommandQueue::CommandQueue(Microsoft::WRL::ComPtr<ID3D12Device2> device, D3D12_COMMAND_LIST_TYPE type)
    : m_FenceValue(0),
    m_CommandListType(type),
    m_d3d12Device(device)
{
    D3D12_COMMAND_QUEUE_DESC desc = {};
    desc.Type = type;
    desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    desc.NodeMask = 0;

    DX_CHECK(m_d3d12Device->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_d3d12CommandQueue)));
    DX_CHECK(m_d3d12Device->CreateFence(m_FenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_d3d12Fence)));

    m_FenceEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
    assert(m_FenceEvent && "Failed to create fence event handle.");
}

Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CommandQueue::CreateCommandAllocator()
{
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator;
    DX_CHECK(m_d3d12Device->CreateCommandAllocator(m_CommandListType, IID_PPV_ARGS(&commandAllocator)));

    return commandAllocator;
}

Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> CommandQueue::CreateCommandList(Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator)
{
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> commandList;
    DX_CHECK(m_d3d12Device->CreateCommandList(0, m_CommandListType, allocator.Get(), nullptr, IID_PPV_ARGS(&commandList)));

    return commandList;
}

Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> CommandQueue::GetCommandList()
{
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> commandList;

    // before the commandlist can be reset, an unused allocator is required
    // it can be reused as long as it is not currently in-flight
    if (!m_CommandAllocatorQueue.empty() && IsFenceComplete(m_CommandAllocatorQueue.front().fenceValue))
    {
        commandAllocator = m_CommandAllocatorQueue.front().commandAllocator;
        m_CommandAllocatorQueue.pop();

        DX_CHECK(commandAllocator->Reset());
    }
    else
    {
        commandAllocator = CreateCommandAllocator();
    }

    // with a valid command allocator, create a commandlist
    if (!m_CommandListQueue.empty())
    {
        commandList = m_CommandListQueue.front();
        m_CommandListQueue.pop();

        DX_CHECK(commandList->Reset(commandAllocator.Get(), nullptr));
    }
    else
    {
        commandList = CreateCommandList(commandAllocator);
    }

    // Associate the command allocator with the command list so that it can be
    // retrieved when the command list is executed.
    DX_CHECK(commandList->SetPrivateDataInterface(__uuidof(ID3D12CommandAllocator), commandAllocator.Get()));

    // It should be noted that when assigning a COM object to the private data of a ID3D12Object object using the ID3D12Object::SetPrivateDataInterface method, the internal reference counter of the assigned COM object is incremented. The ref counter of the assigned COM object is only decremented if either the owning ID3D12Object object is destroyed or the instance of the COM object with the same interface is replaced with another COM object of the same interface or a NULL pointer. 

    return commandList;
}

uint64_t CommandQueue::ExecuteCommandList(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> commandList)
{
    // before it can be executed, it must be closed
    commandList->Close();

    ID3D12CommandAllocator* commandAllocator;
    UINT dataSize = sizeof(commandAllocator);
    DX_CHECK(commandList->GetPrivateData(__uuidof(ID3D12CommandAllocator), &dataSize, &commandAllocator));
    // You should be aware that retrieving a COM pointer of a COM object associated with the private data of the ID3D12Object object will also increment the reference counter of that COM object.

    ID3D12CommandList* const ppCommandLists[] = {
        commandList.Get()
    };

    m_d3d12CommandQueue->ExecuteCommandLists(1, ppCommandLists);
    uint64_t fenceValue = Signal();

    m_CommandAllocatorQueue.emplace(CommandAllocatorEntry{ fenceValue, commandAllocator });
    m_CommandListQueue.push(commandList);

    // The ownership of the command allocator has been transferred to the ComPtr
    // in the command allocator queue. It is safe to release the reference 
    // in this temporary COM pointer here.
    commandAllocator->Release();

    return fenceValue;
}

uint64_t CommandQueue::Signal()
{
    uint64_t fenceValue = ++m_FenceValue;
    m_d3d12CommandQueue->Signal(m_d3d12Fence.Get(), fenceValue);
    return fenceValue;
}

bool CommandQueue::IsFenceComplete(uint64_t fenceValue)
{
    return m_d3d12Fence->GetCompletedValue() >= fenceValue;
}

void CommandQueue::WaitForFenceValue(uint64_t fenceValue)
{
    if (!IsFenceComplete(fenceValue))
    {
        m_d3d12Fence->SetEventOnCompletion(fenceValue, m_FenceEvent);
        ::WaitForSingleObject(m_FenceEvent, DWORD_MAX);
    }
}

void CommandQueue::Flush()
{
    WaitForFenceValue(Signal());
}
#endif
