//***************************************************************************************
// color.hlsl by Frank Luna (C) 2015 All Rights Reserved.
//
// Transforms and colors geometry.
//***************************************************************************************

// cbuffer cbPerObject : register(b0)
// {
// 	float4x4 gWorldViewProj;
// 	float4 g_Color;
// 	uint g_UseCustomColor;
// };
struct ObjectConstants
{
	float4x4 gWorldViewProj;
	float4 g_Color;
	uint g_UseCustomColor;
};
ConstantBuffer<ObjectConstants> gObjConstants : register(b0);

struct VertexIn
{
	float3 PosL  : POSITION;
    float4 Color : COLOR;
};

struct VertexOut
{
	float4 PosH  : SV_POSITION;
    float4 Color : COLOR;
};

VertexOut VS(VertexIn vin)
{
	VertexOut vout;
	
	// Transform to homogeneous clip space.
	vout.PosH = mul(float4(vin.PosL, 1.0f), gObjConstants.gWorldViewProj);
	
	// Just pass vertex color into the pixel shader.
    vout.Color = vin.Color;
    
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
	return gObjConstants.g_UseCustomColor ? gObjConstants.g_Color : pin.Color;
}


