/**************************************************************************/
/*  draw_debug.h                                                          */
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

#include "core/object/object.h"
#include "core/os/mutex.h"
#include "core/templates/local_vector.h"
#include "core/templates/rid.h"
#include "scene/resources/material.h"

class Camera3D;
class Mesh;
class Viewport;

// Immediate-mode debug drawing, similar to the DrawDebug* helpers of Unreal Engine.
//
// All primitives are batched into a small fixed set of meshes (lines/triangles,
// depth-tested/on-top) that live directly on the RenderingServer. Their GPU
// buffers act as a pool: they only grow (power of two) when a frame needs more
// vertices than are allocated, are updated in place every frame and shrink back
// after a long period of low usage. Drawing thousands of shapes every frame does
// not create any nodes, resources or GPU buffers.
//
// Lifetime of a primitive is controlled by `duration`:
//   == 0  one frame (when called from a physics tick: until the next physics tick),
//    > 0  for that many seconds (real time),
//    < 0  persistent until clear() / clear_persistent() is called.
//
// The public API is thread-safe.
class DrawDebug : public Object {
	GDCLASS(DrawDebug, Object);

	static DrawDebug *singleton;

public:
	static constexpr uint32_t MIN_POOL_VERTICES = 1024;
	static constexpr uint32_t MAX_POOL_VERTICES = 1 << 24;
	static constexpr int SHRINK_DELAY_FRAMES = 300;

private:
	enum BatchType {
		BATCH_LINES,
		BATCH_LINES_ON_TOP,
		BATCH_TRIANGLES,
		BATCH_TRIANGLES_ON_TOP,
		BATCH_MAX,
	};

	struct Vertex {
		float position[3];
		uint8_t color[4];
	};

	struct TimedRange {
		uint32_t start = 0;
		uint32_t count = 0;
		double expire = 0.0;
	};

	struct Batch {
		LocalVector<Vertex> frame; // One process frame.
		LocalVector<Vertex> physics; // Until the next physics tick.
		LocalVector<Vertex> timed; // Timed and persistent, see timed_ranges.
		LocalVector<TimedRange> timed_ranges;
		bool dirty = false;

		// Pooled GPU resources.
		RID mesh;
		RID instance;
		RID scenario;
		Ref<StandardMaterial3D> material;
		uint32_t capacity = 0; // Allocated vertices.
		uint32_t uploaded = 0; // Vertices that may hold non-zero data on the GPU.
		uint32_t vertex_offset = 0;
		uint32_t vertex_stride = 0;
		uint32_t color_offset = 0;
		uint32_t color_stride = 0;
		int low_usage_frames = 0;
		bool visible = false;
		Vector<uint8_t> vertex_staging;
		Vector<uint8_t> attribute_staging;
	};

	struct Text {
		Vector3 position;
		String text;
		Color color;
		int font_size = 16;
		double expire = 0.0;
	};

	struct Message {
		int key = -1;
		String text;
		Color color;
		double expire = 0.0;
	};

	struct Overlay {
		ObjectID viewport;
		RID viewport_rid;
		RID canvas;
		RID canvas_item;
		bool used = false;
		bool has_content = false;
	};

	// Accumulates the vertices of one primitive into the right batch and lifetime list.
	// Holds the mutex while alive.
	class Emitter {
		DrawDebug *owner = nullptr;
		Batch *batch = nullptr;
		LocalVector<Vertex> *target = nullptr;
		TimedRange range;
		uint8_t color[4] = {};
		bool timed = false;
		bool active = false;

