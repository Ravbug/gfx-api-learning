#pragma once

#include <simd/simd.h>

struct Vertex{
	simd_float4 color;
	simd_float2 pos;
};

struct UniformBuffer{
	float time;
};
