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
#include "DXMainSample.h"


D3D12HelloWindow::D3D12HelloWindow(UINT width, UINT height, std::wstring name) :
    DXSample(width, height, name),
    m_frameIndex(0),
    m_pCbvDataBegin(nullptr),
    m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
    m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)),
    m_rtvDescriptorSize(0),
    m_constantBufferData{}
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
        // 初始化render target view 描述符堆
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};  //创建空描述符堆
        rtvHeapDesc.NumDescriptors = FrameCount; //堆中描述符个数
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; // 堆中描述符类型
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE; //默认堆类型
        
        //创建描述符堆
        ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));
        m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        //初始化常量缓冲区描述符堆
        D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc = {};
        cbvHeapDesc.NumDescriptors = 1;
        cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&m_cbvHeap)));

        //初始化shader resource view 描述符堆
        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
        srvHeapDesc.NumDescriptors = 1;
        srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_srvHeap)));
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
    ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_BUNDLE, IID_PPV_ARGS(&m_bundleAllocator)));
}

// Load the sample assets.
void D3D12HelloWindow::LoadAssets()
{
        
    //创建根签名（初始为空）
    {
        D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
        if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
        {
            featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
        }
        CD3DX12_DESCRIPTOR_RANGE1 ranges[2];
        CD3DX12_ROOT_PARAMETER1 rootParameters[2];
        ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
        ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

        rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_VERTEX);
        rootParameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL);

        D3D12_STATIC_SAMPLER_DESC sampler = {};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        sampler.MipLODBias = 0;
        sampler.MaxAnisotropy = 0;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        sampler.MinLOD = 0.0f;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = 0;
        sampler.RegisterSpace = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;


        D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlages = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;

        // CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
        // rootSignatureDesc.Init(0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 1, &sampler, rootSignatureFlages);

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
        ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
    }

	//创建管线状态、和加载Shader编译Shader
    {
        ComPtr<ID3DBlob> vertShader;
        ComPtr<ID3DBlob> fragShader;

#if defined(_DEBUG)
        UINT compileFlages = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        UINT compileFlages = 0;
#endif

        ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(), nullptr, nullptr, "VSMain", "vs_5_0", compileFlages, 0, &vertShader, nullptr));
        ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(), nullptr, nullptr, "PSMain", "ps_5_0", compileFlages, 0, &fragShader, nullptr));

        // 定义顶点缓冲区布局(类似VAO)
        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0 ,0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0 ,20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
        };

        //创建管线状态对象&&初始化
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
        psoDesc.pRootSignature = m_rootSignature.Get();
        psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertShader.Get());
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(fragShader.Get());
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState.DepthEnable = FALSE;
        psoDesc.DepthStencilState.StencilEnable = FALSE;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.SampleDesc.Count = 1;
        ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));
    }

    // 创建一个command命令列表
    ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), m_pipelineState.Get(), IID_PPV_ARGS(&m_commandList)));

    // 命令列表在刚创建的时候是空的，需要先关闭等待稍后调用reset
    ThrowIfFailed(m_commandList->Close());

    //创建顶点缓冲区
    {
        Vertex triangleVertices[] =
        {
            { { 0.0f,    0.25f * m_aspectRatio, 0.0f }, { 0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
            { { 0.25f,  -0.25f * m_aspectRatio, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
            { { -0.25f, -0.25f * m_aspectRatio, 0.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } }
        };
        constexpr UINT vertexBufferSize = sizeof(triangleVertices);
        ThrowIfFailed(m_device->CreateCommittedResource(
            get_rvalue_ptr(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
            D3D12_HEAP_FLAG_NONE,
            get_rvalue_ptr(CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize)),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_vertexBuffer)));

        // 将三角形Mesh数据复制到顶点缓冲区
        UINT8* pVertexDataBegin;
        CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
        ThrowIfFailed(m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
        memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
        m_vertexBuffer->Unmap(0, nullptr);

        //初始化顶点缓冲区View
        m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
        m_vertexBufferView.StrideInBytes = sizeof(Vertex);
        m_vertexBufferView.SizeInBytes = vertexBufferSize;
    }

    ComPtr<ID3D12Resource> textureUploadHeap;
    {
        // 创建Texture Resource 描述符
        D3D12_RESOURCE_DESC textureDesc = {};
        textureDesc.MipLevels = 1;
        textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDesc.Width = TextureWidth;
        textureDesc.Height = TextureHeight;
        textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        textureDesc.DepthOrArraySize = 1;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.SampleDesc.Quality = 0;
        textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

        ThrowIfFailed(m_device->CreateCommittedResource(
            get_rvalue_ptr(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)), 
            D3D12_HEAP_FLAG_NONE, &textureDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&m_texture)));
        const UINT64 uploadBufferSize = GetRequiredIntermediateSize(m_texture.Get(), 0, 1);
        ThrowIfFailed(m_device->CreateCommittedResource(
            get_rvalue_ptr(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
            D3D12_HEAP_FLAG_NONE,
            get_rvalue_ptr(CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize)),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&textureUploadHeap)));

        std::vector<UINT8> texture = GenerateTextureData();

    	D3D12_SUBRESOURCE_DATA textureData = {};
        textureData.pData = &texture[0];
        textureData.RowPitch = TextureWidth * TexturePixelSize;
        textureData.SlicePitch = textureData.RowPitch * TextureHeight;
        UpdateSubresources(m_commandList.Get(), m_texture.Get(), textureUploadHeap.Get(), 0, 0, 1, &textureData);
        m_commandList->ResourceBarrier(1, 
        get_rvalue_ptr(CD3DX12_RESOURCE_BARRIER::Transition(m_texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)));


        //根据纹理创建SRV
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = textureDesc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        m_device->CreateShaderResourceView(m_texture.Get(), &srvDesc, m_srvHeap->GetCPUDescriptorHandleForHeapStart());
    }

    
    {
        const UINT constantBufferSize = sizeof(SceneConstantBuffer);
        ThrowIfFailed(m_device->CreateCommittedResource(
            get_rvalue_ptr(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
            D3D12_HEAP_FLAG_NONE,
            get_rvalue_ptr(CD3DX12_RESOURCE_DESC::Buffer(constantBufferSize)),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_constantBuffer)));

        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = m_constantBuffer->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = constantBufferSize;
        m_device->CreateConstantBufferView(&cbvDesc, m_cbvHeap->GetCPUDescriptorHandleForHeapStart());

        CD3DX12_RANGE readRange(0, 0);
        ThrowIfFailed(m_constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_pCbvDataBegin)));
        memcpy(m_pCbvDataBegin, &m_constantBufferData, sizeof(m_constantBufferData));
    }
    {
        ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_BUNDLE, m_bundleAllocator.Get(), m_pipelineState.Get(), IID_PPV_ARGS(&m_bundle)));
        m_bundle->SetGraphicsRootSignature(m_rootSignature.Get());
        m_bundle->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_bundle->IASetVertexBuffers(0, 1, &m_vertexBufferView);
        m_bundle->DrawInstanced(3, 1, 0, 0);
        ThrowIfFailed(m_bundle->Close());
    }

    // ThrowIfFailed(m_commandList->Close());
    // ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
    // m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

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
        WaitForPreviousFrame();
    }
}