	public:
		_FORCE_INLINE_ bool is_active() const { return active; }
		void set_color(const Color &p_color);
		_FORCE_INLINE_ void vertex(const Vector3 &p_pos) {
			Vertex v;
			v.position[0] = (float)p_pos.x;
			v.position[1] = (float)p_pos.y;
			v.position[2] = (float)p_pos.z;
			v.color[0] = color[0];
			v.color[1] = color[1];
			v.color[2] = color[2];
			v.color[3] = color[3];
			target->push_back(v);
		}
		_FORCE_INLINE_ void line(const Vector3 &p_a, const Vector3 &p_b) {
			vertex(p_a);
			vertex(p_b);
		}
		_FORCE_INLINE_ void triangle(const Vector3 &p_a, const Vector3 &p_b, const Vector3 &p_c) {
			vertex(p_a);
			vertex(p_b);
			vertex(p_c);
		}
		_FORCE_INLINE_ void quad(const Vector3 &p_a, const Vector3 &p_b, const Vector3 &p_c, const Vector3 &p_d) {
			triangle(p_a, p_b, p_c);
			triangle(p_a, p_c, p_d);
		}

		Emitter(DrawDebug *p_owner, bool p_solid, const Color &p_color, double p_duration, bool p_depth_test);
		~Emitter();
	};

	mutable Mutex mutex;
	bool enabled = true;
	bool initialized = false;

	Batch batches[BATCH_MAX];
	LocalVector<Text> frame_texts;
	LocalVector<Text> physics_texts;
	LocalVector<Text> timed_texts;
	LocalVector<Message> messages;
	LocalVector<Overlay> overlays;

	uint64_t process_frame_tag = UINT64_MAX;
	uint64_t physics_frame_tag = UINT64_MAX;

	static double _now();
	void _check_stale_frames();
	void _clear_physics();
	void _clear_all();

	void _ensure_initialized();
	void _free_gpu_resources();
	void _resize_pool(Batch &p_batch, BatchType p_type, uint32_t p_capacity);
	void _update_batch(Batch &p_batch, BatchType p_type, RID p_scenario);
	void _remove_timed(double p_now, bool p_persistent_only);
	void _collect_overlay_targets(LocalVector<Viewport *> &r_viewports, LocalVector<Camera3D *> &r_cameras);
	void _update_overlays(double p_now);
	void _on_frame_pre_draw();

	// Geometry helpers.
	static void _axis_basis(const Vector3 &p_axis, Vector3 &r_u, Vector3 &r_v);
	static void _emit_arc(Emitter &p_e, const Vector3 &p_center, const Vector3 &p_u, const Vector3 &p_v, real_t p_radius, real_t p_from, real_t p_to, int p_segments);
	static void _emit_disc(Emitter &p_e, const Vector3 &p_center, const Vector3 &p_u, const Vector3 &p_v, real_t p_radius, int p_segments);
	static void _box_corners(const Transform3D &p_xform, const Vector3 &p_half_extents, Vector3 *r_corners);
	static void _emit_box(Emitter &p_e, const Vector3 *p_corners, bool p_solid);
	static void _emit_sphere(Emitter &p_e, const Vector3 &p_center, real_t p_radius, int p_segments, bool p_solid);
	static void _emit_arrow_head(Emitter &p_e, const Vector3 &p_from, const Vector3 &p_to, real_t p_size);
	static void _emit_plane(Emitter &p_e, const Plane &p_plane, const Vector3 &p_center, real_t p_size, bool p_solid);
	static void _emit_mesh(Emitter &p_e, const Ref<Mesh> &p_mesh, const Transform3D &p_transform, bool p_solid);

protected:
	static void _bind_methods();

public:
	static DrawDebug *get_singleton() { return singleton; }
	static void register_project_settings();

	void set_enabled(bool p_enabled);
	bool is_enabled() const;

	// Lines.
	void line(const Vector3 &p_from, const Vector3 &p_to, const Color &p_color, double p_duration, bool p_depth_test);
	void lines(const PackedVector3Array &p_points, const Color &p_color, double p_duration, bool p_depth_test);
	void polyline(const PackedVector3Array &p_points, const Color &p_color, double p_duration, bool p_depth_test, bool p_closed);
	void arrow(const Vector3 &p_from, const Vector3 &p_to, const Color &p_color, double p_duration, bool p_depth_test, real_t p_head_size);
	void ray(const Vector3 &p_origin, const Vector3 &p_direction, const Color &p_color, double p_duration, bool p_depth_test);
	void point(const Vector3 &p_position, real_t p_size, const Color &p_color, double p_duration, bool p_depth_test);
	void crosshair(const Vector3 &p_position, real_t p_size, const Color &p_color, double p_duration, bool p_depth_test);
	void coordinate_system(const Transform3D &p_transform, real_t p_scale, double p_duration, bool p_depth_test);

