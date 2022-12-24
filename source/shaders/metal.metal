#include "shader_defs.h"

struct VertexOut {
	float4 color;
	float4 pos [[position]];
};

vertex VertexOut vertexShader(const device Vertex *vertexArray [[buffer(0)]], unsigned int vid [[vertex_id]])
{
	Vertex in = vertexArray[vid];
	VertexOut out{
		.color = {in.color[0],in.color[1],in.color[2],in.color[3]},
		.pos = {in.pos[0],in.pos[1],0,1}
	};
	
	return out;
}

fragment float4 fragmentShader(VertexOut interpolated [[stage_in]])
{
	return interpolated.color;
}
