/**************************************************************************/
/*  draw_debug.cpp                                                        */
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

#include "draw_debug.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/math/face3.h"
#include "core/math/math_funcs_binary.h"
#include "core/math/projection.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/3d/camera_3d.h"
#include "scene/main/scene_tree.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"
#include "scene/resources/3d/world_3d.h"
#include "scene/resources/font.h"
#include "scene/resources/mesh.h"
#include "scene/theme/theme_db.h"
#include "servers/rendering/rendering_server.h"

#ifdef TOOLS_ENABLED
#include "editor/scene/3d/node_3d_editor_plugin.h"
#include "editor/scene/3d/node_3d_editor_viewport.h"
#endif

DrawDebug *DrawDebug::singleton = nullptr;

static constexpr double PERSISTENT_EXPIRE = Math::INF;
static constexpr int OVERLAY_CANVAS_LAYER = 1 << 20;
static constexpr int MESSAGE_FONT_SIZE = 16;
static constexpr real_t MESSAGE_MARGIN = 12.0;

#define DEFAULT_DEBUG_COLOR Color(1.0, 0.0, 0.0)

/* Emitter */

void DrawDebug::Emitter::set_color(const Color &p_color) {
	color[0] = (uint8_t)CLAMP(p_color.r * 255.0f, 0.0f, 255.0f);
	color[1] = (uint8_t)CLAMP(p_color.g * 255.0f, 0.0f, 255.0f);
	color[2] = (uint8_t)CLAMP(p_color.b * 255.0f, 0.0f, 255.0f);
	color[3] = (uint8_t)CLAMP(p_color.a * 255.0f, 0.0f, 255.0f);
}

DrawDebug::Emitter::Emitter(DrawDebug *p_owner, bool p_solid, const Color &p_color, double p_duration, bool p_depth_test) {
	owner = p_owner;
	owner->mutex.lock();
	if (!owner->enabled) {
		return;
	}

	owner->_check_stale_frames();

	BatchType type;
	if (p_solid) {
		type = p_depth_test ? BATCH_TRIANGLES : BATCH_TRIANGLES_ON_TOP;
	} else {
		type = p_depth_test ? BATCH_LINES : BATCH_LINES_ON_TOP;
	}
	batch = &owner->batches[type];
	batch->dirty = true;

	if (p_duration == 0.0) {
		target = Engine::get_singleton()->is_in_physics_frame() ? &batch->physics : &batch->frame;
	} else {
		timed = true;
		target = &batch->timed;
		range.start = target->size();
		range.expire = p_duration < 0.0 ? PERSISTENT_EXPIRE : _now() + p_duration;
	}

	set_color(p_color);
	active = true;
}

DrawDebug::Emitter::~Emitter() {
	if (active && timed) {
		range.count = target->size() - range.start;
		if (range.count > 0) {
			batch->timed_ranges.push_back(range);
		}
	}
	owner->mutex.unlock();
}

/* Lifetime management */

double DrawDebug::_now() {
	return OS::get_singleton()->get_ticks_usec() / 1000000.0;
}

void DrawDebug::_check_stale_frames() {
	// One-frame primitives that were never drawn (e.g. no draw happened because the window is minimized)
	// are dropped when the next frame starts submitting.
	const uint64_t process_frame = Engine::get_singleton()->get_process_frames();
	if (process_frame != process_frame_tag) {
		for (Batch &b : batches) {
			if (!b.frame.is_empty()) {
				b.frame.clear();
				b.dirty = true;
			}
		}
		frame_texts.clear();
		process_frame_tag = process_frame;
	}

	// Primitives drawn from a physics tick are kept until the next tick, so they don't flicker
	// when the render rate is higher than the physics rate.
	if (Engine::get_singleton()->is_in_physics_frame()) {
		const uint64_t physics_frame = Engine::get_singleton()->get_physics_frames();
		if (physics_frame != physics_frame_tag) {
			_clear_physics();
			physics_frame_tag = physics_frame;
		}
	}
}

void DrawDebug::_clear_physics() {
	for (Batch &b : batches) {
		if (!b.physics.is_empty()) {
			b.physics.clear();
			b.dirty = true;
		}
	}
	physics_texts.clear();
}

void DrawDebug::_clear_all() {
	for (Batch &b : batches) {
		b.frame.clear();
		b.physics.clear();
		b.timed.clear();
		b.timed_ranges.clear();
		b.dirty = true;
	}
	frame_texts.clear();
	physics_texts.clear();
	timed_texts.clear();
	messages.clear();
}

void DrawDebug::_remove_timed(double p_now, bool p_persistent_only) {
	auto should_remove = [p_now, p_persistent_only](double p_expire) {
		return p_persistent_only ? p_expire >= PERSISTENT_EXPIRE : p_expire <= p_now;
	};

	for (Batch &b : batches) {
		bool any = false;
		for (const TimedRange &r : b.timed_ranges) {
			if (should_remove(r.expire)) {
				any = true;
				break;
			}
		}
		if (!any) {
			continue;
		}

		// Compact in place, preserving order.
		uint32_t write = 0;
		uint32_t kept = 0;
		for (uint32_t i = 0; i < b.timed_ranges.size(); i++) {
			TimedRange r = b.timed_ranges[i];
			if (should_remove(r.expire)) {
				continue;
			}
			if (r.start != write) {
				memmove(&b.timed[write], &b.timed[r.start], sizeof(Vertex) * r.count);
				r.start = write;
			}
			write += r.count;
			b.timed_ranges[kept++] = r;
		}
		b.timed.resize(write);
		b.timed_ranges.resize(kept);
		b.dirty = true;
	}

	for (uint32_t i = 0; i < timed_texts.size();) {
		if (should_remove(timed_texts[i].expire)) {
			timed_texts.remove_at(i);
		} else {
			i++;
		}
	}

	for (uint32_t i = 0; i < messages.size();) {
		if (should_remove(messages[i].expire)) {
			messages.remove_at(i);
		} else {
			i++;
		}
	}
}

/* GPU pool */

