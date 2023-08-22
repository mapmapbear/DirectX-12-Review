// 按住左键拖动旋转,右键缩放

// RTV,DSV				: 有描述符堆, 有描述符(视图)
// 顶点缓冲区,索引缓冲区: 无描述符堆, 有描述符(视图), 默认堆, 通过输入布局描述指定着色器寄存器
// 常量缓冲区			: 有描述符堆, 有描述符(视图), 上传堆, 通过根签名指定着色器寄存器, CPU每帧更新

#include "BoxApp.h"
#include <DirectXMath.h>
#include <minwindef.h>
#include <winuser.h>

const int gNumFrameResources = 3;

template <typename T>
std::vector<T> AddVectors(const std::vector<T> &vector1, const std::vector<T> &vector2) {
	std::vector<T> result;

	// 检查向量的大小是否相同
	if (vector1.size() != vector2.size()) {
		OutputDebugStringW(L"Error: Vectors must have the same size.");
		return result;
	} 

	// 遍历向量并相加对应位置的元素
	for (size_t i = 0; i < vector1.size(); ++i) {
		result.push_back(vector1[i] + vector2[i]);
	}

	return result;
}

BoxApp::BoxApp(HINSTANCE hInstance) :
		D3DApp(hInstance) {
}

BoxApp::~BoxApp() {
}

bool BoxApp::Initialize() {
	if (!D3DApp::Initialize())
		return false;
	mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	needBlur = true;
	blurCount = 2;

	mWaves = std::make_unique<Waves>(128, 128, 1.0f, 0.03f, 4.0f, 0.2f);
	mBlurFilter = std::make_unique<BlurFilter>(md3dDevice.Get(),
			mClientWidth, mClientHeight, DXGI_FORMAT_R8G8B8A8_UNORM);

	// 重置命令列表,准备初始化
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));
	LoadTextures();

	BuildRootSignature();
	BuildPostProcessRootSignature();
	BuildShadersAndInputLayout();
	BuildWavesGeometryBuffers();
	BuildShapeGeometry1();
	BuildRoomGeometry();
	BuildSkullGeometry();
	BuildTreeSpritesGeometry();
	BuildMaterials();
#ifdef INSTANCE_RENDER
	BuildInstanceRenderItems();
	//BuildSkyRenderItems();
#else
	BuildRenderItems();
#endif
	BuildFrameResources();
	BuildDescriptorHeaps();
	BuildConstantBufferViews();
	BuildPSO();

	// 执行初始化命令
	ThrowIfFailed(mCommandList->Close())
			ID3D12CommandList *cmdsList[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsList), cmdsList);

	// 等待初始化完成
	FlushCommandQueue();

	return true;
}

void BoxApp::OnResize() {
	D3DApp::OnResize();

	mCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	BoundingFrustum::CreateFromMatrix(mCamFrustum, mCamera.GetProj());

	if (mBlurFilter != nullptr) {
		mBlurFilter->OnResize(mClientWidth, mClientHeight);
	}
}

void BoxApp::Update(const GameTimer &gt) {
	OnKeyboardInput(gt);
	UpdateCamera(gt);
	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
	mCurrFrameResources = mFrameResources[mCurrFrameResourceIndex].get();
	if (mCurrFrameResources->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResources->Fence) {
		HANDLE eventHandle = CreateEventEx(nullptr, L"false", false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResources->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}
#ifndef __IMGUI
	UpdateImGui(gt, mMainPassCB);
#endif
	AnimateMaterial(gt);
#ifndef INSTANCE_RENDER
	UpdateObjectCBs(gt);
	UpdateWaves(gt);
#else
	UpdateInstanceData(gt);
#endif
	UpdateMainPassCB(gt);
	UpdateReflectedPassCB(gt);
	UpdateMaterialCBs(gt);
}

#ifndef __IMGUI
void BoxApp::UpdateImGui(const GameTimer &gt, PassConstants &buffer) {
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
		ImGui::Checkbox("ScreenBlur", &this->needBlur);
		ImGui::Checkbox("Geometry", &this->needBlur);
		ImGui::Checkbox("Frustum Cull", &this->mFrustumCullingEnabled);
		// ImGui::SameLine(0.0f, 25.0f);
		ImGui::Text("BlurCount: %d", this->blurCount);
		ImGui::SliderInt("##1", &this->blurCount, 1, 8, "%d");
		// ImGui::Text("Theta: %.2f degrees", XMConvertToDegrees(theta));
		// ImGui::SliderFloat("##2", &theta, -XM_PI, XM_PI, "");
		// ImGui::Text("Position: (%.1f, %.1f, 0.0)", tx, ty);
		// ImGui::Text("FOV: %.2f degrees", XMConvertToDegrees(fov));
		// ImGui::SliderFloat("##3", &fov, XM_PIDIV4, XM_PI / 3 * 2, "");
		//
		// if (ImGui::Checkbox("Use Custom Color", &customColor)) {
		// }
		// if (customColor) {
		// 	ImGui::ColorEdit3("ClearColor", gColor);
		// 	printf("");
		// }
	}
	ImGui::End();
	buffer.useCustomColor = customColor ? 1 : 0;
	buffer.color = XMFLOAT4(gColor[0], gColor[1], gColor[2], 1.0);
}
#endif

#ifndef INSTANCE_RENDER
void BoxApp::UpdateObjectCBs(const GameTimer &gt) {
	auto currObjectCB = mCurrFrameResources->ObjectCB.get();
	for (auto &e : mAllRitems) {
		if (e->NumFramesDirty > 0) {
			XMMATRIX world = XMLoadFloat4x4(&e->World);
			XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));
#ifdef DYNAMIC_RESOURCES
			objConstants.materialIndex = e->Mat->MatCBIndex;
#endif
			currObjectCB->CopyData(e->ObjCBIndex, objConstants);
			e->NumFramesDirty--;
		}
	}
}

#else
void BoxApp::UpdateInstanceData(const GameTimer &gt) {
	XMMATRIX view = mCamera.GetView();
	XMMATRIX invView = XMMatrixInverse(get_rvalue_ptr(XMMatrixDeterminant(view)), view);

	auto currInstanceBuffer = mCurrFrameResources->InstanceBuffer.get();
	for (auto &e : mAllRitems) {
		const auto &instanceData = e->Instances;

		int visibleInstanceCount = 0;

		for (UINT i = 0; i < (UINT)instanceData.size(); ++i) {
			XMMATRIX world = XMLoadFloat4x4(&instanceData[i].World);
			XMMATRIX texTransform = XMLoadFloat4x4(&instanceData[i].TexTransform);

			XMMATRIX invWorld = XMMatrixInverse(get_rvalue_ptr(XMMatrixDeterminant(world)), world);

			// View space to the object's local space.
			XMMATRIX viewToLocal = XMMatrixMultiply(invView, invWorld);

			// Transform the camera frustum from view space to the object's local space.
			BoundingFrustum localSpaceFrustum;
			mCamFrustum.Transform(localSpaceFrustum, viewToLocal);

			// Perform the box/frustum intersection test in local space.
			if ((localSpaceFrustum.Contains(e->Bounds) != DirectX::DISJOINT) || (mFrustumCullingEnabled == false)) {
				InstanceData data;
				XMStoreFloat4x4(&data.World, XMMatrixTranspose(world));
				XMStoreFloat4x4(&data.TexTransform, XMMatrixTranspose(texTransform));
				data.MaterialIndex = instanceData[i].MaterialIndex;

				// Write the instance data to structured buffer for the visible objects.
				currInstanceBuffer->CopyData(visibleInstanceCount++, data);
			}
		}

		e->InstanceCount = visibleInstanceCount;

		std::wostringstream outs;
		outs.precision(6);
		outs << L"Instancing and Culling Demo" << L"    " << e->InstanceCount << L" objects visible out of " << e->Instances.size();
		mMainWndCaption = outs.str();
	}
}

#endif

