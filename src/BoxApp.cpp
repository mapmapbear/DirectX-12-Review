// 按住左键拖动旋转,右键缩放

// RTV,DSV				: 有描述符堆, 有描述符(视图)
// 顶点缓冲区,索引缓冲区: 无描述符堆, 有描述符(视图), 默认堆, 通过输入布局描述指定着色器寄存器
// 常量缓冲区			: 有描述符堆, 有描述符(视图), 上传堆, 通过根签名指定着色器寄存器, CPU每帧更新

#include "BoxApp.h"

const int gNumFrameResources = 3;

template <typename T>
std::vector<T> AddVectors(const std::vector<T> &vector1, const std::vector<T> &vector2) {
	std::vector<T> result;

	// 检查向量的大小是否相同
	if (vector1.size() != vector2.size()) {
		::OutputDebugStringW(L"Error: Vectors must have the same size.");
		return result;
	}

	// 遍历向量并相加对应位置的元素
	for (size_t i = 0; i < vector1.size(); ++i) {
		result.push_back(vector1[i] + vector2[i]);
	}

	return result;
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
	mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	mWaves = std::make_unique<Waves>(128, 128, 1.0f, 0.03f, 4.0f, 0.2f);
	// 重置命令列表,准备初始化
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));
	LoadTextures();

	BuildRootSignature();
	BuildShadersAndInputLayout();
	BuildWavesGeometryBuffers();
	BuildShapeGeometry1();
	BuildRoomGeometry();
	BuildSkullGeometry();
	BuildMaterials();
	BuildRenderItems();
	BuildFrameResources();
	BuildDescriptorHeaps();
	BuildConstantBufferViews();
	BuildPSO();

	// 执行初始化命令
	ThrowIfFailed(mCommandList->Close())
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
	OnKeyboardInput(gt);
	UpdateCamera(gt);
	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
	mCurrFrameResources = mFrameResources[mCurrFrameResourceIndex].get();
	if (mCurrFrameResources->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResources->Fence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, L"false", false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResources->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}
#ifdef __IMGUI
	UpdateImGui(gt, mMainPassCB);
#endif
	AnimateMaterial(gt);
	UpdateObjectCBs(gt);
	UpdateMainPassCB(gt);
	UpdateReflectedPassCB(gt);
	UpdateMaterialCBs(gt);
	UpdateWaves(gt);
}

#ifdef __IMGUI
void BoxApp::UpdateImGui(const GameTimer &gt, PassConstants& buffer) {
	static bool animateCube = true, customColor = false;
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
	static float tx = 0.0f, ty = 0.0f, phi = 0.0f, theta = 0.0f, scale = 1.0f, fov = XM_PIDIV2;
	float dt = gt.DeltaTime();
	if (animateCube) {
		phi += 0.3f * dt, theta += 0.37f * dt;
		phi = XMScalarModAngle(phi);
		theta = XMScalarModAngle(theta);
	}
	if (ImGui::Begin("Use ImGui")) {
		{
			const char *items[] = { "Opaque", "Transparent", "WireFrame" };
			static int item_current = 0;
			ImGui::Combo("PSO", &item_current, items, IM_ARRAYSIZE(items));
			posState = item_current;
		}
		ImGui::Checkbox("Animate Cube", &animateCube);
		ImGui::SameLine(0.0f, 25.0f);
		if (ImGui::Button("Reset Params")) {
			tx = ty = phi = theta = 0.0f;
			scale = 1.0f;
			fov = XM_PIDIV2;
		}
		ImGui::SliderFloat("Scale", &scale, 0.2f, 2.0f);
		ImGui::Text("Phi: %.2f degrees", XMConvertToDegrees(phi));
		ImGui::SliderFloat("##1", &phi, -XM_PI, XM_PI, "");
		ImGui::Text("Theta: %.2f degrees", XMConvertToDegrees(theta));
		ImGui::SliderFloat("##2", &theta, -XM_PI, XM_PI, "");
		ImGui::Text("Position: (%.1f, %.1f, 0.0)", tx, ty);
		ImGui::Text("FOV: %.2f degrees", XMConvertToDegrees(fov));
		ImGui::SliderFloat("##3", &fov, XM_PIDIV4, XM_PI / 3 * 2, "");

		if (ImGui::Checkbox("Use Custom Color", &customColor)) {
		}
		if (customColor) {
			ImGui::ColorEdit3("ClearColor", gColor);
			printf("");
		}
	}
	ImGui::End();
	buffer.useCustomColor = customColor ? 1 : 0;
	buffer.color = XMFLOAT4(gColor[0], gColor[1], gColor[2], 1.0);
}
#endif
void BoxApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResources->ObjectCB.get();
	for (auto& e : mAllRitems)
	{
		if (e->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);
			XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));
			currObjectCB->CopyData(e->ObjCBIndex, objConstants);
			e->NumFramesDirty--;
		}
	}
}

void BoxApp::UpdateMainPassCB(const GameTimer& gt) {
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
	mMainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };
	mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[0].Strength = { 0.6f, 0.6f, 0.6f };
	mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[1].Strength = { 0.3f, 0.3f, 0.3f };
	mMainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
	mMainPassCB.Lights[2].Strength = { 0.15f, 0.15f, 0.15f };

	auto currPassCB = mCurrFrameResources->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void BoxApp::UpdateReflectedPassCB(const GameTimer &gt) {
	mReflectedPassCB = mMainPassCB;

	XMVECTOR mirrorPlane = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f); // xy plane
	XMMATRIX R = XMMatrixReflect(mirrorPlane);

	// Reflect the lighting.
	for (int i = 0; i < 3; ++i) {
		XMVECTOR lightDir = XMLoadFloat3(&mMainPassCB.Lights[i].Direction);
		XMVECTOR reflectedLightDir = XMVector3TransformNormal(lightDir, R);
		XMStoreFloat3(&mReflectedPassCB.Lights[i].Direction, reflectedLightDir);
	}

	// Reflected pass stored in index 1
	auto currPassCB = mCurrFrameResources->PassCB.get();
	currPassCB->CopyData(1, mReflectedPassCB);
}

