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
    float4 gDiffuseAlbedo; //材质反照率
    float3 gFresnelR0; //RF(0)值，即材质的反射属性
    float gRoughness; //材质的粗糙度
    float4x4 gMatTransform; //UV动画变换矩阵
    uint gDiffuseMapIndex; //纹理数组索引
    uint gMatPad0;
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

Texture2D gDiffuseMap[6] : register(t2); // 纹理
TextureCube gCubeMap : register(t0);

SamplerState gsamPointWrap : register(s0); // 采样器
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




// 从 Gamma 空间到线性空间的颜色解码
float3 gammaDecode(float3 color, float gamma)
{
    return pow(color, gamma);
}