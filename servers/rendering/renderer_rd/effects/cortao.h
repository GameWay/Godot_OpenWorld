/**************************************************************************/
/*  cortao.h                                                              */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "servers/rendering/renderer_rd/pipeline_deferred_rd.h"
#include "servers/rendering/renderer_rd/shaders/effects/cortao/cortao_filter.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/cortao/cortao_main.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/cortao/cortao_prepass.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/cortao/cortao_temporal.glsl.gen.h"
#include "servers/rendering/renderer_rd/storage_rd/render_buffer_custom_data_rd.h"
#include "servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h"

#define RB_SCOPE_CORTAO SNAME("rb_cortao")

namespace RendererRD {

// Per render buffers state of the CORTAO history.
class CORTAOHistoryData : public RenderBufferCustomDataRD {
	GDCLASS(CORTAOHistoryData, RenderBufferCustomDataRD);

public:
	uint32_t frame = 0;
	uint32_t current = 0; // Which of the two history textures is written this frame.
	uint64_t last_frame_number = 0;
	bool valid = false;

	virtual void configure(RenderSceneBuffersRD *p_render_buffers) override { valid = false; }
	virtual void free_data() override { valid = false; }
};

// OEngine: CORTAO (Circular Occluder Ray-Traced Ambient Occlusion), https://github.com/theagentd/CORTAO.
// Screen space ambient occlusion and bent normals traced against circular occluders, followed by a spatial
// prefilter (part of CORTAO) and a temporal accumulation pass (ours, CORTAO leaves it to the engine).
// The result goes to RB_SCOPE_SSAO / RB_FINAL as RGBA8: occlusion in R, view space bent normal in GBA (n * 0.5 + 0.5).
class CORTAO {
public:
	struct Settings {
		float radius = 1.0; // World space radius.
		float intensity = 1.0; // Exponent applied to the occlusion, 1.0 is physically based.
		float screen_radius = 0.2; // Largest radius as a fraction of the screen height.
		float slope_compensation = 4.0;
		float backface_boost = 0.0;
	};

	struct ViewData {
		Projection projection; // Including the TAA jitter, without depth correction.
		Projection prev_projection;
		Vector3 eye_offset;
	};

	CORTAO();
	~CORTAO();

	void set_quality(RSE::EnvironmentSSAOQuality p_quality);

	void process(Ref<RenderSceneBuffersRD> p_render_buffers, const RID *p_normal_roughness_slices, const ViewData *p_views, const Transform3D &p_cam_transform, const Transform3D &p_prev_cam_transform, const Settings &p_settings);

private:
	enum {
		SAMPLE_COUNT_MAX = 5, // One shader variant per RSE::EnvironmentSSAOQuality.
	};

	// Must match the structs in thirdparty/cortao/shaders.
	struct PrepassParameters {
		uint32_t packed_resolution;
		float depth_scale;
		float depth_offset;
		uint32_t random_seed;

		uint32_t mip_count;
		uint32_t pad[3];
	};

	struct MainPassParameters {
		uint32_t packed_resolution;
		uint32_t random_seed;
		float normal_bias;
		float radius;

		float unproject_params[4];
		float projection_params[4];

		float max_apparent_circle_radius_coefficient;
		float mip_bias;
		float inverse_max_slope_compensation;
		float backface_threshold;

		float backface_boost;
		uint32_t pad[3];
	};

	struct FilterPassParameters {
		uint32_t packed_resolution;
		float depth_sensitivity;
		float normal_sensitivity;
		uint32_t pad;
	};

	struct TemporalPushConstant {
		float reproject_rows[3][4];
		float unproject_params[4];
		float prev_projection[4];

		float inv_resolution[2];
		float intensity;
		uint32_t flags;

		float max_frames;
		float depth_tolerance;
		float clamp_margin;
		float pad;
	};

	enum {
		TEMPORAL_FLAG_ORTHOGONAL = 1 << 0,
		TEMPORAL_FLAG_HISTORY_VALID = 1 << 1,
	};

	RSE::EnvironmentSSAOQuality quality = RSE::ENV_SSAO_QUALITY_MEDIUM;

	CortaoPrepassShaderRD prepass_shader;
	RID prepass_shader_version;
	PipelineDeferredRD prepass_pipelines[2]; // Perspective, orthographic.

	CortaoMainShaderRD main_shader;
	RID main_shader_version;
	PipelineDeferredRD main_pipelines[SAMPLE_COUNT_MAX * 2];

	CortaoFilterShaderRD filter_shader;
	RID filter_shader_version;
	PipelineDeferredRD filter_pipelines[2]; // 5 taps, 13 taps.

	CortaoTemporalShaderRD temporal_shader;
	RID temporal_shader_version;
	PipelineDeferredRD temporal_pipeline;

	RID occluder_lookup_table;
	RID bent_normal_lookup_table;
	RID nearest_border_sampler;
	RID nearest_clamp_sampler;

	static uint32_t _get_mip_count(const Size2i &p_size);
};

} // namespace RendererRD
