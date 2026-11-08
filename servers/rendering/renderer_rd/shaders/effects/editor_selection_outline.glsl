#[vertex]

#version 450

#VERSION_DEFINES

layout(location = 0) out vec2 uv_interp;

void main() {
	vec2 base_arr[3] = vec2[](vec2(-1.0, -1.0), vec2(-1.0, 3.0), vec2(3.0, -1.0));
	gl_Position = vec4(base_arr[gl_VertexIndex], 0.0, 1.0);
	uv_interp = clamp(gl_Position.xy, vec2(0.0, 0.0), vec2(1.0, 1.0)) * 2.0; // saturate(x) * 2.0
}

#[fragment]

#version 450

#VERSION_DEFINES

// OEngine editor selection outline.
// The selection mask is the depth of the selected geometry only (reverse Z, 0.0 = nothing).
// The outline is drawn outside the silhouette of the mask, at full strength where the selected
// geometry is visible and dimmed where it is hidden behind other geometry.

layout(location = 0) in vec2 uv_interp;

layout(set = 0, binding = 0) uniform sampler2D selection_depth;
layout(set = 0, binding = 1) uniform sampler2D scene_depth;

layout(location = 0) out vec4 frag_color;

layout(push_constant, std430) uniform Params {
	vec4 color;
	vec2 target_pixel_size; // 1.0 / target size.
	float width; // In target pixels.
	float occluded_alpha;
	float depth_bias;
	float pad[3];
}
params;

// Returns 0 when there is no selected geometry, 1 when it is visible and 2 when it is hidden.
int sample_selection(vec2 p_uv) {
	float sel = textureLod(selection_depth, p_uv, 0.0).r;
	if (sel <= 0.0) {
		return 0;
	}
	// Farthest scene depth around the sample: the scene depth is TAA-jittered and MSAA-resolved,
	// which on grazing surfaces would otherwise make the selected geometry look hidden by itself.
	vec2 px = params.target_pixel_size;
	float scene = textureLod(scene_depth, p_uv, 0.0).r;
	scene = min(scene, textureLod(scene_depth, p_uv + vec2(px.x, 0.0), 0.0).r);
	scene = min(scene, textureLod(scene_depth, p_uv - vec2(px.x, 0.0), 0.0).r);
	scene = min(scene, textureLod(scene_depth, p_uv + vec2(0.0, px.y), 0.0).r);
	scene = min(scene, textureLod(scene_depth, p_uv - vec2(0.0, px.y), 0.0).r);
	// Reverse Z: a larger value is closer to the camera. The bias is relative, since the selection
	// mask is rendered without TAA jitter and can differ slightly from the scene depth.
	return sel >= scene * (1.0 - params.depth_bias) ? 1 : 2;
}

void main() {
	vec2 uv = uv_interp;

	if (sample_selection(uv) != 0) {
		// Inside the silhouette, the outline is only drawn outside.
		discard;
	}

	// `width` fully opaque pixels, then one pixel of fade.
	int radius = int(ceil(params.width + 1.0));
	float best_visible = 1e10;
	float best_hidden = 1e10;

	for (int y = -radius; y <= radius; y++) {
		for (int x = -radius; x <= radius; x++) {
			float dist = length(vec2(x, y));
			if (dist > params.width + 1.0) {
				continue;
			}
			int s = sample_selection(uv + vec2(x, y) * params.target_pixel_size);
			if (s == 1) {
				best_visible = min(best_visible, dist);
			} else if (s == 2) {
				best_hidden = min(best_hidden, dist);
			}
		}
	}

	float visible = clamp(params.width + 1.0 - best_visible, 0.0, 1.0);
	float hidden = clamp(params.width + 1.0 - best_hidden, 0.0, 1.0) * params.occluded_alpha;
	float alpha = max(visible, hidden);

	if (alpha <= 0.0) {
		discard;
	}

	frag_color = vec4(params.color.rgb, params.color.a * alpha);
}
