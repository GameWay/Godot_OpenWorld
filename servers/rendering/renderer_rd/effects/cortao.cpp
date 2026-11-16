/**************************************************************************/
/*  cortao.cpp                                                            */
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

#include "cortao.h"

#include "thirdparty/cortao/tables/CORTAOBentNormalLookupTable16Rays.h"
#include "thirdparty/cortao/tables/CORTAOOccluderLookupTable16Rays.h"

#include "core/templates/hashfuncs.h"
#include "servers/rendering/renderer_compositor.h"
#include "servers/rendering/renderer_rd/effects/ss_effects.h" // RB_SCOPE_SSAO, RB_FINAL.
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"
#include "servers/rendering/rendering_server_globals.h"

namespace RendererRD {

static const int cortao_sample_counts[] = { 4, 8, 12, 16, 24 }; // RSE::EnvironmentSSAOQuality VERY_LOW to ULTRA.

CORTAO::CORTAO() {
	static_assert(sizeof(PrepassParameters) == 32);
	static_assert(sizeof(MainPassParameters) == 80);
	static_assert(sizeof(FilterPassParameters) == 16);
	static_assert(sizeof(TemporalPushConstant) == 112);
	static_assert(sizeof(cortao_sample_counts) / sizeof(cortao_sample_counts[0]) == SAMPLE_COUNT_MAX);

	{
		Vector<String> modes;
		modes.push_back("\n#define CORTAO_PERSPECTIVE_PROJECTION 1\n");
		modes.push_back("\n#define CORTAO_ORTHOGRAPHIC_PROJECTION 1\n");
		prepass_shader.initialize(modes);
		prepass_shader_version = prepass_shader.version_create();
		for (int i = 0; i < 2; i++) {
			prepass_pipelines[i].create_compute_pipeline(prepass_shader.version_get_shader(prepass_shader_version, i));
		}
	}

	{
		Vector<String> modes;
		for (int i = 0; i < SAMPLE_COUNT_MAX; i++) {
			modes.push_back(vformat("\n#define CORTAO_SAMPLE_COUNT %d\n#define CORTAO_PERSPECTIVE_PROJECTION 1\n", cortao_sample_counts[i]));
			modes.push_back(vformat("\n#define CORTAO_SAMPLE_COUNT %d\n#define CORTAO_ORTHOGRAPHIC_PROJECTION 1\n", cortao_sample_counts[i]));
		}
		main_shader.initialize(modes);
		main_shader_version = main_shader.version_create();
		for (int i = 0; i < SAMPLE_COUNT_MAX * 2; i++) {
			main_pipelines[i].create_compute_pipeline(main_shader.version_get_shader(main_shader_version, i));
		}
	}

	{
		Vector<String> modes;
		modes.push_back("\n#define CORTAO_LARGE_FILTER 0\n");
		modes.push_back("\n#define CORTAO_LARGE_FILTER 1\n");
		filter_shader.initialize(modes);
		filter_shader_version = filter_shader.version_create();
		for (int i = 0; i < 2; i++) {
			filter_pipelines[i].create_compute_pipeline(filter_shader.version_get_shader(filter_shader_version, i));
		}
	}

	{
		Vector<String> modes;
		modes.push_back("");
		temporal_shader.initialize(modes);
		temporal_shader_version = temporal_shader.version_create();
		temporal_pipeline.create_compute_pipeline(temporal_shader.version_get_shader(temporal_shader_version, 0));
	}

	{
		// Occluder lookup table: R16_UNORM instead of R16_UINT, integer texture sampling needs SM 6.7 on D3D12.
		RD::TextureFormat tf;
		tf.format = RD::DATA_FORMAT_R16_UNORM;
		tf.texture_type = RD::TEXTURE_TYPE_3D;
		tf.width = ::CORTAO::occluderLookupTable16RaysSize[0];
		tf.height = ::CORTAO::occluderLookupTable16RaysSize[1];
		tf.depth = ::CORTAO::occluderLookupTable16RaysSize[2];
		tf.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT;

		Vector<uint8_t> data;
		data.resize(sizeof(::CORTAO::occluderLookupTable16Rays));
		memcpy(data.ptrw(), ::CORTAO::occluderLookupTable16Rays, sizeof(::CORTAO::occluderLookupTable16Rays));
		occluder_lookup_table = RD::get_singleton()->texture_create(tf, RD::TextureView(), { data });
		RD::get_singleton()->set_resource_name(occluder_lookup_table, "CORTAO occluder lookup table");
	}

	{
		// Bent normal lookup table: 2^16 R8G8_SNORM entries, as a 256x256 texture indexed by the ray bits.
		RD::TextureFormat tf;
		tf.format = RD::DATA_FORMAT_R8G8_SNORM;
		tf.width = 256;
		tf.height = 256;
		tf.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT;

		static_assert(sizeof(::CORTAO::bentNormalLookupTable16Rays) == 256 * 256 * 2);
		Vector<uint8_t> data;
		data.resize(sizeof(::CORTAO::bentNormalLookupTable16Rays));
		memcpy(data.ptrw(), ::CORTAO::bentNormalLookupTable16Rays, sizeof(::CORTAO::bentNormalLookupTable16Rays));
		bent_normal_lookup_table = RD::get_singleton()->texture_create(tf, RD::TextureView(), { data });
		RD::get_singleton()->set_resource_name(bent_normal_lookup_table, "CORTAO bent normal lookup table");
	}

	{
		RD::SamplerState sampler_state;
		sampler_state.mag_filter = RD::SAMPLER_FILTER_NEAREST;
		sampler_state.min_filter = RD::SAMPLER_FILTER_NEAREST;
		sampler_state.mip_filter = RD::SAMPLER_FILTER_NEAREST;
		sampler_state.repeat_u = RD::SAMPLER_REPEAT_MODE_CLAMP_TO_BORDER;
		sampler_state.repeat_v = RD::SAMPLER_REPEAT_MODE_CLAMP_TO_BORDER;
		sampler_state.repeat_w = RD::SAMPLER_REPEAT_MODE_CLAMP_TO_BORDER;
		sampler_state.border_color = RD::SAMPLER_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK; // Packed data of 0 = no geometry.
		nearest_border_sampler = RD::get_singleton()->sampler_create(sampler_state);

		sampler_state.repeat_u = RD::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE;
		sampler_state.repeat_v = RD::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE;
		sampler_state.repeat_w = RD::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE;
		nearest_clamp_sampler = RD::get_singleton()->sampler_create(sampler_state);
	}
}

CORTAO::~CORTAO() {
	for (int i = 0; i < 2; i++) {
		prepass_pipelines[i].free();
		filter_pipelines[i].free();
	}
	for (int i = 0; i < SAMPLE_COUNT_MAX * 2; i++) {
		main_pipelines[i].free();
	}
	temporal_pipeline.free();

	prepass_shader.version_free(prepass_shader_version);
	main_shader.version_free(main_shader_version);
	filter_shader.version_free(filter_shader_version);
	temporal_shader.version_free(temporal_shader_version);

	RD::get_singleton()->free_rid(occluder_lookup_table);
	RD::get_singleton()->free_rid(bent_normal_lookup_table);
	RD::get_singleton()->free_rid(nearest_border_sampler);
	RD::get_singleton()->free_rid(nearest_clamp_sampler);
}

void CORTAO::set_quality(RSE::EnvironmentSSAOQuality p_quality) {
	quality = RSE::EnvironmentSSAOQuality(CLAMP(int(p_quality), 0, SAMPLE_COUNT_MAX - 1));
}

uint32_t CORTAO::_get_mip_count(const Size2i &p_size) {
	// CORTAO supports up to 8 mipmaps, the smallest ones are rarely sampled. Stop around 32 pixels.
	uint32_t mips = 1;
	while (mips < 8 && (MIN(p_size.x, p_size.y) >> mips) >= 32) {
		mips++;
	}
	return mips;
}

void CORTAO::process(Ref<RenderSceneBuffersRD> p_render_buffers, const RID *p_normal_roughness_slices, const ViewData *p_views, const Transform3D &p_cam_transform, const Transform3D &p_prev_cam_transform, const Settings &p_settings) {
	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	ERR_FAIL_NULL(uniform_set_cache);
	ERR_FAIL_COND(p_render_buffers.is_null());

	const uint32_t view_count = p_render_buffers->get_view_count();
	const Size2i size = p_render_buffers->get_internal_size();
	const uint32_t mip_count = _get_mip_count(size);
	const uint32_t packed_resolution = uint32_t(size.x) | (uint32_t(size.y) << 16);

	Ref<CORTAOHistoryData> history;
	if (p_render_buffers->has_custom_data(RB_SCOPE_CORTAO)) {
		history = p_render_buffers->get_custom_data(RB_SCOPE_CORTAO);
	}
	if (history.is_null()) {
		history.instantiate();
		p_render_buffers->set_custom_data(RB_SCOPE_CORTAO, history);
	}

	// The SSAO output is RGBA8 here, while ASSAO uses R8.
	if (p_render_buffers->has_texture(RB_SCOPE_SSAO, RB_FINAL) && RD::get_singleton()->texture_get_format(p_render_buffers->get_texture(RB_SCOPE_SSAO, RB_FINAL)).format != RD::DATA_FORMAT_R8G8B8A8_UNORM) {
		p_render_buffers->clear_context(RB_SCOPE_SSAO);
	}

	// Textures are freed with the render buffers when the size or view count changes.
	if (!p_render_buffers->has_texture(RB_SCOPE_CORTAO, SNAME("packed"))) {
		const uint32_t usage = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT;
		p_render_buffers->create_texture(RB_SCOPE_CORTAO, SNAME("packed"), RD::DATA_FORMAT_R32_SFLOAT, usage, RD::TEXTURE_SAMPLES_1, Size2i(), 0, mip_count);
		p_render_buffers->create_texture(RB_SCOPE_CORTAO, SNAME("raw"), RD::DATA_FORMAT_R8G8B8A8_SNORM, usage);
		p_render_buffers->create_texture(RB_SCOPE_CORTAO, SNAME("filtered"), RD::DATA_FORMAT_R8G8B8A8_SNORM, usage);
		p_render_buffers->create_texture(RB_SCOPE_CORTAO, SNAME("history0"), RD::DATA_FORMAT_R16G16B16A16_SFLOAT, usage);
		p_render_buffers->create_texture(RB_SCOPE_CORTAO, SNAME("history1"), RD::DATA_FORMAT_R16G16B16A16_SFLOAT, usage);
		p_render_buffers->create_texture(RB_SCOPE_CORTAO, SNAME("history_depth0"), RD::DATA_FORMAT_R32G32_SFLOAT, usage);
		p_render_buffers->create_texture(RB_SCOPE_CORTAO, SNAME("history_depth1"), RD::DATA_FORMAT_R32G32_SFLOAT, usage);
		history->valid = false;
	}
	if (!p_render_buffers->has_texture(RB_SCOPE_SSAO, RB_FINAL)) {
		p_render_buffers->create_texture(RB_SCOPE_SSAO, RB_FINAL, RD::DATA_FORMAT_R8G8B8A8_UNORM, RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT);
	}

	// The history is only usable if it was written in the previous frame.
	const uint64_t frame_number = RSG::rasterizer->get_frame_number();
	const bool history_valid = history->valid && history->last_frame_number + 1 == frame_number;
	history->last_frame_number = frame_number;
	history->frame++;
	history->current ^= 1;
	history->valid = true;

	const StringName history_cur_name = history->current ? SNAME("history1") : SNAME("history0");
	const StringName history_prev_name = history->current ? SNAME("history0") : SNAME("history1");
	const StringName history_depth_cur_name = history->current ? SNAME("history_depth1") : SNAME("history_depth0");
	const StringName history_depth_prev_name = history->current ? SNAME("history_depth0") : SNAME("history_depth1");

	const uint32_t random_seed = hash_murmur3_one_32(history->frame);
	const int quality_index = int(quality);
	const bool large_filter = quality < RSE::ENV_SSAO_QUALITY_ULTRA;

	RD::get_singleton()->draw_command_begin_label("CORTAO");

	for (uint32_t v = 0; v < view_count; v++) {
		const Projection &projection = p_views[v].projection;
		const Projection &prev_projection = p_views[v].prev_projection;
		const bool orthogonal = projection.is_orthogonal();
		const Projection inv_projection = projection.inverse();

		RID packed = p_render_buffers->get_texture_slice(RB_SCOPE_CORTAO, SNAME("packed"), v, 0, 1, mip_count);
		RID raw = p_render_buffers->get_texture_slice(RB_SCOPE_CORTAO, SNAME("raw"), v, 0);
		RID filtered = p_render_buffers->get_texture_slice(RB_SCOPE_CORTAO, SNAME("filtered"), v, 0);
		RID history_cur = p_render_buffers->get_texture_slice(RB_SCOPE_CORTAO, history_cur_name, v, 0);
		RID history_prev = p_render_buffers->get_texture_slice(RB_SCOPE_CORTAO, history_prev_name, v, 0);
		RID history_depth_cur = p_render_buffers->get_texture_slice(RB_SCOPE_CORTAO, history_depth_cur_name, v, 0);
		RID history_depth_prev = p_render_buffers->get_texture_slice(RB_SCOPE_CORTAO, history_depth_prev_name, v, 0);
		RID final = p_render_buffers->get_texture_slice(RB_SCOPE_SSAO, RB_FINAL, v, 0);
		RID depth = p_render_buffers->get_depth_texture(v);

		// Unprojection from UV to view space and projection back, with Y flipped (UV 0 is the top of the screen).
		const float proj_x = projection.columns[0][0];
		const float proj_y = projection.columns[1][1];
		const float proj_offset_x = projection.columns[2][0];
		const float proj_offset_y = projection.columns[2][1];
		const float inv_x = inv_projection.columns[0][0];
		const float inv_y = inv_projection.columns[1][1];
		const float inv_offset_x = inv_projection.columns[3][0];
		const float inv_offset_y = inv_projection.columns[3][1];
		const float unproject_params[4] = { 2.0f * inv_x, -2.0f * inv_y, -inv_x + inv_offset_x, inv_y + inv_offset_y };

		RD::ComputeListID compute_list = RD::get_singleton()->compute_list_begin();

		/* Prepass */
		{
			// Linear depth from the hardware depth: d = (A * z + B) / w, with reverse Z in [0, 1].
			Projection correction;
			correction.set_depth_correction(false);
			const Projection corrected = correction * projection;
			const float a = corrected.columns[2][2];
			const float b = corrected.columns[3][2];

			PrepassParameters params = {};
			params.packed_resolution = packed_resolution;
			if (orthogonal) {
				// d = A * z + B, linear depth = -z.
				params.depth_scale = -1.0f / a;
				params.depth_offset = b / a;
			} else {
				// d = (A * z + B) / -z, 1 / linear depth = (d + A) / B.
				params.depth_scale = 1.0f / b;
				params.depth_offset = a / b;
			}
			params.random_seed = random_seed;
			params.mip_count = mip_count;

			RID shader = prepass_shader.version_get_shader(prepass_shader_version, orthogonal ? 1 : 0);
			RD::Uniform u_depth(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ nearest_clamp_sampler, depth }));
			RD::Uniform u_normal(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ nearest_clamp_sampler, p_normal_roughness_slices[v] }));

			RD::Uniform u_mips;
			u_mips.uniform_type = RD::UNIFORM_TYPE_IMAGE;
			u_mips.binding = 0;
			for (uint32_t i = 0; i < 8; i++) {
				// The shader never writes past mip_count, the remaining slots repeat the last mip.
				u_mips.append_id(p_render_buffers->get_texture_slice(RB_SCOPE_CORTAO, SNAME("packed"), v, MIN(i, mip_count - 1)));
			}

			RD::get_singleton()->compute_list_bind_compute_pipeline(compute_list, prepass_pipelines[orthogonal ? 1 : 0].get_rid());
			RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader, 0, u_depth, u_normal), 0);
			RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader, 1, u_mips), 1);
			RD::get_singleton()->compute_list_set_push_constant(compute_list, &params, sizeof(PrepassParameters));
			RD::get_singleton()->compute_list_dispatch_threads(compute_list, size.x, size.y, 1);
			RD::get_singleton()->compute_list_add_barrier(compute_list);
		}

		/* Main pass */
		{
			MainPassParameters params = {};
			params.packed_resolution = packed_resolution;
			params.random_seed = random_seed;
			params.normal_bias = inv_x / float(size.x) * Math::SQRT2;
			params.radius = MAX(p_settings.radius, 0.001f);
			for (int i = 0; i < 4; i++) {
				params.unproject_params[i] = unproject_params[i];
			}
			params.projection_params[0] = 0.5f * proj_x;
			params.projection_params[1] = -0.5f * proj_y;
			params.projection_params[2] = -0.5f + 0.5f * proj_offset_x;
			params.projection_params[3] = -0.5f - 0.5f * proj_offset_y;

			const float max_apparent_circle_radius = inv_y * CLAMP(p_settings.screen_radius, 0.01f, 1.0f) * 2.0f;
			params.max_apparent_circle_radius_coefficient = max_apparent_circle_radius / Math::sqrt(max_apparent_circle_radius * max_apparent_circle_radius + 1.0f);
			params.mip_bias = Math::log2(proj_y * float(size.y) * 0.5f) - 4.0f;
			params.inverse_max_slope_compensation = 1.0f / MAX(p_settings.slope_compensation, 1.0f);
			params.backface_threshold = 0.5f;
			params.backface_boost = CLAMP(p_settings.backface_boost, 0.0f, 1.0f);

			const int variant = quality_index * 2 + (orthogonal ? 1 : 0);
			RID shader = main_shader.version_get_shader(main_shader_version, variant);
			RD::Uniform u_packed(RD::UNIFORM_TYPE_TEXTURE, 0, packed);
			RD::Uniform u_normal(RD::UNIFORM_TYPE_TEXTURE, 1, p_normal_roughness_slices[v]);
			RD::Uniform u_occluder_lut(RD::UNIFORM_TYPE_TEXTURE, 2, occluder_lookup_table);
			RD::Uniform u_bent_normal_lut(RD::UNIFORM_TYPE_TEXTURE, 3, bent_normal_lookup_table);
			RD::Uniform u_border_sampler(RD::UNIFORM_TYPE_SAMPLER, 4, nearest_border_sampler);
			RD::Uniform u_clamp_sampler(RD::UNIFORM_TYPE_SAMPLER, 5, nearest_clamp_sampler);
			RD::Uniform u_dest(RD::UNIFORM_TYPE_IMAGE, 0, raw);

			RD::get_singleton()->compute_list_bind_compute_pipeline(compute_list, main_pipelines[variant].get_rid());
			RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader, 0, u_packed, u_normal, u_occluder_lut, u_bent_normal_lut, u_border_sampler, u_clamp_sampler), 0);
			RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader, 1, u_dest), 1);
			RD::get_singleton()->compute_list_set_push_constant(compute_list, &params, sizeof(MainPassParameters));
			RD::get_singleton()->compute_list_dispatch_threads(compute_list, size.x, size.y, 1);
			RD::get_singleton()->compute_list_add_barrier(compute_list);
		}

		/* Filter pass */
		{
			FilterPassParameters params = {};
			params.packed_resolution = packed_resolution;
			params.depth_sensitivity = 512.0f;
			params.normal_sensitivity = 128.0f;

			RID shader = filter_shader.version_get_shader(filter_shader_version, large_filter ? 1 : 0);
			RD::Uniform u_packed(RD::UNIFORM_TYPE_TEXTURE, 0, packed);
			RD::Uniform u_source(RD::UNIFORM_TYPE_TEXTURE, 1, raw);
			RD::Uniform u_border_sampler(RD::UNIFORM_TYPE_SAMPLER, 2, nearest_border_sampler);
			RD::Uniform u_dest(RD::UNIFORM_TYPE_IMAGE, 0, filtered);

			RD::get_singleton()->compute_list_bind_compute_pipeline(compute_list, filter_pipelines[large_filter ? 1 : 0].get_rid());
			RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader, 0, u_packed, u_source, u_border_sampler), 0);
			RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader, 1, u_dest), 1);
			RD::get_singleton()->compute_list_set_push_constant(compute_list, &params, sizeof(FilterPassParameters));
			// Each thread filters 2x2 pixels centered on odd coordinates.
			RD::get_singleton()->compute_list_dispatch_threads(compute_list, size.x / 2 + 1, size.y / 2 + 1, 1);
			RD::get_singleton()->compute_list_add_barrier(compute_list);
		}

		/* Temporal pass */
		{
			TemporalPushConstant push = {};

			// Current eye view space -> world -> previous eye view space.
			const Transform3D eye_offset(Basis(), p_views[v].eye_offset);
			const Transform3D reproject = (p_prev_cam_transform * eye_offset).affine_inverse() * (p_cam_transform * eye_offset);
			for (int i = 0; i < 3; i++) {
				push.reproject_rows[i][0] = reproject.basis.rows[i].x;
				push.reproject_rows[i][1] = reproject.basis.rows[i].y;
				push.reproject_rows[i][2] = reproject.basis.rows[i].z;
				push.reproject_rows[i][3] = reproject.origin[i];
			}
			for (int i = 0; i < 4; i++) {
				push.unproject_params[i] = unproject_params[i];
			}

			const bool prev_orthogonal = prev_projection.is_orthogonal();
			push.prev_projection[0] = 0.5f * prev_projection.columns[0][0];
			push.prev_projection[1] = -0.5f * prev_projection.columns[1][1];
			if (prev_orthogonal) {
				push.prev_projection[2] = 0.5f + 0.5f * prev_projection.columns[3][0];
				push.prev_projection[3] = 0.5f - 0.5f * prev_projection.columns[3][1];
			} else {
				push.prev_projection[2] = 0.5f - 0.5f * prev_projection.columns[2][0];
				push.prev_projection[3] = 0.5f + 0.5f * prev_projection.columns[2][1];
			}

			push.inv_resolution[0] = 1.0f / float(size.x);
			push.inv_resolution[1] = 1.0f / float(size.y);
			push.intensity = MAX(p_settings.intensity, 0.0f);
			push.flags = (orthogonal ? TEMPORAL_FLAG_ORTHOGONAL : 0) | ((history_valid && orthogonal == prev_orthogonal) ? TEMPORAL_FLAG_HISTORY_VALID : 0);
			push.max_frames = 16.0f;
			push.depth_tolerance = 0.05f;
			push.clamp_margin = 0.05f;

			RID shader = temporal_shader.version_get_shader(temporal_shader_version, 0);
			RD::Uniform u_packed(RD::UNIFORM_TYPE_TEXTURE, 0, packed);
			RD::Uniform u_source(RD::UNIFORM_TYPE_TEXTURE, 1, filtered);
			RD::Uniform u_history_prev(RD::UNIFORM_TYPE_TEXTURE, 2, history_prev);
			RD::Uniform u_history_depth_prev(RD::UNIFORM_TYPE_TEXTURE, 3, history_depth_prev);
			RD::Uniform u_sampler(RD::UNIFORM_TYPE_SAMPLER, 4, nearest_clamp_sampler);
			RD::Uniform u_history_cur(RD::UNIFORM_TYPE_IMAGE, 0, history_cur);
			RD::Uniform u_history_depth_cur(RD::UNIFORM_TYPE_IMAGE, 1, history_depth_cur);
			RD::Uniform u_dest(RD::UNIFORM_TYPE_IMAGE, 2, final);

			RD::get_singleton()->compute_list_bind_compute_pipeline(compute_list, temporal_pipeline.get_rid());
			RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader, 0, u_packed, u_source, u_history_prev, u_history_depth_prev, u_sampler), 0);
			RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader, 1, u_history_cur, u_history_depth_cur, u_dest), 1);
			RD::get_singleton()->compute_list_set_push_constant(compute_list, &push, sizeof(TemporalPushConstant));
			RD::get_singleton()->compute_list_dispatch_threads(compute_list, size.x, size.y, 1);
		}

		RD::get_singleton()->compute_list_end();
	}

	RD::get_singleton()->draw_command_end_label();
}

} // namespace RendererRD