void DrawDebug::_ensure_initialized() {
	if (initialized) {
		return;
	}
	initialized = true;

	RenderingServer *rs = RenderingServer::get_singleton();
	for (int i = 0; i < BATCH_MAX; i++) {
		Batch &b = batches[i];
		const bool on_top = i == BATCH_LINES_ON_TOP || i == BATCH_TRIANGLES_ON_TOP;

		b.material.instantiate();
		b.material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
		b.material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
		b.material->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
		b.material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
		b.material->set_flag(BaseMaterial3D::FLAG_SRGB_VERTEX_COLOR, true);
		b.material->set_flag(BaseMaterial3D::FLAG_DISABLE_FOG, true);
		b.material->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST, on_top);
		b.material->set_render_priority(on_top ? Material::RENDER_PRIORITY_MAX : Material::RENDER_PRIORITY_MAX - 1);

		b.mesh = rs->mesh_create();
		_resize_pool(b, BatchType(i), MIN_POOL_VERTICES);

		b.instance = rs->instance_create();
		rs->instance_set_base(b.instance, b.mesh);
		rs->instance_set_layer_mask(b.instance, 0xFFFFFFFF);
		rs->instance_set_ignore_culling(b.instance, true);
		rs->instance_geometry_set_cast_shadows_setting(b.instance, RSE::SHADOW_CASTING_SETTING_OFF);
		rs->instance_geometry_set_flag(b.instance, RSE::INSTANCE_FLAG_IGNORE_OCCLUSION_CULLING, true);
		rs->instance_set_visible(b.instance, false);
		b.visible = false;
	}
}

void DrawDebug::_free_gpu_resources() {
	RenderingServer *rs = RenderingServer::get_singleton();
	if (!rs) {
		return;
	}

	for (Batch &b : batches) {
		if (b.instance.is_valid()) {
			rs->free_rid(b.instance);
			b.instance = RID();
		}
		if (b.mesh.is_valid()) {
			rs->free_rid(b.mesh);
			b.mesh = RID();
		}
		b.material.unref();
		b.capacity = 0;
		b.uploaded = 0;
	}

	for (Overlay &o : overlays) {
		rs->free_rid(o.canvas_item);
		rs->free_rid(o.canvas);
	}
	overlays.clear();
	initialized = false;
}

void DrawDebug::_resize_pool(Batch &p_batch, BatchType p_type, uint32_t p_capacity) {
	RenderingServer *rs = RenderingServer::get_singleton();
	const bool triangles = p_type == BATCH_TRIANGLES || p_type == BATCH_TRIANGLES_ON_TOP;

	uint32_t capacity = Math::next_power_of_2(MAX(p_capacity, MIN_POOL_VERTICES));
	if (triangles) {
		capacity = ((capacity + 2) / 3) * 3; // Whole triangles only.
	}

	PackedVector3Array vertices;
	vertices.resize(capacity);
	vertices.fill(Vector3());
	PackedColorArray colors;
	colors.resize(capacity);
	colors.fill(Color(0, 0, 0, 0));

	Array arrays;
	arrays.resize(RSE::ARRAY_MAX);
	arrays[RSE::ARRAY_VERTEX] = vertices;
	arrays[RSE::ARRAY_COLOR] = colors;

	RenderingServerTypes::SurfaceData sd;
	const Error err = rs->mesh_create_surface_data_from_arrays(&sd, triangles ? RSE::PRIMITIVE_TRIANGLES : RSE::PRIMITIVE_LINES, arrays);
	ERR_FAIL_COND_MSG(err != OK, "DrawDebug: failed to create the vertex pool.");
	sd.material = p_batch.material->get_rid();

	rs->mesh_clear(p_batch.mesh);
	rs->mesh_add_surface(p_batch.mesh, sd);
	rs->mesh_surface_set_material(p_batch.mesh, 0, p_batch.material->get_rid());
	// Debug geometry can be anywhere, never cull it.
	rs->mesh_set_custom_aabb(p_batch.mesh, AABB(Vector3(-1e7, -1e7, -1e7), Vector3(2e7, 2e7, 2e7)));

	uint32_t offsets[RSE::ARRAY_MAX];
	uint32_t vertex_stride = 0;
	uint32_t normal_stride = 0;
	uint32_t attribute_stride = 0;
	uint32_t skin_stride = 0;
	rs->mesh_surface_make_offsets_from_format(sd.format, capacity, 0, offsets, vertex_stride, normal_stride, attribute_stride, skin_stride);

	p_batch.capacity = capacity;
	p_batch.uploaded = 0;
	p_batch.vertex_offset = offsets[RSE::ARRAY_VERTEX];
	p_batch.vertex_stride = vertex_stride;
	p_batch.color_offset = offsets[RSE::ARRAY_COLOR];
	p_batch.color_stride = attribute_stride;
	p_batch.low_usage_frames = 0;
	p_batch.dirty = true;
}

void DrawDebug::_update_batch(Batch &p_batch, BatchType p_type, RID p_scenario) {
	RenderingServer *rs = RenderingServer::get_singleton();

	if (p_scenario != p_batch.scenario) {
		p_batch.scenario = p_scenario;
		rs->instance_set_scenario(p_batch.instance, p_scenario);
	}

	uint32_t total = p_batch.frame.size() + p_batch.physics.size() + p_batch.timed.size();

	if (total == 0 || !p_scenario.is_valid()) {
		if (p_batch.visible) {
			rs->instance_set_visible(p_batch.instance, false);
			p_batch.visible = false;
		}
		if (total == 0) {
			p_batch.dirty = false;
			// Release a big pool after a long idle period.
			if (p_batch.capacity > MIN_POOL_VERTICES && ++p_batch.low_usage_frames > SHRINK_DELAY_FRAMES) {
				_resize_pool(p_batch, p_type, MIN_POOL_VERTICES);
				p_batch.dirty = false;
			}
		}
		return;
	}

	if (total > MAX_POOL_VERTICES) {
		WARN_PRINT_ONCE(vformat("DrawDebug: too many vertices in one frame (%d), the excess is not drawn.", total));
		total = MAX_POOL_VERTICES;
	}
	if (p_type == BATCH_TRIANGLES || p_type == BATCH_TRIANGLES_ON_TOP) {
		total -= total % 3;
	} else {
		total -= total % 2;
	}

	if (total > p_batch.capacity) {
		_resize_pool(p_batch, p_type, total);
	} else if (p_batch.capacity > MIN_POOL_VERTICES && total < p_batch.capacity / 4) {
		if (++p_batch.low_usage_frames > SHRINK_DELAY_FRAMES) {
			_resize_pool(p_batch, p_type, total * 2);
		}
	} else {
		p_batch.low_usage_frames = 0;
	}

	if (p_batch.dirty) {
		// Vertices past `total` that still hold old data are zeroed (zero-sized, fully transparent).
		const uint32_t write_count = MAX(total, p_batch.uploaded);
		p_batch.vertex_staging.resize(write_count * p_batch.vertex_stride);
		p_batch.attribute_staging.resize(write_count * p_batch.color_stride);
		uint8_t *vw = p_batch.vertex_staging.ptrw();
		uint8_t *aw = p_batch.attribute_staging.ptrw();

		uint32_t index = 0;
		const LocalVector<Vertex> *lists[3] = { &p_batch.frame, &p_batch.physics, &p_batch.timed };
		for (const LocalVector<Vertex> *list : lists) {
			const uint32_t count = MIN(list->size(), total - index);
			const Vertex *src = list->ptr();
			for (uint32_t i = 0; i < count; i++) {
				memcpy(vw + (index + i) * p_batch.vertex_stride + p_batch.vertex_offset, src[i].position, sizeof(float) * 3);
				memcpy(aw + (index + i) * p_batch.color_stride + p_batch.color_offset, src[i].color, 4);
			}
			index += count;
		}
		if (write_count > total) {
			memset(vw + total * p_batch.vertex_stride, 0, (write_count - total) * p_batch.vertex_stride);
			memset(aw + total * p_batch.color_stride, 0, (write_count - total) * p_batch.color_stride);
		}

		rs->mesh_surface_update_vertex_region(p_batch.mesh, 0, 0, p_batch.vertex_staging);
		rs->mesh_surface_update_attribute_region(p_batch.mesh, 0, 0, p_batch.attribute_staging);
		p_batch.uploaded = total;
		p_batch.dirty = false;
	}

	if (!p_batch.visible) {
		rs->instance_set_visible(p_batch.instance, true);
		p_batch.visible = true;
	}
}

