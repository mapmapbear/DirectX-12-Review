//=============================================================================
// Sky.fx by Frank Luna (C) 2011 All Rights Reserved.
//=============================================================================

// Include common HLSL code.
#include "Common.hlsl"

struct VertexIn
{
    float3 PosL : POSITION;
    float4 NormalL : NORMAL;
    float2 UV0 : TEXCOORD;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosL : POSITION;
	nointerpolation uint MatIndex  : MATINDEX;
};

float4x4 ScaleMatrix(float scaleFactor)
{
    return float4x4(
        scaleFactor, 0.0f, 0.0f, 0.0f,
        0.0f, scaleFactor, 0.0f, 0.0f,
        0.0f, 0.0f, scaleFactor, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    );
}

VertexOut VS(VertexIn vin)
{
    VertexOut vout;

	// Use local vertex position as cubemap lookup vector.
    vout.PosL = vin.PosL;
	
	// Transform to world space.
    //float4x4 world = ScaleMatrix(5000) * gInstanceData[0].World;
    float4x4 world = gInstanceData[0].World;
    float4 posW = mul(float4(vin.PosL, 1.0f), world);

	// Always center sky about camera.
    posW.xyz += gCBPass.gEyePosW;

	// Set z = w so that z/w = 1 (i.e., skydome always on far plane).
    vout.PosH = mul(posW, gCBPass.gViewProj).xyww;
	
    return vout;
}
 
float4 PS(VertexOut pin) : SV_Target
{
	return gCubeMap.Sample(gsamLinearWrap, pin.PosL);
}

