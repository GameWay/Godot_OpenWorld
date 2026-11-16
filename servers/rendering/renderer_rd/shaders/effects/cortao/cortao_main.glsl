#[compute]

#version 450

#VERSION_DEFINES

// OEngine: CORTAO main pass. Traces rays against circular occluders built from the packed buffer and outputs
// occlusion and a view space bent normal. See thirdparty/cortao/shaders/CORTAOMainPass.h.
// The projection type comes from the variant defines. CORTAO_SAMPLE_COUNT, CORTAO_RAY_COUNT,
// CORTAO_BENT_NORMAL_LOOKUP_TABLE, CORTAO_PACKED_NORMAL_BITS and CORTAO_16_BIT come from the general defines.

#extension GL_EXT_control_flow_attributes : enable

#if CORTAO_16_BIT
#extension GL_EXT_shader_explicit_arithmetic_types : require
#endif

#define CORTAO_GLSL 1
#define CORTAO_OCCLUDER_LOOKUP_TABLE 1

#include "thirdparty/cortao/shaders/CORTAOMainPass.h"

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(push_constant, std430) uniform Params {
	CORTAOMainPassParameters parameters;
}
params;

layout(set = 0, binding = 0) uniform texture2D packed_data; // R32F bits, with mipmaps.
layout(set = 0, binding = 1) uniform texture2D source_normal_roughness;
layout(set = 0, binding = 2) uniform utexture3D occluder_lookup_table; // R8/R16/R32_UINT for 8/16/32 rays.
layout(set = 0, binding = 3) uniform texture2D bent_normal_lookup_table; // R8G8_SNORM, 256 entries per row.
layout(set = 0, binding = 4) uniform sampler nearest_border_sampler;
layout(set = 0, binding = 5) uniform sampler nearest_clamp_sampler;

layout(rgba8_snorm, set = 1, binding = 0) uniform restrict writeonly image2D dest; // Occlusion, bent normal.

cortao_uint cortao_samplePackedData(cortao_float2 texCoords, cortao_float mipmap) {
	return floatBitsToUint(textureLod(sampler2D(packed_data, nearest_border_sampler), texCoords, mipmap).r);
}

cortao_half3 cortao_loadNormal(cortao_int2 pixelCoords) {
	return cortao_half3(normalize(texelFetch(sampler2D(source_normal_roughness, nearest_clamp_sampler), pixelCoords, 0).xyz * 2.0 - 1.0));
}

cortao_uint cortao_sampleOccluderLookupTable(cortao_float3 texCoords) {
	// Nearest clamp-to-edge lookup done as a texel fetch: integer texture sampling needs Shader Model 6.7 on D3D12, loads do not.
	const ivec3 size = ivec3(24, 24, 16); // All the lookup tables we ship are 24x24x16.
	ivec3 texel = clamp(ivec3(floor(texCoords * vec3(size))), ivec3(0), size - 1);
	return texelFetch(usampler3D(occluder_lookup_table, nearest_clamp_sampler), texel, 0).r;
}

#if CORTAO_BENT_NORMAL_LOOKUP_TABLE
cortao_half3 cortao_loadBentNormalLookupTable(cortao_uint rayBits) {
	vec2 bent_normal = texelFetch(sampler2D(bent_normal_lookup_table, nearest_clamp_sampler), ivec2(rayBits & 0xFFu, rayBits >> 8u), 0).xy;
	return cortao_half3(bent_normal, sqrt(max(0.0, 1.0 - dot(bent_normal, bent_normal))));
}
#endif

void cortao_writeResult(cortao_int2 pixelCoords, cortao_half occlusion, cortao_half3 bentNormal, cortao_half3x3 tangentSpaceTransform, cortao_uint rayBits) {
	imageStore(dest, pixelCoords, vec4(float(occlusion), vec3(bentNormal)));
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