/* Text overlay */

void DrawDebug::_collect_overlay_targets(LocalVector<Viewport *> &r_viewports, LocalVector<Camera3D *> &r_cameras) {
#ifdef TOOLS_ENABLED
	if (Engine::get_singleton()->is_editor_hint()) {
		Node3DEditor *editor = Node3DEditor::get_singleton();
		if (!editor || !editor->is_visible_in_tree()) {
			return;
		}
		for (uint32_t i = 0; i < Node3DEditor::VIEWPORTS_COUNT; i++) {
			Node3DEditorViewport *vp = editor->get_editor_viewport(i);
			if (vp && vp->is_visible_in_tree() && vp->get_viewport_node()) {
				r_viewports.push_back(vp->get_viewport_node());
				r_cameras.push_back(vp->get_camera_3d());
			}
		}
		return;
	}
#endif

	SceneTree *tree = SceneTree::get_singleton();
	if (!tree) {
		return;
	}
	Window *root = tree->get_root().ptr();
	if (root) {
		r_viewports.push_back(root);
		r_cameras.push_back(root->get_camera_3d());
	}
}

void DrawDebug::_update_overlays(double p_now) {
	RenderingServer *rs = RenderingServer::get_singleton();

	for (Overlay &o : overlays) {
		o.used = false;
	}

	const bool has_text = !frame_texts.is_empty() || !physics_texts.is_empty() || !timed_texts.is_empty() || !messages.is_empty();
	Ref<Font> font = has_text && ThemeDB::get_singleton() ? ThemeDB::get_singleton()->get_fallback_font() : Ref<Font>();

	if (font.is_valid()) {
		LocalVector<Viewport *> viewports;
		LocalVector<Camera3D *> cameras;
		_collect_overlay_targets(viewports, cameras);

		auto draw_text = [&](RID p_item, Point2 p_pos, const String &p_text, int p_size, const Color &p_color, bool p_center) {
			if (p_center) {
				p_pos.x -= font->get_string_size(p_text, HORIZONTAL_ALIGNMENT_LEFT, -1, p_size).x * 0.5;
			}
			font->draw_string_outline(p_item, p_pos, p_text, HORIZONTAL_ALIGNMENT_LEFT, -1, p_size, MAX(2, p_size / 5), Color(0, 0, 0, p_color.a));
			font->draw_string(p_item, p_pos, p_text, HORIZONTAL_ALIGNMENT_LEFT, -1, p_size, p_color);
		};

		for (uint32_t vi = 0; vi < viewports.size(); vi++) {
			Viewport *viewport = viewports[vi];
			const ObjectID id = viewport->get_instance_id();

			Overlay *overlay = nullptr;
			for (Overlay &o : overlays) {
				if (o.viewport == id) {
					overlay = &o;
					break;
				}
			}
			if (!overlay) {
				Overlay o;
				o.viewport = id;
				o.viewport_rid = viewport->get_viewport_rid();
				o.canvas = rs->canvas_create();
				o.canvas_item = rs->canvas_item_create();
				rs->canvas_item_set_parent(o.canvas_item, o.canvas);
				rs->viewport_attach_canvas(o.viewport_rid, o.canvas);
				rs->viewport_set_canvas_stacking(o.viewport_rid, o.canvas, OVERLAY_CANVAS_LAYER, 0);
				overlays.push_back(o);
				overlay = &overlays[overlays.size() - 1];
			}

			overlay->used = true;
			overlay->has_content = true;
			rs->canvas_item_clear(overlay->canvas_item);

			Camera3D *camera = cameras[vi];
			if (camera && camera->is_inside_tree()) {
				const LocalVector<Text> *lists[3] = { &frame_texts, &physics_texts, &timed_texts };
				for (const LocalVector<Text> *list : lists) {
					for (const Text &t : *list) {
						if (camera->is_position_behind(t.position)) {
							continue;
						}
						draw_text(overlay->canvas_item, camera->unproject_position(t.position), t.text, t.font_size, t.color, true);
					}
				}
			}

			// On-screen messages only go to the first (main) viewport.
			if (vi == 0 && !messages.is_empty()) {
				const real_t line_height = font->get_height(MESSAGE_FONT_SIZE) + 2.0;
				Point2 pos(MESSAGE_MARGIN, MESSAGE_MARGIN + font->get_ascent(MESSAGE_FONT_SIZE));
				for (const Message &m : messages) {
					draw_text(overlay->canvas_item, pos, m.text, MESSAGE_FONT_SIZE, m.color, false);
					pos.y += line_height;
				}
			}
		}
	}

	// Clear unused overlays (they are kept for reuse) and free those whose viewport is gone.
	for (uint32_t i = 0; i < overlays.size();) {
		Overlay &o = overlays[i];
		if (!ObjectDB::get_instance(o.viewport)) {
			rs->free_rid(o.canvas_item);
			rs->free_rid(o.canvas);
			overlays.remove_at_unordered(i);
			continue;
		}
		if (!o.used && o.has_content) {
			rs->canvas_item_clear(o.canvas_item);
			o.has_content = false;
		}
		i++;
	}
}

