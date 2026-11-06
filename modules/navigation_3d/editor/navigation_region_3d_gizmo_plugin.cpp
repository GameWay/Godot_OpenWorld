/**************************************************************************/
/*  navigation_region_3d_gizmo_plugin.cpp                                 */
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

#include "navigation_region_3d_gizmo_plugin.h"

#include "core/math/random_pcg.h"
#include "scene/3d/navigation/navigation_region_3d.h"
#include "scene/3d/navigation/navigation_traversal_profile_3d.h"
#include "servers/navigation_3d/navigation_server_3d.h"

NavigationRegion3DGizmoPlugin::NavigationRegion3DGizmoPlugin() {
	create_material("face_material", NavigationServer3D::get_singleton()->get_debug_navigation_geometry_face_color(), false, false, true);
	create_material("face_material_disabled", NavigationServer3D::get_singleton()->get_debug_navigation_geometry_face_disabled_color(), false, false, true);
	create_material("edge_material", NavigationServer3D::get_singleton()->get_debug_navigation_geometry_edge_color());
	create_material("edge_material_disabled", NavigationServer3D::get_singleton()->get_debug_navigation_geometry_edge_disabled_color());
	// OEngine: traversal surfaces and rejected candidates take their colors from the vertices.
	create_material("traversal_material", Color(1.0, 1.0, 1.0), false, false, true);
	create_material("rejected_material", Color(1.0, 1.0, 1.0), false, true, true);

	Color baking_aabb_material_color = Color(0.8, 0.5, 0.7);
	baking_aabb_material_color.a = 0.1;
	create_material("baking_aabb_material", baking_aabb_material_color);
}

bool NavigationRegion3DGizmoPlugin::has_gizmo(Node3D *p_spatial) {
	return Object::cast_to<NavigationRegion3D>(p_spatial) != nullptr;
}

String NavigationRegion3DGizmoPlugin::get_gizmo_name() const {
	return "NavigationRegion3D";
}

int NavigationRegion3DGizmoPlugin::get_priority() const {
	return -1;
}

void NavigationRegion3DGizmoPlugin::set_rejected_profile(const StringName &p_profile) {
	rejected_profile = p_profile;
}

StringName NavigationRegion3DGizmoPlugin::get_rejected_profile() const {
	return rejected_profile;
}

Color NavigationRegion3DGizmoPlugin::get_reject_reason_color(int p_reason) {
	static const Color colors[NavigationTraversalProfile3D::REJECT_MAX] = {
		Color(1.0, 1.0, 1.0), // None.
		Color(0.55, 0.55, 0.55), // Kind not allowed.
		Color(0.6, 0.38, 0.2), // No support.
		Color(1.0, 0.15, 0.15), // Height.
		Color(1.0, 0.55, 0.0), // Gap.
		Color(1.0, 0.95, 0.2), // Slope.
		Color(1.0, 0.5, 0.75), // No grip.
		Color(0.95, 0.2, 0.95), // Grip angle.
		Color(0.55, 0.3, 1.0), // Grip depth.
		Color(0.2, 0.45, 1.0), // Blocked by the body capsule.
		Color(0.1, 0.85, 0.85), // Thickness.
		Color(0.3, 0.95, 0.3), // Landing delta.
		Color(0.85, 1.0, 0.75), // Walkable.
	};
	if (p_reason < 0 || p_reason >= NavigationTraversalProfile3D::REJECT_MAX) {
		return colors[0];
	}
	return colors[p_reason];
}

String NavigationRegion3DGizmoPlugin::get_reject_reason_name(int p_reason) {
	static const char *names[NavigationTraversalProfile3D::REJECT_MAX] = {
		"None",
		"Kind",
		"No Support",
		"Height",
		"Gap",
		"Slope",
		"No Grip",
		"Grip Angle",
		"Grip Depth",
		"Blocked",
		"Thickness",
		"Landing Delta",
		"Walkable",
	};
	if (p_reason < 0 || p_reason >= NavigationTraversalProfile3D::REJECT_MAX) {
		return String();
	}
	return names[p_reason];
}

