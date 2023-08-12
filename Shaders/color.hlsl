//Test
// ******************** 渲染流水线 ********************

// 输入装配器阶段:	从显存中读取几何数据(顶点和索引),再将它们装配为几何图元
// 顶点着色器阶段:	图元被装配完毕后,其顶点送入VS.可以吧顶点着色器看做一种输入与输出数据皆为单个顶点的函数(对顶点的操作由GPU执行)
// 外壳着色器阶段:
// 曲面细分阶段  :
// 域着色器阶段  :
// 几何着色器阶段:
// 光栅化阶段    :	为投影主屏幕上的3D三角形计算出对应的颜色
// 像素着色器阶段:	GPU执行的程序,针对每一个像素片段(亦有译作片元)进行处理,并根据顶点的属性差值作为输入来计算出对应的颜色
// 输出合并阶段  :	通过像素着色器生成的像素片段会被移送至输出合并阶段
//					在此阶段中,一些像素可能会被丢弃(eg,未通过深度缓冲区测试/模板缓冲区测试),剩下的像素片段会被写入后台缓冲区中
//					混合操作在此阶段实现,此技术可令当前处理的像素与后台缓冲区中的对应像素相融合,而不仅是对后者进行完全的覆写

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

#ifdef DYNAMIC_RESOURCES
struct MaterialData {
	float4 gDiffuseAlbedo; //材质反照率
	float3 gFresnelR0; //RF(0)值，即材质的反射属性
	float gRoughness; //材质的粗糙度
	float4x4 gMatTransform; //UV动画变换矩阵
	uint gDiffuseMapIndex; //纹理数组索引
	uint gMatPad0;
	uint gMatPad1;
	uint gMatPad2;
};
StructuredBuffer<MaterialData> gMaterialData : register(t0, space1);
#else
struct cbMaterial
{
    float4 gDiffuseAlbedo; // 漫反射反照率
    float3 gFresnelR0; // 材质属性RF(0°),影响镜面反射
    float gRoughness;
    float4x4 gMatTransform;
};
ConstantBuffer<cbMaterial> gMatCBPass : register(b2);
#endif

#ifdef DYNAMIC_RESOURCES
	Texture2D gDiffuseMap[6] : register(t0); // 纹理
#else
	Texture2D gDiffuseMap : register(t0);
#endif

SamplerState gsamPointWrap        : register(s0); // 采样器
SamplerState gsamPointClamp       : register(s1);
SamplerState gsamLinearWrap       : register(s2);
SamplerState gsamLinearClamp      : register(s3);
SamplerState gsamAnisotropicWrap  : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);


struct cbPerObject // 通过根签名将常量缓冲区与寄存器槽绑定
{
	float4x4 gWorld;
	float4x4 gTexTransform; // UV需要乘这个矩阵
#ifdef DYNAMIC_RESOURCES
	uint gMaterialDataIndex;
	uint gObjPad0;
	uint gObjPad1;
	uint gObjPad2;
#endif
};
ConstantBuffer<cbPerObject> gCBPerObject : register(b0);

