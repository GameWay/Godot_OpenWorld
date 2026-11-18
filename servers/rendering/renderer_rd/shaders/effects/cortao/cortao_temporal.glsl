#[compute]

#version 450

#VERSION_DEFINES

// OEngine: temporal accumulation of the CORTAO output.
// CORTAO only removes part of its noise spatially and expects a temporal filter after it. The history is reprojected
// with the camera motion, rejected on depth mismatches, and clamped to the current 3x3 neighborhood so that moving
// objects do not leave trails. Moving objects are found with the motion vectors of the previous frame (the current ones
// are written later, by the opaque pass): their history is taken along their own motion and kept short. Pixels with little history get an extra depth and normal aware blur, and the result
// fades out with the distance. The final texture holds the occlusion in R and the view space bent normal in GBA.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform texture2D packed_data; // R32F, linear depth in the high bits (0.0 for the sky).
layout(set = 0, binding = 1) uniform texture2D source_ssao; // RGBA8_SNORM: occlusion, bent normal.
layout(set = 0, binding = 2) uniform texture2D history_prev; // RGBA16F: occlusion, bent normal in the previous view space.
layout(set = 0, binding = 3) uniform texture2D history_depth_prev; // RG32F: linear depth, accumulated frames.
layout(set = 0, binding = 4) uniform sampler nearest_sampler;
layout(set = 0, binding = 5) uniform texture2D source_normal_roughness;
layout(set = 0, binding = 6) uniform texture2D last_velocity; // RG16F, previous frame motion vectors: previous UV - UV.

layout(rgba16f, set = 1, binding = 0) uniform restrict writeonly image2D history_cur;
layout(rg32f, set = 1, binding = 1) uniform restrict writeonly image2D history_depth_cur;
layout(rgba8, set = 1, binding = 2) uniform restrict writeonly image2D dest;

#define FLAG_ORTHOGONAL (1 << 0)
#define FLAG_HISTORY_VALID (1 << 1)
#define FLAG_USE_VELOCITY (1 << 2)

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
	float velocity_threshold; // In pixels.

	// Distance fade: 1.0 up to the fade start, 0.0 from its end (fade = depth * mul + add).
	float fade_mul;
	float fade_add;
	// Pixels with fewer accumulated frames than this get the disocclusion blur (0 disables it).
	float disocclusion_frames;
	float disocclusion_radius; // In pixels.
}
params;

vec3 load_normal(ivec2 p_pos) {
	return normalize(texelFetch(sampler2D(source_normal_roughness, nearest_sampler), p_pos, 0).xyz * 2.0 - 1.0);
}

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

		// The camera reprojection assumes a static scene. Where last frame's motion vector disagrees with it, the pixel
		// belongs to a moving object: follow its motion instead (one frame late, which is close enough for smooth
		// motion), compare depths with its current depth, and keep the history short to absorb the remaining error.
		bool moving = false;
		if (bool(params.flags & FLAG_USE_VELOCITY)) {
			vec2 object_prev_uv = uv + texelFetch(sampler2D(last_velocity, nearest_sampler), pos, 0).xy;
			if (length((object_prev_uv - prev_uv) * vec2(size)) > params.velocity_threshold) {
				moving = true;
				prev_uv = object_prev_uv;
				prev_depth = depth;
			}
		}

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
				if (moving) {
					frames = min(frames, max(2.0, params.max_frames * 0.25));
				}
			}
		}
	}

	// Clamp the history to what the current frame sees around this pixel. A large correction means the history
	// belongs to something that moved, so most of the accumulation is dropped.
	float clamped_ao = clamp(history.x, ao_min - params.clamp_margin, ao_max + params.clamp_margin);
	frames = min(frames, mix(params.max_frames, 2.0, clamp(abs(clamped_ao - history.x) * 8.0, 0.0, 1.0)));

	vec3 normal = load_normal(pos);

	// Disocclusion: newly visible pixels have little or no history and would show the raw noise for a few frames.
	// Blur them over a wider, depth and normal aware footprint, fading the blur out as the history builds up.
	if (frames < params.disocclusion_frames) {
		float blur = 1.0 - clamp((frames - 1.0) / max(params.disocclusion_frames - 1.0, 1.0), 0.0, 1.0);
		vec4 sum = vec4(current.x, bent_normal);
		float weight_sum = 1.0;
		int radius = int(params.disocclusion_radius);
		for (int y = -2; y <= 2; y++) {
			for (int x = -2; x <= 2; x++) {
				if (x == 0 && y == 0) {
					continue;
				}
				ivec2 p = clamp(pos + ivec2(x, y) * radius, ivec2(0), size - 1);
				float sample_depth = texelFetch(sampler2D(packed_data, nearest_sampler), p, 0).r;
				if (sample_depth <= 0.0) {
					continue;
				}
				float depth_weight = exp2(-abs(sample_depth - depth) / depth * 64.0);
				float normal_weight = pow(max(dot(load_normal(p), normal), 0.0), 8.0);
				float w = depth_weight * normal_weight;
				vec4 s = texelFetch(sampler2D(source_ssao, nearest_sampler), p, 0);
				sum += vec4(s.x, s.yzw) * w;
				weight_sum += w;
			}
		}
		sum /= weight_sum;
		current.x = mix(current.x, sum.x, blur);
		vec3 blurred_bent_normal = sum.yzw;
		float blurred_length = length(blurred_bent_normal);
		if (blurred_length > 0.0001) {
			bent_normal = normalize(mix(bent_normal, blurred_bent_normal / blurred_length, blur));
		}
	}

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

	// Distance fade, applied to the output only: occlusion toward 1.0, bent normal toward the surface normal.
	float fade = clamp(depth * params.fade_mul + params.fade_add, 0.0, 1.0);
	float final_ao = mix(1.0, pow(clamp(ao, 0.0, 1.0), params.intensity), fade);
	vec3 final_bent_normal = normalize(mix(normal, accumulated_bent_normal, fade));
	imageStore(dest, pos, vec4(final_ao, final_bent_normal * 0.5 + 0.5));
}