std::vector<UINT8> D3D12HelloWindow::GenerateTextureData()
{
    const UINT rowPitch = TextureWidth * TexturePixelSize;
    const UINT cellPitch = rowPitch >> 3; //复选纹理单元格宽度
    const UINT cellHeight = TextureWidth >> 3; // 复选纹理单元格高度
    const UINT textureSize = rowPitch * TextureHeight;

    std::vector<UINT8> data(textureSize);
    UINT8* pData = &data[0];
    for(UINT n = 0; n < textureSize; n+= TexturePixelSize)
    {
        UINT x = n % rowPitch;
        UINT y = n / rowPitch;
        UINT i = x / cellPitch;
        UINT j = y / cellHeight;

        if (i % 2 == j % 2)
        {
            pData[n] = 0x00;        // R
            pData[n + 1] = 0x00;    // G
            pData[n + 2] = 0x00;    // B
            pData[n + 3] = 0xff;    // A
        }
        else
        {
            pData[n] = 0xff;        // R
            pData[n + 1] = 0xff;    // G
            pData[n + 2] = 0xff;    // B
            pData[n + 3] = 0xff;    // A
        }
    }
    return data;
}

// Update frame-based values.
void D3D12HelloWindow::OnUpdate()
{
    const float translationSpeed = 0.005f;
    const float offsetBounds = 1.25f;
    m_constantBufferData.offset.x += translationSpeed;
    if (m_constantBufferData.offset.x > offsetBounds)
    {
        m_constantBufferData.offset.x = -offsetBounds;
    }
    memcpy(m_pCbvDataBegin, &m_constantBufferData, sizeof(m_constantBufferData));
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

    //必要状态设置
    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());

    ID3D12DescriptorHeap* ppHeaps[] = { m_cbvHeap.Get() };
    m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
    m_commandList->SetGraphicsRootDescriptorTable(0, m_cbvHeap->GetGPUDescriptorHandleForHeapStart());

    m_commandList->RSSetViewports(1, &m_viewport);
    m_commandList->RSSetScissorRects(1, &m_scissorRect);

    // 指示后台缓冲区从Persent状态转换为RT状态（只读 -> 可写）
    m_commandList->ResourceBarrier(1, get_rvalue_ptr(CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), 
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET)));

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
    m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    // 将clearColor 渲染到RT上
    const float clearColor[] = { 0.0f, 0.2f, 0.4f, 0.6f };
    m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    // m_commandList->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    // m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    // m_commandList->DrawInstanced(3, 1, 0, 0);
    m_commandList->ExecuteBundle(m_bundle.Get());

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
