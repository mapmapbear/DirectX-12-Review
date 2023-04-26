﻿#include "FrameResource.h"

FrameResource::FrameResource(ID3D12Device *device, UINT passCount, UINT objectCount) {
    ThrowIfFailed(device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(CmdListAlloc.GetAddressOf())));

    PassCB = std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true);
    ObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(device, objectCount, true);
}

FrameResource::FrameResource(ID3D12Device *device, UINT passCount, UINT objectCount, UINT waveVertCount, UINT materialCount) {
	ThrowIfFailed(device->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(CmdListAlloc.GetAddressOf())));

	PassCB = std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true);
	ObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(device, objectCount, true);
	WavesVB = std::make_unique<UploadBuffer<Vertex>>(device, waveVertCount, false);
	MaterialCB = std::make_unique<UploadBuffer<MaterialConstants>>(device, materialCount, false);
}


FrameResource::~FrameResource()
{

}
