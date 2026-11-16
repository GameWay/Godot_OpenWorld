#[compute]

#version 450

#VERSION_DEFINES

// OEngine: CORTAO prepass. Packs linear depth and a low precision normal into an R32 buffer with mipmaps.
// See thirdparty/cortao/shaders/CORTAOPrepass.h.

#extension GL_EXT_control_flow_attributes : enable

#define CORTAO_GLSL 1
// The depth buffer is reversed, so the skybox is at depth 0.0 for both projection types.
#define CORTAO_SKYBOX_DEPTH 0.0

#include "thirdparty/cortao/shaders/CORTAOPrepass.h"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(push_constant, std430) uniform Params {
	CORTAOPrepassParameters parameters;
}
params;

layout(set = 0, binding = 0) uniform sampler2D source_depth;
layout(set = 0, binding = 1) uniform sampler2D source_normal_roughness;

// R32F, the packed bits are stored as float bits (avoids integer texture sampling, which D3D12 only supports from SM 6.7).
layout(r32f, set = 1, binding = 0) uniform restrict writeonly image2D packed_mips[8];

float cortao_loadDepth(ivec2 pixelCoords) {
	return texelFetch(source_depth, pixelCoords, 0).r;
}

vec3 cortao_loadNormal(ivec2 pixelCoords) {
	// The normal roughness buffer stores view space normals.
	return normalize(texelFetch(source_normal_roughness, pixelCoords, 0).xyz * 2.0 - 1.0);
}

void cortao_writePackedData(ivec2 pixelCoords, int mipLevel, uint data) {
	imageStore(packed_mips[mipLevel], pixelCoords, vec4(uintBitsToFloat(data)));
}

void main() {
	cortao_prepass(gl_GlobalInvocationID.xy, params.parameters);
}