void DrawDebug::_on_frame_pre_draw() {
	MutexLock lock(mutex);

	if (!enabled) {
		return;
	}

	_ensure_initialized();

	RID scenario;
	SceneTree *tree = SceneTree::get_singleton();
	if (tree) {
		Window *root = tree->get_root().ptr();
		if (root) {
			Ref<World3D> world = root->find_world_3d();
			if (world.is_valid()) {
				scenario = world->get_scenario();
			}
		}
	}

	// No physics tick submitted anything since the last one: its primitives are outdated.
	const uint64_t physics_frame = Engine::get_singleton()->get_physics_frames();
	if (physics_frame != physics_frame_tag) {
		_clear_physics();
		physics_frame_tag = physics_frame;
	}

	const double now = _now();

	for (int i = 0; i < BATCH_MAX; i++) {
		_update_batch(batches[i], BatchType(i), scenario);
	}
	_update_overlays(now);

	// One-frame primitives have been submitted for drawing.
	for (Batch &b : batches) {
		if (!b.frame.is_empty()) {
			b.frame.clear();
			b.dirty = true;
		}
	}
	frame_texts.clear();

	_remove_timed(now, false);
}

/* Geometry helpers */

void DrawDebug::_axis_basis(const Vector3 &p_axis, Vector3 &r_u, Vector3 &r_v) {
	Vector3 n = p_axis.normalized();
	if (n.is_zero_approx()) {
		n = Vector3(0, 1, 0);
	}
	r_u = n.get_any_perpendicular().normalized();
	r_v = n.cross(r_u).normalized();
}

void DrawDebug::_emit_arc(Emitter &p_e, const Vector3 &p_center, const Vector3 &p_u, const Vector3 &p_v, real_t p_radius, real_t p_from, real_t p_to, int p_segments) {
	const real_t step = (p_to - p_from) / p_segments;
	Vector3 prev = p_center + (p_u * Math::cos(p_from) + p_v * Math::sin(p_from)) * p_radius;
	for (int i = 1; i <= p_segments; i++) {
		const real_t a = p_from + step * i;
		const Vector3 p = p_center + (p_u * Math::cos(a) + p_v * Math::sin(a)) * p_radius;
		p_e.line(prev, p);
		prev = p;
	}
}

void DrawDebug::_emit_disc(Emitter &p_e, const Vector3 &p_center, const Vector3 &p_u, const Vector3 &p_v, real_t p_radius, int p_segments) {
	const real_t step = Math::TAU / p_segments;
	Vector3 prev = p_center + p_u * p_radius;
	for (int i = 1; i <= p_segments; i++) {
		const real_t a = step * i;
		const Vector3 p = p_center + (p_u * Math::cos(a) + p_v * Math::sin(a)) * p_radius;
		p_e.triangle(p_center, prev, p);
		prev = p;
	}
}

void DrawDebug::_box_corners(const Transform3D &p_xform, const Vector3 &p_half_extents, Vector3 *r_corners) {
	for (int i = 0; i < 8; i++) {
		const Vector3 local((i & 1) ? p_half_extents.x : -p_half_extents.x, (i & 2) ? p_half_extents.y : -p_half_extents.y, (i & 4) ? p_half_extents.z : -p_half_extents.z);
		r_corners[i] = p_xform.xform(local);
	}
}

void DrawDebug::_emit_box(Emitter &p_e, const Vector3 *p_corners, bool p_solid) {
	if (!p_solid) {
		for (int i = 0; i < 8; i++) {
			for (int bit = 1; bit < 8; bit <<= 1) {
				if (!(i & bit)) {
					p_e.line(p_corners[i], p_corners[i | bit]);
				}
			}
		}
		return;
	}

	for (int axis = 0; axis < 3; axis++) {
		const int bit = 1 << axis;
		const int b1 = 1 << ((axis + 1) % 3);
		const int b2 = 1 << ((axis + 2) % 3);
		for (int side = 0; side < 2; side++) {
			const int s = side ? bit : 0;
			p_e.quad(p_corners[s], p_corners[s | b1], p_corners[s | b1 | b2], p_corners[s | b2]);
		}
	}
}

void DrawDebug::_emit_sphere(Emitter &p_e, const Vector3 &p_center, real_t p_radius, int p_segments, bool p_solid) {
	const int segments = MAX(4, p_segments);
	const int rings = MAX(2, segments / 2);

	auto point = [&](int p_ring, int p_segment) {
		const real_t theta = Math::PI * p_ring / rings;
		const real_t phi = Math::TAU * p_segment / segments;
		const real_t r = Math::sin(theta);
		return p_center + Vector3(r * Math::cos(phi), Math::cos(theta), r * Math::sin(phi)) * p_radius;
	};

	if (p_solid) {
		for (int j = 0; j < rings; j++) {
			for (int i = 0; i < segments; i++) {
				p_e.quad(point(j, i), point(j + 1, i), point(j + 1, i + 1), point(j, i + 1));
			}
		}
		return;
	}

	// Latitudes.
	for (int j = 1; j < rings; j++) {
		for (int i = 0; i < segments; i++) {
			p_e.line(point(j, i), point(j, i + 1));
		}
	}
	// Meridians.
	for (int i = 0; i < segments; i++) {
		for (int j = 0; j < rings; j++) {
			p_e.line(point(j, i), point(j + 1, i));
		}
	}
}

void DrawDebug::_emit_arrow_head(Emitter &p_e, const Vector3 &p_from, const Vector3 &p_to, real_t p_size) {
	const Vector3 dir = p_to - p_from;
	const real_t length = dir.length();
	if (length <= CMP_EPSILON || p_size <= 0) {
		return;
	}
	const Vector3 d = dir / length;
	Vector3 u, v;
	_axis_basis(d, u, v);
	const Vector3 base = p_to - d * p_size;
	const real_t spread = p_size * 0.5;
	p_e.line(p_to, base + u * spread);
	p_e.line(p_to, base - u * spread);
	p_e.line(p_to, base + v * spread);
	p_e.line(p_to, base - v * spread);
}

void DrawDebug::_emit_plane(Emitter &p_e, const Plane &p_plane, const Vector3 &p_center, real_t p_size, bool p_solid) {
	const Vector3 n = p_plane.normal.normalized();
	if (n.is_zero_approx()) {
		return;
	}
	const Vector3 c = p_plane.project(p_center);
	Vector3 u, v;
	_axis_basis(n, u, v);
	const real_t h = p_size * 0.5;
	const Vector3 a = c + (u + v) * h;
	const Vector3 b = c + (u - v) * h;
	const Vector3 d = c + (-u - v) * h;
	const Vector3 e = c + (-u + v) * h;

	if (p_solid) {
		p_e.quad(a, b, d, e);
		return;
	}

	p_e.line(a, b);
	p_e.line(b, d);
	p_e.line(d, e);
	p_e.line(e, a);
	p_e.line(a, d);
	p_e.line(b, e);
	// Normal.
	const Vector3 tip = c + n * h;
	p_e.line(c, tip);
	_emit_arrow_head(p_e, c, tip, h * 0.2);
}

