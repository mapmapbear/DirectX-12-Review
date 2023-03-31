#include <DirectXColors.h>
#include "d3dApp.h"
#include "UploadBuffer.h"
#include "FreamResource.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

struct Vertex {
	XMFLOAT3 Pos;
	XMFLOAT4 Color;
};


class GameApp : public D3DApp {
public:
	GameApp(HINSTANCE hInstance);
	~GameApp();

	// void BuildMaterials();

	virtual bool Initialize() override;

private:
	virtual void OnResize() override;
	virtual void Update(const GameTimer &gt) override;
	virtual void Draw(const GameTimer &gt) override;

	virtual void OnMouseDown(WPARAM btnState, int x, int y) override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y) override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y) override;
	void BuildDescriptorHeaps();
	void BuildConstantBuffers();
	void BuildRootSignature();
	void BuildShadersAndInputLayout();
	void BuildBoxGeometry();
	void BuildPSO();

	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	ComPtr<ID3D12DescriptorHeap> mCbvHeap = nullptr;

	std::unique_ptr<UploadBuffer<ObjectConstants>> mObjectCB = nullptr;
	std::unique_ptr<MeshGeometry> mBoxGeo = nullptr;

	ComPtr<ID3DBlob> mvsByteCode = nullptr;
	ComPtr<ID3DBlob> mpsByteCode = nullptr;

	// std::vector<D3D12_INPUT_LAYOUT_DESC> mInputLayout;
	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
	ComPtr<ID3D12PipelineState> mPSO = nullptr;

	XMFLOAT4X4 mWorld = MathHelper::Identity4x4();
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();

	float mTheta = 1.0f * XM_PI;
	float mPhi = XM_PIDIV4;
	float mRadius = 5.0f;
	POINT mLastMousePos;
	ImVec4 ccolor;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
};