void BoxApp::UpdateMaterialCBs(const GameTimer &gt) {
	auto currMaterialCB = mCurrFrameResources->MaterialCB.get();
	for (auto &e : mMaterials) {
		Material *mat = e.second.get();
		if (mat->NumFramesDirty > 0) {
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

			MaterialConstants matConstants;
			matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
			matConstants.FresnelR0 = mat->FresnelR0;
			matConstants.Roughness = mat->Roughness;
			XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));

			currMaterialCB->CopyData(mat->MatCBIndex, matConstants);

			mat->NumFramesDirty--;
		}
	}
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

void BoxApp::UpdateWaves(const GameTimer& gt) {
	static float t_base = 0.0f;
	if ((mTimer.TotalTime() - t_base) >= 0.25f) {
		t_base += 0.25f;
		int i = MathHelper::Rand(4, mWaves->RowCount() - 5);
		int j = MathHelper::Rand(4, mWaves->ColumnCount() - 5);
		float r = MathHelper::RandF(0.2f, 0.5f);
		mWaves->Disturb(i, j, r);
	}
	mWaves->Update(gt.DeltaTime());

	auto currWavesVB = mCurrFrameResources->WavesVB.get();
	for (int i = 0; i < mWaves->VertexCount(); ++i) {
		Vertex v;

		v.Pos = mWaves->Position(i);
		v.Normal = mWaves->Normal(i);

		// Derive tex-coords from position by
		// mapping [-w/2,w/2] --> [0,1]
		v.UV0.x = 0.5f + v.Pos.x / mWaves->Width();
		v.UV0.y = 0.5f - v.Pos.z / mWaves->Depth();

		currWavesVB->CopyData(i, v);
	}

	// Set the dynamic VB of the wave renderitem to the current frame VB.
	mWavesRitem->Geo->VertexBufferGPU = currWavesVB->Resource();
}

void BoxApp::OnKeyboardInput(const GameTimer &gt) {
	if (GetAsyncKeyState('1') & 0x8000) {
		mIsWireframe = true;
		misTransparent = false;
	} else if (GetAsyncKeyState('2') & 0x8000) {
		misTransparent = true;
		mIsWireframe = false;
	} else {
		mIsWireframe = false;
		misTransparent = false;
	}
}

void BoxApp::Draw(const GameTimer& gt)
{
	auto cmdListAlloc = mCurrFrameResources->CmdListAlloc;
	ThrowIfFailed(cmdListAlloc->Reset());

	// if (mIsWireframe)
	switch (posState) {
		case 0:
			ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));
			break;
		case 1:
			ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["transparent"].Get()));
			break;
		case 2:
			ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque_wireframe"].Get()));
			break;
		default:
			ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));
			break;
	}
#ifdef __IMGUI
	ImGui::Render();
#endif

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	mCommandList->ResourceBarrier(1, get_rvalue_ptr(CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET)));
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), (float *)&mMainPassCB.FogColor, 0, nullptr);

	// mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	mCommandList->OMSetRenderTargets(1, get_rvalue_ptr(CurrentBackBufferView()), true, get_rvalue_ptr(DepthStencilView()));

	// 设置 mCbvHeap,里面为每个帧资源的每一个物体和过程常量设了一个描述符,
	// 将帧资源中的物体缓存和过程缓存使用 index*bytesize,通过handle偏移绑定,
	// 物体常量和过程常量缓存通过CopyData()赋值
	// 其实PSO中有根签名
	ID3D12DescriptorHeap *descriptorHeaps1[] = {mCbvHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps1), descriptorHeaps1);
	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	auto passCB = mCurrFrameResources->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

	DrawRenderItems(mCommandList.Get(), mOpaqueRitems);

	// Draw AlphaTest Render Queue
	// mCommandList->SetPipelineState(mPSOs["alphaTested"].Get());
	// DrawRenderItems(mCommandList.Get(), mAlphaTestRitems);
	
	mCommandList->OMSetStencilRef(1);
	mCommandList->SetPipelineState(mPSOs["markStencilMirrors"].Get());
	DrawRenderItems(mCommandList.Get(), mStencilMirrorsRitems);
	//
	UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));
	mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress() + 1 * passCBByteSize);
	mCommandList->SetPipelineState(mPSOs["drawStencilReflections"].Get());
	DrawRenderItems(mCommandList.Get(), mRelectedRitems);

	mCommandList->SetPipelineState(mPSOs["transparent"].Get());
	DrawRenderItems(mCommandList.Get(), mTransparentRitems);

	mCommandList->SetDescriptorHeaps(1, mImGUIHeap.GetAddressOf());
#ifdef __IMGUI
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), mCommandList.Get());
#endif
	mCommandList->ResourceBarrier(1, get_rvalue_ptr(CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT)));
	ThrowIfFailed(mCommandList->Close());

	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	mCurrFrameResources->Fence = ++mCurrentFence;

	// mFence 配合 mCurrentFence 不停的将 ++ 命令加入GPU命令队列中
	// 它并没有跳跃,而是 0->1->2->3... 一个一个的增加
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void BoxApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));
	auto objectCB = mCurrFrameResources->ObjectCB->Resource();
	auto materialCB = mCurrFrameResources->MaterialCB->Resource();

	// For each render item...
	for (size_t i = 0; i < ritems.size(); ++i) {
		auto ri = ritems[i];
		// Geo->VertexBufferView()中存储了顶点的起始位置和大小
		cmdList->IASetVertexBuffers(0, 1, get_rvalue_ptr(ri->Geo->VertexBufferView()));
		cmdList->IASetIndexBuffer(get_rvalue_ptr(ri->Geo->IndexBufferView()));
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress();
		objCBAddress += ri->ObjCBIndex * objCBByteSize;

		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = materialCB->GetGPUVirtualAddress();
		matCBAddress += ri->Mat->MatCBIndex * matCBByteSize;

		ID3D12DescriptorHeap *descriptorHeaps[] = { mSrvHeap.Get() };
		mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
		CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mSrvHeap->GetGPUDescriptorHandleForHeapStart());
		// BuildRenderItems()中设置了Mat,struct Material内含有DiffuseSrvHeapIndex
		tex.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvDescriptorSize);

		cmdList->SetGraphicsRootDescriptorTable(0, tex);
		cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
		cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
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

