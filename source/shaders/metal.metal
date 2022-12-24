#include "shader_defs.h"

#include <simd/simd.h>

using namespace metal;

struct VertexOut {
	float4 color;
	float4 pos [[position]];
};

vertex VertexOut vertexShader(const device Vertex *vertexArray [[buffer(0)]], constant  UniformBuffer &uniformData [[buffer(1)]], unsigned int vid [[vertex_id]])
{
	Vertex in = vertexArray[vid];
	
	float anim = uniformData.time / 100;
	
	float2x2 rotmat = {
		float2(cos(anim),-sin(anim)),
		float2(sin(anim), cos(anim))
	};
	
	auto transformed = rotmat * in.pos;
	
	VertexOut out{
		.color = in.color,
		.pos = {transformed.x,transformed.y,0,1}
	};
	
	return out;
}

fragment float4 fragmentShader(VertexOut interpolated [[stage_in]])
{
	return interpolated.color;
}
