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

Texture2D gDiffuseMap : register(t0); // 纹理
SamplerState gsamLinear : register(s0); // 采样器


struct cbPerObject // 通过根签名将常量缓冲区与寄存器槽绑定
{
	float4x4 gWorld;
	float4x4 gTexTransform; // UV需要乘这个矩阵
};
ConstantBuffer<cbPerObject> gCBPerObject : register(b0);

struct cbPass {
	float4x4 gView;
	float4x4 gInvView;
	float4x4 gProj;
	float4x4 gInvProj;
	float4x4 gViewProj;
	float4x4 gInvViewProj;
	float3 gEyePow;
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

struct cbMaterial {
	float4 gDiffuseAlbedo; // 漫反射反照率
	float3 gFresnelR0; // 材质属性RF(0°),影响镜面反射
	float gRoughness;
	float4x4 gMatTransform;
};
ConstantBuffer<cbMaterial> gMatCBPass : register(b2);

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
	float4 UV = mul(float4(vin.UV0, 0.0f, 1.0f), gCBPerObject.gTexTransform);
	vout.UV0 = mul(UV, gMatCBPass.gMatTransform).xy;
	return vout;
}

// 在光栅化期间(为三角形计算像素颜色)对顶点着色器(或几何着色器)输出的顶点属性进行差值
// 随后,再将这些差值数据传至像素着色器中作为它的输入
// SV_Target: 返回值的类型应当与渲染目标格式相匹配(该输出值会被存于渲染目标之中)
float4 PS(VertexOut pin) : SV_Target {
	// 从纹理中提取此像素的漫反射反照率
	// 将纹理样本与常量缓冲区中的反照率相乘
	float4 diffuseAlbedo = gDiffuseMap.Sample(gsamLinear, pin.UV0.yx) * gMatCBPass.gDiffuseAlbedo;
#ifdef ALPHA_TEST
	// Discard pixel if texture alpha < 0.1.  We do this test as soon
	// as possible in the shader so that we can potentially exit the
	// shader early, thereby skipping the rest of the shader code.
	clip(diffuseAlbedo.a - 0.1f);
#endif

	pin.NormalW = normalize(pin.NormalW);

	// Vector from point being lit to eye
	float3 toEyeW = normalize(gCBPass.gEyePow - pin.PosW);
	float distToEye = length(toEyeW);
	toEyeW /= distToEye; // normalize

	// Indirect lighting.
	float4 ambient = gCBPass.gAmbientLight * diffuseAlbedo;

	const float shininess = 1.0f - gMatCBPass.gRoughness;

	Material mat = { diffuseAlbedo, gMatCBPass.gFresnelR0, shininess };
	float3 shadowFactor = 1.0f;
	float4 directLight = ComputeLighting(gCBPass.gLights, mat, pin.PosW, pin.NormalW, toEyeW, shadowFactor);
	float4 litColor = ambient + directLight;
#ifdef FOG
	float fogAmount = saturate((distToEye - gCBPass.gFogStart) / gCBPass.gFogRange);
	litColor = lerp(litColor, gCBPass.gFogColor, fogAmount);
	// return float4(1.0, 0.0, 0.0, 1.0);
#endif
	litColor.a = diffuseAlbedo.a;
	// return litColor;
	// return gCBPass.g_UseCustomColor ? float4(1.0, 0.0, 1.0, 1.0) : litColor;
	// return gCBPass.g_UseCustomColor ? gCBPass.g_Color : litColor;
	return litColor;
}