void NavigationRegion3DGizmoPlugin::redraw(EditorNode3DGizmo *p_gizmo) {
	NavigationRegion3D *navigationregion = Object::cast_to<NavigationRegion3D>(p_gizmo->get_node_3d());

	p_gizmo->clear();
	Ref<NavigationMesh> navigationmesh = navigationregion->get_navigation_mesh();
	if (navigationmesh.is_null()) {
		return;
	}

	AABB baking_aabb = navigationmesh->get_filter_baking_aabb();
	if (baking_aabb.has_volume()) {
		Vector3 baking_aabb_offset = navigationmesh->get_filter_baking_aabb_offset();

		if (p_gizmo->is_selected()) {
			Ref<Material> material = get_material("baking_aabb_material", p_gizmo);
			p_gizmo->add_solid_box(material, baking_aabb.get_size(), baking_aabb.get_center() + baking_aabb_offset);
		}
	}

	Vector<Vector3> vertices = navigationmesh->get_vertices();
	const Vector3 *vr = vertices.ptr();
	const int polygon_count = navigationmesh->get_polygon_count();
	List<Face3> faces;
	for (int i = 0; i < polygon_count; i++) {
		Vector<int> p = navigationmesh->get_polygon(i);

		for (int j = 2; j < p.size(); j++) {
			Face3 f;
			f.vertex[0] = vr[p[0]];
			f.vertex[1] = vr[p[j - 1]];
			f.vertex[2] = vr[p[j]];

			faces.push_back(f);
		}
	}

	if (faces.is_empty()) {
		return;
	}

	HashMap<_EdgeKey, bool, _EdgeKey> edge_map;
	Vector<Vector3> tmeshfaces;
	tmeshfaces.resize(faces.size() * 3);

	{
		Vector3 *tw = tmeshfaces.ptrw();
		int tidx = 0;

		for (const Face3 &f : faces) {
			for (int j = 0; j < 3; j++) {
				tw[tidx++] = f.vertex[j];
				_EdgeKey ek;
				ek.from = f.vertex[j].snappedf(CMP_EPSILON);
				ek.to = f.vertex[(j + 1) % 3].snappedf(CMP_EPSILON);
				if (ek.from < ek.to) {
					SWAP(ek.from, ek.to);
				}

				HashMap<_EdgeKey, bool, _EdgeKey>::Iterator F = edge_map.find(ek);

				if (F) {
					F->value = false;

				} else {
					edge_map[ek] = true;
				}
			}
		}
	}
	Vector<Vector3> lines;

	for (const KeyValue<_EdgeKey, bool> &E : edge_map) {
		if (E.value) {
			lines.push_back(E.key.from);
			lines.push_back(E.key.to);
		}
	}

	Ref<TriangleMesh> tmesh = memnew(TriangleMesh);
	tmesh->create(tmeshfaces);

	// OEngine: an unselected navmesh is drawn faintly and covers the ground and every obstacle
	// outline, so it stole clicks from objects standing on it. Only a selected region, whose
	// navmesh is shown in full, takes part in viewport picking.
	if (p_gizmo->is_selected()) {
		p_gizmo->add_collision_triangles(tmesh);
		p_gizmo->add_collision_segments(lines);
	}

	// OEngine: walkable polygons keep the navigation colors; traversal polygons are drawn
	// separately in their kind colors. Every polygon is fan-triangulated.
	bool enabled_geometry_face_random_color = NavigationServer3D::get_singleton()->get_debug_navigation_enable_geometry_face_random_color();
	bool enabled_edge_lines = NavigationServer3D::get_singleton()->get_debug_navigation_enable_edge_lines();
	Color debug_navigation_geometry_face_color = NavigationServer3D::get_singleton()->get_debug_navigation_geometry_face_color();

	Vector<Vector3> face_vertex_array;
	Vector<Color> face_color_array;
	Vector<Vector3> line_vertex_array;
	RandomPCG rand;
	Color polygon_color = debug_navigation_geometry_face_color;

	for (int i = 0; i < polygon_count; i++) {
		Vector<int> polygon = navigationmesh->get_polygon(i);
		if (polygon.size() < 3 || navigationmesh->is_traversal_polygon(i)) {
			continue;
		}
		if (enabled_geometry_face_random_color) {
			// Generate the polygon color, slightly randomly modified from the settings one.
			polygon_color.set_hsv(debug_navigation_geometry_face_color.get_h() + rand.random(-1.0, 1.0) * 0.1, debug_navigation_geometry_face_color.get_s(), debug_navigation_geometry_face_color.get_v() + rand.random(-1.0, 1.0) * 0.2);
			polygon_color.a = debug_navigation_geometry_face_color.a;
		}
		for (int j = 2; j < polygon.size(); j++) {
			face_vertex_array.push_back(vr[polygon[0]]);
			face_vertex_array.push_back(vr[polygon[j - 1]]);
			face_vertex_array.push_back(vr[polygon[j]]);
			if (enabled_geometry_face_random_color) {
				face_color_array.push_back(polygon_color);
				face_color_array.push_back(polygon_color);
				face_color_array.push_back(polygon_color);
			}
		}
		if (enabled_edge_lines) {
			for (int j = 0; j < polygon.size(); j++) {
				line_vertex_array.push_back(vr[polygon[j]]);
				line_vertex_array.push_back(vr[polygon[(j + 1) % polygon.size()]]);
			}
		}
	}

	if (!face_vertex_array.is_empty()) {
		Ref<ArrayMesh> debug_mesh = Ref<ArrayMesh>(memnew(ArrayMesh));
		Array face_mesh_array;
		face_mesh_array.resize(Mesh::ARRAY_MAX);
		face_mesh_array[Mesh::ARRAY_VERTEX] = face_vertex_array;
		if (enabled_geometry_face_random_color) {
			face_mesh_array[Mesh::ARRAY_COLOR] = face_color_array;
		}
		debug_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, face_mesh_array);

		const String face_material_name = navigationregion->is_enabled() ? "face_material" : "face_material_disabled";
		Ref<StandardMaterial3D> face_material = get_material(face_material_name, p_gizmo);
		if (navigationregion->is_enabled()) {
			Color mat_color = NavigationServer3D::get_singleton()->get_debug_navigation_geometry_face_color();
			if (!p_gizmo->is_selected()) {
				mat_color.a *= 0.3;
			}
			face_material->set_albedo(mat_color);
		} else {
			Color mat_color = NavigationServer3D::get_singleton()->get_debug_navigation_geometry_face_disabled_color();
			if (!p_gizmo->is_selected()) {
				mat_color.a *= 0.3;
			}
			face_material->set_albedo(mat_color);
		}
		p_gizmo->add_mesh(debug_mesh, face_material);
	}

	if (!line_vertex_array.is_empty()) {
		const String line_material_name = navigationregion->is_enabled() ? "edge_material" : "edge_material_disabled";
		Ref<StandardMaterial3D> line_material = get_material(line_material_name, p_gizmo);
		if (navigationregion->is_enabled()) {
			Color mat_color = NavigationServer3D::get_singleton()->get_debug_navigation_geometry_edge_color();
			if (!p_gizmo->is_selected()) {
				mat_color.a *= 0.3;
			}
			line_material->set_albedo(mat_color);
		} else {
			Color mat_color = NavigationServer3D::get_singleton()->get_debug_navigation_geometry_edge_disabled_color();
			if (!p_gizmo->is_selected()) {
				mat_color.a *= 0.3;
			}
			line_material->set_albedo(mat_color);
		}
		p_gizmo->add_lines(line_vertex_array, line_material);
	}

	// Traversal surfaces: kind-colored faces, outlines and source -> target arrows.
	{
		Vector<Vector3> traversal_faces;
		Vector<Color> traversal_face_colors;
		Vector<Vector3> traversal_lines;
		Vector<Color> traversal_line_colors;
		navigationmesh->get_debug_traversal_geometry(traversal_faces, traversal_face_colors, traversal_lines, traversal_line_colors);
		if (!navigationregion->is_enabled()) {
			const Color disabled = NavigationServer3D::get_singleton()->get_debug_navigation_geometry_face_disabled_color();
			traversal_face_colors.fill(disabled);
			traversal_line_colors.fill(Color(disabled, 1.0));
		}
		Ref<StandardMaterial3D> traversal_material = get_material("traversal_material", p_gizmo);
		if (!traversal_faces.is_empty()) {
			Ref<ArrayMesh> mesh = Ref<ArrayMesh>(memnew(ArrayMesh));
			Array arrays;
			arrays.resize(Mesh::ARRAY_MAX);
			arrays[Mesh::ARRAY_VERTEX] = traversal_faces;
			arrays[Mesh::ARRAY_COLOR] = traversal_face_colors;
			mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
			p_gizmo->add_mesh(mesh, traversal_material);
		}
		if (!traversal_lines.is_empty()) {
			Ref<ArrayMesh> mesh = Ref<ArrayMesh>(memnew(ArrayMesh));
			Array arrays;
			arrays.resize(Mesh::ARRAY_MAX);
			arrays[Mesh::ARRAY_VERTEX] = traversal_lines;
			arrays[Mesh::ARRAY_COLOR] = traversal_line_colors;
			mesh->add_surface_from_arrays(Mesh::PRIMITIVE_LINES, arrays);
			p_gizmo->add_mesh(mesh, traversal_material);
		}
	}

	// Rejected candidates of the chosen profile, only for a selected region. They exist only right
	// after a bake with NavigationTraversalRules3D.keep_rejected_candidates and are never saved.
	if (p_gizmo->is_selected() && rejected_profile != StringName()) {
		const Array rejected = navigationmesh->get_traversal_debug().get("rejected", Array());
		Vector<Vector3> rejected_lines;
		Vector<Color> rejected_colors;
		const Vector3 lift = Vector3(0.0, 0.05, 0.0);
		const Vector3 tick = Vector3(0.0, 0.3, 0.0);
		for (const Variant &entry : rejected) {
			if (entry.get_type() != Variant::DICTIONARY) {
				continue;
			}
			const Dictionary record = entry;
			const Dictionary reasons = record.get("reasons", Dictionary());
			const int reason = reasons.get(rejected_profile, int(NavigationTraversalProfile3D::REJECT_NONE));
			if (reason == NavigationTraversalProfile3D::REJECT_NONE) {
				continue;
			}
			const Vector3 approach = Vector3(record.get("approach", Vector3())) + lift;
			const Vector3 exit = Vector3(record.get("exit", Vector3())) + lift;
			const Color color = get_reject_reason_color(reason);
			const Vector3 segment[4] = { approach, exit, approach, approach + tick };
			for (const Vector3 &point : segment) {
				rejected_lines.push_back(point);
				rejected_colors.push_back(color);
			}
		}
		if (!rejected_lines.is_empty()) {
			Ref<ArrayMesh> mesh = Ref<ArrayMesh>(memnew(ArrayMesh));
			Array arrays;
			arrays.resize(Mesh::ARRAY_MAX);
			arrays[Mesh::ARRAY_VERTEX] = rejected_lines;
			arrays[Mesh::ARRAY_COLOR] = rejected_colors;
			mesh->add_surface_from_arrays(Mesh::PRIMITIVE_LINES, arrays);
			p_gizmo->add_mesh(mesh, get_material("rejected_material", p_gizmo));
		}
	}
}