void BoxApp::UpdateMainPassCB(const GameTimer &gt) {
	XMMATRIX view = mCamera.GetView();
	XMMATRIX proj = mCamera.GetProj();

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
	mMainPassCB.RenderTargetSize = XMFLOAT2(static_cast<float>(mClientWidth), static_cast<float>(mClientHeight));
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
	auto currMatSB = mCurrFrameResources->MaterialBuffer.get();
	for (auto &e : mMaterials) {
		Material *mat = e.second.get();
		if (mat->NumFramesDirty > 0) {
		#ifdef DYNAMIC_RESOURCES
			MaterialData matData;
			matData.DiffuseAlbedo = mat->DiffuseAlbedo;
			matData.FresnelR0 = mat->FresnelR0;
			matData.Roughness = mat->Roughness;
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);
			XMStoreFloat4x4(&matData.MatTransform, XMMatrixTranspose(matTransform));
			matData.DiffuseMapIndex = mat->DiffuseSrvHeapIndex;
			currMatSB->CopyData(mat->MatCBIndex, matData);
			mat->NumFramesDirty--;
		#else
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);
			MaterialConstants matConstants;
			matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
			matConstants.FresnelR0 = mat->FresnelR0;
			matConstants.Roughness = mat->Roughness;
			XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));
			currMatSB->CopyData(mat->MatCBIndex, matConstants);
			mat->NumFramesDirty--;
		#endif
		}
	}
}

void BoxApp::UpdateCamera(const GameTimer &gt) {
	// Convert Spherical to Cartesian coordinates.
	// mEyePos.x = mRadius * sinf(mPhi) * cosf(mTheta);
	// mEyePos.z = mRadius * sinf(mPhi) * sinf(mTheta);
	// mEyePos.y = mRadius * cosf(mPhi);

	// Build the view matrix.
	// XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
	// XMVECTOR target = XMVectorZero();
	// XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	//
	// XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	// XMStoreFloat4x4(&mView, view);
}

void BoxApp::UpdateWaves(const GameTimer &gt) {
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

	if (GetAsyncKeyState('W') & 0x8000) {
		mCamera.Walk(10.0f * gt.DeltaTime());
	}

	if (GetAsyncKeyState('S') & 0x8000) {
		mCamera.Walk(-10.0f * gt.DeltaTime());
	}

	if (GetAsyncKeyState('A') & 0x8000) {
		mCamera.Strafe(-10.0f * gt.DeltaTime());
	}

	if (GetAsyncKeyState('D') & 0x8000) {
		mCamera.Strafe(10.0f * gt.DeltaTime());
	}

	if (GetAsyncKeyState('Q') & 0x8000) {
		mCamera.Up(10.0f * gt.DeltaTime());
	}

	if (GetAsyncKeyState('E') & 0x8000) {
		mCamera.Up(-10.0f * gt.DeltaTime());
	}


	mCamera.UpdateViewMatrix();

	// const float dt = gt.DeltaTime();
	//
	// if(GetAsyncKeyState('A') & 0x8000)
	// 	mSkullTranslation.x -= 1.0f*dt;
	//
	// if(GetAsyncKeyState('D') & 0x8000)
	// 	mSkullTranslation.x += 1.0f*dt;
	//
	// if(GetAsyncKeyState('W') & 0x8000)
	// 	mSkullTranslation.y += 1.0f*dt;
	//
	// if(GetAsyncKeyState('S') & 0x8000)
	// 	mSkullTranslation.y -= 1.0f*dt;
	//
	// // Don't let user move below ground plane.
	// mSkullTranslation.y = MathHelper::Max(mSkullTranslation.y, 0.0f);
	// // mSkullTranslation.x = 50;
	// // Update the new world matrix.
	// XMMATRIX skullRotate = XMMatrixRotationY(0.5f*MathHelper::Pi);
	// XMMATRIX skullScale = XMMatrixScaling(0.45f, 0.45f, 0.45f);
	// XMMATRIX skullOffset = XMMatrixTranslation(mSkullTranslation.x, mSkullTranslation.y, mSkullTranslation.z);
	// XMMATRIX skullWorld = skullRotate*skullScale*skullOffset;
	// XMStoreFloat4x4(&mSkullRitem->World, skullWorld);
	//
	// // Update reflection world matrix.
	// XMVECTOR mirrorPlane = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f); // xy plane
	// XMMATRIX R = XMMatrixReflect(mirrorPlane);
	// XMStoreFloat4x4(&mReflectedSkullRitem->World, skullWorld * R);
	//
	// XMVECTOR mirrorPlane1 = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f); // xy plane
	// XMMATRIX R1 = XMMatrixReflect(mirrorPlane1);
	// XMMATRIX floorWorld = XMLoadFloat4x4(&mFloorRitem->World);
	// XMStoreFloat4x4(&mReflectedFloorRitem->World, floorWorld * R1);
	//
	// // Update shadow world matrix.
	// XMVECTOR shadowPlane = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f); // xz plane
	// XMVECTOR toMainLight = XMVectorNegate(XMLoadFloat3(&mMainPassCB.Lights[0].Direction));
	// XMMATRIX S = XMMatrixShadow(shadowPlane, toMainLight);
	// XMMATRIX shadowOffsetY = XMMatrixTranslation(0.0f, 0.001f, 0.0f);
	// XMStoreFloat4x4(&mShadowedSkullRitem->World, skullWorld * S * shadowOffsetY);
	//
	// XMVECTOR mirrorPlane2 = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f); // xy plane
	// XMMATRIX R2 = XMMatrixReflect(mirrorPlane2);
	// XMMATRIX R3 = XMMatrixRotationY(-0.5f * MathHelper::Pi);
	// XMMATRIX reflectedShadow = XMLoadFloat4x4(&mShadowedSkullRitem->World);
	// // XMStoreFloat4x4(&mReflectedShadowedSkullRitem->World, reflectedShadow * R2);
	// XMStoreFloat4x4(&mReflectedShadowedSkullRitem->World, reflectedShadow * R3 * R2);
	//
	// mSkullRitem->NumFramesDirty = gNumFrameResources;
	// mFloorRitem->NumFramesDirty = gNumFrameResources;
	// mReflectedFloorRitem->NumFramesDirty = gNumFrameResources;
	// mReflectedSkullRitem->NumFramesDirty = gNumFrameResources;
	// mReflectedShadowedSkullRitem->NumFramesDirty = gNumFrameResources;
	// mShadowedSkullRitem->NumFramesDirty = gNumFrameResources;
}

void BoxApp::OnMouseMove(WPARAM btnState, int x, int y) {
	if ((btnState & MK_LBUTTON) != 0) {
		float dx = DirectX::XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		float dy = DirectX::XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));
		mCamera.Pitch(dy);
		mCamera.RotateY(dx);
	}
	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

void BoxApp::Draw(const GameTimer &gt) {
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
#ifndef __IMGUI
	ImGui::Render();
#endif

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	mCommandList->ResourceBarrier(1, get_rvalue_ptr(CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET)));
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), (float *)&mMainPassCB.FogColor, 0, nullptr);

	// mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	mCommandList->OMSetRenderTargets(1, get_rvalue_ptr(CurrentBackBufferView()), true, get_rvalue_ptr(DepthStencilView()));

// 	ID3D12DescriptorHeap *descriptorHeaps1[] = { mCbvHeap.Get() };
// 	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps1), descriptorHeaps1);
// 	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