struct cbPass {
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
ConstantBuffer<cbPass> gCBPass : register(b1);




// 从 Gamma 空间到线性空间的颜色解码
float3 gammaDecode(float3 color, float gamma)
{
    return pow(color, gamma);
}

struct VertexIn {
	// 语义 "POSITION" 对应 D3D12_INPUT_ELEMENT_DESC 的 "POSITION"
	// D3D12_INPUT_ELEMENT_DESC 通过先后顺序对应 Vertex 结构体中的属性
	float3 PosL : POSITION;
	float4 NormalL : NORMAL;
	float2 UV0 : TEXCOORD;
};

struct VertexOut {
	// SV: System Value, 它所修饰的顶点着色器输出元素存有齐次裁剪空间中的顶点位置信息
	// 必须为输出位置信息的参数附上 SV_POSITION 语义
	float4 PosH : SV_POSITION;
	float3 PosW : POSITION;
	float3 NormalW : NORMAL;
	float2 UV0 : TEXCOORD;
};

VertexOut VS(VertexIn vin) {
	VertexOut vout;
	
	float4 posW = mul(float4(vin.PosL, 1.0f), gCBPerObject.gWorld);
	// 转换到齐次裁剪空间
	vout.PosH = mul(posW, gCBPass.gViewProj);
	vout.PosW = posW.xyz;
	vout.NormalW = mul(vin.NormalL, gCBPerObject.gWorld).xyz;
#ifdef DYNAMIC_RESOURCES
	MaterialData matData = gMaterialData[gCBPerObject.gMaterialDataIndex];
	float4 UV = mul(float4(vin.UV0, 0.0f, 1.0f), matData.gMatTransform);
	vout.UV0 = mul(UV, matData.gMatTransform).xy;
#else
	float4 UV = mul(float4(vin.UV0, 0.0f, 1.0f), gMatCBPass.gMatTransform);
	vout.UV0 = mul(UV, gMatCBPass.gMatTransform).xy;
#endif
	return vout;
}

#ifdef DYNAMIC_RESOURCES
float4 PS(VertexOut pin) : SV_Target
{ 
    //获取材质数据(需要点出来，和CB使用不太一样)
    MaterialData matData = gMaterialData[gCBPerObject.gMaterialDataIndex];
    float4 diffuseAlbedo = matData.gDiffuseAlbedo;
    float3 fresnelR0 = matData.gFresnelR0;
    float roughness = matData.gRoughness;
    uint diffuseTexIndex = matData.gDiffuseMapIndex;

    //在数组中动态地查找纹理
    diffuseAlbedo *= gDiffuseMap[diffuseTexIndex].Sample(gsamAnisotropicWrap, pin.UV0);
    #ifdef ALPHA_TEST
	clip(diffuseAlbedo.a - 0.1f);
	#endif
    float3 worldNormal = normalize(pin.NormalW);
    float3 worldView = normalize(gCBPass.gEyePosW - pin.PosW);
    
    Material mat = { diffuseAlbedo, fresnelR0, roughness };
    float3 shadowFactor = 1.0f;//暂时使用1.0，不对计算产生影响
    float4 directLight = ComputeLighting(gCBPass.gLights, mat, pin.PosW, worldNormal, worldView, shadowFactor);
    float4 ambient = gCBPass.gAmbientLight * diffuseAlbedo;
    float4 diffuse = directLight * diffuseAlbedo;
    float4 finalCol = ambient + diffuse;
    finalCol.a = diffuseAlbedo.a;

    return finalCol;
}
#else
float4 PS(VertexOut pin) : SV_Target
{
	float4 diffuseAlbedo = gDiffuseMap.Sample(gsamAnisotropicWrap, pin.UV0) * gMatCBPass.gDiffuseAlbedo;
	
#ifdef ALPHA_TEST
	clip(diffuseAlbedo.a - 0.1f);
#endif
	
    // Interpolating normal can unnormalize it, so renormalize it.
    pin.NormalW = normalize(pin.NormalW);

    // Vector from point being lit to eye. 
	float3 toEyeW = gCBPass.gEyePosW - pin.PosW;
	float distToEye = length(toEyeW);
	toEyeW /= distToEye; // normalize

    // Light terms.
	float4 ambient = gCBPass.gAmbientLight * diffuseAlbedo;

    const float shininess = 1.0f - gMatCBPass.gRoughness;
	Material mat = { diffuseAlbedo, gMatCBPass.gFresnelR0, shininess };
    float3 shadowFactor = 1.0f;
	float4 directLight = ComputeLighting(gCBPass.gLights, mat, pin.PosW,
        pin.NormalW, toEyeW, shadowFactor);

    float4 litColor = ambient + directLight;

#ifdef FOG
	float fogAmount = saturate((distToEye - gCBPass.gFogStart) / gCBPass.gFogRange);
	litColor = lerp(litColor, gCBPass.gFogColor, fogAmount);
#endif

    // Common convention to take alpha from diffuse albedo.
    litColor.a = diffuseAlbedo.a;

    return litColor;
}
#endif