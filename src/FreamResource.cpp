#include "FrameResource.h"

FrameResource::FrameResource(ID3D12Device *device, UINT passCount, UINT objectCount, UINT materialCount) {
    ThrowIfFailed(device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(CmdListAlloc.GetAddressOf())));

    PassCB = std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true);
#ifdef INSTANCE_RENDER
	InstanceBufferArr.resize(objectCount);
	for (int i = 0; i < InstanceBufferArr.size(); ++i) {
		InstanceBufferArr[i] = std::make_unique<UploadBuffer<InstanceData>>(device, 100, false);
	}
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
	InstanceBufferArr.resize(objectCount);
	for (int i = 0; i < InstanceBufferArr.size(); ++i) {
		InstanceBufferArr[i] = std::make_unique<UploadBuffer<InstanceData>>(device, 20, false);	
	}
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