void DrawDebug::_emit_mesh(Emitter &p_e, const Ref<Mesh> &p_mesh, const Transform3D &p_transform, bool p_solid) {
	const Vector<Face3> faces = p_mesh->get_faces();
	for (const Face3 &f : faces) {
		const Vector3 a = p_transform.xform(f.vertex[0]);
		const Vector3 b = p_transform.xform(f.vertex[1]);
		const Vector3 c = p_transform.xform(f.vertex[2]);
		if (p_solid) {
			p_e.triangle(a, b, c);
		} else {
			p_e.line(a, b);
			p_e.line(b, c);
			p_e.line(c, a);
		}
	}
}

/* Public API */

void DrawDebug::set_enabled(bool p_enabled) {
	MutexLock lock(mutex);
	if (enabled == p_enabled) {
		return;
	}
	enabled = p_enabled;
	if (!enabled) {
		_clear_all();
		RenderingServer *rs = RenderingServer::get_singleton();
		for (Batch &b : batches) {
			if (b.visible) {
				rs->instance_set_visible(b.instance, false);
				b.visible = false;
			}
		}
		for (Overlay &o : overlays) {
			if (o.has_content) {
				rs->canvas_item_clear(o.canvas_item);
				o.has_content = false;
			}
		}
	}
}

bool DrawDebug::is_enabled() const {
	MutexLock lock(mutex);
	return enabled;
}

void DrawDebug::line(const Vector3 &p_from, const Vector3 &p_to, const Color &p_color, double p_duration, bool p_depth_test) {
	Emitter e(this, false, p_color, p_duration, p_depth_test);
	if (e.is_active()) {
		e.line(p_from, p_to);
	}
}

void DrawDebug::lines(const PackedVector3Array &p_points, const Color &p_color, double p_duration, bool p_depth_test) {
	ERR_FAIL_COND_MSG(p_points.size() % 2 != 0, "DrawDebug.lines() expects pairs of points.");
	Emitter e(this, false, p_color, p_duration, p_depth_test);
	if (e.is_active()) {
		const Vector3 *r = p_points.ptr();
		for (int i = 0; i + 1 < p_points.size(); i += 2) {
			e.line(r[i], r[i + 1]);
		}
	}
}

void DrawDebug::polyline(const PackedVector3Array &p_points, const Color &p_color, double p_duration, bool p_depth_test, bool p_closed) {
	if (p_points.size() < 2) {
		return;
	}
	Emitter e(this, false, p_color, p_duration, p_depth_test);
	if (e.is_active()) {
		const Vector3 *r = p_points.ptr();
		for (int i = 0; i + 1 < p_points.size(); i++) {
			e.line(r[i], r[i + 1]);
		}
		if (p_closed && p_points.size() > 2) {
			e.line(r[p_points.size() - 1], r[0]);
		}
	}
}

void DrawDebug::arrow(const Vector3 &p_from, const Vector3 &p_to, const Color &p_color, double p_duration, bool p_depth_test, real_t p_head_size) {
	Emitter e(this, false, p_color, p_duration, p_depth_test);
	if (e.is_active()) {
		e.line(p_from, p_to);
		const real_t head = p_head_size > 0 ? p_head_size : p_from.distance_to(p_to) * 0.2;
		_emit_arrow_head(e, p_from, p_to, head);
	}
}

void DrawDebug::ray(const Vector3 &p_origin, const Vector3 &p_direction, const Color &p_color, double p_duration, bool p_depth_test) {
	line(p_origin, p_origin + p_direction, p_color, p_duration, p_depth_test);
}

void DrawDebug::point(const Vector3 &p_position, real_t p_size, const Color &p_color, double p_duration, bool p_depth_test) {
	Emitter e(this, true, p_color, p_duration, p_depth_test);
	if (e.is_active()) {
		Vector3 corners[8];
		_box_corners(Transform3D(Basis(), p_position), Vector3(p_size, p_size, p_size) * 0.5, corners);
		_emit_box(e, corners, true);
	}
}

void DrawDebug::crosshair(const Vector3 &p_position, real_t p_size, const Color &p_color, double p_duration, bool p_depth_test) {
	Emitter e(this, false, p_color, p_duration, p_depth_test);
	if (e.is_active()) {
		const real_t h = p_size * 0.5;
		e.line(p_position - Vector3(h, 0, 0), p_position + Vector3(h, 0, 0));
		e.line(p_position - Vector3(0, h, 0), p_position + Vector3(0, h, 0));
		e.line(p_position - Vector3(0, 0, h), p_position + Vector3(0, 0, h));
	}
}

void DrawDebug::coordinate_system(const Transform3D &p_transform, real_t p_scale, double p_duration, bool p_depth_test) {
	static const Color axis_colors[3] = { Color(1.0, 0.2, 0.2), Color(0.2, 1.0, 0.2), Color(0.2, 0.4, 1.0) };
	Emitter e(this, false, Color(), p_duration, p_depth_test);
	if (e.is_active()) {
		for (int i = 0; i < 3; i++) {
			e.set_color(axis_colors[i]);
			const Vector3 tip = p_transform.origin + p_transform.basis.get_column(i) * p_scale;
			e.line(p_transform.origin, tip);
			_emit_arrow_head(e, p_transform.origin, tip, p_transform.origin.distance_to(tip) * 0.15);
		}
	}
}

void DrawDebug::box(const Vector3 &p_center, const Vector3 &p_half_extents, const Color &p_color, double p_duration, bool p_depth_test) {
	box_oriented(Transform3D(Basis(), p_center), p_half_extents, p_color, p_duration, p_depth_test);
}

void DrawDebug::box_oriented(const Transform3D &p_transform, const Vector3 &p_half_extents, const Color &p_color, double p_duration, bool p_depth_test) {
	Emitter e(this, false, p_color, p_duration, p_depth_test);
	if (e.is_active()) {
		Vector3 corners[8];
		_box_corners(p_transform, p_half_extents, corners);
		_emit_box(e, corners, false);
	}
}

void DrawDebug::aabb(const AABB &p_aabb, const Color &p_color, double p_duration, bool p_depth_test) {
	box(p_aabb.get_center(), p_aabb.size.abs() * 0.5, p_color, p_duration, p_depth_test);
}

void DrawDebug::sphere(const Vector3 &p_center, real_t p_radius, const Color &p_color, double p_duration, bool p_depth_test, int p_segments) {
	Emitter e(this, false, p_color, p_duration, p_depth_test);
	if (e.is_active()) {
		_emit_sphere(e, p_center, p_radius, p_segments, false);
	}
}

void DrawDebug::circle(const Vector3 &p_center, real_t p_radius, const Vector3 &p_normal, const Color &p_color, double p_duration, bool p_depth_test, int p_segments) {
	Emitter e(this, false, p_color, p_duration, p_depth_test);
	if (e.is_active()) {
		Vector3 u, v;
		_axis_basis(p_normal, u, v);
		_emit_arc(e, p_center, u, v, p_radius, 0, Math::TAU, MAX(3, p_segments));
	}
}