void BoxApp::BuildDescriptorHeaps()
{
	UINT objCount = (UINT)(mOpaqueRitems.size() + mTransparentRitems.size() + mAlphaTestRitems.size());

	// 为每个帧资源中的每一个物体都创建一个CBV描述符
	// 为每个帧资源的渲染过程CBV而+1
	UINT numDescriptors = (objCount + 1) * gNumFrameResources;

	// 偏移到过程常量
	mPassCbvOffset = objCount * gNumFrameResources;

	// 创建cbv堆
	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
	cbvHeapDesc.NumDescriptors = numDescriptors; // 描述符数量
	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	cbvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&mCbvHeap)));

	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 6;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvHeap)));

	//
	// Fill out the heap with actual descriptors.
	//
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvHeap->GetCPUDescriptorHandleForHeapStart());

	auto woodCrateTex = mTextures["woodCrateTex"]->Resource;
	auto woodCrateTex2 = mTextures["woodCrateTex2"]->Resource;
	auto waterTex = mTextures["water"]->Resource;
	auto checkboardTex = mTextures["checkboardTex"]->Resource;
	auto iceTex = mTextures["iceTex"]->Resource;
	auto white1x1Tex = mTextures["white1x1Tex"]->Resource;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = woodCrateTex->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = woodCrateTex->GetDesc().MipLevels;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	md3dDevice->CreateShaderResourceView(woodCrateTex.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	srvDesc.Format = woodCrateTex2->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = woodCrateTex2->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(woodCrateTex2.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	srvDesc.Format = waterTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = waterTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(waterTex.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	srvDesc.Format = checkboardTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = checkboardTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(checkboardTex.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	srvDesc.Format = iceTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = iceTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(iceTex.Get(), &srvDesc, hDescriptor);
	
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	srvDesc.Format = white1x1Tex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = white1x1Tex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(white1x1Tex.Get(), &srvDesc, hDescriptor);
}

// 改的还是 cbvHeap,每个帧资源中的每一个物体都需要一个对应的CBV描述符,将物体的常量缓冲区地址和偏移后的句柄绑定,	在描述符堆中的句柄按照字节偏移 (frameIndex * objCount + i) * mCbvSrvUavDescriptorSize  物体的常量缓存地址按照字节偏移 i * sizeof(ObjectConstants)
void BoxApp::BuildConstantBufferViews()
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

	// UINT objCount = (UINT)mOpaqueRitems.size();
	UINT objCount = (UINT)(mOpaqueRitems.size() + mTransparentRitems.size() + mAlphaTestRitems.size());

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

	// material CBV
	for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex) {
		auto materialCB = mFrameResources[frameIndex]->MaterialCB->Resource();
		// for (UINT i = 0; i < mMaterials.size(); ++i) {
		for (auto& m : mMaterials){
			D3D12_GPU_VIRTUAL_ADDRESS cbAddress = materialCB->GetGPUVirtualAddress(); // 这里将 World和描述符关联
			int i = m.second.get()->MatCBIndex;
			// 偏移到缓冲区中第i个物体的常量缓冲区
			cbAddress += i * matCBByteSize;

			// 偏移到该物体在描述符堆中的CBV
			int heapIndex = frameIndex * objCount + i;
			auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
			// 在描述符堆中的句柄按照字节偏移 (frameIndex * objCount + i) * mCbvSrvUavDescriptorSize
			handle.Offset(heapIndex, mCbvSrvUavDescriptorSize);

			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
			cbvDesc.BufferLocation = cbAddress; // 物体的常量缓存地址按照字节偏移 i * sizeof(ObjectConstants)
			cbvDesc.SizeInBytes = matCBByteSize;

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

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> BoxApp::GetStaticSamplers() {
	// 应用程序一般只会用到这些采样器的一部分
	// 所以就将它们全部提前定义好,并作为根签名的一部分保留下来

	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
			0, // shaderRegister 着色器寄存器
			D3D12_FILTER_MIN_MAG_MIP_POINT, // filter 过滤器类型 点过滤
			D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressU U轴方向上所用的寻址模式 重复寻址模式
			D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressV
			D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
			1, // shaderRegister
			D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressU 钳位寻址模式
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressV
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
			2, // shaderRegister
			D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter 线性过滤
			D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressU
			D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressV
			D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
			3, // shaderRegister
			D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressU
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressV
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
			4, // shaderRegister
			D3D12_FILTER_ANISOTROPIC, // filter
			D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressU
			D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressV
			D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressW
			0.0f, // mipLODBias mipmap层级的偏置值
			8); // maxAnisotropy 最大各向异性值

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
			5, // shaderRegister
			D3D12_FILTER_ANISOTROPIC, // filter
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressU
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressV
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressW
			0.0f, // mipLODBias
			8); // maxAnisotropy

	return {
		pointWrap, pointClamp,
		linearWrap, linearClamp,
		anisotropicWrap, anisotropicClamp
	};
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 1> BoxApp::GetStaticSamplers1() {
	// 应用程序一般只会用到这些采样器的一部分
	// 所以就将它们全部提前定义好,并作为根签名的一部分保留下来

	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
			0, // shaderRegister 着色器寄存器
			D3D12_FILTER_MIN_MAG_MIP_POINT, // filter 过滤器类型 点过滤
			D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressU U轴方向上所用的寻址模式 重复寻址模式
			D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressV
			D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	
	return { pointWrap };
}
void BoxApp::BuildRootSignature() {
	CD3DX12_DESCRIPTOR_RANGE texTable;
	texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // register t0
	

	CD3DX12_ROOT_PARAMETER slotRootParameter[4];

	slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[1].InitAsConstantBufferView(0); // register b0
	slotRootParameter[2].InitAsConstantBufferView(1); // register b1
	slotRootParameter[3].InitAsConstantBufferView(2); // register b2

	auto staticSamplers = GetStaticSamplers1(); // register s0 ~ s6

	// 根签名是根参数数组
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter,
			(UINT)staticSamplers.size(), staticSamplers.data(),
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// 创建具有4个槽位的根签名,第一个指向含有单个着色器资源视图的描述符表,其它三个各指向一个常量缓冲区视图
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
			serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr) {
		::OutputDebugStringA((char *)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
			0,
			serializedRootSig->GetBufferPointer(),
			serializedRootSig->GetBufferSize(),
			IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void BoxApp::BuildShadersAndInputLayout()
{
	const D3D_SHADER_MACRO defines[] = {
		"FOG", "1",
		NULL, NULL
	};

	const D3D_SHADER_MACRO alphaTestDefines[] = {
		"FOG", "1",
		"ALPHA_TEST", "1",
		NULL, NULL
	};
	
	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", defines, "PS", "ps_5_1");
	mShaders["alphaTestedPS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", alphaTestDefines, "PS", "ps_5_1");
	// LPCSTR SemanticName; UINT SemanticIndex; DXGI_FORMAT Format; UINT InputSlot; UINT AlignedByteOffset; D3D12_INPUT_CLASSIFICATION InputSlotClass; UINT InstanceDataStepRate; D3D12_INPUT_ELEMENT_DESC;
	mInputLayout = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}

void BoxApp::BuildPSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	// PSO for opaque objects
	ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = { mInputLayout.data(),(UINT)mInputLayout.size() };
	opaquePsoDesc.pRootSignature = mRootSignature.Get();
	opaquePsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
		mShaders["standardVS"]->GetBufferSize()
	};
	opaquePsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
		mShaders["opaquePS"]->GetBufferSize()
	};
	opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePsoDesc.SampleMask = UINT_MAX;
	opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePsoDesc.NumRenderTargets = 1;
	opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
	opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	opaquePsoDesc.DSVFormat = mDepthStencilFormat;

	HRESULT hr = md3dDevice->GetDeviceRemovedReason();
	HRESULT removeReason = md3dDevice->GetDeviceRemovedReason();
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])))

	// PSO for opaque wireframe objects

	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaqueWireframePsoDesc = opaquePsoDesc;
	opaqueWireframePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaqueWireframePsoDesc, IID_PPV_ARGS(&mPSOs["opaque_wireframe"])))

	D3D12_GRAPHICS_PIPELINE_STATE_DESC transparentPsoDesc = opaquePsoDesc;
	D3D12_RENDER_TARGET_BLEND_DESC transparencyBlendDesc;
	transparencyBlendDesc.BlendEnable = true;
	transparencyBlendDesc.LogicOpEnable = false;
	transparencyBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
	transparencyBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	transparencyBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
	transparencyBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
	transparencyBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
	transparencyBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	transparencyBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
	transparencyBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	transparentPsoDesc.BlendState.RenderTarget[0] = transparencyBlendDesc;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&transparentPsoDesc, IID_PPV_ARGS(&mPSOs["transparent"])));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC alphaTestedPsoDesc = opaquePsoDesc;
	alphaTestedPsoDesc.PS = {
		reinterpret_cast<BYTE *>(mShaders["alphaTestedPS"]->GetBufferPointer()),
		mShaders["alphaTestedPS"]->GetBufferSize()
	};
	alphaTestedPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&alphaTestedPsoDesc, IID_PPV_ARGS(&mPSOs["alphaTested"])))

	CD3DX12_BLEND_DESC mirrorBlendState(D3D12_DEFAULT);
	mirrorBlendState.RenderTarget[0].RenderTargetWriteMask = 0;

	D3D12_DEPTH_STENCIL_DESC mirrorDSS;
	mirrorDSS.DepthEnable = true;
	mirrorDSS.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	mirrorDSS.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	mirrorDSS.StencilEnable = true;
	mirrorDSS.StencilReadMask = 0xff;
	mirrorDSS.StencilWriteMask = 0xff;

	mirrorDSS.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	mirrorDSS.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	mirrorDSS.FrontFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
	mirrorDSS.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

	// We are not rendering backfacing polygons, so these settings do not matter.
	mirrorDSS.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	mirrorDSS.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	mirrorDSS.BackFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
	mirrorDSS.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC markMirrorsPsoDesc = opaquePsoDesc;
	markMirrorsPsoDesc.BlendState = mirrorBlendState;
	markMirrorsPsoDesc.DepthStencilState = mirrorDSS;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&markMirrorsPsoDesc, IID_PPV_ARGS(&mPSOs["markStencilMirrors"])));

	D3D12_DEPTH_STENCIL_DESC reflectionsDSS;
	reflectionsDSS.DepthEnable = true;
	reflectionsDSS.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	reflectionsDSS.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	reflectionsDSS.StencilEnable = true;
	reflectionsDSS.StencilReadMask = 0xff;
	reflectionsDSS.StencilWriteMask = 0xff;

	reflectionsDSS.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDSS.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDSS.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDSS.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;

	// We are not rendering backfacing polygons, so these settings do not matter.
	reflectionsDSS.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDSS.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDSS.BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDSS.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC drawReflectionsPsoDesc = opaquePsoDesc;
	drawReflectionsPsoDesc.DepthStencilState = reflectionsDSS;
	drawReflectionsPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
	drawReflectionsPsoDesc.RasterizerState.FrontCounterClockwise = true;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&drawReflectionsPsoDesc, IID_PPV_ARGS(&mPSOs["drawStencilReflections"])));
}

