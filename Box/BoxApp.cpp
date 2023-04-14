// 按住左键拖动旋转,右键缩放

// RTV,DSV				: 有描述符堆, 有描述符(视图)
// 顶点缓冲区,索引缓冲区: 无描述符堆, 有描述符(视图), 默认堆, 通过输入布局描述指定着色器寄存器
// 常量缓冲区			: 有描述符堆, 有描述符(视图), 上传堆, 通过根签名指定着色器寄存器, CPU每帧更新

#include "BoxApp.h"

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance,
	_In_ LPSTR lpCmdLine, _In_ int nShowCmd)
{
	// Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	try
	{
		BoxApp theApp(hInstance);
		if (!theApp.Initialize())
			return 0;

		return theApp.Run();
	}
	catch (DxException& e)
	{
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}
}

BoxApp::BoxApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{

}

BoxApp::~BoxApp()
{

}

bool BoxApp::Initialize()
{
	if (!D3DApp::Initialize())
		return 0;

	// 重置命令列表,准备初始化
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));
	BuildBoxGeometry();

	BuildRootSignature();
	BuildShadersAndInputLayout();
	BuildRenderItems();
	BuildFrameResources();
	BuildDescriptorHeaps();
	BuildConstantBufferViews();
	// BuildConstantBuffers();
	BuildPSO();

	// 执行初始化命令
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsList[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsList), cmdsList);

	// 等待初始化完成
	FlushCommandQueue();

	return true;
}

void BoxApp::OnResize()
{
	D3DApp::OnResize();

	// The window resized, so update the aspect ratio and recompute the projection matrix.
	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f*MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	XMStoreFloat4x4(&mProj, P);
}

void BoxApp::Update(const GameTimer& gt)
{
	UpdateCamera(gt);
	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
	mCurrFrameResources = mFrameResources[mCurrFrameResourceIndex].get();
	if (mCurrFrameResources->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResources->Fence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResources->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	UpdateObjectCBs(gt);
	UpdateMainPassCB(gt);
}

void BoxApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResources->ObjectCB.get();
	for (auto& e : mAllRitems)
	{
		if (e->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			e->NumFramesDirty--;
		}
	}
}

void BoxApp::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = XMLoadFloat4x4(&mView);
	XMMATRIX proj = XMLoadFloat4x4(&mProj);

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(get_rvalue_ptr(XMMatrixDeterminant(view)), view);
	XMMATRIX invProj = XMMatrixInverse(get_rvalue_ptr(XMMatrixDeterminant(proj)), proj);
	XMMATRIX invViewProj = XMMatrixInverse(get_rvalue_ptr(XMMatrixDeterminant(viewProj)), viewProj);

	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	mMainPassCB.EyePosW = mEyePos;
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();

	auto currPassCB = mCurrFrameResources->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void BoxApp::UpdateCamera(const GameTimer& gt)
{
	// Convert Spherical to Cartesian coordinates.
	mEyePos.x = mRadius * sinf(mPhi) * cosf(mTheta);
	mEyePos.z = mRadius * sinf(mPhi) * sinf(mTheta);
	mEyePos.y = mRadius * cosf(mPhi);

	// Build the view matrix.
	XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&mView, view);
}

