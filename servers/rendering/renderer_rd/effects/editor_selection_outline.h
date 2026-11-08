/**************************************************************************/
/*  editor_selection_outline.h                                            */
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

#include "servers/rendering/renderer_rd/pipeline_cache_rd.h"
#include "servers/rendering/renderer_rd/shaders/effects/editor_selection_outline.glsl.gen.h"

namespace RendererRD {

// OEngine: composites the editor selection outline from a depth mask of the selected geometry.
class EditorSelectionOutline {
public:
	struct Params {
		Color color = Color(1.0, 0.55, 0.1, 1.0);
		float width = 2.0; // In target pixels.
		float occluded_alpha = 0.35;
		float depth_bias = 0.002; // Relative.
	};

private:
	struct PushConstant {
		float color[4];
		float target_pixel_size[2];
		float width;
		float occluded_alpha;
		float depth_bias;
		float pad[3];
	};

	EditorSelectionOutlineShaderRD shader;
	RID shader_version;
	PipelineCacheRD pipeline;

public:
	void draw(RID p_selection_depth, RID p_scene_depth, RID p_dest_framebuffer, const Size2i &p_target_size, const Params &p_params);

	EditorSelectionOutline();
	~EditorSelectionOutline();
};

} // namespace RendererRD