void BoxApp::BuildFrameResources()
{
	for(int i = 0; i < gNumFrameResources; ++i)
	{
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(), 2, (UINT)mAllRitems.size(), mMaterials.size(), mWaves->VertexCount()));
	}
}

void BoxApp::BuildShapeGeometry1() {
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.5f, 0.5f, 1.5f, 3);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);

	//
	// We are concatenating all the geometry into one big vertex/index buffer.  So
	// define the regions in the buffer each submesh covers.
	//

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = (UINT)box.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();

	// Cache the starting index for each object in the concatenated index buffer.
	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = (UINT)box.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();

	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = boxVertexOffset;


	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = gridVertexOffset;

	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

	SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

	//
	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.
	//

	auto totalVertexCount =
			box.Vertices.size() +
			grid.Vertices.size() +
			sphere.Vertices.size() +
			cylinder.Vertices.size();

	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;
	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k) {
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Normal = box.Vertices[i].Normal;
		vertices[k].UV0 = box.Vertices[i].TexC;
	}

	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k) {
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Normal = grid.Vertices[i].Normal;
		vertices[k].UV0 = grid.Vertices[i].TexC;
	}

	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k) {
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Normal = sphere.Vertices[i].Normal;
		vertices[k].UV0 = sphere.Vertices[i].TexC;
	}

	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k) {
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Normal = cylinder.Vertices[i].Normal;
		vertices[k].UV0 = cylinder.Vertices[i].TexC;
	}

	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "shapeGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU))
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU))
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
			mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
			mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->DrawArgs["box"] = boxSubmesh;
	geo->DrawArgs["grid"] = gridSubmesh;
	geo->DrawArgs["sphere"] = sphereSubmesh;
	geo->DrawArgs["cylinder"] = cylinderSubmesh;

	mGeometries[geo->Name] = std::move(geo);
}

