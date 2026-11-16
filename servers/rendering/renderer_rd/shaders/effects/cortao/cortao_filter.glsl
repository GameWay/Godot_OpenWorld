#[compute]

#version 450

#VERSION_DEFINES

// OEngine: CORTAO filter pass. A sparse depth and normal aware blur of occlusion and bent normals that
// prepares the result for the temporal pass. See thirdparty/cortao/shaders/CORTAOFilterPass.h.
// CORTAO_LARGE_FILTER comes from the variant defines.

#extension GL_EXT_control_flow_attributes : enable

#define CORTAO_GLSL 1
#define CORTAO_FILTER_BENT_NORMAL 1

#include "thirdparty/cortao/shaders/CORTAOFilterPass.h"

// Each thread filters a 2x2 pixel block.
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(push_constant, std430) uniform Params {
	CORTAOFilterPassParameters parameters;
}
params;

layout(set = 0, binding = 0) uniform texture2D packed_data; // R32F bits.
layout(set = 0, binding = 1) uniform texture2D source_ssao; // RGBA8_SNORM: occlusion, bent normal.
layout(set = 0, binding = 2) uniform sampler nearest_border_sampler;

layout(rgba8_snorm, set = 1, binding = 0) uniform restrict writeonly image2D dest;

vec2 inv_resolution;

// The offsets are applied to the texture coordinates instead of using textureGatherOffset(),
// as the offsets are not constant expressions. The coordinates are on texel corners, so the
// gathered footprint is the same.
uvec4 cortao_gatherPackedData(vec2 texCoords, ivec2 offset) {
	return floatBitsToUint(textureGather(sampler2D(packed_data, nearest_border_sampler), texCoords + vec2(offset) * inv_resolution, 0));
}

mat4 cortao_gatherSSAO(vec2 texCoords, ivec2 offset) {
	vec2 uv = texCoords + vec2(offset) * inv_resolution;
	vec4 r = textureGather(sampler2D(source_ssao, nearest_border_sampler), uv, 0);
	vec4 g = textureGather(sampler2D(source_ssao, nearest_border_sampler), uv, 1);
	vec4 b = textureGather(sampler2D(source_ssao, nearest_border_sampler), uv, 2);
	vec4 a = textureGather(sampler2D(source_ssao, nearest_border_sampler), uv, 3);
	// One column per gathered texel.
	return transpose(mat4(r, g, b, a));
}

void cortao_writeFilterOutput(ivec2 pixelCoords, vec4 ssao) {
	imageStore(dest, pixelCoords, ssao);
}

void main() {
	inv_resolution = 1.0 / vec2(cortao_getResolution(params.parameters.packedResolution));
	cortao_filterPass(gl_GlobalInvocationID.xy, params.parameters);
}