#ifdef DYNAMIC_RESOURCES
	ID3D12DescriptorHeap *descriptorHeaps[] = { mSrvHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
	mCommandList->SetGraphicsRootDescriptorTable(4, mSrvHeap->GetGPUDescriptorHandleForHeapStart());

	CD3DX12_GPU_DESCRIPTOR_HANDLE skyTexDescriptor(mSrvHeap->GetGPUDescriptorHandleForHeapStart());
	skyTexDescriptor.Offset(mSkyTexHeapIndex, mCbvSrvDescriptorSize);
	mCommandList->SetGraphicsRootDescriptorTable(0, skyTexDescriptor);

	auto matSB = mCurrFrameResources->MaterialBuffer->Resource();
	mCommandList->SetGraphicsRootShaderResourceView(2, //根参数索引
			matSB->GetGPUVirtualAddress()); //子资源地址 
#endif
	auto passCB = mCurrFrameResources->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(3, passCB->GetGPUVirtualAddress());

	#ifdef INSTANCE_RENDER
	DrawInstanceRenderItems(mCommandList.Get(), mRitemLayer[static_cast<int>(RenderLayer::Opaque)]);

	mCommandList->SetPipelineState(mPSOs["SkySphere"].Get());
	DrawInstanceRenderItems(mCommandList.Get(), mRitemLayer[static_cast<int>(RenderLayer::Sky)]);
	#else
	DrawRenderItems(mCommandList.Get(), mRitemLayer[static_cast<int>(RenderLayer::Opaque)]);
	#endif
	// Draw AlphaTest Render Queue
	mCommandList->SetPipelineState(mPSOs["alphaTested"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[static_cast<int>(RenderLayer::AlphaTest)]);

	mCommandList->OMSetStencilRef(1);
	mCommandList->SetPipelineState(mPSOs["markStencilMirrors"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[static_cast<int>(RenderLayer::Mirrors)]);

// 	UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));
// 	mCommandList->SetGraphicsRootConstantBufferView(3, passCB->GetGPUVirtualAddress() + 1 * passCBByteSize);
// 	mCommandList->SetPipelineState(mPSOs["drawStencilReflections"].Get());
// 	DrawRenderItems(mCommandList.Get(), mRitemLayer[static_cast<int>(RenderLayer::Reflected)]);

// 	mCommandList->SetGraphicsRootConstantBufferView(3, passCB->GetGPUVirtualAddress());
// 	mCommandList->OMSetStencilRef(0);
// 	mCommandList->SetPipelineState(mPSOs["shadow"].Get());
// 	DrawRenderItems(mCommandList.Get(), mRitemLayer[static_cast<int>(RenderLayer::Shadow)]);
// 
// 	mCommandList->SetPipelineState(mPSOs["treeSprites"].Get());
// 	DrawRenderItems(mCommandList.Get(), mRitemLayer[static_cast<int>(RenderLayer::AlphaTestedTreeSprites)]);
// 
// 	mCommandList->SetPipelineState(mPSOs["transparent"].Get());
// 	DrawRenderItems(mCommandList.Get(), mRitemLayer[static_cast<int>(RenderLayer::Transparent)]);

	// if (this->needBlur) {
	// mBlurFilter->Execute(mCommandList.Get(), mPostProcessRootSignature.Get(),
	// 		mPSOs["horzBlur"].Get(), mPSOs["vertBlur"].Get(), CurrentBackBuffer(), this->blurCount);
	// mCommandList->ResourceBarrier(1, get_rvalue_ptr(CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST)));
	// mCommandList->SetDescriptorHeaps(1, mImGUIHeap.GetAddressOf());
	// mCommandList->CopyResource(CurrentBackBuffer(), mBlurFilter->Output());
	// mCommandList->ResourceBarrier(1, get_rvalue_ptr(CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET)));
	// }
#ifndef __IMGUI
	mCommandList->SetDescriptorHeaps(1, mImGUIHeap.GetAddressOf());
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), mCommandList.Get());
#endif

	// mCommandList->ResourceBarrier(1, get_rvalue_ptr(CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE)));
	//
	// mBlurFilter->Execute(mCommandList.Get(), mPostProcessRootSignature.Get(),
	// 		mPSOs["horzBlur"].Get(), mPSOs["vertBlur"].Get(), CurrentBackBuffer(), 4);
	//
	// mCommandList->ResourceBarrier(1, get_rvalue_ptr(CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST)));
	//
	// mCommandList->CopyResource(CurrentBackBuffer(), mBlurFilter->Output());
	// mCommandList->ResourceBarrier(1, get_rvalue_ptr(CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET)));
	mCommandList->ResourceBarrier(1, get_rvalue_ptr(CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT)));
	ThrowIfFailed(mCommandList->Close());

	ID3D12CommandList *cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	ThrowIfFailed(mSwapChain->Present(0, 0));

	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	mCurrFrameResources->Fence = ++mCurrentFence;

	// mFence 配合 mCurrentFence 不停的将 ++ 命令加入GPU命令队列中
	// 它并没有跳跃,而是 0->1->2->3... 一个一个的增加
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void BoxApp::DrawRenderItems(ID3D12GraphicsCommandList *cmdList, const std::vector<RenderItem *> &ritems) {
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));
#ifdef INSTANCE_RENDER
	auto instanceBuffer = mCurrFrameResources->InstanceBuffer->Resource();
#else
	auto objectCB = mCurrFrameResources->ObjectCB->Resource();
#endif
	auto materialCB = mCurrFrameResources->MaterialBuffer->Resource();
#ifdef DYNAMIC_RESOURCES
	for (size_t i = 0; i < ritems.size(); ++i) {
		auto ri = ritems[i];

		cmdList->IASetVertexBuffers(0, 1, get_rvalue_ptr(ri->Geo->VertexBufferView()));
		cmdList->IASetIndexBuffer(get_rvalue_ptr(ri->Geo->IndexBufferView()));
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		#ifndef INSTANCE_RENDER
		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
		#endif
#ifdef INSTANCE_RENDER
		cmdList->SetGraphicsRootShaderResourceView(1, instanceBuffer->GetGPUVirtualAddress());
		cmdList->DrawIndexedInstanced(ri->IndexCount, ri->InstanceCount, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
#else
		cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
#endif
	}
#else
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
#endif
}

