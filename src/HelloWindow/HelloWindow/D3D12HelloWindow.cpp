//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#include "stdafx.h"
#include "D3D12HelloWindow.h"

D3D12HelloWindow::D3D12HelloWindow(UINT width, UINT height, std::wstring name) :
    DXSample(width, height, name),
    m_frameIndex(0),
    m_rtvDescriptorSize(0)
{
}

void D3D12HelloWindow::OnInit()
{
    LoadPipeline();
    LoadAssets();
}

// Load the rendering pipeline dependencies.
void D3D12HelloWindow::LoadPipeline()
{
    UINT dxgiFactoryFlags = 0;

//仅开启Debug开关的程序才使用
#if defined(_DEBUG)
    // Enable the debug layer (requires the Graphics Tools "optional feature").
    // NOTE: Enabling the debug layer after device creation will invalidate the active device.
    {
        //COM对象: 获取DeBug接口(ID, Ptr_Address)
        // #define IID_PPV_ARGS(ppType) __uuidof(**(ppType)), IID_PPV_ARGS_Helper(ppType) 解引用COM指针
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer(); //启用调试层，发生错误时在调试器输出错误

            // 启用额外的工厂调试模式
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif
    //创建重要DXGI对象(适配器、交换链等)
    ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

    if (m_useWarpDevice) //软件适配器
    {
        ComPtr<IDXGIAdapter> warpAdapter;
        ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

        ThrowIfFailed(D3D12CreateDevice(
            warpAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&m_device)
            ));
    }
    else //硬件(主显示器)
    {
        ComPtr<IDXGIAdapter1> hardwareAdapter;
        GetHardwareAdapter(factory.Get(), &hardwareAdapter); // 获取硬件适配器:从DXGI对象接口队列里面获取到硬件适配器指针

        ThrowIfFailed(D3D12CreateDevice(
            hardwareAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&m_device)
            ));
    }

    // 创建command指令描述符
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE; //默认command指令队列
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT; //直接执行指令 番外: TYPE_BUNDLE 打包指令集合

    // 创建一个command指令队列,实际执行从队列中提出指令执行
    ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

    // 创建离屏缓冲区描述结构
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = FrameCount;
    swapChainDesc.Width = m_width;
    swapChainDesc.Height = m_height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    //创建离屏缓冲区
    ComPtr<IDXGISwapChain1> swapChain;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(
        m_commandQueue.Get(),        // 交换链需要队列，以便它可以强制对其进行刷新.
        Win32Application::GetHwnd(),
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain
        ));

    // 禁用Alt + Enter 全屏
    ThrowIfFailed(factory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER));

    ThrowIfFailed(swapChain.As(&m_swapChain)); //获取离屏缓冲区指针
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex(); //获取当前离屏缓冲区索引


    {
        // 初始化描述符堆
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};  //创建空描述符堆
        rtvHeapDesc.NumDescriptors = FrameCount; //堆中描述符个数
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; // 堆中描述符类型
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE; //默认堆类型
        
        //创建描述符堆
        ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

        m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    }

    // Create frame resources.
    {
        //获取CPU第一个描述符的句柄
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

        // Create a RTV for each frame.
        for (UINT n = 0; n < FrameCount; n++)
        {
            ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
            // 创建RT视图
            m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
            rtvHandle.Offset(1, m_rtvDescriptorSize); //每次偏移20 bit
        }
    }
    // Command 分配器分配内存（传入一个Alloctor对象 && obj_ptr）
    ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));
}

// Load the sample assets.
void D3D12HelloWindow::LoadAssets()
{
    // 创建一个command命令列表
    ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_commandList)));

    // 命令列表在刚创建的时候是空的，需要先关闭等待稍后调用reset
    ThrowIfFailed(m_commandList->Close());

    // 创建围栏对象（同步对象）
    {
        ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
        m_fenceValue = 1;

        // Create an event handle to use for frame synchronization.
        m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (m_fenceEvent == nullptr)
        {
            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
        }
    }
}

// Update frame-based values.
void D3D12HelloWindow::OnUpdate()
{
}

// Render the scene.
void D3D12HelloWindow::OnRender()
{
    // 将我们渲染场景所需的所有命令记录到命令列表中。
    PopulateCommandList();

    // 执行命令列表
    ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // 离屏RT渲染到当前帧
    ThrowIfFailed(m_swapChain->Present(1, 0));
    // 等待上一帧渲染完
    WaitForPreviousFrame();
}

void D3D12HelloWindow::OnDestroy()
{
    // Ensure that the GPU is no longer referencing resources that are about to be
    // cleaned up by the destructor.
    WaitForPreviousFrame();

    CloseHandle(m_fenceEvent);
}

void D3D12HelloWindow::PopulateCommandList()
{
    // 命令列表分配器只能在关联时被重置
    // 命令列表已经在 GPU 上完成执行；
    // 应用程序应该使用确定 GPU 执行进度的栅栏。
    ThrowIfFailed(m_commandAllocator->Reset());

    // 但是，当对特定命令调用 Execute Command List() 时
    // 列表，然后可以随时重置该命令列表，并且必须在重新录制。
    ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get()));

    // 指示后台缓冲区从Persent状态转换为RT状态（只读 -> 可写）
    m_commandList->ResourceBarrier(1, get_rvalue_ptr(CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), 
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET)));

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);

    // 将clearColor 渲染到RT上
    const float clearColor[] = { 0.0f, 0.2f, 0.4f, 0.6f };
    m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

    // 指示后台缓冲区从RT装换为Persent状态（可写 -> 只读）
    m_commandList->ResourceBarrier(1, get_rvalue_ptr(CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), 
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT)));

    // 渲染活动完成后关闭command列表
    ThrowIfFailed(m_commandList->Close());
}

void D3D12HelloWindow::WaitForPreviousFrame()
{
    // 在command队列中插入一个围栏，更新插入围栏的fence值，围栏fence计数+1
    const UINT64 fence = m_fenceValue;
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fence));
    m_fenceValue++;

    // 当前完成的围栏值 < 当前同步的围栏计数（等待完成）
    if (m_fence->GetCompletedValue() < fence)
    {
        ThrowIfFailed(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}
