/**************************************************************************/
/*  nav_traversal_baker_3d.h                                              */
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

#include "core/math/vector2.h"
#include "core/math/vector3.h"
#include "core/string/string_name.h"
#include "core/templates/local_vector.h"
#include "core/templates/vector.h"
#include "core/variant/dictionary.h"
#include "scene/3d/navigation/navigation_traversal_profile_3d.h"

class NavigationTraversalRules3D;

// Second navigation bake pass: finds directed traversal surfaces (climb, climb over,
// vault, drop, hang drop) between boundary edges of a Recast navigation mesh. All
// probes run against the triangles of the bake source geometry, so the pass is safe
// to run on a worker thread.
class NavigationTraversalBaker3D {
public:
	using Kind = NavigationTraversalProfile3D::TraversalKind;
	using Reject = NavigationTraversalProfile3D::RejectReason;

	// Thread-safe snapshot of one NavigationTraversalProfile3D.
	struct Profile {
		StringName id;
		uint32_t layer_mask = 0;
		uint32_t allowed_kinds = 0;
		Vector2 height_ranges[NavigationTraversalProfile3D::KIND_MAX];
		real_t max_gap = 0.0;
		real_t max_slope = 0.0;
		real_t max_thickness = 0.0;
		real_t max_landing_delta = 0.0;
		real_t max_grip_angle = 0.0;
		real_t max_grip_depth = 0.0;
		real_t time_scale = 0.0;
		real_t extra_penalty = 0.0;
	};

	// Thread-safe snapshot of NavigationTraversalRules3D, taken on the main thread.
	struct Settings {
		real_t search_distance = 2.6;
		real_t search_height = 3.2;
		real_t sample_spacing = 0.4;
		real_t edge_inset = 0.1;
		real_t support_search = 0.4;
		real_t body_radius = 0.32;
		real_t body_height = 1.8;
		real_t hand_spacing = 0.42;
		real_t grip_probe_depth = 0.15;
		real_t cell_height = 0.25;
		// Gravity of falling phases, physics/3d/default_gravity.
		real_t gravity = 9.8;
		bool keep_rejected = false;
		LocalVector<Profile> profiles;
	};

	struct Output {
		Vector<Vector3> vertices;
		Vector<Vector<int>> polygons;
		Dictionary surfaces;
		Dictionary debug;
	};

	// Seconds the game executor needs to follow p_path by phases (holds, crest, hang, falls);
	// used for the enter cost.
	static real_t path_duration(const Vector<Vector3> &p_path, Kind p_kind, real_t p_gravity);

	static Settings make_settings(const Ref<NavigationTraversalRules3D> &p_rules, real_t p_cell_height);

	// p_geometry_vertices / p_geometry_indices: NavigationMeshSourceGeometryData3D data.
	// p_vertices / p_polygons: the Recast result in the same space.
	static void bake(const Settings &p_settings, const Vector<float> &p_geometry_vertices, const Vector<int> &p_geometry_indices,
			const Vector<Vector3> &p_vertices, const Vector<Vector<int>> &p_polygons, Output &r_output);
};
