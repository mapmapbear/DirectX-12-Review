# LearnDirectX-12

## build 

Visual Stadio 2022 X64

## Release Log

23/5/17 更新多张纹理 + 纹理动画



## BUG Log

### 1. Shader Model 5.0 与 Shader Model 5.1

DX12特有的Shader Model 5.1 对常量缓冲区的声明方式与Model 5.0做了一些改变

```
// 5.0
cbuffer cbPerObject : register(b0)
{
	float4x4 gWorldViewProj;
	float4 g_Color;
	uint g_UseCustomColor;
};

// 5.1
struct ObjectConstants					
{
	float4x4 gWorldViewProj;
	float4 g_Color;
	uint g_UseCustomColor;
};
ConstantBuffer<ObjectConstants> gObjConstants : register(b0);
```

记得在Shader编译的时候选择合适的Shader Model版本, 否则会引发一些DX程序的异常报错