void BoxApp::BuildRoomGeometry() {
	// Create and specify geometry.  For this sample we draw a floor
	// and a wall with a mirror on it.  We put the floor, wall, and
	// mirror geometry in one vertex buffer.
	//
	//   |--------------|
	//   |              |
	//   |----|----|----|
	//   |Wall|Mirr|Wall|
	//   |    | or |    |
	//   /--------------/
	//  /   Floor      /
	// /--------------/

	std::array<Vertex, 20> vertices = {
		// Floor: Observe we tile texture coordinates.
		Vertex(-3.5f, 0.0f, -10.0f, 0.0f, 1.0f, 0.0f, 0.0f, 4.0f), // 0
		Vertex(-3.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f),
		Vertex(7.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 4.0f, 0.0f),
		Vertex(7.5f, 0.0f, -10.0f, 0.0f, 1.0f, 0.0f, 4.0f, 4.0f),

		// Wall: Observe we tile texture coordinates, and that we
		// leave a gap in the middle for the mirror.
		Vertex(-3.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 2.0f), // 4
		Vertex(-3.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
		Vertex(-2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.5f, 0.0f),
		Vertex(-2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.5f, 2.0f),

		Vertex(2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 2.0f), // 8
		Vertex(2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
		Vertex(7.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 2.0f, 0.0f),
		Vertex(7.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 2.0f, 2.0f),

		Vertex(-3.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f), // 12
		Vertex(-3.5f, 6.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
		Vertex(7.5f, 6.0f, 0.0f, 0.0f, 0.0f, -1.0f, 6.0f, 0.0f),
		Vertex(7.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 6.0f, 1.0f),

		// Mirror
		Vertex(-2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f), // 16
		Vertex(-2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
		Vertex(2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f),
		Vertex(2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f)
	};

	std::array<std::int16_t, 30> indices = {
		// Floor
		0, 1, 2,
		0, 2, 3,

		// Walls
		4, 5, 6,
		4, 6, 7,

		8, 9, 10,
		8, 10, 11,

		12, 13, 14,
		12, 14, 15,

		// Mirror
		16, 17, 18,
		16, 18, 19
	};

	SubmeshGeometry floorSubmesh;
	floorSubmesh.IndexCount = 6;
	floorSubmesh.StartIndexLocation = 0;
	floorSubmesh.BaseVertexLocation = 0;

	SubmeshGeometry wallSubmesh;
	wallSubmesh.IndexCount = 18;
	wallSubmesh.StartIndexLocation = 6;
	wallSubmesh.BaseVertexLocation = 0;

	SubmeshGeometry mirrorSubmesh;
	mirrorSubmesh.IndexCount = 6;
	mirrorSubmesh.StartIndexLocation = 24;
	mirrorSubmesh.BaseVertexLocation = 0;

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "roomGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
			mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
			mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->DrawArgs["floor"] = floorSubmesh;
	geo->DrawArgs["wall"] = wallSubmesh;
	geo->DrawArgs["mirror"] = mirrorSubmesh;

	mGeometries[geo->Name] = std::move(geo);
}

void BoxApp::BuildSkullGeometry() {
	std::ifstream fin("Models/skull.txt");

	if (!fin) {
		MessageBox(0, L"Models/skull.txt not found.", 0, 0);
		return;
	}

	UINT vcount = 0;
	UINT tcount = 0;
	std::string ignore;

	fin >> ignore >> vcount;
	fin >> ignore >> tcount;
	fin >> ignore >> ignore >> ignore >> ignore;

	std::vector<Vertex> vertices(vcount);
	for (UINT i = 0; i < vcount; ++i) {
		fin >> vertices[i].Pos.x >> vertices[i].Pos.y >> vertices[i].Pos.z;
		fin >> vertices[i].Normal.x >> vertices[i].Normal.y >> vertices[i].Normal.z;

		// Model does not have texture coordinates, so just zero them out.
		vertices[i].UV0 = { 0.0f, 0.0f };
	}

	fin >> ignore;
	fin >> ignore;
	fin >> ignore;

	std::vector<std::int32_t> indices(3 * tcount);
	for (UINT i = 0; i < tcount; ++i) {
		fin >> indices[i * 3 + 0] >> indices[i * 3 + 1] >> indices[i * 3 + 2];
	}

	fin.close();

	//
	// Pack the indices of all the meshes into one index buffer.
	//

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::int32_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "skullGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
			mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
			mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R32_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["skull"] = submesh;

	mGeometries[geo->Name] = std::move(geo);
}

void BoxApp::BuildRenderItems()
{
	// 将每一个物体都存到 mAllRitems, mOpaqueRitems 中,相同物体的顶点/索引偏移相同,但是它们的世界矩阵不同,ObjIndex++. 
	// 渲染对象中存储了 World, ObjCBIndex, Geo, PrimitiveType, IndexCount, StartIndexLocation, BaseVertexLocation
	auto boxRitem = std::make_unique<RenderItem>();
	boxRitem->World = MathHelper::Identity4x4();
	// XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixTranslation(0.0f, 0.5f, 0.0f));
	boxRitem->ObjCBIndex = 0;
	boxRitem->Geo = mGeometries["shapeGeo"].get();
	boxRitem->Mat = mMaterials["grass"].get();
	boxRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
	boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
	boxRitem->Name = "box";
	mAllRitems.push_back(std::make_unique<RenderItem>(*boxRitem));
	mAlphaTestArr.push_back(std::move(boxRitem));

	auto gridItem = std::make_unique<RenderItem>();
	gridItem->World = MathHelper::Identity4x4();
	gridItem->ObjCBIndex = 1;
	XMStoreFloat4x4(&gridItem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixTranslation(0.0f, 0.5f, 0.0f));
	gridItem->Geo = mGeometries["shapeGeo"].get();
	gridItem->Mat = mMaterials["stone"].get();
	gridItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridItem->IndexCount = gridItem->Geo->DrawArgs["grid"].IndexCount;
	gridItem->StartIndexLocation = gridItem->Geo->DrawArgs["grid"].StartIndexLocation;
	gridItem->BaseVertexLocation = gridItem->Geo->DrawArgs["grid"].BaseVertexLocation;
	gridItem->Name = "grid";
	mAllRitems.push_back(std::make_unique<RenderItem>(*gridItem));
	mOpaqueArr.push_back(std::move(gridItem));

	auto wavesRitem = std::make_unique<RenderItem>();
	wavesRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&wavesRitem->TexTransform, XMMatrixScaling(5.0f, 5.0f, 1.0f));
	XMStoreFloat4x4(&wavesRitem->World, XMMatrixTranslation(0.0f, 1.0f, 0.0f));
	wavesRitem->ObjCBIndex = 2;
	wavesRitem->Mat = mMaterials["water"].get();
	wavesRitem->Geo = mGeometries["waterGeo"].get();
	wavesRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wavesRitem->IndexCount = wavesRitem->Geo->DrawArgs["grid"].IndexCount;
	wavesRitem->StartIndexLocation = wavesRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	wavesRitem->BaseVertexLocation = wavesRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
	wavesRitem->Name = "waves";
	mWavesRitem = wavesRitem.get();
	mAllRitems.push_back(std::make_unique<RenderItem>(*wavesRitem));
	mTransparentArr.push_back(std::move(wavesRitem));



	UINT objCBIndex = 3;

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
	// 	leftCylRitem->Mat = mMaterials["stone"].get();
	// 	leftCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	// 	leftCylRitem->IndexCount = leftCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
	// 	leftCylRitem->StartIndexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
	// 	leftCylRitem->BaseVertexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	// 	leftCylRitem->Name = "leftCyl" + std::to_string(i);
	// 	XMStoreFloat4x4(&rightCylRitem->World, rightCylWorld);
	// 	rightCylRitem->ObjCBIndex = objCBIndex++;
	// 	rightCylRitem->Geo = mGeometries["shapeGeo"].get();
	// 	rightCylRitem->Mat = mMaterials["stone"].get();
	// 	rightCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	// 	rightCylRitem->IndexCount = rightCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
	// 	rightCylRitem->StartIndexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
	// 	rightCylRitem->BaseVertexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	// 	rightCylRitem->Name = "rightCyl" + std::to_string(i);
	//
	// 	XMStoreFloat4x4(&leftSphereRitem->World, leftSphereWorld);
	// 	leftSphereRitem->ObjCBIndex = objCBIndex++;
	// 	leftSphereRitem->Geo = mGeometries["shapeGeo"].get();
	// 	leftSphereRitem->Mat = mMaterials["stone"].get();
	// 	leftSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	// 	leftSphereRitem->IndexCount = leftSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
	// 	leftSphereRitem->StartIndexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
	// 	leftSphereRitem->BaseVertexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;
	// 	leftSphereRitem->Name = "leftSphere" + std::to_string(i);
	//
	// 	XMStoreFloat4x4(&rightSphereRitem->World, rightSphereWorld);
	// 	rightSphereRitem->ObjCBIndex = objCBIndex++;
	// 	rightSphereRitem->Geo = mGeometries["shapeGeo"].get();
	// 	rightSphereRitem->Mat = mMaterials["stone"].get();
	// 	rightSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	// 	rightSphereRitem->IndexCount = rightSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
	// 	rightSphereRitem->StartIndexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
	// 	rightSphereRitem->BaseVertexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;
	// 	rightSphereRitem->Name = "rightSphere" + std::to_string(i);
	//
	//
	// 	mAllRitems.push_back(std::make_unique<RenderItem>(*leftCylRitem));
	// 	mAllRitems.push_back(std::make_unique<RenderItem>(*rightCylRitem));
	// 	mAllRitems.push_back(std::make_unique<RenderItem>(*leftSphereRitem));
	// 	mAllRitems.push_back(std::make_unique<RenderItem>(*rightSphereRitem));
	//
	// 	mOpaqueArr.push_back(std::move(leftCylRitem));
	// 	mOpaqueArr.push_back(std::move(rightCylRitem));
	// 	mOpaqueArr.push_back(std::move(leftSphereRitem));
	// 	mOpaqueArr.push_back(std::move(rightSphereRitem));
	// }

	auto floorRitem = std::make_unique<RenderItem>();
	floorRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&floorRitem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixTranslation(0.0f, 1.5f, 0.0f));
	floorRitem->TexTransform = MathHelper::Identity4x4();
	floorRitem->ObjCBIndex = objCBIndex++;
	floorRitem->Geo = mGeometries["roomGeo"].get();
	floorRitem->Mat = mMaterials["checkertile"].get();
	floorRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	floorRitem->IndexCount = floorRitem->Geo->DrawArgs["floor"].IndexCount;
	floorRitem->StartIndexLocation = floorRitem->Geo->DrawArgs["floor"].StartIndexLocation;
	floorRitem->BaseVertexLocation = floorRitem->Geo->DrawArgs["floor"].BaseVertexLocation;
	floorRitem->Name = "floor";
	mAllRitems.push_back(std::make_unique<RenderItem>(*floorRitem));
	mOpaqueArr.push_back(std::move(floorRitem));
	
	auto wallsRitem = std::make_unique<RenderItem>();
	wallsRitem->World = MathHelper::Identity4x4();
	wallsRitem->TexTransform = MathHelper::Identity4x4();
	wallsRitem->ObjCBIndex = objCBIndex++;
	wallsRitem->Mat = mMaterials["stone"].get();
	wallsRitem->Geo = mGeometries["roomGeo"].get();
	wallsRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallsRitem->IndexCount = wallsRitem->Geo->DrawArgs["wall"].IndexCount;
	wallsRitem->StartIndexLocation = wallsRitem->Geo->DrawArgs["wall"].StartIndexLocation;
	wallsRitem->BaseVertexLocation = wallsRitem->Geo->DrawArgs["wall"].BaseVertexLocation;
	wallsRitem->Name = "walls";	
	mAllRitems.push_back(std::make_unique<RenderItem>(*wallsRitem));
	mOpaqueArr.push_back(std::move(wallsRitem));

	auto mirrorRitem = std::make_unique<RenderItem>();
	mirrorRitem->World = MathHelper::Identity4x4(); 
	mirrorRitem->TexTransform = MathHelper::Identity4x4();
	mirrorRitem->ObjCBIndex = objCBIndex++;
	mirrorRitem->Mat = mMaterials["iceMirror"].get();
	mirrorRitem->Geo = mGeometries["roomGeo"].get();
	mirrorRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	mirrorRitem->IndexCount = mirrorRitem->Geo->DrawArgs["mirror"].IndexCount;
	mirrorRitem->StartIndexLocation = mirrorRitem->Geo->DrawArgs["mirror"].StartIndexLocation;
	mirrorRitem->BaseVertexLocation = mirrorRitem->Geo->DrawArgs["mirror"].BaseVertexLocation;
	mirrorRitem->Name = "mirror";
	mAllRitems.push_back(std::make_unique<RenderItem>(*mirrorRitem));
	mTransparentArr.push_back(std::make_unique<RenderItem>(*mirrorRitem));
	mStencilMirrorsArr.push_back(std::move(mirrorRitem));

	auto skullRitem = std::make_unique<RenderItem>();
	skullRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&skullRitem->World, XMMatrixScaling(0.5f, 0.5f, 0.5f) * XMMatrixRotationRollPitchYaw(0.0, DirectX::XMConvertToRadians(90), 0.0) * XMMatrixTranslation(0.0f, 1.5f, -3.0f));
	skullRitem->TexTransform = MathHelper::Identity4x4();
	skullRitem->ObjCBIndex = objCBIndex++;
	skullRitem->Mat = mMaterials["skullMat"].get();
	skullRitem->Geo = mGeometries["skullGeo"].get();
	skullRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	skullRitem->IndexCount = skullRitem->Geo->DrawArgs["skull"].IndexCount;
	skullRitem->StartIndexLocation = skullRitem->Geo->DrawArgs["skull"].StartIndexLocation;
	skullRitem->BaseVertexLocation = skullRitem->Geo->DrawArgs["skull"].BaseVertexLocation;
	skullRitem->Name = "skull";

	auto reflectedSkullRitem = std::make_unique<RenderItem>();
	*reflectedSkullRitem = *skullRitem;
	XMStoreFloat4x4(&reflectedSkullRitem->World, XMMatrixScaling(0.5f, 0.5f, 0.5f) * XMMatrixRotationRollPitchYaw(0.0, DirectX::XMConvertToRadians(90), 0.0) * XMMatrixTranslation(0.0f, 1.5f, 3.0f));
	reflectedSkullRitem->ObjCBIndex = objCBIndex++;
	mAllRitems.push_back(std::make_unique<RenderItem>(*reflectedSkullRitem));
	mRelectedArr.push_back(std::move(reflectedSkullRitem));


	mAllRitems.push_back(std::make_unique<RenderItem>(*skullRitem));
	mOpaqueArr.push_back(std::move(skullRitem));

	for (auto &e : mOpaqueArr)
		mOpaqueRitems.push_back(e.get());
	for (auto &e : mTransparentArr)
		mTransparentRitems.push_back(e.get());
	for (auto &e : mAlphaTestArr)
		mAlphaTestRitems.push_back(e.get());
	for (auto &e : mStencilMirrorsArr) {
		mStencilMirrorsRitems.push_back(e.get());
	}
	for (auto &e : mRelectedArr) {
		mRelectedRitems.push_back(e.get());
	}
}

float BoxApp::GetHillsHeight(float x, float z) const {
	return 0.3f * (z * sinf(0.1f * x) + x * cosf(0.1f * z));
}

void BoxApp::BuildWavesGeometryBuffers() {
	std::vector<std::uint16_t> indices(3 * mWaves->TriangleCount());
	assert(mWaves->VertexCount() < 0x0000ffff);

	int m = mWaves->RowCount();
	int n = mWaves->ColumnCount();
	int k = 0;
	for (int i = 0; i < m - 1; ++i) {
		for (int j = 0; j < n - 1; ++j) {
			indices[k] = i * n + j;
			indices[k + 1] = i * n + j + 1;
			indices[k + 2] = (i + 1) * n + j;

			indices[k + 3] = (i + 1) * n + j;
			indices[k + 4] = i * n + j + 1;
			indices[k + 5] = (i + 1) * n + j + 1;

			k += 6;
		}
	}

	UINT bvByteSize = mWaves->VertexCount() * sizeof(Vertex);
	UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "waterGeo";

	geo->VertexBufferCPU = nullptr;
	geo->VertexBufferGPU = nullptr;

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU))
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
			mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = bvByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["grid"] = submesh;

	mGeometries["waterGeo"] = std::move(geo);
}