void BoxApp::Draw(const GameTimer& gt)
{
	// ThrowIfFailed(mDirectCmdListAlloc->Reset());
	// auto cmdListAlloc = mCurrFrameResources->CmdListAlloc;
	//
	// ThrowIfFailed(cmdListAlloc->Reset());

	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), mPSO.Get()));

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	// 在顶点缓冲区及其对应视图创建完成后,将它与渲染流水线上的一个输入槽相绑定
	// 这样就能向流水线中的输入装配器阶段传递顶点数据了
	// para1: 在绑定多个顶点缓冲区时,起始输入槽0~15
	// para2: 将要与输入槽绑定的顶点缓冲区数量
	// para3: 指向顶点缓冲区视图数组中第一个元素的指针
	// 将顶点缓冲区设置到输入槽上并不会对其执行实际的绘制操作,而是仅为顶点数据送至渲染流水线做好准备
	// 最后一步是通过 ID3D12GraphicsCommandList::DrawInstanced 方法真正的绘制顶点
	// 在使用索引的时候,用 ID3D12GraphicsCommandList::DrawIndexedInstanced 绘制
	// mCommandList->IASetVertexBuffers(0, 1, &mBoxGeo->VertexBufferView());
	// // 将索引缓冲区绑定到输入装配器阶段
	// mCommandList->IASetIndexBuffer(&mBoxGeo->IndexBufferView());
	// mCommandList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	//
	// // 根签名只定义了应用程序要绑定到渲染流水线的资源,却没有真正地执行任何资源绑定操作
	// // 只有率先通过命令列表设置好根签名
	// // 就能用 SetGraphicsRootDescriptorTable 方法令描述符表与渲染流水线相绑定
	// // para1: 将根参数按此索引(欲绑定到的寄存器槽号)进行设置
	// // para2: 要向着色器绑定的描述符表中第一个描述符位于描述符堆中的句柄
	// mCommandList->SetGraphicsRootDescriptorTable(0, mCbvHeap->GetGPUDescriptorHandleForHeapStart());
	//
	// // 在使用索引的时候,用 ID3D12GraphicsCommandList::DrawIndexedInstanced 绘制
	// // para1: 每个实例将要绘制的索引数量
	// // para2: 用于实现被称作实例化的高级技术.目前只绘制一个实例,暂设为1
	// // para3: 起始索引
	// // para4: 在本次绘制调用读取顶点之前,要为每个索引都加上此整数值
	// // para5: 用与实现被称作实例化的高级技术,暂设为0
	// mCommandList->DrawIndexedInstanced(
	// 	mBoxGeo->DrawArgs["box"].IndexCount,
	// 	1, 0, 0, 0);

	int passCbvIndex = mPassCbvOffset + mCurrFrameResourceIndex;
	auto passCbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
	passCbvHandle.Offset(passCbvIndex, mCbvSrvUavDescriptorSize);
	// 将过程常量绑定到着色器寄存器,cbuffer cbPass : register(b1)
	mCommandList->SetGraphicsRootDescriptorTable(1, passCbvHandle);

	DrawRenderItems(mCommandList.Get(), {});

	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	// Done recording commands.
	ThrowIfFailed(mCommandList->Close());

	// Add the command list to the queue for execution.
	ID3D12CommandList* cmdLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);

	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	// FlushCommandQueue();
	mCurrFrameResources->Fence = ++mCurrentFence;

	// mFence 配合 mCurrentFence 不停的将 ++ 命令加入GPU命令队列中
	// 它并没有跳跃,而是 0->1->2->3... 一个一个的增加
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);

}



void BoxApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

	auto objectCB = mCurrFrameResources->ObjectCB->Resource();

	// for (size_t i = 0; i < ritems.size(); ++i)
	{
		// auto ri = ritems[i];
		// Geo->VertexBufferView()中存储了顶点的起始位置和大小
		cmdList->IASetVertexBuffers(0, 1, &mBoxGeo->VertexBufferView());
		// 将索引缓冲区绑定到输入装配器阶段
		cmdList->IASetIndexBuffer(&mBoxGeo->IndexBufferView());
		cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		// 为了绘制当前的帧资源和当前物体,偏移到描述符堆中对应的CBV处
		UINT cbvIndex = mCurrFrameResourceIndex * (UINT)mOpaqueRitems.size();
		auto cbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
		cbvHandle.Offset(cbvIndex, mCbvSrvUavDescriptorSize);

		cmdList->SetGraphicsRootDescriptorTable(0, cbvHandle); // cbuffer cbPerObject : register(b0)
		// cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
		cmdList->DrawIndexedInstanced(
			mBoxGeo->DrawArgs["box"].IndexCount,
			1, 0, 0, 0);
	}
}

void BoxApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWnd);
}

void BoxApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void BoxApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		// Make each pixel correspond to a quarter of a degree.
		float dx = XMConvertToRadians(0.25f*static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f*static_cast<float>(y - mLastMousePos.y));

		// Update angles based on input to orbit camera around box.
		mTheta += -dx; // 觉得原来的别扭,这里我换了方向
		mPhi += -dy;

		// Restrict the angle mPhi.
		mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
	}
	if ((btnState & MK_RBUTTON) != 0) // 原来是 else if, 不顺畅
	{
		// Make each pixel correspond to 0.005 unit in the scene.
		float dx = 0.005f*static_cast<float>(x - mLastMousePos.x);
		float dy = 0.005f*static_cast<float>(y - mLastMousePos.y);

		// Update the camera radius based on input.
		mRadius += -(dx - dy); // 我换了方向

		// Restrict the radius.
		mRadius = MathHelper::Clamp(mRadius, 3.0f, 15.0f);
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

// void BoxApp::BuildDescriptorHeaps()
// {
// 	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
// 	cbvHeapDesc.NumDescriptors = 1;
// 	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
// 	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE; // 着色器可见
// 	cbvHeapDesc.NodeMask = 0;
// 	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&cbvHeapDesc,
// 		IID_PPV_ARGS(&mCbvHeap)));
// }

void BoxApp::BuildDescriptorHeaps()
{
	UINT objCount = (UINT)mOpaqueRitems.size();

	// 为每个帧资源中的每一个物体都创建一个CBV描述符
	// 为每个帧资源的渲染过程CBV而+1
	UINT numDescriptors = (objCount + 1) * gNumFrameResources;

	// 偏移到过程常量
	mPassCbvOffset = objCount * gNumFrameResources;

	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
	cbvHeapDesc.NumDescriptors = numDescriptors; // 描述符数量
	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	cbvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&cbvHeapDesc,
		IID_PPV_ARGS(&mCbvHeap)));
}

void BoxApp::BuildConstantBufferViews()
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

	UINT objCount = (UINT)mOpaqueRitems.size();

	// 每个帧资源中的每一个物体都需要一个对应的CBV描述符
	for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
	{
		auto objectCB = mFrameResources[frameIndex]->ObjectCB->Resource();
		for (UINT i = 0; i < objCount; ++i)
		{
			D3D12_GPU_VIRTUAL_ADDRESS cbAddress = objectCB->GetGPUVirtualAddress(); // 这里将 World和描述符关联

			// 偏移到缓冲区中第i个物体的常量缓冲区
			cbAddress += i * objCBByteSize;

			// 偏移到该物体在描述符堆中的CBV
			int heapIndex = frameIndex * objCount + i;
			auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
			// 在描述符堆中的句柄按照字节偏移 (frameIndex * objCount + i) * mCbvSrvUavDescriptorSize
			handle.Offset(heapIndex, mCbvSrvUavDescriptorSize);

			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
			cbvDesc.BufferLocation = cbAddress; // 物体的常量缓存地址按照字节偏移 i * sizeof(ObjectConstants)
			cbvDesc.SizeInBytes = objCBByteSize;

			md3dDevice->CreateConstantBufferView(&cbvDesc, handle);
		}
	}

	UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

	// 最后3个描述符依次是每个帧资源的渲染过程CBV
	for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
	{
		auto passCB = mFrameResources[frameIndex]->PassCB->Resource();
		D3D12_GPU_VIRTUAL_ADDRESS cbAddress = passCB->GetGPUVirtualAddress(); // 这里将 过程常量与描述符联系起来

		// 偏移到描述符堆中对应的渲染过程CBV
		int heapIndex = mPassCbvOffset + frameIndex; // heapIndex = objCount * gNumFrameResources + frameIndex
		auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
		handle.Offset(heapIndex, mCbvSrvUavDescriptorSize); // 渲染过程的句柄地址偏移 objCount * gNumFrameResources + frameIndex

		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
		cbvDesc.BufferLocation = cbAddress; // 渲染过程的物体常量偏移 每帧只有一个,所以不偏移
		cbvDesc.SizeInBytes = passCBByteSize;

		md3dDevice->CreateConstantBufferView(&cbvDesc, handle);
	}
}

