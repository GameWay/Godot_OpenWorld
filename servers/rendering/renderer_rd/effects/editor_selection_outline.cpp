/**************************************************************************/
/*  editor_selection_outline.cpp                                          */
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

#include "editor_selection_outline.h"

#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

using namespace RendererRD;

EditorSelectionOutline::EditorSelectionOutline() {
	Vector<String> modes;
	modes.push_back("\n");
	shader.initialize(modes);
	shader_version = shader.version_create();
	pipeline.setup(shader.version_get_shader(shader_version, 0), RD::RENDER_PRIMITIVE_TRIANGLES, RD::PipelineRasterizationState(), RD::PipelineMultisampleState(), RD::PipelineDepthStencilState(), RD::PipelineColorBlendState::create_blend(), 0);
}

EditorSelectionOutline::~EditorSelectionOutline() {
	shader.version_free(shader_version);
}

void EditorSelectionOutline::draw(RID p_selection_depth, RID p_scene_depth, RID p_dest_framebuffer, const Size2i &p_target_size, const Params &p_params) {
	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	ERR_FAIL_NULL(uniform_set_cache);
	MaterialStorage *material_storage = MaterialStorage::get_singleton();
	ERR_FAIL_NULL(material_storage);
	ERR_FAIL_COND(p_target_size.x <= 0 || p_target_size.y <= 0);

	RID shader_rid = shader.version_get_shader(shader_version, 0);
	ERR_FAIL_COND(shader_rid.is_null());

	RID sampler = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	RD::Uniform u_selection(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ sampler, p_selection_depth }));
	RD::Uniform u_scene(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ sampler, p_scene_depth }));

	PushConstant push_constant = {};
	push_constant.color[0] = p_params.color.r;
	push_constant.color[1] = p_params.color.g;
	push_constant.color[2] = p_params.color.b;
	push_constant.color[3] = p_params.color.a;
	push_constant.target_pixel_size[0] = 1.0f / p_target_size.x;
	push_constant.target_pixel_size[1] = 1.0f / p_target_size.y;
	push_constant.width = MAX(p_params.width, 0.5f);
	push_constant.occluded_alpha = p_params.occluded_alpha;
	push_constant.depth_bias = p_params.depth_bias;

	RD::DrawListID draw_list = RD::get_singleton()->draw_list_begin(p_dest_framebuffer, RD::DRAW_DEFAULT_ALL);
	RD::get_singleton()->draw_list_bind_render_pipeline(draw_list, pipeline.get_render_pipeline(RD::INVALID_ID, RD::get_singleton()->framebuffer_get_format(p_dest_framebuffer)));
	RD::get_singleton()->draw_list_bind_uniform_set(draw_list, uniform_set_cache->get_cache(shader_rid, 0, u_selection, u_scene), 0);
	RD::get_singleton()->draw_list_set_push_constant(draw_list, &push_constant, sizeof(PushConstant));
	RD::get_singleton()->draw_list_draw(draw_list, false, 1u, 3u);
	RD::get_singleton()->draw_list_end();
}