void DrawDebug::cylinder(const Vector3 &p_from, const Vector3 &p_to, real_t p_radius, const Color &p_color, double p_duration, bool p_depth_test, int p_segments) {
	Emitter e(this, false, p_color, p_duration, p_depth_test);
	if (e.is_active()) {
		const int segments = MAX(3, p_segments);
		Vector3 u, v;
		_axis_basis(p_to - p_from, u, v);
		_emit_arc(e, p_from, u, v, p_radius, 0, Math::TAU, segments);
		_emit_arc(e, p_to, u, v, p_radius, 0, Math::TAU, segments);
		const real_t step = Math::TAU / segments;
		for (int i = 0; i < segments; i++) {
			const Vector3 offset = (u * Math::cos(step * i) + v * Math::sin(step * i)) * p_radius;
			e.line(p_from + offset, p_to + offset);
		}
	}
}

void DrawDebug::cone(const Vector3 &p_apex, const Vector3 &p_direction, real_t p_length, real_t p_angle, const Color &p_color, double p_duration, bool p_depth_test, int p_segments) {
	Emitter e(this, false, p_color, p_duration, p_depth_test);
	if (e.is_active()) {
		const int segments = MAX(3, p_segments);
		Vector3 d = p_direction.normalized();
		if (d.is_zero_approx()) {
			d = Vector3(0, -1, 0);
		}
		Vector3 u, v;
		_axis_basis(d, u, v);
		const real_t angle = CLAMP(p_angle, (real_t)0.001, (real_t)(Math::PI * 0.499));
		const Vector3 base = p_apex + d * p_length;
		const real_t radius = p_length * Math::tan(angle);
		_emit_arc(e, base, u, v, radius, 0, Math::TAU, segments);
		const real_t step = Math::TAU / segments;
		for (int i = 0; i < segments; i++) {
			e.line(p_apex, base + (u * Math::cos(step * i) + v * Math::sin(step * i)) * radius);
		}
	}
}

void DrawDebug::capsule(const Vector3 &p_from, const Vector3 &p_to, real_t p_radius, const Color &p_color, double p_duration, bool p_depth_test, int p_segments) {
	Emitter e(this, false, p_color, p_duration, p_depth_test);
	if (!e.is_active()) {
		return;
	}
	const int segments = MAX(4, p_segments);
	const Vector3 axis = p_to - p_from;
	if (axis.is_zero_approx()) {
		_emit_sphere(e, p_from, p_radius, segments, false);
		return;
	}
	const Vector3 d = axis.normalized();
	Vector3 u, v;
	_axis_basis(d, u, v);

	_emit_arc(e, p_from, u, v, p_radius, 0, Math::TAU, segments);
	_emit_arc(e, p_to, u, v, p_radius, 0, Math::TAU, segments);
	e.line(p_from + u * p_radius, p_to + u * p_radius);
	e.line(p_from - u * p_radius, p_to - u * p_radius);
	e.line(p_from + v * p_radius, p_to + v * p_radius);
	e.line(p_from - v * p_radius, p_to - v * p_radius);

	const int half = MAX(2, segments / 2);
	_emit_arc(e, p_to, u, d, p_radius, 0, Math::PI, half);
	_emit_arc(e, p_to, v, d, p_radius, 0, Math::PI, half);
	_emit_arc(e, p_from, u, -d, p_radius, 0, Math::PI, half);
	_emit_arc(e, p_from, v, -d, p_radius, 0, Math::PI, half);
}

void DrawDebug::plane(const Plane &p_plane, const Vector3 &p_center, real_t p_size, const Color &p_color, double p_duration, bool p_depth_test) {
	Emitter e(this, false, p_color, p_duration, p_depth_test);
	if (e.is_active()) {
		_emit_plane(e, p_plane, p_center, p_size, false);
	}
}

void DrawDebug::frustum(const Projection &p_projection, const Transform3D &p_transform, const Color &p_color, double p_duration, bool p_depth_test) {
	Vector3 p[8];
	if (!p_projection.get_endpoints(p_transform, p)) {
		return;
	}
	Emitter e(this, false, p_color, p_duration, p_depth_test);
	if (e.is_active()) {
		// 0..3 far plane, 4..7 near plane (left-top, left-bottom, right-top, right-bottom).
		for (int base = 0; base <= 4; base += 4) {
			e.line(p[base + 0], p[base + 1]);
			e.line(p[base + 1], p[base + 3]);
			e.line(p[base + 3], p[base + 2]);
			e.line(p[base + 2], p[base + 0]);
		}
		for (int i = 0; i < 4; i++) {
			e.line(p[i], p[i + 4]);
		}
	}
}

void DrawDebug::camera_frustum(Camera3D *p_camera, const Color &p_color, double p_duration, bool p_depth_test) {
	ERR_FAIL_NULL(p_camera);
	ERR_FAIL_COND(!p_camera->is_inside_tree());
	frustum(p_camera->get_camera_projection(), p_camera->get_camera_transform(), p_color, p_duration, p_depth_test);
}

void DrawDebug::mesh(const Ref<Mesh> &p_mesh, const Transform3D &p_transform, const Color &p_color, double p_duration, bool p_depth_test) {
	ERR_FAIL_COND(p_mesh.is_null());
	Emitter e(this, false, p_color, p_duration, p_depth_test);
	if (e.is_active()) {
		_emit_mesh(e, p_mesh, p_transform, false);
	}
}

void DrawDebug::grid(const Transform3D &p_transform, int p_cells, real_t p_cell_size, const Color &p_color, double p_duration, bool p_depth_test) {
	ERR_FAIL_COND(p_cells <= 0);
	Emitter e(this, false, p_color, p_duration, p_depth_test);
	if (e.is_active()) {
		const real_t half = p_cells * p_cell_size * 0.5;
		for (int i = 0; i <= p_cells; i++) {
			const real_t o = -half + i * p_cell_size;
			e.line(p_transform.xform(Vector3(o, 0, -half)), p_transform.xform(Vector3(o, 0, half)));
			e.line(p_transform.xform(Vector3(-half, 0, o)), p_transform.xform(Vector3(half, 0, o)));
		}
	}
}

void DrawDebug::solid_triangle(const Vector3 &p_a, const Vector3 &p_b, const Vector3 &p_c, const Color &p_color, double p_duration, bool p_depth_test) {
	Emitter e(this, true, p_color, p_duration, p_depth_test);
	if (e.is_active()) {
		e.triangle(p_a, p_b, p_c);
	}
}

void DrawDebug::solid_box(const Vector3 &p_center, const Vector3 &p_half_extents, const Color &p_color, double p_duration, bool p_depth_test) {
	solid_box_oriented(Transform3D(Basis(), p_center), p_half_extents, p_color, p_duration, p_depth_test);
}

