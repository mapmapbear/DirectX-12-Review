struct DirectionLight
{
	float4 Ambient;
	float4 Diffuse;
	float4 Specular;
	float3 Direction;
	float pad;
}

void ComputeDirectionLight(Material mat, DirectionLight light, VertexIn in)
{
	
}