void BoxApp::BuildConstantBuffers()
{
	// 绘制1个物体所需的常量数据
	mObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(md3dDevice.Get(), 1, true);
	// 计算常量对象大小,配合index用于在GPU虚拟地址中偏移
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

	// 缓冲区的起始地址
	D3D12_GPU_VIRTUAL_ADDRESS cbAddress = mObjectCB->Resource()->GetGPUVirtualAddress();

	// 偏移到常量缓冲区中绘制第i个物体所需的常量数据
	int boxCBIndex = 0;
	cbAddress += boxCBIndex * objCBByteSize;

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
	cbvDesc.BufferLocation = cbAddress;
	cbvDesc.SizeInBytes = objCBByteSize;

	md3dDevice->CreateConstantBufferView(
		&cbvDesc,
		mCbvHeap->GetCPUDescriptorHandleForHeapStart());
}

void BoxApp::BuildRootSignature()
{
	// 着色器程序一般需要以资源作为输入(eg,常量缓冲区,纹理,采样器等)
	// 根签名定义了着色器程序所需的具体资源
	// 如果把着色器程序看做一个函数,而将输入的资源看做向函数传递的参数数据
	// 那么便可类似地认为根签名定义的是函数签名

	// 根签名以一组描述绘制调用过程中,着色器所需资源的根参数定义而成 

	// 根参数可以是根常量,根描述符,或根描述符表
	// 描述符表指定的是描述符堆中存有描述符的一块连续区域
	CD3DX12_ROOT_PARAMETER slotRootParameter[2];

	CD3DX12_DESCRIPTOR_RANGE cbvTable1;
	// para1: 描述符表的类型
	// para2: 表中描述符的数量
	// para3: 将这段描述符区域绑定至此基址着色器寄存器
	cbvTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);

	// para1: 描述符区域的数量
	// para2: 指向描述符区域数组的指针
	slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable1);

	CD3DX12_DESCRIPTOR_RANGE cbvTable2;
	// para1: 描述符表的类型
	// para2: 表中描述符的数量
	// para3: 将这段描述符区域绑定至此基址着色器寄存器
	cbvTable2.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);

	// para1: 描述符区域的数量
	// para2: 指向描述符区域数组的指针
	slotRootParameter[1].InitAsDescriptorTable(1, &cbvTable2);

	// 根签名是根参数数组
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, slotRootParameter, 0, nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	// Direct3D 12规定,必须先将根签名的描述布局进行序列化处理,待其转换为以 ID3DBlob 接口表示的序列化数据格式后,才可以将它传入 CreateRootSignature 方法
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(&mRootSignature)));
}

void BoxApp::BuildShadersAndInputLayout()
{
	mvsByteCode = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "VS", "vs_5_1");
	mpsByteCode = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "PS", "ps_5_1");

	mInputLayout =
	{
		// para1: 语义,通过语义可以将顶点结构体中的元素与顶点着色器的输入签名中的元素一一映射
		// para2: 附到语义上的索引.可在不引入新语义的情况下区分元素(eg,POSITION0,POSITION1)
		// para3: DXGI_FORMAT 顶点元素的数据类型
		// para4: 输入槽,支持0~15
		// para5: 特定输入槽中,顶点结构体的首地址到某元素起始地址的偏移量(用字节表示)
		// para6: 暂定如此
		// para7: 暂定如此
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	};
}