void DrawDebug::solid_box_oriented(const Transform3D &p_transform, const Vector3 &p_half_extents, const Color &p_color, double p_duration, bool p_depth_test) {
	Emitter e(this, true, p_color, p_duration, p_depth_test);
	if (e.is_active()) {
		Vector3 corners[8];
		_box_corners(p_transform, p_half_extents, corners);
		_emit_box(e, corners, true);
	}
}

void DrawDebug::solid_aabb(const AABB &p_aabb, const Color &p_color, double p_duration, bool p_depth_test) {
	solid_box(p_aabb.get_center(), p_aabb.size.abs() * 0.5, p_color, p_duration, p_depth_test);
}

void DrawDebug::solid_sphere(const Vector3 &p_center, real_t p_radius, const Color &p_color, double p_duration, bool p_depth_test, int p_segments) {
	Emitter e(this, true, p_color, p_duration, p_depth_test);
	if (e.is_active()) {
		_emit_sphere(e, p_center, p_radius, p_segments, true);
	}
}

void DrawDebug::solid_circle(const Vector3 &p_center, real_t p_radius, const Vector3 &p_normal, const Color &p_color, double p_duration, bool p_depth_test, int p_segments) {
	Emitter e(this, true, p_color, p_duration, p_depth_test);
	if (e.is_active()) {
		Vector3 u, v;
		_axis_basis(p_normal, u, v);
		_emit_disc(e, p_center, u, v, p_radius, MAX(3, p_segments));
	}
}

void DrawDebug::solid_plane(const Plane &p_plane, const Vector3 &p_center, real_t p_size, const Color &p_color, double p_duration, bool p_depth_test) {
	Emitter e(this, true, p_color, p_duration, p_depth_test);
	if (e.is_active()) {
		_emit_plane(e, p_plane, p_center, p_size, true);
	}
}

void DrawDebug::solid_mesh(const Ref<Mesh> &p_mesh, const Transform3D &p_transform, const Color &p_color, double p_duration, bool p_depth_test) {
	ERR_FAIL_COND(p_mesh.is_null());
	Emitter e(this, true, p_color, p_duration, p_depth_test);
	if (e.is_active()) {
		_emit_mesh(e, p_mesh, p_transform, true);
	}
}

void DrawDebug::text(const Vector3 &p_position, const String &p_text, const Color &p_color, double p_duration, int p_font_size) {
	MutexLock lock(mutex);
	if (!enabled) {
		return;
	}
	_check_stale_frames();

	Text t;
	t.position = p_position;
	t.text = p_text;
	t.color = p_color;
	t.font_size = MAX(1, p_font_size);

	if (p_duration == 0.0) {
		if (Engine::get_singleton()->is_in_physics_frame()) {
			physics_texts.push_back(t);
		} else {
			frame_texts.push_back(t);
		}
	} else {
		t.expire = p_duration < 0.0 ? PERSISTENT_EXPIRE : _now() + p_duration;
		timed_texts.push_back(t);
	}
}

void DrawDebug::message(const String &p_text, const Color &p_color, double p_duration, int p_key) {
	MutexLock lock(mutex);
	if (!enabled) {
		return;
	}

	// A zero duration still shows the message for one drawn frame.
	const double expire = p_duration < 0.0 ? PERSISTENT_EXPIRE : _now() + p_duration;

	if (p_key >= 0) {
		for (Message &m : messages) {
			if (m.key == p_key) {
				m.text = p_text;
				m.color = p_color;
				m.expire = expire;
				return;
			}
		}
	}

	Message m;
	m.key = p_key;
	m.text = p_text;
	m.color = p_color;
	m.expire = expire;
	messages.push_back(m);
}

void DrawDebug::clear() {
	MutexLock lock(mutex);
	_clear_all();
}

void DrawDebug::clear_persistent() {
	MutexLock lock(mutex);
	_remove_timed(_now(), true);
}

int DrawDebug::get_vertex_count() const {
	MutexLock lock(mutex);
	int count = 0;
	for (const Batch &b : batches) {
		count += b.uploaded;
	}
	return count;
}

int DrawDebug::get_pool_capacity() const {
	MutexLock lock(mutex);
	int count = 0;
	for (const Batch &b : batches) {
		count += b.capacity;
	}
	return count;
}

/* Binding */

void DrawDebug::register_project_settings() {
	GLOBAL_DEF(PropertyInfo(Variant::BOOL, "debug/draw_debug/enabled_in_release_builds"), false);
}

