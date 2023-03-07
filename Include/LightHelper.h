#pragma once
#include <cstring>
#include <DirectXMath.h>
using namespace DirectX;

struct Material
{
	XMFLOAT4 ambient;
	XMFLOAT4 diffuse;
	XMFLOAT4 specular;
	XMFLOAT4 reflect;
};

struct DirectionLight
{
	XMFLOAT4 ambient;
	XMFLOAT4 diffuse;
	XMFLOAT4 specular;
	XMFLOAT4 direction;
};

struct PointLight
{
	PointLight() { memset(this, 0, sizeof(PointLight)); }
	XMFLOAT4 ambient;
	XMFLOAT4 diffuse;
	XMFLOAT4 specular;

	XMFLOAT3 position;
	float range;
	XMFLOAT3 att;
	float pad;
};

struct SpotLight
{
	XMFLOAT4 ambient;
	XMFLOAT4 diffuse;
	XMFLOAT4 specular;

	XMFLOAT3 position;
	float range;
	XMFLOAT3 direction;
	float spot;

	XMFLOAT3 att;
	float pad;
};




