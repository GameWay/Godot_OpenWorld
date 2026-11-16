#[compute]

#version 450

#VERSION_DEFINES

// OEngine: CORTAO main pass. Traces rays against circular occluders built from the packed buffer and outputs
// occlusion and a view space bent normal. See thirdparty/cortao/shaders/CORTAOMainPass.h.
// CORTAO_SAMPLE_COUNT and the projection type come from the variant defines.

#extension GL_EXT_control_flow_attributes : enable

#define CORTAO_GLSL 1
#define CORTAO_RAY_COUNT 16
#define CORTAO_OCCLUDER_LOOKUP_TABLE 1
#define CORTAO_BENT_NORMAL_LOOKUP_TABLE 1

#include "thirdparty/cortao/shaders/CORTAOMainPass.h"

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(push_constant, std430) uniform Params {
	CORTAOMainPassParameters parameters;
}
params;

layout(set = 0, binding = 0) uniform texture2D packed_data; // R32F bits, with mipmaps.
layout(set = 0, binding = 1) uniform texture2D source_normal_roughness;
layout(set = 0, binding = 2) uniform texture3D occluder_lookup_table; // R16_UNORM, 16 rays.
layout(set = 0, binding = 3) uniform texture2D bent_normal_lookup_table; // R8G8_SNORM, 256x256 = 2^16 entries.
layout(set = 0, binding = 4) uniform sampler nearest_border_sampler;
layout(set = 0, binding = 5) uniform sampler nearest_clamp_sampler;

layout(rgba8_snorm, set = 1, binding = 0) uniform restrict writeonly image2D dest; // Occlusion, bent normal.

uint cortao_samplePackedData(vec2 texCoords, float mipmap) {
	return floatBitsToUint(textureLod(sampler2D(packed_data, nearest_border_sampler), texCoords, mipmap).r);
}

vec3 cortao_loadNormal(ivec2 pixelCoords) {
	return normalize(texelFetch(sampler2D(source_normal_roughness, nearest_clamp_sampler), pixelCoords, 0).xyz * 2.0 - 1.0);
}

uint cortao_sampleOccluderLookupTable(vec3 texCoords) {
	return uint(textureLod(sampler3D(occluder_lookup_table, nearest_clamp_sampler), texCoords, 0.0).r * 65535.0 + 0.5);
}

vec3 cortao_loadBentNormalLookupTable(uint rayBits) {
	vec2 bent_normal = texelFetch(sampler2D(bent_normal_lookup_table, nearest_clamp_sampler), ivec2(rayBits & 0xFFu, rayBits >> 8u), 0).xy;
	return vec3(bent_normal, sqrt(max(0.0, 1.0 - dot(bent_normal, bent_normal))));
}

void cortao_writeResult(ivec2 pixelCoords, float occlusion, vec3 bentNormal, mat3 tangentSpaceTransform, uint rayBits) {
	imageStore(dest, pixelCoords, vec4(occlusion, bentNormal));
}

// Reorders the threads inside the workgroup to be more cache coherent.
uvec2 deinterleave(uint index) {
	uvec2 p = uvec2(index, index) >> uvec2(0, 1);

	p &= 0x5555u;
	p = (p ^ (p >> 1u)) & 0x3333u;
	p = (p ^ (p >> 2u)) & 0x0f0fu;
	p = (p ^ (p >> 4u)) & 0x00ffu;

	return p;
}

void main() {
	uvec2 pixel_coords = gl_WorkGroupID.xy * gl_WorkGroupSize.xy + deinterleave(gl_LocalInvocationIndex);
	cortao_mainPass(pixel_coords, params.parameters);
}