void BoxApp::BuildBoxGeometry()
{
	std::array<Vertex, 8> vertices = // 顶点,基于局部坐标
	{
		Vertex({ XMFLOAT3(-1.0f, -1.0f ,-1.0f), XMFLOAT4(Colors::White) }),
		Vertex({ XMFLOAT3(-1.0f, +1.0f, -1.0f), XMFLOAT4(Colors::Black) }),
		Vertex({ XMFLOAT3(+1.0f, +1.0f, -1.0f), XMFLOAT4(Colors::Red) }),
		Vertex({ XMFLOAT3(+1.0f, -1.0f, -1.0f), XMFLOAT4(Colors::Green) }),
		Vertex({ XMFLOAT3(-1.0f, -1.0f, +1.0f), XMFLOAT4(Colors::Blue) }),
		Vertex({ XMFLOAT3(-1.0f, +1.0f, +1.0f), XMFLOAT4(Colors::Yellow) }),
		Vertex({ XMFLOAT3(+1.0f, +1.0f, +1.0f), XMFLOAT4(Colors::Cyan) }),
		Vertex({ XMFLOAT3(+1.0f, -1.0f, +1.0f), XMFLOAT4(Colors::Magenta) })
	};

	std::array<std::uint16_t, 36> indices =
	{
		// front face
		0, 1, 2,
		0, 2, 3,

		// back face
		4, 6, 5,
		4, 7, 6,

		// left face
		4, 5, 1,
		4, 1, 0,

		// right face
		3, 2, 6,
		3, 6, 7,

		// top face
		1, 5, 6,
		1, 6, 2,

		// bottom face
		4, 0, 3,
		4, 3, 7
	};

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	mBoxGeo = std::make_unique<MeshGeometry>();
	mBoxGeo->Name = "boxGeo";
	// 创建顶点和索引在内存中的副本
	ThrowIfFailed(D3DCreateBlob(vbByteSize, &mBoxGeo->VertexBufferCPU));
	CopyMemory(mBoxGeo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &mBoxGeo->IndexBufferCPU));
	CopyMemory(mBoxGeo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);
	// 将顶点数据和索引数据上传到GPU默认堆
	mBoxGeo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, mBoxGeo->VertexBufferUploader);

	mBoxGeo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, mBoxGeo->IndexBufferUploader);

	mBoxGeo->VertexByteStride = sizeof(Vertex);
	mBoxGeo->VertexBufferByteSize = vbByteSize;
	mBoxGeo->IndexFormat = DXGI_FORMAT_R16_UINT;
	mBoxGeo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	mBoxGeo->DrawArgs["box"] = submesh;
}

void BoxApp::BuildPSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
	ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	// 输入布局描述 结构体 D3D12_INPUT_LAYOUT_DESC
	// para1: D3D12_INPUT_ELEMENT_DESC *pInputElementDescs
	// para2: UINT NumElements
	psoDesc.InputLayout = { mInputLayout.data(),(UINT)mInputLayout.size() }; // 输入布局描述,将顶点结构体映射到VS的输入参数中
	psoDesc.pRootSignature = mRootSignature.Get(); // 根签名,指定了着色器程序所需的资源(CBV对应哪个着色器寄存器)
	psoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mvsByteCode->GetBufferPointer()),
		mvsByteCode->GetBufferSize()
	};
	psoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mpsByteCode->GetBufferPointer()),
		mpsByteCode->GetBufferSize()
	};
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.SampleMask = UINT_MAX; // 对所有的采样点进行采样
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1; // 同时所用的渲染目标数量(即 RTVFormats 数组中渲染目标格式的数量)
	psoDesc.RTVFormats[0] = mBackBufferFormat; // 渲染目标的格式
	psoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	psoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	psoDesc.DSVFormat = mDepthStencilFormat;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSO)));
}

void BoxApp::BuildFrameResources()
{
	for (int i = 0; i < gNumFrameResources; ++i)
	{
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(), 1, (UINT)mAllRitems.size()));
	}
}