	// Wireframe shapes.
	void box(const Vector3 &p_center, const Vector3 &p_half_extents, const Color &p_color, double p_duration, bool p_depth_test);
	void box_oriented(const Transform3D &p_transform, const Vector3 &p_half_extents, const Color &p_color, double p_duration, bool p_depth_test);
	void aabb(const AABB &p_aabb, const Color &p_color, double p_duration, bool p_depth_test);
	void sphere(const Vector3 &p_center, real_t p_radius, const Color &p_color, double p_duration, bool p_depth_test, int p_segments);
	void circle(const Vector3 &p_center, real_t p_radius, const Vector3 &p_normal, const Color &p_color, double p_duration, bool p_depth_test, int p_segments);
	void cylinder(const Vector3 &p_from, const Vector3 &p_to, real_t p_radius, const Color &p_color, double p_duration, bool p_depth_test, int p_segments);
	void cone(const Vector3 &p_apex, const Vector3 &p_direction, real_t p_length, real_t p_angle, const Color &p_color, double p_duration, bool p_depth_test, int p_segments);
	void capsule(const Vector3 &p_from, const Vector3 &p_to, real_t p_radius, const Color &p_color, double p_duration, bool p_depth_test, int p_segments);
	void plane(const Plane &p_plane, const Vector3 &p_center, real_t p_size, const Color &p_color, double p_duration, bool p_depth_test);
	void frustum(const Projection &p_projection, const Transform3D &p_transform, const Color &p_color, double p_duration, bool p_depth_test);
	void camera_frustum(Camera3D *p_camera, const Color &p_color, double p_duration, bool p_depth_test);
	void mesh(const Ref<Mesh> &p_mesh, const Transform3D &p_transform, const Color &p_color, double p_duration, bool p_depth_test);
	void grid(const Transform3D &p_transform, int p_cells, real_t p_cell_size, const Color &p_color, double p_duration, bool p_depth_test);

	// Solid shapes.
	void solid_triangle(const Vector3 &p_a, const Vector3 &p_b, const Vector3 &p_c, const Color &p_color, double p_duration, bool p_depth_test);
	void solid_box(const Vector3 &p_center, const Vector3 &p_half_extents, const Color &p_color, double p_duration, bool p_depth_test);
	void solid_box_oriented(const Transform3D &p_transform, const Vector3 &p_half_extents, const Color &p_color, double p_duration, bool p_depth_test);
	void solid_aabb(const AABB &p_aabb, const Color &p_color, double p_duration, bool p_depth_test);
	void solid_sphere(const Vector3 &p_center, real_t p_radius, const Color &p_color, double p_duration, bool p_depth_test, int p_segments);
	void solid_circle(const Vector3 &p_center, real_t p_radius, const Vector3 &p_normal, const Color &p_color, double p_duration, bool p_depth_test, int p_segments);
	void solid_plane(const Plane &p_plane, const Vector3 &p_center, real_t p_size, const Color &p_color, double p_duration, bool p_depth_test);
	void solid_mesh(const Ref<Mesh> &p_mesh, const Transform3D &p_transform, const Color &p_color, double p_duration, bool p_depth_test);

	// Text.
	void text(const Vector3 &p_position, const String &p_text, const Color &p_color, double p_duration, int p_font_size);
	void message(const String &p_text, const Color &p_color, double p_duration, int p_key);

	void clear();
	void clear_persistent();
	int get_vertex_count() const;
	int get_pool_capacity() const;

	DrawDebug();
	~DrawDebug();
};