#ifdef INSTANCE_RENDER
void BoxApp::DrawInstanceRenderItems(ID3D12GraphicsCommandList *cmdList, const std::vector<RenderItem *> &ritems) {
	// For each render item...
	for (size_t i = 0; i < ritems.size(); ++i) {
		auto ri = ritems[i];

		cmdList->IASetVertexBuffers(0, 1, get_rvalue_ptr(ri->Geo->VertexBufferView()));
		cmdList->IASetIndexBuffer(get_rvalue_ptr(ri->Geo->IndexBufferView()));
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		auto instanceBuffer = mCurrFrameResources->InstanceBuffer->Resource();
		mCommandList->SetGraphicsRootShaderResourceView(1, instanceBuffer->GetGPUVirtualAddress());

		cmdList->DrawIndexedInstanced(ri->IndexCount, ri->InstanceCount, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

#endif
void BoxApp::OnMouseDown(WPARAM btnState, int x, int y) {
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWnd);
}

void BoxApp::OnMouseUp(WPARAM btnState, int x, int y) {
	ReleaseCapture();
}

void BoxApp::BuildDescriptorHeaps() {
	UINT objCount = 0;
	// for (const auto &layer : mRitemLayer) {
	// 	objCount += layer.size();
	// }
	objCount = mAllRitems.size();
	// UINT objCount = (UINT)(mRitemLayer[(int)RenderLayer::Opaque].size() + mRitemLayer[(int)RenderLayer::Transparent].size() + mRitemLayer[(int)RenderLayer::AlphaTest].size());

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
	// srvHeapDesc.NumDescriptors = 7 + 4; // 4 is Blur Desc Count
	srvHeapDesc.NumDescriptors = 8 + 4;
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
	auto treeTex = mTextures["treeTex"]->Resource;
	auto skyTex = mTextures["skyTex"]->Resource;

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

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	auto desc = treeTex->GetDesc();
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
	srvDesc.Format = treeTex->GetDesc().Format;
	srvDesc.Texture2DArray.MostDetailedMip = 0;
	srvDesc.Texture2DArray.MipLevels = -1;
	srvDesc.Texture2DArray.FirstArraySlice = 0;
	srvDesc.Texture2DArray.ArraySize = treeTex->GetDesc().DepthOrArraySize;
	md3dDevice->CreateShaderResourceView(treeTex.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	/*auto desc = skyTex->GetDesc();*/
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; 
	srvDesc.Format = skyTex->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE; 
	srvDesc.Texture2D.MostDetailedMip = 0; 
	srvDesc.Texture2D.MipLevels = skyTex->GetDesc().MipLevels; 
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f; 
	md3dDevice->CreateShaderResourceView(skyTex.Get(), &srvDesc, hDescriptor);
	mSkyTexHeapIndex = 7;

	mBlurFilter->BuildDescriptors(
			CD3DX12_CPU_DESCRIPTOR_HANDLE(mSrvHeap->GetCPUDescriptorHandleForHeapStart(), 8, mCbvSrvUavDescriptorSize), // 8 is Textures Desc Count
			CD3DX12_GPU_DESCRIPTOR_HANDLE(mSrvHeap->GetGPUDescriptorHandleForHeapStart(), 8, mCbvSrvUavDescriptorSize),
			mCbvSrvUavDescriptorSize);
}

// 改的还是 cbvHeap,每个帧资源中的每一个物体都需要一个对应的CBV描述符,将物体的常量缓冲区地址和偏移后的句柄绑定,	在描述符堆中的句柄按照字节偏移 (frameIndex * objCount + i) * mCbvSrvUavDescriptorSize  物体的常量缓存地址按照字节偏移 i * sizeof(ObjectConstants)
void BoxApp::BuildConstantBufferViews() {
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

	UINT objCount = 0;
	// for (const auto &layer : mRitemLayer) {
	// 	objCount += layer.size();
	// }
	objCount = mAllRitems.size();
	// objCount += 1;
	// UINT objCount = (UINT)(mRitemLayer[(int)RenderLayer::Opaque].size() + mRitemLayer[(int)RenderLayer::Transparent].size() + mRitemLayer[(int)RenderLayer::AlphaTest].size());

#ifndef INSTANCE_RENDER
	// 每个帧资源中的每一个物体都需要一个对应的CBV描述符
	for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex) {
		auto objectCB = mFrameResources[frameIndex]->ObjectCB->Resource();
		for (UINT i = 0; i < objCount; ++i) {
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
#endif // !INSTANCE_RENDER

#ifndef DYNAMIC_RESOURCES
	for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex) {
		auto materialCB = mFrameResources[frameIndex]->MaterialBuffer->Resource();
		// for (UINT i = 0; i < mMaterials.size(); ++i) {
		for (auto &m : mMaterials) {
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
#endif // !DYNAMIC_RESOURCES

	UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

	// 最后3个描述符依次是每个帧资源的渲染过程CBV
	for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex) {
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

void BoxApp::BuildConstantBuffers() {
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
#ifdef DYNAMIC_RESOURCES
	CD3DX12_DESCRIPTOR_RANGE srvTable;
	srvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 6, 2);

	CD3DX12_DESCRIPTOR_RANGE srvTable1;
	srvTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
	
	CD3DX12_ROOT_PARAMETER slotRootParameter[5];
	slotRootParameter[0].InitAsDescriptorTable(1, &srvTable1, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[1].InitAsShaderResourceView(0, 1);
	slotRootParameter[2].InitAsShaderResourceView(1, 1);
	slotRootParameter[3].InitAsConstantBufferView(0);
	slotRootParameter[4].InitAsDescriptorTable(1, &srvTable, D3D12_SHADER_VISIBILITY_PIXEL);


#else
	CD3DX12_DESCRIPTOR_RANGE texTable;
	texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // register t0

	CD3DX12_ROOT_PARAMETER slotRootParameter[4];

	slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[1].InitAsConstantBufferView(0); // register b0
	slotRootParameter[2].InitAsConstantBufferView(1); // register b1
	slotRootParameter[3].InitAsConstantBufferView(2); // register b2
#endif
	

	auto staticSamplers = GetStaticSamplers(); // register s0 ~ s6

	// 根签名是根参数数组
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(5, slotRootParameter,
			(staticSamplers.size()), staticSamplers.data(),
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// 创建具有4个槽位的根签名,第一个指向含有单个着色器资源视图的描述符表,其它三个各指向一个常量缓冲区视图
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
			serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr) {
		OutputDebugStringA(static_cast<char *>(errorBlob->GetBufferPointer()));
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
			0,
			serializedRootSig->GetBufferPointer(),
			serializedRootSig->GetBufferSize(),
			IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void BoxApp::BuildWaveRootSignature() {
	CD3DX12_DESCRIPTOR_RANGE srvTable;
	srvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

	CD3DX12_DESCRIPTOR_RANGE uavTable;
	uavTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
	CD3DX12_ROOT_PARAMETER slotRootParameter[3];

	slotRootParameter[0].InitAsConstants(6, 0);
	slotRootParameter[1].InitAsDescriptorTable(1, &srvTable);
	slotRootParameter[2].InitAsDescriptorTable(1, &uavTable);

	CD3DX12_ROOT_SIGNATURE_DESC rootSignDesc(3, slotRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSignDesc, D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());
	if (errorBlob != nullptr) {
		OutputDebugStringA(static_cast<char *>(errorBlob->GetBufferPointer()));
	}
	ThrowIfFailed(hr);
	ThrowIfFailed(md3dDevice->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(mWaveRootSignature.GetAddressOf())));
}

void BoxApp::BuildPostProcessRootSignature() {
	CD3DX12_DESCRIPTOR_RANGE srvTable;
	srvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

	CD3DX12_DESCRIPTOR_RANGE uavTable;
	uavTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[3];

	// Perfomance TIP: Order from most frequent to least frequent.
	slotRootParameter[0].InitAsConstants(12, 0);
	slotRootParameter[1].InitAsDescriptorTable(1, &srvTable);
	slotRootParameter[2].InitAsDescriptorTable(1, &uavTable);

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(3, slotRootParameter,
			0, nullptr,
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
			serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr) {
		OutputDebugStringA(static_cast<char *>(errorBlob->GetBufferPointer()));
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
			0,
			serializedRootSig->GetBufferPointer(),
			serializedRootSig->GetBufferSize(),
			IID_PPV_ARGS(mPostProcessRootSignature.GetAddressOf())));
}

void BoxApp::BuildShadersAndInputLayout() {
	const D3D_SHADER_MACRO defines[] = {
		"FOG", "1",
		nullptr, nullptr
	};

	const D3D_SHADER_MACRO alphaTestDefines[] = {
		"FOG", "1",
		"ALPHA_TEST", "1",
		nullptr, nullptr
	};
	
	const D3D_SHADER_MACRO dynamicResourcesDefines[] = {
		"FOG", "1",
		"ALPHA_TEST", "1",
		"DYNAMIC_RESOURCES", "1",
		nullptr, nullptr
	};

	const D3D_SHADER_MACRO dynamicResourcesInstanceDefines[] = {
		"FOG", "1",
		"ALPHA_TEST", "1",
		"DYNAMIC_RESOURCES", "1",
		"INSTANCE_RENDER", "1",
		nullptr, nullptr
	};

#ifdef DYNAMIC_RESOURCES
	#ifdef INSTANCE_RENDER
	mShaders["standardVS"] = d3dUtil::CompileShader(L"../../Shaders\\color.hlsl", dynamicResourcesInstanceDefines, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"../../Shaders\\color.hlsl", dynamicResourcesInstanceDefines, "PS", "ps_5_1");
	mShaders["alphaTestedPS"] = d3dUtil::CompileShader(L"../../Shaders\\color.hlsl", dynamicResourcesInstanceDefines, "PS", "ps_5_1");
	#else 
	mShaders["standardVS"] = d3dUtil::CompileShader(L"../../Shaders\\color.hlsl", dynamicResourcesDefines, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"../../Shaders\\color.hlsl", dynamicResourcesDefines, "PS", "ps_5_1");
	mShaders["alphaTestedPS"] = d3dUtil::CompileShader(L"../../Shaders\\color.hlsl", dynamicResourcesDefines, "PS", "ps_5_1");
	#endif
#else
	mShaders["standardVS"] = d3dUtil::CompileShader(L"../../Shaders\\color.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"../../Shaders\\color.hlsl", defines, "PS", "ps_5_1");
	mShaders["alphaTestedPS"] = d3dUtil::CompileShader(L"../../Shaders\\color.hlsl", alphaTestDefines, "PS", "ps_5_1");
#endif
	
	mShaders["treeSpriteVS"] = d3dUtil::CompileShader(L"../../Shaders\\TreeSprite.hlsl", nullptr, "VS", "vs_5_0");
	mShaders["treeSpriteGS"] = d3dUtil::CompileShader(L"../../Shaders\\TreeSprite.hlsl", nullptr, "GS", "gs_5_0");
	mShaders["treeSpritePS"] = d3dUtil::CompileShader(L"../../Shaders\\TreeSprite.hlsl", alphaTestDefines, "PS", "ps_5_0");
	mShaders["horzBlurCS"] = d3dUtil::CompileShader(L"../../Shaders\\Blur.hlsl", nullptr, "HorzBlurCS", "cs_5_0");
	mShaders["vertBlurCS"] = d3dUtil::CompileShader(L"../../Shaders\\Blur.hlsl", nullptr, "VertBlurCS", "cs_5_0");
	// mShaders["wavesUpdateCS"] = d3dUtil::CompileShader(L"Shaders\\WaveSim.hlsl", nullptr, "UpdateWavesCS", "cs_5_0");
	mShaders["SkyVS"] = d3dUtil::CompileShader(L"../../Shaders\\Sky.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["SkyPS"] = d3dUtil::CompileShader(L"../../Shaders\\Sky.hlsl", nullptr, "PS", "ps_5_1");
	// LPCSTR SemanticName; UINT SemanticIndex; DXGI_FORMAT Format; UINT InputSlot; UINT AlignedByteOffset; D3D12_INPUT_CLASSIFICATION InputSlotClass; UINT InstanceDataStepRate; D3D12_INPUT_ELEMENT_DESC;
	mInputLayout = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	mTreeSpriteInputLayout = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "SIZE", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}

void BoxApp::BuildPSO() {
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	// PSO for opaque objects
	ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = { mInputLayout.data(), static_cast<UINT>(mInputLayout.size()) };
	opaquePsoDesc.pRootSignature = mRootSignature.Get();
	opaquePsoDesc.VS = {
		reinterpret_cast<BYTE *>(mShaders["standardVS"]->GetBufferPointer()),
		mShaders["standardVS"]->GetBufferSize()
	};
	opaquePsoDesc.PS = {
		reinterpret_cast<BYTE *>(mShaders["opaquePS"]->GetBufferPointer()),
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

	D3D12_DEPTH_STENCIL_DESC shadowDSS;
	shadowDSS.DepthEnable = true;
	shadowDSS.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	shadowDSS.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	shadowDSS.StencilEnable = true;
	shadowDSS.StencilReadMask = 0xff;
	shadowDSS.StencilWriteMask = 0xff;

	shadowDSS.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	shadowDSS.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	shadowDSS.FrontFace.StencilPassOp = D3D12_STENCIL_OP_INCR;
	shadowDSS.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;

	// We are not rendering backfacing polygons, so these settings do not matter.
	shadowDSS.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	shadowDSS.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	shadowDSS.BackFace.StencilPassOp = D3D12_STENCIL_OP_INCR;
	shadowDSS.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowPsoDesc = transparentPsoDesc;
	shadowPsoDesc.DepthStencilState = shadowDSS;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&shadowPsoDesc, IID_PPV_ARGS(&mPSOs["shadow"])));

	auto x = mShaders.find("treeSpriteVS");

	// D3D12_GRAPHICS_PIPELINE_STATE_DESC treeSpritePsoDesc = opaquePsoDesc;
	// ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	// treeSpritePsoDesc.VS = {
	// 	reinterpret_cast<BYTE *>(mShaders["treeSpriteVS"]->GetBufferPointer()),
	// 	mShaders["treeSpriteVS"]->GetBufferSize()
	// };
	// treeSpritePsoDesc.GS = {
	// 	reinterpret_cast<BYTE *>(mShaders["treeSpriteGS"]->GetBufferPointer()),
	// 	mShaders["treeSpriteGS"]->GetBufferSize()
	// };
	// treeSpritePsoDesc.PS = {
	// 	reinterpret_cast<BYTE *>(mShaders["treeSpritePS"]->GetBufferPointer()),
	// 	mShaders["treeSpritePS"]->GetBufferSize()
	// };
	// treeSpritePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
	// treeSpritePsoDesc.InputLayout = { mTreeSpriteInputLayout.data(), static_cast<UINT>(mTreeSpriteInputLayout.size()) };
	// treeSpritePsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	//
	// ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&treeSpritePsoDesc, IID_PPV_ARGS(&mPSOs["treeSprites"])))

			D3D12_COMPUTE_PIPELINE_STATE_DESC horzBlurPSO = {};
	horzBlurPSO.pRootSignature = mPostProcessRootSignature.Get();
	horzBlurPSO.CS = {
		reinterpret_cast<BYTE *>(mShaders["horzBlurCS"]->GetBufferPointer()),
		mShaders["horzBlurCS"]->GetBufferSize()
	};
	horzBlurPSO.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	ThrowIfFailed(md3dDevice->CreateComputePipelineState(&horzBlurPSO, IID_PPV_ARGS(&mPSOs["horzBlur"])));

	//
	// PSO for vertical blur
	//
	D3D12_COMPUTE_PIPELINE_STATE_DESC vertBlurPSO = {};
	vertBlurPSO.pRootSignature = mPostProcessRootSignature.Get();
	vertBlurPSO.CS = {
		reinterpret_cast<BYTE *>(mShaders["vertBlurCS"]->GetBufferPointer()),
		mShaders["vertBlurCS"]->GetBufferSize()
	};
	vertBlurPSO.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	ThrowIfFailed(md3dDevice->CreateComputePipelineState(&vertBlurPSO, IID_PPV_ARGS(&mPSOs["vertBlur"])));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC skySpherePsoDesc = opaquePsoDesc;
	skySpherePsoDesc.VS = { reinterpret_cast<BYTE *>(mShaders["SkyVS"]->GetBufferPointer()), mShaders["SkyVS"]->GetBufferSize() };
	skySpherePsoDesc.PS = { reinterpret_cast<BYTE *>(mShaders["SkyPS"]->GetBufferPointer()), mShaders["SkyPS"]->GetBufferSize() };
	skySpherePsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	skySpherePsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&skySpherePsoDesc, IID_PPV_ARGS(&mPSOs["SkySphere"])));

	// D3D12_COMPUTE_PIPELINE_STATE_DESC wavesUpdatePSO = {};
	// wavesUpdatePSO.pRootSignature = mWaveRootSignature.Get();
	// wavesUpdatePSO.CS = {
	// reinterpret_cast<BYTE *>(mShaders["wavesUpdateCS"]->GetBufferPointer()),
	// mShaders["wavesUpdateCS"]->GetBufferSize()
	// };
	// wavesUpdatePSO.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	// ThrowIfFailed(md3dDevice->CreateComputePipelineState(&wavesUpdatePSO, IID_PPV_ARGS(&mPSOs["wavesUpdate"])))
}

void BoxApp::BuildFrameResources() {
	for (int i = 0; i < gNumFrameResources; ++i) {
		#ifdef INSTANCE_RENDER
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(), 1, mInstanceCount, (UINT)mMaterials.size(), mWaves->VertexCount()));
		#else
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(), 2, static_cast<UINT>(mAllRitems.size()), mMaterials.size(), mWaves->VertexCount()));
		#endif
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
	UINT gridVertexOffset = static_cast<UINT>(box.Vertices.size());
	UINT sphereVertexOffset = gridVertexOffset + static_cast<UINT>(grid.Vertices.size());
	UINT cylinderVertexOffset = sphereVertexOffset + static_cast<UINT>(sphere.Vertices.size());

	// Cache the starting index for each object in the concatenated index buffer.
	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = static_cast<UINT>(box.Indices32.size());
	UINT sphereIndexOffset = gridIndexOffset + static_cast<UINT>(grid.Indices32.size());
	UINT cylinderIndexOffset = sphereIndexOffset + static_cast<UINT>(sphere.Indices32.size());

	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = static_cast<UINT>(box.Indices32.size());
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = boxVertexOffset;

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = static_cast<UINT>(grid.Indices32.size());
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = gridVertexOffset;

	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = static_cast<UINT>(sphere.Indices32.size());
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

	SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = static_cast<UINT>(cylinder.Indices32.size());
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

	const UINT vbByteSize = static_cast<UINT>(vertices.size()) * sizeof(Vertex);
	const UINT ibByteSize = static_cast<UINT>(indices.size()) * sizeof(std::uint16_t);

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

	const UINT vbByteSize = static_cast<UINT>(vertices.size()) * sizeof(Vertex);
	const UINT ibByteSize = static_cast<UINT>(indices.size()) * sizeof(std::uint16_t);

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

void BoxApp::BuildSkyGeometry() {
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData skySphere = geoGen.CreateSphere(1.0, 20, 20);

	//将顶点数据传入Vertex结构体的数据元素
	size_t verticesCount = skySphere.Vertices.size(); //总顶点数
	std::vector<Vertex> vertices(verticesCount); //创建顶点列表
	for (size_t i = 0; i < verticesCount; i++) {
		vertices[i].Pos = skySphere.Vertices[i].Position;
		vertices[i].Normal = skySphere.Vertices[i].Normal;
		vertices[i].UV0 = skySphere.Vertices[i].TexC;
	}

	//创建索引列表,并初始化
	std::vector<std::uint16_t> indices = skySphere.GetIndices16();

	//顶点列表大小
	const UINT vbByteSize = (UINT)verticesCount * sizeof(Vertex);
	//索引列表大小
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	//绘制三参数
	SubmeshGeometry submesh;
	submesh.BaseVertexLocation = 0;
	submesh.StartIndexLocation = 0;
	submesh.IndexCount = (UINT)indices.size();

	//赋值MeshGeometry结构中的数据元素
	auto geo = std::make_unique<MeshGeometry>(); //指向MeshGeometry的指针
	geo->Name = "skySphereGeo";
	geo->VertexByteStride = sizeof(Vertex); //单个顶点的大小
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexBufferByteSize = ibByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->DrawArgs["skySphere"] = submesh;

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
			mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
			mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);
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

	XMFLOAT3 vMinf3(+MathHelper::Infinity, +MathHelper::Infinity, +MathHelper::Infinity);
	XMFLOAT3 vMaxf3(-MathHelper::Infinity, -MathHelper::Infinity, -MathHelper::Infinity);

	XMVECTOR vMin = XMLoadFloat3(&vMinf3);
	XMVECTOR vMax = XMLoadFloat3(&vMaxf3);

	std::vector<Vertex> vertices(vcount);
	for (UINT i = 0; i < vcount; ++i) {
		fin >> vertices[i].Pos.x >> vertices[i].Pos.y >> vertices[i].Pos.z;
		fin >> vertices[i].Normal.x >> vertices[i].Normal.y >> vertices[i].Normal.z;

		XMVECTOR P = XMLoadFloat3(&vertices[i].Pos);

		// Project point onto unit sphere and generate spherical texture coordinates.
		XMFLOAT3 spherePos;
		XMStoreFloat3(&spherePos, XMVector3Normalize(P));

		float theta = atan2f(spherePos.z, spherePos.x);

		// Put in [0, 2pi].
		if (theta < 0.0f)
			theta += XM_2PI;

		float phi = acosf(spherePos.y);

		float u = theta / (2.0f * XM_PI);
		float v = phi / XM_PI;

		vertices[i].UV0 = { u, v };

		vMin = XMVectorMin(vMin, P);
		vMax = XMVectorMax(vMax, P);
	}

	BoundingBox bounds;
	XMStoreFloat3(&bounds.Center, XMVectorScale(XMVectorAdd(vMin, vMax), 0.5));
	XMStoreFloat3(&bounds.Extents, XMVectorScale(XMVectorSubtract(vMax, vMin), 0.5));

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
	submesh.Bounds = bounds;

	geo->DrawArgs["skull"] = submesh;

	mGeometries[geo->Name] = std::move(geo);
}

void BoxApp::BuildTreeSpritesGeometry() {
	struct TreeSpriteVertex {
		XMFLOAT3 Pos;
		XMFLOAT2 Size;
	};

	static const int treeCount = 16;
	std::array<TreeSpriteVertex, 16> vertices;
	for (UINT i = 0; i < treeCount; ++i) {
		float x = MathHelper::RandF(-45.0f, 45.0f);
		float z = MathHelper::RandF(-45.0f, 45.0f);
		float y = GetHillsHeight(x, z);

		// Move tree slightly above land height.
		y += 8.0f;

		vertices[i].Pos = XMFLOAT3(x, y, z);
		vertices[i].Size = XMFLOAT2(20.0f, 20.0f);
	}

	std::array<std::uint16_t, 16> indices = {
		0, 1, 2, 3, 4, 5, 6, 7,
		8, 9, 10, 11, 12, 13, 14, 15
	};

	const UINT vbByteSize = static_cast<UINT>(vertices.size()) * sizeof(TreeSpriteVertex);
	const UINT ibByteSize = static_cast<UINT>(indices.size()) * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "treeSpritesGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
			mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
			mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(TreeSpriteVertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = static_cast<UINT>(indices.size());
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["points"] = submesh;

	mGeometries["treeSpritesGeo"] = std::move(geo);
}

#ifndef INSTANCE_RENDER
void BoxApp::BuildRenderItems() {
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
	mRitemLayer[static_cast<int>(RenderLayer::AlphaTest)].push_back(boxRitem.get());
	mAllRitems.push_back(std::move(boxRitem));
	 
	auto gridItem = std::make_unique<RenderItem>();
	gridItem->World = MathHelper::Identity4x4();
	gridItem->ObjCBIndex = 1;
	XMStoreFloat4x4(&gridItem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixTranslation(0.0f, -1.5f, 0.0f));
	gridItem->Geo = mGeometries["shapeGeo"].get();
	gridItem->Mat = mMaterials["stone"].get();
	gridItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridItem->IndexCount = gridItem->Geo->DrawArgs["grid"].IndexCount;
	gridItem->StartIndexLocation = gridItem->Geo->DrawArgs["grid"].StartIndexLocation;
	gridItem->BaseVertexLocation = gridItem->Geo->DrawArgs["grid"].BaseVertexLocation;
	gridItem->Name = "grid";
	mRitemLayer[static_cast<int>(RenderLayer::Opaque)].push_back(gridItem.get());
	mAllRitems.push_back(std::move(gridItem));

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
	mRitemLayer[static_cast<int>(RenderLayer::Transparent)].push_back(wavesRitem.get());
	mWavesRitem = wavesRitem.get();
	mAllRitems.push_back(std::move(wavesRitem));

	UINT objCBIndex = 3;

	for (int i = 0; i < 5; ++i) {
		auto leftCylRitem = std::make_unique<RenderItem>();
		auto rightCylRitem = std::make_unique<RenderItem>();
		auto leftSphereRitem = std::make_unique<RenderItem>();
		auto rightSphereRitem = std::make_unique<RenderItem>();

		XMMATRIX leftCylWorld = XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i * 5.0f);
		XMMATRIX rightCylWorld = XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i * 5.0f);

		XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i * 5.0f);
		XMMATRIX rightSphereWorld = XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i * 5.0f);

		XMStoreFloat4x4(&leftCylRitem->World, leftCylWorld);
		leftCylRitem->ObjCBIndex = objCBIndex++;
		leftCylRitem->Geo = mGeometries["shapeGeo"].get();
		leftCylRitem->Mat = mMaterials["skullMat"].get();
		leftCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftCylRitem->IndexCount = leftCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		leftCylRitem->StartIndexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		leftCylRitem->BaseVertexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
		leftCylRitem->Name = "leftCyl" + std::to_string(i);
		XMStoreFloat4x4(&rightCylRitem->World, rightCylWorld);
		rightCylRitem->ObjCBIndex = objCBIndex++;
		rightCylRitem->Geo = mGeometries["shapeGeo"].get();
		rightCylRitem->Mat = mMaterials["iceMirror"].get();
		rightCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightCylRitem->IndexCount = rightCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		rightCylRitem->StartIndexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		rightCylRitem->BaseVertexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
		rightCylRitem->Name = "rightCyl" + std::to_string(i);

		XMStoreFloat4x4(&leftSphereRitem->World, leftSphereWorld);
		leftSphereRitem->ObjCBIndex = objCBIndex++;
		leftSphereRitem->Geo = mGeometries["shapeGeo"].get();
		leftSphereRitem->Mat = mMaterials["bricks"].get();
		leftSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftSphereRitem->IndexCount = leftSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		leftSphereRitem->StartIndexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		leftSphereRitem->BaseVertexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;
		leftSphereRitem->Name = "leftSphere" + std::to_string(i);

		XMStoreFloat4x4(&rightSphereRitem->World, rightSphereWorld);
		rightSphereRitem->ObjCBIndex = objCBIndex++;
		rightSphereRitem->Geo = mGeometries["shapeGeo"].get();
		rightSphereRitem->Mat = mMaterials["checkertile"].get();
		rightSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightSphereRitem->IndexCount = rightSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		rightSphereRitem->StartIndexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		rightSphereRitem->BaseVertexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;
		rightSphereRitem->Name = "rightSphere" + std::to_string(i);

		mRitemLayer[static_cast<int>(RenderLayer::Opaque)].push_back(leftCylRitem.get());
		mRitemLayer[static_cast<int>(RenderLayer::Opaque)].push_back(rightCylRitem.get());
		mRitemLayer[static_cast<int>(RenderLayer::Opaque)].push_back(leftSphereRitem.get());
		mRitemLayer[static_cast<int>(RenderLayer::Opaque)].push_back(rightSphereRitem.get());

		mAllRitems.push_back(std::move(leftCylRitem));
		mAllRitems.push_back(std::move(rightCylRitem));
		mAllRitems.push_back(std::move(leftSphereRitem));
		mAllRitems.push_back(std::move(rightSphereRitem));
	}

	// auto floorRitem = std::make_unique<RenderItem>();
	// floorRitem->World = MathHelper::Identity4x4();
	// // XMStoreFloat4x4(&floorRitem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f) * XMMatrixTranslation(0.0f, 1.5f, 0.0f));
	// floorRitem->TexTransform = MathHelper::Identity4x4();
	// floorRitem->ObjCBIndex = objCBIndex++;
	// floorRitem->Geo = mGeometries["roomGeo"].get();
	// floorRitem->Mat = mMaterials["checkertile"].get();
	// floorRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	// floorRitem->IndexCount = floorRitem->Geo->DrawArgs["floor"].IndexCount;
	// floorRitem->StartIndexLocation = floorRitem->Geo->DrawArgs["floor"].StartIndexLocation;
	// floorRitem->BaseVertexLocation = floorRitem->Geo->DrawArgs["floor"].BaseVertexLocation;
	// floorRitem->Name = "floor";
	// mFloorRitem = floorRitem.get();
	// mRitemLayer[(int)RenderLayer::Opaque].push_back(floorRitem.get());
	//
	// auto reflectFloorRitem = std::make_unique<RenderItem>();
	// *reflectFloorRitem = *floorRitem;
	// reflectFloorRitem->ObjCBIndex = objCBIndex++;
	// reflectFloorRitem->Name = "reflectFloor";
	// mReflectedFloorRitem = reflectFloorRitem.get();
	// mRitemLayer[(int)RenderLayer::Reflected].push_back(reflectFloorRitem.get());
	// mAllRitems.push_back(std::move(reflectFloorRitem));
	// mAllRitems.push_back(std::move(floorRitem));
	//
	// auto wallsRitem = std::make_unique<RenderItem>();
	// wallsRitem->World = MathHelper::Identity4x4();
	// wallsRitem->TexTransform = MathHelper::Identity4x4();
	// wallsRitem->ObjCBIndex = objCBIndex++;
	// // wallsRitem->Mat = mMaterials["stone"].get();
	// wallsRitem->Mat = mMaterials["treeMat"].get();
	// wallsRitem->Geo = mGeometries["roomGeo"].get();
	// wallsRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	// wallsRitem->IndexCount = wallsRitem->Geo->DrawArgs["wall"].IndexCount;
	// wallsRitem->StartIndexLocation = wallsRitem->Geo->DrawArgs["wall"].StartIndexLocation;
	// wallsRitem->BaseVertexLocation = wallsRitem->Geo->DrawArgs["wall"].BaseVertexLocation;
	// wallsRitem->Name = "walls";
	// mRitemLayer[(int)RenderLayer::Opaque].push_back(wallsRitem.get());
	// mAllRitems.push_back(std::move(wallsRitem));
	//
	// auto mirrorRitem = std::make_unique<RenderItem>();
	// mirrorRitem->World = MathHelper::Identity4x4();
	// mirrorRitem->TexTransform = MathHelper::Identity4x4();
	// mirrorRitem->ObjCBIndex = objCBIndex++;
	// mirrorRitem->Mat = mMaterials["iceMirror"].get();
	// mirrorRitem->Geo = mGeometries["roomGeo"].get();
	// mirrorRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	// mirrorRitem->IndexCount = mirrorRitem->Geo->DrawArgs["mirror"].IndexCount;
	// mirrorRitem->StartIndexLocation = mirrorRitem->Geo->DrawArgs["mirror"].StartIndexLocation;
	// mirrorRitem->BaseVertexLocation = mirrorRitem->Geo->DrawArgs["mirror"].BaseVertexLocation;
	// mirrorRitem->Name = "mirror";
	// mRitemLayer[(int)RenderLayer::Mirrors].push_back(mirrorRitem.get());
	// mRitemLayer[(int)RenderLayer::Transparent].push_back(mirrorRitem.get());
	// mAllRitems.push_back(std::move(mirrorRitem));
	//
	// auto skullRitem = std::make_unique<RenderItem>();
	// skullRitem->World = MathHelper::Identity4x4();
	// skullRitem->TexTransform = MathHelper::Identity4x4();
	// skullRitem->ObjCBIndex = objCBIndex++;
	// skullRitem->Mat = mMaterials["skullMat"].get();
	// skullRitem->Geo = mGeometries["skullGeo"].get();
	// skullRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	// skullRitem->IndexCount = skullRitem->Geo->DrawArgs["skull"].IndexCount;
	// skullRitem->StartIndexLocation = skullRitem->Geo->DrawArgs["skull"].StartIndexLocation;
	// skullRitem->BaseVertexLocation = skullRitem->Geo->DrawArgs["skull"].BaseVertexLocation;
	// skullRitem->Name = "skull";
	// mSkullRitem = skullRitem.get();
	// mRitemLayer[(int)RenderLayer::Opaque].push_back(skullRitem.get());
	//
	// auto shadowedSkullRitem = std::make_unique<RenderItem>();
	// *shadowedSkullRitem = *skullRitem;
	// shadowedSkullRitem->ObjCBIndex = objCBIndex++;
	// shadowedSkullRitem->Mat = mMaterials["shadowMat"].get();
	// mShadowedSkullRitem = shadowedSkullRitem.get();
	// mRitemLayer[(int)RenderLayer::Shadow].push_back(shadowedSkullRitem.get());
	//
	// auto reflectedShadowedSkullRitem = std::make_unique<RenderItem>();
	// *reflectedShadowedSkullRitem = *skullRitem;
	// reflectedShadowedSkullRitem->ObjCBIndex = objCBIndex++;
	// reflectedShadowedSkullRitem->Mat = mMaterials["shadowMat"].get();
	// mReflectedShadowedSkullRitem = reflectedShadowedSkullRitem.get();
	// mRitemLayer[(int)RenderLayer::Reflected].push_back(reflectedShadowedSkullRitem.get());
	//
	// auto reflectedSkullRitem = std::make_unique<RenderItem>();
	// *reflectedSkullRitem = *skullRitem;
	// reflectedSkullRitem->ObjCBIndex = objCBIndex++;
	// reflectedSkullRitem->Name = "reflectSkull";
	// mRitemLayer[(int)RenderLayer::Reflected].push_back(reflectedSkullRitem.get());
	// mReflectedSkullRitem = reflectedSkullRitem.get();
	// mAllRitems.push_back(std::move(reflectedSkullRitem));
	// mAllRitems.push_back(std::move(reflectedShadowedSkullRitem));
	// mAllRitems.push_back(std::move(shadowedSkullRitem));
	// mAllRitems.push_back(std::move(skullRitem));

	auto treeSpritesRitem = std::make_unique<RenderItem>();
	treeSpritesRitem->Name = "Tree";
	treeSpritesRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&treeSpritesRitem->World, XMMatrixTranslation(0.0f, 0.0f, 2.0f));
	treeSpritesRitem->ObjCBIndex = objCBIndex++;
	treeSpritesRitem->Mat = mMaterials["treeMat"].get();
	treeSpritesRitem->Geo = mGeometries["treeSpritesGeo"].get();
	treeSpritesRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
	treeSpritesRitem->IndexCount = treeSpritesRitem->Geo->DrawArgs["points"].IndexCount;
	treeSpritesRitem->StartIndexLocation = treeSpritesRitem->Geo->DrawArgs["points"].StartIndexLocation;
	treeSpritesRitem->BaseVertexLocation = treeSpritesRitem->Geo->DrawArgs["points"].BaseVertexLocation;
	mRitemLayer[static_cast<int>(RenderLayer::AlphaTestedTreeSprites)].push_back(treeSpritesRitem.get());
	mAllRitems.push_back(std::move(treeSpritesRitem));
}