void BoxApp::BuildRenderItems()
{
	// 将每一个物体都存到 mAllRitems, mOpaqueRitems 中,相同物体的顶点/索引偏移相同,但是它们的世界矩阵不同,ObjIndex++. 
	// 渲染对象中存储了 World, ObjCBIndex, Geo, PrimitiveType, IndexCount, StartIndexLocation, BaseVertexLocation
	auto boxRitem = std::make_unique<RenderItem>();
	// XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 0.5f, 0.0f));
	boxRitem->World = MathHelper::Identity4x4();
	boxRitem->ObjCBIndex = 0;
	boxRitem->Geo = mBoxGeo.get();
	boxRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
	boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(boxRitem));

	// auto gridItem = std::make_unique<RenderItem>();
	// gridItem->World = MathHelper::Identity4x4();
	// gridItem->ObjCBIndex = 1;
	// gridItem->Geo = mBoxGeo.get();
	// gridItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	// gridItem->IndexCount = gridItem->Geo->DrawArgs["grid"].IndexCount;
	// gridItem->StartIndexLocation = gridItem->Geo->DrawArgs["grid"].StartIndexLocation;
	// gridItem->BaseVertexLocation = gridItem->Geo->DrawArgs["grid"].BaseVertexLocation;
	// mAllRitems.push_back(std::move(gridItem));

	// UINT objCBIndex = 2;
	// for (int i = 0; i < 5; ++i)
	// {
	// 	auto leftCylRitem = std::make_unique<RenderItem>();
	// 	auto rightCylRitem = std::make_unique<RenderItem>();
	// 	auto leftSphereRitem = std::make_unique<RenderItem>();
	// 	auto rightSphereRitem = std::make_unique<RenderItem>();
	//
	// 	XMMATRIX leftCylWorld = XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i * 5.0f);
	// 	XMMATRIX rightCylWorld = XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i * 5.0f);
	//
	// 	XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i * 5.0f);
	// 	XMMATRIX rightSphereWorld = XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i * 5.0f);
	//
	// 	XMStoreFloat4x4(&leftCylRitem->World, leftCylWorld);
	// 	leftCylRitem->ObjCBIndex = objCBIndex++;
	// 	leftCylRitem->Geo = mGeometries["shapeGeo"].get();
	// 	leftCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	// 	leftCylRitem->IndexCount = leftCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
	// 	leftCylRitem->StartIndexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
	// 	leftCylRitem->BaseVertexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	//
	// 	XMStoreFloat4x4(&rightCylRitem->World, rightCylWorld);
	// 	rightCylRitem->ObjCBIndex = objCBIndex++;
	// 	rightCylRitem->Geo = mGeometries["shapeGeo"].get();
	// 	rightCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	// 	rightCylRitem->IndexCount = rightCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
	// 	rightCylRitem->StartIndexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
	// 	rightCylRitem->BaseVertexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	//
	// 	XMStoreFloat4x4(&leftSphereRitem->World, leftSphereWorld);
	// 	leftSphereRitem->ObjCBIndex = objCBIndex++;
	// 	leftSphereRitem->Geo = mGeometries["shapeGeo"].get();
	// 	leftSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	// 	leftSphereRitem->IndexCount = leftSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
	// 	leftSphereRitem->StartIndexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
	// 	leftSphereRitem->BaseVertexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;
	//
	// 	XMStoreFloat4x4(&rightSphereRitem->World, rightSphereWorld);
	// 	rightSphereRitem->ObjCBIndex = objCBIndex++;
	// 	rightSphereRitem->Geo = mGeometries["shapeGeo"].get();
	// 	rightSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	// 	rightSphereRitem->IndexCount = rightSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
	// 	rightSphereRitem->StartIndexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
	// 	rightSphereRitem->BaseVertexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;
	//
	// 	mAllRitems.push_back(std::move(leftCylRitem));
	// 	mAllRitems.push_back(std::move(rightCylRitem));
	// 	mAllRitems.push_back(std::move(leftSphereRitem));
	// 	mAllRitems.push_back(std::move(rightSphereRitem));
	// }

	for (auto& e : mAllRitems)
		mOpaqueRitems.push_back(e.get());
}