void BoxApp::BuildMaterials() {
	auto grass = std::make_unique<Material>();
	grass->Name = "grass";
	grass->MatCBIndex = 0;
	grass->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.5f);
	grass->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
	grass->Roughness = 0.125f;
	grass->DiffuseSrvHeapIndex = 0;

	auto stone = std::make_unique<Material>();
	stone->Name = "stone";
	stone->MatCBIndex = 1;
	stone->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.5f);
	stone->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
	stone->Roughness = 0.125f;
	stone->DiffuseSrvHeapIndex = 1;


	// 当前这种水的材质定义不好
	auto water = std::make_unique<Material>();
	water->Name = "water";
	water->MatCBIndex = 2;
	water->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.5f);
	water->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	water->Roughness = 0.0f;
	water->DiffuseSrvHeapIndex = 2;

	auto icemirror = std::make_unique<Material>();
	icemirror->Name = "iceMirror";
	icemirror->MatCBIndex = 3;
	icemirror->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.3f);
	icemirror->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	icemirror->Roughness = 0.5f;
	icemirror->DiffuseSrvHeapIndex = 4;

	auto skullMat = std::make_unique<Material>();
	skullMat->Name = "skullMat";
	skullMat->MatCBIndex = 4;
	skullMat->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	skullMat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	skullMat->Roughness = 0.3f;
	skullMat->DiffuseSrvHeapIndex = 5;

	auto bricks = std::make_unique<Material>();
	bricks->Name = "bricks";
	bricks->MatCBIndex = 5;
	bricks->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	bricks->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	bricks->Roughness = 0.25f;
	bricks->DiffuseSrvHeapIndex = 1;

	auto checkertile = std::make_unique<Material>();
	checkertile->Name = "checkertile";
	checkertile->MatCBIndex = 6;
	checkertile->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	checkertile->FresnelR0 = XMFLOAT3(0.07f, 0.07f, 0.07f);
	checkertile->Roughness = 0.3f;
	checkertile->DiffuseSrvHeapIndex = 3;

	// 将材质数据存放在系统内存之中,为了GPU能够在着色器中访问,还需复制到常量缓冲区中
	mMaterials["grass"] = std::move(grass);
	mMaterials["water"] = std::move(water);
	mMaterials["stone"] = std::move(stone);
	mMaterials["iceMirror"] = std::move(icemirror);
	mMaterials["skullMat"] = std::move(skullMat);
	mMaterials["bricks"] = std::move(bricks);
	mMaterials["checkertile"] = std::move(checkertile);
}

