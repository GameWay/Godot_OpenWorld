#[compute]

#version 450

#VERSION_DEFINES

// OEngine: temporal accumulation of the CORTAO output.
// CORTAO only removes part of its noise spatially and expects a temporal filter after it. The history is reprojected
// with the camera motion, rejected on depth mismatches, and clamped to the current 3x3 neighborhood so that moving
// objects do not leave trails. The final texture holds the occlusion in R and the view space bent normal in GBA.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform texture2D packed_data; // R32F, linear depth in the high bits (0.0 for the sky).
layout(set = 0, binding = 1) uniform texture2D source_ssao; // RGBA8_SNORM: occlusion, bent normal.
layout(set = 0, binding = 2) uniform texture2D history_prev; // RGBA16F: occlusion, bent normal in the previous view space.
layout(set = 0, binding = 3) uniform texture2D history_depth_prev; // RG32F: linear depth, accumulated frames.
layout(set = 0, binding = 4) uniform sampler nearest_sampler;

layout(rgba16f, set = 1, binding = 0) uniform restrict writeonly image2D history_cur;
layout(rg32f, set = 1, binding = 1) uniform restrict writeonly image2D history_depth_cur;
layout(rgba8, set = 1, binding = 2) uniform restrict writeonly image2D dest;

#define FLAG_ORTHOGONAL (1 << 0)
#define FLAG_HISTORY_VALID (1 << 1)

layout(push_constant, std430) uniform Params {
	// Current view space to previous view space, as the rows of a 3x4 matrix.
	vec4 reproject_row0;
	vec4 reproject_row1;
	vec4 reproject_row2;

	vec4 unproject_params; // Same as CORTAO: view direction (or position for orthographic) from UV.
	vec4 prev_projection; // Previous view space to UV: uv = xy * scale (/ depth for perspective) + offset.

	vec2 inv_resolution;
	float intensity;
	uint flags;

	float max_frames;
	float depth_tolerance;
	float clamp_margin;
	float pad;
}
params;

vec4 load_history(ivec2 p_pos, ivec2 p_size) {
	return texelFetch(sampler2D(history_prev, nearest_sampler), clamp(p_pos, ivec2(0), p_size - 1), 0);
}