void DrawDebug::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &DrawDebug::set_enabled);
	ClassDB::bind_method(D_METHOD("is_enabled"), &DrawDebug::is_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");

	ClassDB::bind_method(D_METHOD("line", "from", "to", "color", "duration", "depth_test"), &DrawDebug::line, DEFVAL(DEFAULT_DEBUG_COLOR), DEFVAL(0.0), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("lines", "points", "color", "duration", "depth_test"), &DrawDebug::lines, DEFVAL(DEFAULT_DEBUG_COLOR), DEFVAL(0.0), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("polyline", "points", "color", "duration", "depth_test", "closed"), &DrawDebug::polyline, DEFVAL(DEFAULT_DEBUG_COLOR), DEFVAL(0.0), DEFVAL(true), DEFVAL(false));
	ClassDB::bind_method(D_METHOD("arrow", "from", "to", "color", "duration", "depth_test", "head_size"), &DrawDebug::arrow, DEFVAL(DEFAULT_DEBUG_COLOR), DEFVAL(0.0), DEFVAL(true), DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("ray", "origin", "direction", "color", "duration", "depth_test"), &DrawDebug::ray, DEFVAL(DEFAULT_DEBUG_COLOR), DEFVAL(0.0), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("point", "position", "size", "color", "duration", "depth_test"), &DrawDebug::point, DEFVAL(0.1), DEFVAL(DEFAULT_DEBUG_COLOR), DEFVAL(0.0), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("crosshair", "position", "size", "color", "duration", "depth_test"), &DrawDebug::crosshair, DEFVAL(0.5), DEFVAL(DEFAULT_DEBUG_COLOR), DEFVAL(0.0), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("coordinate_system", "transform", "scale", "duration", "depth_test"), &DrawDebug::coordinate_system, DEFVAL(1.0), DEFVAL(0.0), DEFVAL(true));

	ClassDB::bind_method(D_METHOD("box", "center", "half_extents", "color", "duration", "depth_test"), &DrawDebug::box, DEFVAL(DEFAULT_DEBUG_COLOR), DEFVAL(0.0), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("box_oriented", "transform", "half_extents", "color", "duration", "depth_test"), &DrawDebug::box_oriented, DEFVAL(DEFAULT_DEBUG_COLOR), DEFVAL(0.0), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("aabb", "aabb", "color", "duration", "depth_test"), &DrawDebug::aabb, DEFVAL(DEFAULT_DEBUG_COLOR), DEFVAL(0.0), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("sphere", "center", "radius", "color", "duration", "depth_test", "segments"), &DrawDebug::sphere, DEFVAL(DEFAULT_DEBUG_COLOR), DEFVAL(0.0), DEFVAL(true), DEFVAL(16));
	ClassDB::bind_method(D_METHOD("circle", "center", "radius", "normal", "color", "duration", "depth_test", "segments"), &DrawDebug::circle, DEFVAL(Vector3(0, 1, 0)), DEFVAL(DEFAULT_DEBUG_COLOR), DEFVAL(0.0), DEFVAL(true), DEFVAL(32));
	ClassDB::bind_method(D_METHOD("cylinder", "from", "to", "radius", "color", "duration", "depth_test", "segments"), &DrawDebug::cylinder, DEFVAL(DEFAULT_DEBUG_COLOR), DEFVAL(0.0), DEFVAL(true), DEFVAL(16));
	ClassDB::bind_method(D_METHOD("cone", "apex", "direction", "length", "angle", "color", "duration", "depth_test", "segments"), &DrawDebug::cone, DEFVAL(Math::PI / 6.0), DEFVAL(DEFAULT_DEBUG_COLOR), DEFVAL(0.0), DEFVAL(true), DEFVAL(16));
	ClassDB::bind_method(D_METHOD("capsule", "from", "to", "radius", "color", "duration", "depth_test", "segments"), &DrawDebug::capsule, DEFVAL(DEFAULT_DEBUG_COLOR), DEFVAL(0.0), DEFVAL(true), DEFVAL(16));
	ClassDB::bind_method(D_METHOD("plane", "plane", "center", "size", "color", "duration", "depth_test"), &DrawDebug::plane, DEFVAL(Vector3()), DEFVAL(2.0), DEFVAL(DEFAULT_DEBUG_COLOR), DEFVAL(0.0), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("frustum", "projection", "transform", "color", "duration", "depth_test"), &DrawDebug::frustum, DEFVAL(DEFAULT_DEBUG_COLOR), DEFVAL(0.0), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("camera_frustum", "camera", "color", "duration", "depth_test"), &DrawDebug::camera_frustum, DEFVAL(DEFAULT_DEBUG_COLOR), DEFVAL(0.0), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("mesh", "mesh", "transform", "color", "duration", "depth_test"), &DrawDebug::mesh, DEFVAL(DEFAULT_DEBUG_COLOR), DEFVAL(0.0), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("grid", "transform", "cells", "cell_size", "color", "duration", "depth_test"), &DrawDebug::grid, DEFVAL(10), DEFVAL(1.0), DEFVAL(Color(0.5, 0.5, 0.5)), DEFVAL(0.0), DEFVAL(true));

	ClassDB::bind_method(D_METHOD("solid_triangle", "a", "b", "c", "color", "duration", "depth_test"), &DrawDebug::solid_triangle, DEFVAL(DEFAULT_DEBUG_COLOR), DEFVAL(0.0), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("solid_box", "center", "half_extents", "color", "duration", "depth_test"), &DrawDebug::solid_box, DEFVAL(DEFAULT_DEBUG_COLOR), DEFVAL(0.0), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("solid_box_oriented", "transform", "half_extents", "color", "duration", "depth_test"), &DrawDebug::solid_box_oriented, DEFVAL(DEFAULT_DEBUG_COLOR), DEFVAL(0.0), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("solid_aabb", "aabb", "color", "duration", "depth_test"), &DrawDebug::solid_aabb, DEFVAL(DEFAULT_DEBUG_COLOR), DEFVAL(0.0), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("solid_sphere", "center", "radius", "color", "duration", "depth_test", "segments"), &DrawDebug::solid_sphere, DEFVAL(DEFAULT_DEBUG_COLOR), DEFVAL(0.0), DEFVAL(true), DEFVAL(16));
	ClassDB::bind_method(D_METHOD("solid_circle", "center", "radius", "normal", "color", "duration", "depth_test", "segments"), &DrawDebug::solid_circle, DEFVAL(Vector3(0, 1, 0)), DEFVAL(DEFAULT_DEBUG_COLOR), DEFVAL(0.0), DEFVAL(true), DEFVAL(32));
	ClassDB::bind_method(D_METHOD("solid_plane", "plane", "center", "size", "color", "duration", "depth_test"), &DrawDebug::solid_plane, DEFVAL(Vector3()), DEFVAL(2.0), DEFVAL(DEFAULT_DEBUG_COLOR), DEFVAL(0.0), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("solid_mesh", "mesh", "transform", "color", "duration", "depth_test"), &DrawDebug::solid_mesh, DEFVAL(DEFAULT_DEBUG_COLOR), DEFVAL(0.0), DEFVAL(true));

	ClassDB::bind_method(D_METHOD("text", "position", "text", "color", "duration", "font_size"), &DrawDebug::text, DEFVAL(Color(1, 1, 1)), DEFVAL(0.0), DEFVAL(16));
	ClassDB::bind_method(D_METHOD("message", "text", "color", "duration", "key"), &DrawDebug::message, DEFVAL(Color(1, 1, 1)), DEFVAL(2.0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("clear"), &DrawDebug::clear);
	ClassDB::bind_method(D_METHOD("clear_persistent"), &DrawDebug::clear_persistent);
	ClassDB::bind_method(D_METHOD("get_vertex_count"), &DrawDebug::get_vertex_count);
	ClassDB::bind_method(D_METHOD("get_pool_capacity"), &DrawDebug::get_pool_capacity);
}

DrawDebug::DrawDebug() {
	singleton = this;

#ifdef DEBUG_ENABLED
	enabled = true;
#else
	enabled = bool(GLOBAL_GET("debug/draw_debug/enabled_in_release_builds"));
#endif

	RenderingServer *rs = RenderingServer::get_singleton();
	if (rs) {
		rs->connect(SNAME("frame_pre_draw"), callable_mp(this, &DrawDebug::_on_frame_pre_draw));
	}
}

DrawDebug::~DrawDebug() {
	RenderingServer *rs = RenderingServer::get_singleton();
	if (rs && rs->is_connected(SNAME("frame_pre_draw"), callable_mp(this, &DrawDebug::_on_frame_pre_draw))) {
		rs->disconnect(SNAME("frame_pre_draw"), callable_mp(this, &DrawDebug::_on_frame_pre_draw));
	}

	_free_gpu_resources();

	if (singleton == this) {
		singleton = nullptr;
	}
}
