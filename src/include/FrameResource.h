#pragma once
#pragma once
#pragma once

#include "d3dUtil.h"
#include "MathHelper.h"
#include "UploadBuffer.h"


struct ObjectConstants
{
	DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();
#ifdef DYNAMIC_RESOURCES
	UINT materialIndex = 0;
	UINT matPad0;
	UINT matPad1;
	UINT matPad2;
#endif
};

#ifdef INSTANCE_RENDER
struct InstanceData {
	DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();
	UINT MaterialIndex;
	UINT InstancePad0;
	UINT InstancePad1;
	UINT InstancePad2;
};
#endif

struct PassConstants
{
	DirectX::XMFLOAT4X4 View = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 InvView = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 Proj = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 InvProj = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 ViewProj = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 InvViewProj = MathHelper::Identity4x4();
	DirectX::XMFLOAT3 EyePosW = { 0.0f, 0.0f, 0.0f };
	float cbPerObjectPad1 = 0.0f;
	DirectX::XMFLOAT2 RenderTargetSize = { 0.0f, 0.0f };
	DirectX::XMFLOAT2 InvRenderTargetSize = { 0.0f, 0.0f };
	float NearZ = 0.0f;
	float FarZ = 0.0f;
	float TotalTime = 0.0f;
	float DeltaTime = 0.0f;
	DirectX::XMFLOAT4 AmbientLight = { 0.0f, 0.0f, 0.0f, 1.0f };
	Light Lights[MaxLights];
	DirectX::XMFLOAT4 color;
	float useCustomColor;
	DirectX::XMFLOAT3 cbPerObjectPad2 = { 0.0f, 0.0f, 0.0f};
	DirectX::XMFLOAT4 FogColor = { 0.7f, 0.7f, 0.7f, 1.0f };
	float gFogRange = 50.0f;
	float gFogStart = 5.0f;
	DirectX::XMFLOAT2 cbPerObjectPad3 = { 0.0, 0.0f };
};

#ifdef DYNAMIC_RESOURCES
struct MaterialData {
	DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
	DirectX::XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };
	float Roughness = 64.0f;

	// Used in texture mapping.
	DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();

	UINT DiffuseMapIndex = 0;
	UINT MaterialPad0;
	UINT MaterialPad1;
	UINT MaterialPad2;
};
#endif

struct Vertex
{
	Vertex() = default;
	Vertex(float x, float y, float z, float nx, float ny, float nz, float u, float v) :
			Pos(x, y, z),
			Normal(nx, ny, nz),
			UV0(u, v) {}
	DirectX::XMFLOAT3 Pos;
	DirectX::XMFLOAT3 Normal;
	DirectX::XMFLOAT2 UV0;
};

// Stores the resources needed for the CPU to build the command lists
// for a frame.  
struct FrameResource
{
public:
	FrameResource(const FrameResource& rhs) = delete;
	FrameResource& operator=(const FrameResource& rhs) = delete;
	// FrameResource(ID3D12Device *device, UINT passCount, UINT objectCount);
	FrameResource(ID3D12Device *device, UINT passCount, UINT objectCount, UINT materialCount);
	FrameResource(ID3D12Device *device, UINT passCount, UINT objectCount, UINT materialCount, UINT waveVertCount);
	~FrameResource();

	// We cannot reset the allocator until the GPU is done processing the commands.
	// So each frame needs their own allocator.
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CmdListAlloc;

	std::unique_ptr<UploadBuffer<PassConstants>> PassCB = nullptr;
#ifdef INSTANCE_RENDER
	std::vector<std::unique_ptr<UploadBuffer<InstanceData>>> InstanceBufferArr;
#else
		std::unique_ptr<UploadBuffer<ObjectConstants>> ObjectCB = nullptr;
#endif
	#ifdef DYNAMIC_RESOURCES
		std::unique_ptr<UploadBuffer<MaterialData>> MaterialBuffer = nullptr;
	#else
		std::unique_ptr<UploadBuffer<MaterialConstants>> MaterialBuffer = nullptr;	
	#endif
		std::unique_ptr<UploadBuffer<Vertex>> WavesVB = nullptr;


		// Fence value to mark commands up to this fence point.  This lets us
		// check if these frame resources are still in use by the GPU.
		UINT64 Fence = 0;
};