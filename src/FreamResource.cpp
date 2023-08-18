#include "FrameResource.h"

FrameResource::FrameResource(ID3D12Device *device, UINT passCount, UINT objectCount, UINT materialCount) {
    ThrowIfFailed(device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(CmdListAlloc.GetAddressOf())));

    PassCB = std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true);
#ifdef INSTANCE_RENDER
	InstanceBuffer = std::make_unique<UploadBuffer<InstanceData>>(device, objectCount, false);
#else
	ObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(device, objectCount, true);
#endif
#ifdef DYNAMIC_RESOURCES
	MaterialBuffer = std::make_unique<UploadBuffer<MaterialData>>(device, materialCount, false);
#else
	MaterialBuffer = std::make_unique<UploadBuffer<MaterialConstants>>(device, materialCount, true);
#endif
}

FrameResource::FrameResource(ID3D12Device *device, UINT passCount, UINT objectCount, UINT materialCount, UINT waveVertCount) {
	ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(CmdListAlloc.GetAddressOf())));

	PassCB = std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true);
	#ifdef INSTANCE_RENDER
	UINT maxInstanceCount = objectCount;
	InstanceBuffer = std::make_unique<UploadBuffer<InstanceData>>(device, maxInstanceCount, false);
	#else
	ObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(device, objectCount, true);
	#endif
	#ifdef DYNAMIC_RESOURCES
	MaterialBuffer = std::make_unique<UploadBuffer<MaterialData>>(device, materialCount, false);
	#else
	MaterialBuffer = std::make_unique<UploadBuffer<MaterialConstants>>(device, materialCount, true);
	#endif
	WavesVB = std::make_unique<UploadBuffer<Vertex>>(device, waveVertCount, false);
}

FrameResource::~FrameResource()
{

}
