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
 
VertexOut VS(VertexIn vin, uint instanceID : SV_InstanceID)
{
	VertexOut vout;
    vout.PosL = vin.PosL;
    InstanceData instData = gInstanceData[instanceID];
    float4x4 world = instData.World;
    float4 posW = mul(float4(vin.PosL, 1.0f), world);
    posW.xyz += gCBPass.gEyePosW;
    vout.PosH = mul(posW, gCBPass.gViewProj).xyww;
    vout.MatIndex = 0;
	return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
	return gCubeMap.Sample(gsamLinearWrap, pin.PosL);
}

