#ifndef NUM_DIR_LIGHTS
#define NUM_DIR_LIGHTS 3
#endif

#ifndef NUM_POINT_LIGHTS
#define NUM_POINT_LIGHTS 0
#endif

#ifndef NUM_SPOT_LIGHTS
#define NUM_SPOT_LIGHTS 0
#endif

#include "LightingUtil.hlsl"

struct MaterialData
{
    float4 gDiffuseAlbedo; //���ʷ�����
    float3 gFresnelR0; //RF(0)ֵ�������ʵķ�������
    float gRoughness; //���ʵĴֲڶ�
    float4x4 gMatTransform; //UV�����任����
    uint gDiffuseMapIndex; //������������
    uint gNormalMapIndex;
    uint gMatPad1;
    uint gMatPad2;
};
StructuredBuffer<MaterialData> gMaterialData : register(t1, space1);


struct InstanceData
{
    float4x4 World;
    float4x4 TexTransform;
    uint MaterialIndex;
    uint InstPad0;
    uint InstPad1;
    uint InstPad2;
};
StructuredBuffer<InstanceData> gInstanceData : register(t0, space1);

Texture2D gDiffuseMap[9] : register(t2);
TextureCube gCubeMap : register(t0);
SamplerState gsamPointWrap : register(s0);
SamplerState gsamPointClamp : register(s1);
SamplerState gsamLinearWrap : register(s2);
SamplerState gsamLinearClamp : register(s3);
SamplerState gsamAnisotropicWrap : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);



struct cbPass
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float3 gEyePosW;
    float cbPerObjectPad1;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;
    float4 gAmbientLight;
    Light gLights[MaxLights];
    float4 g_Color;
    float g_UseCustomColor;
    float3 cbPerObjectPad2;
    float4 gFogColor;
    float gFogRange;
    float gFogStart;
    float2 cbPerObjectPad3;
};
ConstantBuffer<cbPass> gCBPass : register(b0);




// �� Gamma �ռ䵽���Կռ����ɫ����
float3 gammaDecode(float3 color, float gamma)
{
    return pow(color, gamma);
}

float3 NormalSampleToWorldSpace(float3 normalMapSample, float3 unitNormalW, float3 tangentW)
{
    float3 normalT = normalMapSample * 2.0f - 1.0f;
    float3 N = unitNormalW;
    float3 T = normalize(tangentW - dot(N, tangentW) * N); //修正T值
    float3 B = cross(N, T); //注意叉乘顺序

    float3x3 TBN = float3x3(T, B, N);

    float3 normalW = mul(normalT, TBN);
    return normalW;
}