void BoxApp::LoadTextures() {
	auto woodCrateTex = std::make_unique<Texture>();
	woodCrateTex->Name = "woodCrateTex";
	woodCrateTex->Filename = L"Textures/WireFence.dds";
	// 上传GPU
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
			mCommandList.Get(), woodCrateTex->Filename.c_str(),
			woodCrateTex->Resource, woodCrateTex->UploadHeap))

	auto woodCrateTex2 = std::make_unique<Texture>();
	woodCrateTex2->Name = "woodCrateTex2";
	woodCrateTex2->Filename = L"Textures/bricks2.dds";
	// 上传GPU
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
			mCommandList.Get(), woodCrateTex2->Filename.c_str(),
			woodCrateTex2->Resource, woodCrateTex2->UploadHeap))

	auto water = std::make_unique<Texture>();
	water->Name = "water";
	water->Filename = L"Textures/water1.dds";
	// 上传GPU
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
			mCommandList.Get(), water->Filename.c_str(),
			water->Resource, water->UploadHeap));

	auto checkboardTex = std::make_unique<Texture>();
	checkboardTex->Name = "checkboardTex";
	checkboardTex->Filename = L"Textures/checkboard.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
			mCommandList.Get(), checkboardTex->Filename.c_str(),
			checkboardTex->Resource, checkboardTex->UploadHeap));

	auto white1x1Tex = std::make_unique<Texture>();
	white1x1Tex->Name = "white1x1Tex";
	white1x1Tex->Filename = L"Textures/white1x1.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
			mCommandList.Get(), white1x1Tex->Filename.c_str(),
			white1x1Tex->Resource, white1x1Tex->UploadHeap));
	
	auto iceTex = std::make_unique<Texture>();
	iceTex->Name = "iceTex";
	iceTex->Filename = L"Textures/ice.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
			mCommandList.Get(), iceTex->Filename.c_str(),
			iceTex->Resource, iceTex->UploadHeap));

	mTextures[woodCrateTex->Name] = std::move(woodCrateTex);
	mTextures[woodCrateTex2->Name] = std::move(woodCrateTex2);
	mTextures[water->Name] = std::move(water);
	mTextures[checkboardTex->Name] = std::move(checkboardTex);
	mTextures[iceTex->Name] = std::move(iceTex);
	mTextures[white1x1Tex->Name] = std::move(white1x1Tex);
}

void BoxApp::AnimateMaterial(const GameTimer& gt) {
	auto watermat = mMaterials["water"].get();
	float &tu = watermat->MatTransform(3.0, 0.0);
	float &tv = watermat->MatTransform(3.0, 1.0);
	tu += 0.1f * gt.DeltaTime();
	tv += 0.02f * gt.DeltaTime();

	if (tu >= 1.0f)
		tu -= 1.0f;
	if (tv >= 1.0f)
		tv -= 1.0f;
	watermat->MatTransform(3.0, 0.0) = tu;
	watermat->MatTransform(3.0, 1.0) = tv;
	watermat->NumFramesDirty = gNumFrameResources;
}