#endif

#ifdef INSTANCE_RENDER
void BoxApp::BuildInstanceRenderItems() {
	auto skullRitem = std::make_unique<RenderItem>();
	skullRitem->World = MathHelper::Identity4x4();
	skullRitem->TexTransform = MathHelper::Identity4x4();
	skullRitem->ObjCBIndex = 0;
	skullRitem->Mat = mMaterials["grass"].get();
	skullRitem->Geo = mGeometries["skullGeo"].get();
	skullRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	skullRitem->InstanceCount = 0;
	skullRitem->IndexCount = skullRitem->Geo->DrawArgs["skull"].IndexCount;
	skullRitem->StartIndexLocation = skullRitem->Geo->DrawArgs["skull"].StartIndexLocation;
	skullRitem->BaseVertexLocation = skullRitem->Geo->DrawArgs["skull"].BaseVertexLocation;
	skullRitem->Bounds = skullRitem->Geo->DrawArgs["skull"].Bounds;


	auto skyRitem = std::make_unique<RenderItem>();
	skyRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&skyRitem->World, XMMatrixScaling(5000.0f, 5000.0f, 5000.0f));
	skyRitem->Name = "Sky";
	skyRitem->NumFramesDirty = 3;
	skyRitem->TexTransform = MathHelper::Identity4x4();
	skyRitem->ObjCBIndex = 1;
	skyRitem->Mat = mMaterials["skyMat"].get();
	skyRitem->Geo = mGeometries["shapeGeo"].get();
	skyRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	skyRitem->IndexCount = skyRitem->Geo->DrawArgs["sphere"].IndexCount;
	skyRitem->StartIndexLocation = skyRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
	skyRitem->BaseVertexLocation = skyRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;
	skyRitem->Instances.resize(1);
	skyRitem->Instances[0].World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&skyRitem->Instances[0].World, XMMatrixScaling(5000.0f, 5000.0f, 5000.0f));
	// XMStoreFloat4x4(&skyRitem->Instances[0].TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	skyRitem->Instances[0].MaterialIndex = 9;
	mRitemLayer[static_cast<int>(RenderLayer::Sky)].push_back(skyRitem.get());
	mAllRitems.push_back(std::move(skyRitem));


	// Generate instance data.
	const int n = 5;
	mInstanceCount = n * n * n;
	skullRitem->Instances.resize(mInstanceCount);

	float width = 200.0f;
	float height = 200.0f;
	float depth = 200.0f;

	float x = -0.5f * width;
	float y = -0.5f * height;
	float z = -0.5f * depth;
	float dx = width / (n - 1);
	float dy = height / (n - 1);
	float dz = depth / (n - 1);
	for (int k = 0; k < n; ++k) {
		for (int i = 0; i < n; ++i) {
			for (int j = 0; j < n; ++j) {
				int index = k * n * n + i * n + j;
				// Position instanced along a 3D grid.
				skullRitem->Instances[index].World = XMFLOAT4X4(
						1.0f, 0.0f, 0.0f, 0.0f,
						0.0f, 1.0f, 0.0f, 0.0f,
						0.0f, 0.0f, 1.0f, 0.0f,
						x + j * dx, y + i * dy, z + k * dz, 1.0f);

				XMStoreFloat4x4(&skullRitem->Instances[index].TexTransform, XMMatrixScaling(2.0f, 2.0f, 1.0f));
				skullRitem->Instances[index].MaterialIndex = index % (mMaterials.size() - 2);
			}
		}
	}

	mAllRitems.push_back(std::move(skullRitem));

	// All the render items are opaque.
	for (auto &e : mAllRitems)
		mRitemLayer[static_cast<int>(RenderLayer::Opaque)].push_back(e.get());
}