void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	ivec2 size = ivec2(round(1.0 / params.inv_resolution));
	if (any(greaterThanEqual(pos, size))) {
		return;
	}

	float depth = texelFetch(sampler2D(packed_data, nearest_sampler), pos, 0).r;
	if (depth <= 0.0) {
		// Sky: unoccluded, no history.
		imageStore(history_cur, pos, vec4(1.0, 0.0, 0.0, 1.0));
		imageStore(history_depth_cur, pos, vec4(0.0));
		imageStore(dest, pos, vec4(1.0, 0.5, 0.5, 1.0));
		return;
	}

	vec4 current = texelFetch(sampler2D(source_ssao, nearest_sampler), pos, 0);
	float bent_length = length(current.yzw);
	vec3 bent_normal = bent_length > 0.0001 ? current.yzw / bent_length : vec3(0.0, 0.0, 1.0);

	// Range of the current occlusion around this pixel, used to clamp the history.
	float ao_min = current.x;
	float ao_max = current.x;
	for (int y = -1; y <= 1; y++) {
		for (int x = -1; x <= 1; x++) {
			if (x == 0 && y == 0) {
				continue;
			}
			ivec2 p = clamp(pos + ivec2(x, y), ivec2(0), size - 1);
			if (texelFetch(sampler2D(packed_data, nearest_sampler), p, 0).r > 0.0) {
				float ao = texelFetch(sampler2D(source_ssao, nearest_sampler), p, 0).x;
				ao_min = min(ao_min, ao);
				ao_max = max(ao_max, ao);
			}
		}
	}

	float frames = 1.0;
	vec4 history = vec4(current.x, bent_normal);

	if (bool(params.flags & FLAG_HISTORY_VALID)) {
		vec2 uv = (vec2(pos) + 0.5) * params.inv_resolution;
		vec2 dir = uv * params.unproject_params.xy + params.unproject_params.zw;
		vec3 view_pos = bool(params.flags & FLAG_ORTHOGONAL) ? vec3(dir, -depth) : vec3(dir, -1.0) * depth;

		vec4 vp = vec4(view_pos, 1.0);
		vec3 prev_view_pos = vec3(dot(params.reproject_row0, vp), dot(params.reproject_row1, vp), dot(params.reproject_row2, vp));
		float prev_depth = -prev_view_pos.z;

		vec2 prev_uv = prev_view_pos.xy * params.prev_projection.xy;
		if (!bool(params.flags & FLAG_ORTHOGONAL)) {
			prev_uv /= prev_depth;
		}
		prev_uv += params.prev_projection.zw;

		if (prev_depth > 0.0 && all(greaterThanEqual(prev_uv, vec2(0.0))) && all(lessThanEqual(prev_uv, vec2(1.0)))) {
			// Bilinear footprint of the history, keeping only the texels on the same surface.
			vec2 texel = prev_uv * vec2(size) - 0.5;
			ivec2 base = ivec2(floor(texel));
			vec2 f = texel - vec2(base);

			// textureGather() order: (0, 1), (1, 1), (1, 0), (0, 0).
			vec4 depths = textureGather(sampler2D(history_depth_prev, nearest_sampler), prev_uv, 0);
			vec4 counts = textureGather(sampler2D(history_depth_prev, nearest_sampler), prev_uv, 1);
			vec4 weights = vec4((1.0 - f.x) * f.y, f.x * f.y, f.x * (1.0 - f.y), (1.0 - f.x) * (1.0 - f.y));
			vec4 same_surface = vec4(lessThan(abs(depths - prev_depth), vec4(prev_depth * params.depth_tolerance)));
			weights *= same_surface;
			float weight_sum = dot(weights, vec4(1.0));

			if (weight_sum > 0.001) {
				weights /= weight_sum;
				history = load_history(base + ivec2(0, 1), size) * weights.x;
				history += load_history(base + ivec2(1, 1), size) * weights.y;
				history += load_history(base + ivec2(1, 0), size) * weights.z;
				history += load_history(base, size) * weights.w;
				frames = min(dot(counts, weights) + 1.0, params.max_frames);
			}
		}
	}

	// Clamp the history to what the current frame sees around this pixel. A large correction means the history
	// belongs to something that moved, so most of the accumulation is dropped.
	float clamped_ao = clamp(history.x, ao_min - params.clamp_margin, ao_max + params.clamp_margin);
	frames = min(frames, mix(params.max_frames, 2.0, clamp(abs(clamped_ao - history.x) * 8.0, 0.0, 1.0)));

	float blend = 1.0 / frames;
	float ao = mix(clamped_ao, current.x, blend);

	// The history bent normal is in the previous view space. The rotation part of the reprojection maps current
	// to previous view space, so its transpose maps back (mat3() takes columns, i.e. the rows given here).
	vec3 history_bent_normal = mat3(params.reproject_row0.xyz, params.reproject_row1.xyz, params.reproject_row2.xyz) * history.yzw;
	vec3 accumulated_bent_normal = mix(history_bent_normal, bent_normal, blend);
	float accumulated_length = length(accumulated_bent_normal);
	accumulated_bent_normal = accumulated_length > 0.0001 ? accumulated_bent_normal / accumulated_length : bent_normal;

	imageStore(history_cur, pos, vec4(ao, accumulated_bent_normal));
	imageStore(history_depth_cur, pos, vec4(depth, frames, 0.0, 0.0));
	imageStore(dest, pos, vec4(pow(clamp(ao, 0.0, 1.0), params.intensity), accumulated_bent_normal * 0.5 + 0.5));
}
