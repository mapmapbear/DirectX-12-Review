#include "Common.hlsl"

struct VertexIn
{
    float3 PosL : POSITION;
    float2 UV0 : TEXCOORD;
};


struct VertexOut {
	float4 PosH : SV_POSITION;
	float2 UV0 : TEXCOORD;
	nointerpolation uint MatIndex  : MATINDEX;
};
VertexOut VS(VertexIn vin, uint instanceID : SV_InstanceID)
{
    VertexOut vout = (VertexOut) 0.0f;
    InstanceData instData = gInstanceData[instanceID];
    float4x4 world = instData.World;
    float4x4 texTransform = instData.TexTransform;
    uint matIndex = instData.MaterialIndex;
    MaterialData matData = gMaterialData[matIndex];
	
    // Transform to world space.
    float4 posW = mul(float4(vin.PosL, 1.0f), world);

    // Transform to homogeneous clip space.
    vout.PosH = mul(posW, gCBPass.gViewProj);
	
	// Output vertex attributes for interpolation across triangle.
    float4 texC = mul(float4(vin.UV0, 0.0f, 1.0f), texTransform);
    vout.UV0 = mul(texC, matData.gMatTransform).xy;
	
    return vout;
}

void PS(VertexOut pin)
{
    MaterialData matData = gMaterialData[pin.MatIndex];
    uint diffuseTexIndex = matData.gDiffuseMapIndex;
    float4 diffuseAlbedo = matData.gDiffuseAlbedo;
	// Dynamically look up the texture in the array.
    diffuseAlbedo *= gDiffuseMap[diffuseTexIndex].Sample(gsamAnisotropicWrap, pin.UV0);

#ifdef ALPHA_TEST
    clip(diffuseAlbedo.a - 0.1f);
#endif
}