void BoxApp::BuildSkyRenderItems() {
	auto skyRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&skyRitem->World, XMMatrixScaling(5000.0f, 5000.0f, 5000.0f));
	skyRitem->Name = "Sky";
	skyRitem->NumFramesDirty = 3;
	skyRitem->TexTransform = MathHelper::Identity4x4();
	skyRitem->ObjCBIndex = 1;
	skyRitem->Mat = mMaterials["skyMat"].get();
	skyRitem->Geo = mGeometries["shapeGeo"].get();
	skyRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	skyRitem->IndexCount = skyRitem->Geo->DrawArgs["sphere"].IndexCount;
	skyRitem->StartIndexLocation = skyRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
	skyRitem->BaseVertexLocation = skyRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

	mRitemLayer[static_cast<int>(RenderLayer::Sky)].push_back(skyRitem.get());
	mAllRitems.push_back(std::move(skyRitem));
}
#endif

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
	UINT ibByteSize = static_cast<UINT>(indices.size()) * sizeof(std::uint16_t);

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
	submesh.IndexCount = static_cast<UINT>(indices.size());
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

	auto shadowMat = std::make_unique<Material>();
	shadowMat->Name = "shadowMat";
	shadowMat->MatCBIndex = 7;
	shadowMat->DiffuseAlbedo = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.5f);
	shadowMat->FresnelR0 = XMFLOAT3(0.001f, 0.001f, 0.001f);
	shadowMat->Roughness = 0.0f;
	shadowMat->DiffuseSrvHeapIndex = 5;

	auto treeMat = std::make_unique<Material>();
	treeMat->Name = "treeMat";
	treeMat->MatCBIndex = 8;
	treeMat->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	treeMat->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
	treeMat->Roughness = 0.8f;
	treeMat->DiffuseSrvHeapIndex = 6;

	auto skyMat = std::make_unique<Material>();
	skyMat->Name = "skyMat";
	skyMat->MatCBIndex = 9;
	skyMat->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	skyMat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	skyMat->Roughness = 0.1f;
	skyMat->DiffuseSrvHeapIndex = 7;

	// 将材质数据存放在系统内存之中,为了GPU能够在着色器中访问,还需复制到常量缓冲区中
	mMaterials["grass"] = std::move(grass);
	mMaterials["water"] = std::move(water);
	mMaterials["stone"] = std::move(stone);
	mMaterials["iceMirror"] = std::move(icemirror);
	mMaterials["skullMat"] = std::move(skullMat);
	mMaterials["bricks"] = std::move(bricks);
	mMaterials["checkertile"] = std::move(checkertile);
	mMaterials["shadowMat"] = std::move(shadowMat);
	mMaterials["treeMat"] = std::move(treeMat);
	mMaterials["skyMat"] = std::move(skyMat);
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

	auto treeTex = std::make_unique<Texture>();
	treeTex->Name = "treeTex";
	treeTex->Filename = L"Textures/treeArray2.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
			mCommandList.Get(), treeTex->Filename.c_str(),
			treeTex->Resource, treeTex->UploadHeap));

	auto skyTex = std::make_unique<Texture>();
	skyTex->Name = "skyTex";
	skyTex->Filename = L"Textures/snowcube1024.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(), mCommandList.Get(),
			skyTex->Filename.c_str(), //将wstring转成wChar_t
			skyTex->Resource, skyTex->UploadHeap));

	mTextures[woodCrateTex->Name] = std::move(woodCrateTex);
	mTextures[woodCrateTex2->Name] = std::move(woodCrateTex2);
	mTextures[water->Name] = std::move(water);
	mTextures[checkboardTex->Name] = std::move(checkboardTex);
	mTextures[iceTex->Name] = std::move(iceTex);
	mTextures[white1x1Tex->Name] = std::move(white1x1Tex);
	mTextures[treeTex->Name] = std::move(treeTex);
	mTextures[skyTex->Name] = std::move(skyTex);
}

void BoxApp::AnimateMaterial(const GameTimer &gt) {
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
