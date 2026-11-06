/**************************************************************************/
/*  nav_traversal_baker_3d.cpp                                            */
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

#include "nav_traversal_baker_3d.h"

#include "core/math/aabb.h"
#include "core/math/face3.h"
#include "core/math/geometry_2d.h"
#include "core/math/geometry_3d.h"
#include "core/os/os.h"
#include "core/templates/hash_map.h"
#include "core/templates/sort_array.h"
#include "core/variant/array.h"
#include "scene/3d/navigation/navigation_traversal_rules_3d.h"

namespace {

using Profile3D = NavigationTraversalProfile3D;
using Reject = NavigationTraversalProfile3D::RejectReason;

// Lift of the body over its support, matches the executor in game code.
constexpr real_t SURFACE_MARGIN = 0.04;
// Speeds used to estimate how long a body path takes (enter cost).
constexpr real_t LANDING_SPEED = 2.4;
constexpr real_t CLIMB_SPEED = 1.9;
constexpr real_t CLIMB_MOVE_SPEED = 2.3;
constexpr real_t VAULT_SPEED = 2.7;
constexpr real_t VAULT_MOVE_SPEED = 3.4;
constexpr real_t WAYPOINT_TOLERANCE = 0.001;
// Two heights closer than this count as one level (Recast step noise, uneven ground).
constexpr real_t LEVEL_TOLERANCE = 0.15;
// Height ranges are inclusive; probes add float noise.
constexpr real_t HEIGHT_EPSILON = 0.005;
constexpr real_t SECTION_STEP = 0.1;
// Merged spans must stay on the same level.
constexpr real_t MERGE_TOLERANCE = 0.15;
// Boundary edges steeper than this run along a walkable slope (ramp side).
constexpr real_t EDGE_SLOPE_LIMIT = 10.0;
// Obstacle faces must face the traversal (cos 35 degrees); diagonal cuts across a corner are skipped.
constexpr real_t FACE_FACING = 0.82;

enum Shape {
	SHAPE_NONE,
	SHAPE_UP,
	SHAPE_DOWN,
	SHAPE_OBSTACLE,
};

_FORCE_INLINE_ Vector2 xz(const Vector3 &p_point) {
	return Vector2(p_point.x, p_point.z);
}

_FORCE_INLINE_ Vector3 at_height(const Vector3 &p_point, real_t p_height) {
	return Vector3(p_point.x, p_height, p_point.z);
}

_FORCE_INLINE_ Vector3 outward_of(const Vector3 &p_a, const Vector3 &p_b) {
	return Vector3(0, 1, 0).cross(p_b - p_a).normalized();
}

real_t segment_triangle_distance_squared(const Vector3 &p_a, const Vector3 &p_b, const Vector3 *p_v) {
	if (Geometry3D::segment_intersects_triangle(p_a, p_b, p_v[0], p_v[1], p_v[2])) {
		return 0.0;
	}
	const Face3 face(p_v[0], p_v[1], p_v[2]);
	real_t result = MIN(p_a.distance_squared_to(face.get_closest_point_to(p_a)), p_b.distance_squared_to(face.get_closest_point_to(p_b)));
	for (int i = 0; i < 3; i++) {
		Vector3 ps;
		Vector3 qt;
		Geometry3D::get_closest_points_between_segments(p_a, p_b, p_v[i], p_v[(i + 1) % 3], ps, qt);
		result = MIN(result, ps.distance_squared_to(qt));
	}
	return result;
}

// Bounding volume hierarchy over the bake source triangles.
class TriangleTree {
	struct Triangle {
		Vector3 vertices[3];
		Vector3 normal;
	};

	struct Node {
		AABB aabb;
		int left = -1;
		int right = -1;
		int first = 0;
		int count = 0;
	};

	struct CentroidCompare {
		const Vector3 *centroids = nullptr;
		int axis = 0;
		bool operator()(int p_a, int p_b) const {
			const real_t a = centroids[p_a][axis];
			const real_t b = centroids[p_b][axis];
			return a < b || (a == b && p_a < p_b);
		}
	};

	LocalVector<Triangle> triangles;
	LocalVector<Vector3> centroids;
	LocalVector<int> order;
	LocalVector<Node> nodes;

	AABB _triangle_aabb(int p_triangle) const {
		const Triangle &triangle = triangles[p_triangle];
		AABB aabb(triangle.vertices[0], Vector3());
		aabb.expand_to(triangle.vertices[1]);
		aabb.expand_to(triangle.vertices[2]);
		return aabb;
	}

	int _build(int p_first, int p_count) {
		const int index = nodes.size();
		nodes.push_back(Node());
		AABB aabb = _triangle_aabb(order[p_first]);
		for (int i = 1; i < p_count; i++) {
			aabb.merge_with(_triangle_aabb(order[p_first + i]));
		}
		aabb = aabb.grow(0.001);
		nodes[index].aabb = aabb;
		if (p_count <= 4) {
			nodes[index].first = p_first;
			nodes[index].count = p_count;
			return index;
		}
		SortArray<int, CentroidCompare> sorter;
		sorter.compare.centroids = centroids.ptr();
		sorter.compare.axis = aabb.get_longest_axis_index();
		const int middle = p_first + p_count / 2;
		sorter.nth_element(p_first, p_first + p_count, middle, order.ptr());
		const int left = _build(p_first, middle - p_first);
		const int right = _build(middle, p_first + p_count - middle);
		nodes[index].left = left;
		nodes[index].right = right;
		return index;
	}

public:
	void create(const Vector<float> &p_vertices, const Vector<int> &p_indices) {
		const int vertex_count = p_vertices.size() / 3;
		const float *v = p_vertices.ptr();
		for (int i = 0; i + 2 < p_indices.size(); i += 3) {
			Triangle triangle;
			bool valid = true;
			for (int corner = 0; corner < 3; corner++) {
				const int vertex = p_indices[i + corner];
				if (vertex < 0 || vertex >= vertex_count) {
					valid = false;
					break;
				}
				triangle.vertices[corner] = Vector3(v[vertex * 3 + 0], v[vertex * 3 + 1], v[vertex * 3 + 2]);
			}
			if (!valid) {
				continue;
			}
			// Source geometry winding is reversed relative to Face3 (the same flip
			// the physics shapes use); the normal points to the front side.
			const Vector3 normal = (triangle.vertices[1] - triangle.vertices[0]).cross(triangle.vertices[2] - triangle.vertices[0]);
			if (normal.length_squared() <= CMP_EPSILON2) {
				continue;
			}
			triangle.normal = normal.normalized();
			triangles.push_back(triangle);
			centroids.push_back((triangle.vertices[0] + triangle.vertices[1] + triangle.vertices[2]) / 3.0);
			order.push_back(int(triangles.size()) - 1);
		}
		if (!triangles.is_empty()) {
			nodes.reserve(triangles.size() / 2 + 1);
			_build(0, triangles.size());
		}
	}

	bool is_empty() const {
		return triangles.is_empty();
	}

	// Nearest front face crossed by the segment.
	bool raycast(const Vector3 &p_from, const Vector3 &p_to, Vector3 &r_point, Vector3 &r_normal) const {
		if (nodes.is_empty()) {
			return false;
		}
		const Vector3 direction = p_to - p_from;
		real_t best = real_t(Math::INF);
		int stack[64];
		int depth = 0;
		stack[depth++] = 0;
		while (depth > 0) {
			const Node &node = nodes[stack[--depth]];
			if (!node.aabb.intersects_segment(p_from, p_to)) {
				continue;
			}
			if (node.left < 0) {
				for (int i = 0; i < node.count; i++) {
					const Triangle &triangle = triangles[order[node.first + i]];
					if (triangle.normal.dot(direction) >= 0.0) {
						continue;
					}
					Vector3 point;
					if (!Geometry3D::segment_intersects_triangle(p_from, p_to, triangle.vertices[0], triangle.vertices[1], triangle.vertices[2], &point)) {
						continue;
					}
					const real_t distance = p_from.distance_squared_to(point);
					if (distance < best) {
						best = distance;
						r_point = point;
						r_normal = triangle.normal;
					}
				}
				continue;
			}
			ERR_FAIL_COND_V(depth + 2 > 64, best < real_t(Math::INF));
			stack[depth++] = node.left;
			stack[depth++] = node.right;
		}
		return best < real_t(Math::INF);
	}

	// True if a sphere of p_radius swept along p_a..p_b touches any triangle.
	bool capsule_overlaps(const Vector3 &p_a, const Vector3 &p_b, real_t p_radius) const {
		if (nodes.is_empty()) {
			return false;
		}
		AABB bounds(p_a, Vector3());
		bounds.expand_to(p_b);
		bounds = bounds.grow(p_radius);
		const real_t radius_squared = p_radius * p_radius;
		int stack[64];
		int depth = 0;
		stack[depth++] = 0;
		while (depth > 0) {
			const Node &node = nodes[stack[--depth]];
			if (!node.aabb.intersects(bounds)) {
				continue;
			}
			if (node.left < 0) {
				for (int i = 0; i < node.count; i++) {
					const Triangle &triangle = triangles[order[node.first + i]];
					if (segment_triangle_distance_squared(p_a, p_b, triangle.vertices) < radius_squared) {
						return true;
					}
				}
				continue;
			}
			ERR_FAIL_COND_V(depth + 2 > 64, true);
			stack[depth++] = node.left;
			stack[depth++] = node.right;
		}
		return false;
	}
};

struct BoundaryEdge {
	Vector3 a;
	Vector3 b;
	Vector3 outward;
	int polygon = -1;
	Vector2 min;
	Vector2 max;
};

struct Destination {
	bool found = false;
	Vector3 point;
	int polygon = -1;
	int edge = -1;
};

struct Grips {
	LocalVector<Vector3> points;
	real_t angle = real_t(Math::INF);
	real_t depth = real_t(Math::INF);
	Vector3 face;
	bool face_found = false;
};

// Geometric measurement of one sampled span.
struct Candidate {
	int edge = -1;
	Vector3 span[2];
	Vector3 nav_start;
	Vector3 nav_end;
	int source_polygon = -1;
	int target_polygon = -1;
	int target_edge = -1;
	bool support = false;
	Vector3 start;
	Vector3 finish;
	Vector3 direction;
	Vector3 side;
	Shape shape = SHAPE_NONE;
	real_t gap = 0.0;
	real_t slope = 0.0;
	real_t crest = 0.0;
	real_t thickness = real_t(Math::INF);
	Vector3 front;
	Vector3 back;
	Grips grips;
	bool sloped_edge = false;
	Reject geometry_reject = Profile3D::REJECT_NONE;
};

// One accepted span for one kind, before merging.
struct Accepted {
	int candidate = -1;
	Profile3D::TraversalKind kind = Profile3D::KIND_CLIMB;
	uint32_t layers = 0;
	real_t enter_cost = 0.0;
	Vector<Vector3> body_path;
	Vector<Vector3> anchors;
	Vector3 quad[4];
};

class Baker {
	const NavigationTraversalBaker3D::Settings &settings;
	TriangleTree tree;
	const Vector<Vector3> &nav_vertices;
	const Vector<Vector<int>> &nav_polygons;

	LocalVector<BoundaryEdge> edges;
	HashMap<Vector2i, LocalVector<int>> buckets;
	real_t bucket_size = 1.0;
	LocalVector<Vector3> centroids;
	struct Portal {
		int polygon = -1;
		Vector3 a;
		Vector3 b;
	};
	LocalVector<LocalVector<Portal>> adjacency;
	real_t walk_limit = 0.0;

	LocalVector<Candidate> candidates;

	bool _ground(const Vector3 &p_point, real_t p_below, real_t p_above, Vector3 &r_point, real_t &r_slope) const {
		Vector3 normal;
		if (!tree.raycast(p_point + Vector3(0, p_above, 0), p_point - Vector3(0, p_below, 0), r_point, normal)) {
			return false;
		}
		r_slope = Math::rad_to_deg(Math::acos(CLAMP(normal.y, real_t(-1.0), real_t(1.0))));
		return true;
	}

	bool _support(const Vector3 &p_point, const Vector3 &p_side, const Vector3 &p_direction, Vector3 &r_point, real_t &r_slope) const {
		// Recast vertices float up to a few cells above the floor.
		const real_t tolerance = settings.cell_height * 3.0 + SURFACE_MARGIN;
		if (!_ground(p_point, tolerance, tolerance, r_point, r_slope)) {
			return false;
		}
		const Vector3 axes[4] = { p_side, -p_side, p_direction, -p_direction };
		for (const Vector3 &axis : axes) {
			const Vector3 offset = axis * (settings.body_radius * 0.75);
			Vector3 foot;
			real_t slope = 0.0;
			if (!_ground(r_point + offset, settings.support_search, settings.support_search, foot, slope)) {
				return false;
			}
			const real_t footprint = Math::rad_to_deg(Math::atan2(Math::abs(foot.y - r_point.y), offset.length()));
			r_slope = MAX(r_slope, MAX(slope, footprint));
		}
		return true;
	}

	void _collect_edges() {
		HashMap<Vector3i, int> welded;
		LocalVector<int> vertex_ids;
		vertex_ids.resize(nav_vertices.size());
		for (int i = 0; i < nav_vertices.size(); i++) {
			const Vector3 &v = nav_vertices[i];
			const Vector3i key(int(Math::round(v.x * 1000.0f)), int(Math::round(v.y * 1000.0f)), int(Math::round(v.z * 1000.0f)));
			HashMap<Vector3i, int>::Iterator found = welded.find(key);
			if (found) {
				vertex_ids[i] = found->value;
			} else {
				const int id = welded.size();
				welded.insert(key, id);
				vertex_ids[i] = id;
			}
		}

		struct EdgeUse {
			int edge = -1;
			int polygon = -1;
			int count = 0;
		};
		HashMap<Vector2i, EdgeUse> uses;
		LocalVector<Vector2i> order;
		centroids.resize(nav_polygons.size());
		adjacency.resize(nav_polygons.size());
		LocalVector<BoundaryEdge> all_edges;
		LocalVector<Vector2i> all_keys;
		for (int polygon_index = 0; polygon_index < nav_polygons.size(); polygon_index++) {
			const Vector<int> &polygon = nav_polygons[polygon_index];
			Vector3 center;
			for (int vertex : polygon) {
				center += nav_vertices[vertex];
			}
			if (!polygon.is_empty()) {
				center /= polygon.size();
			}
			centroids[polygon_index] = center;
			if (polygon.size() < 3) {
				continue;
			}
			for (int side = 0; side < polygon.size(); side++) {
				const int a = polygon[side];
				const int b = polygon[(side + 1) % polygon.size()];
				const Vector2i key(MIN(vertex_ids[a], vertex_ids[b]), MAX(vertex_ids[a], vertex_ids[b]));
				if (key.x == key.y) {
					continue;
				}
				HashMap<Vector2i, EdgeUse>::Iterator use = uses.find(key);
				if (use) {
					use->value.count++;
					if (use->value.polygon != polygon_index) {
						adjacency[use->value.polygon].push_back({ polygon_index, nav_vertices[a], nav_vertices[b] });
						adjacency[polygon_index].push_back({ use->value.polygon, nav_vertices[a], nav_vertices[b] });
					}
					continue;
				}
				BoundaryEdge edge;
				edge.a = nav_vertices[a];
				edge.b = nav_vertices[b];
				if (outward_of(edge.a, edge.b).dot(center - edge.a) > 0.0) {
					SWAP(edge.a, edge.b);
				}
				edge.outward = outward_of(edge.a, edge.b);
				edge.polygon = polygon_index;
				edge.min = xz(edge.a).min(xz(edge.b));
				edge.max = xz(edge.a).max(xz(edge.b));
				EdgeUse new_use;
				new_use.edge = all_edges.size();
				new_use.polygon = polygon_index;
				new_use.count = 1;
				uses.insert(key, new_use);
				all_edges.push_back(edge);
				all_keys.push_back(key);
			}
		}
		for (uint32_t i = 0; i < all_edges.size(); i++) {
			if (uses[all_keys[i]].count == 1 && all_edges[i].outward != Vector3()) {
				edges.push_back(all_edges[i]);
			}
		}

		bucket_size = MAX(real_t(1.0), settings.search_distance);
		for (uint32_t i = 0; i < edges.size(); i++) {
			_for_buckets(edges[i], 0.0, [&](const Vector2i &p_cell) {
				buckets[p_cell].push_back(i);
			});
		}
	}

	template <typename F>
	void _for_buckets(const BoundaryEdge &p_edge, real_t p_margin, F &&p_callback) const {
		const Vector2 low = (p_edge.min - Vector2(p_margin, p_margin)) / bucket_size;
		const Vector2 high = (p_edge.max + Vector2(p_margin, p_margin)) / bucket_size;
		const int z_end = int(Math::floor(high.y));
		const int x_end = int(Math::floor(high.x));
		for (int z = int(Math::floor(low.y)); z <= z_end; z++) {
			for (int x = int(Math::floor(low.x)); x <= x_end; x++) {
				p_callback(Vector2i(x, z));
			}
		}
	}

	Destination _find_destination(const Vector3 &p_center, const Vector3 &p_direction, const LocalVector<int> &p_facing) const {
		const Vector2 start = xz(p_center);
		const Vector2 end = start + xz(p_direction) * settings.search_distance;
		Destination best;
		real_t best_gap = real_t(Math::INF);
		for (int index : p_facing) {
			const BoundaryEdge &other = edges[index];
			Vector2 cross_point;
			if (!Geometry2D::segment_intersects_segment(start, end, xz(other.a), xz(other.b), &cross_point)) {
				continue;
			}
			const Vector2 segment = xz(other.b - other.a);
			const real_t fraction = CLAMP((cross_point - xz(other.a)).dot(segment) / segment.length_squared(), real_t(0.0), real_t(1.0));
			const Vector3 point = other.a.lerp(other.b, fraction);
			const real_t gap = start.distance_to(cross_point);
			if (gap <= 0.001 || gap >= best_gap || Math::abs(point.y - p_center.y) > settings.search_height) {
				continue;
			}
			best_gap = gap;
			best.found = true;
			best.point = point - other.outward * settings.edge_inset;
			best.polygon = other.polygon;
			best.edge = index;
		}
		return best;
	}

	// Two hand probes against a face at p_level, then the lip above each hit.
	Grips _grips(const Vector3 &p_from, const Vector3 &p_to, const Vector3 &p_side, const Vector3 &p_direction, real_t p_level, real_t p_lip) const {
		Grips grips;
		LocalVector<Vector3> normals;
		Vector3 point;
		Vector3 normal;
		if (tree.raycast(at_height(p_from, p_level), at_height(p_to, p_level), point, normal) && normal.dot(p_direction) < -0.1) {
			grips.face = point;
			grips.face_found = true;
		}
		for (int sign = -1; sign <= 1; sign += 2) {
			const Vector3 offset = p_side * (settings.hand_spacing * 0.5 * sign);
			if (!tree.raycast(at_height(p_from + offset, p_level), at_height(p_to + offset, p_level), point, normal)) {
				continue;
			}
			if (normal.dot(p_direction) >= -0.1) {
				continue;
			}
			Vector3 top;
			real_t slope = 0.0;
			if (!_ground(at_height(point + p_direction * SURFACE_MARGIN, p_lip), settings.support_search, settings.support_search, top, slope)) {
				continue;
			}
			grips.points.push_back(at_height(point, top.y));
			normals.push_back(normal);
		}
		if (grips.points.size() == 2) {
			grips.angle = Math::rad_to_deg(normals[0].angle_to(normals[1]));
			grips.depth = Math::abs((grips.points[1] - grips.points[0]).dot(p_direction));
		}
		return grips;
	}

	void _measure(Candidate &r_candidate) {
		Candidate &c = r_candidate;
		Vector3 point;
		real_t slope_source = 0.0;
		real_t slope_target = 0.0;
		const bool source = _support(c.nav_start, c.side, c.direction, c.start, slope_source);
		const bool target = c.target_polygon >= 0 && _support(c.nav_end, c.side, c.direction, c.finish, slope_target);
		if (!source) {
			c.start = c.nav_start;
		}
		if (!target) {
			c.finish = c.nav_end;
		}
		c.support = source && target;
		c.slope = MAX(slope_source, slope_target);
		c.gap = xz(c.finish - c.start).length();
		if (!c.support) {
			c.geometry_reject = Profile3D::REJECT_NO_SUPPORT;
			return;
		}

		// Vertical section between the two supports.
		const real_t high = MAX(c.start.y, c.finish.y);
		const real_t low = MIN(c.start.y, c.finish.y);
		const real_t top = high + settings.search_height;
		const int count = MAX(2, int(Math::ceil(c.gap / SECTION_STEP)));
		c.crest = -real_t(Math::INF);
		bool hole = false;
		for (int i = 0; i < count; i++) {
			const Vector3 sample = c.start.lerp(c.finish, (i + 0.5) / count);
			Vector3 hit;
			real_t slope = 0.0;
			if (_ground(at_height(sample, top), top - low + settings.support_search, 0.0, hit, slope)) {
				c.crest = MAX(c.crest, hit.y);
				if (hit.y < low - LEVEL_TOLERANCE) {
					hole = true;
				}
			} else {
				hole = true;
			}
		}
		const real_t delta = c.finish.y - c.start.y;
		if (c.crest > high + LEVEL_TOLERANCE) {
			c.shape = SHAPE_OBSTACLE;
		} else if (hole) {
			c.geometry_reject = Profile3D::REJECT_GAP;
			return;
		} else if (delta > LEVEL_TOLERANCE) {
			c.shape = SHAPE_UP;
			c.crest = c.finish.y;
		} else if (delta < -LEVEL_TOLERANCE) {
			c.shape = SHAPE_DOWN;
			c.crest = c.start.y;
		} else {
			// Same level, nothing in between: walking already connects it or a gap.
			c.geometry_reject = Profile3D::REJECT_HEIGHT;
			return;
		}

		const Vector3 reach = c.direction * (c.gap + settings.body_radius);
		if (c.shape == SHAPE_UP) {
			c.grips = _grips(c.start, c.start + reach, c.side, c.direction, c.finish.y - settings.grip_probe_depth, c.finish.y);
		} else if (c.shape == SHAPE_DOWN) {
			// Hands hold the source lip; probe from the open side back to the wall.
			c.grips = _grips(c.finish, c.finish - reach, c.side, -c.direction, c.start.y - settings.grip_probe_depth, c.start.y);
		} else {
			// The obstacle must stand on the ground: both faces are hit just above the higher support.
			Vector3 normal;
			const real_t base = high + LEVEL_TOLERANCE;
			const real_t level = MAX(base, c.crest - settings.grip_probe_depth);
			if (!tree.raycast(at_height(c.start, base), at_height(c.finish, base), point, normal) ||
					!tree.raycast(at_height(c.start, level), at_height(c.finish, level), c.front, normal) || normal.dot(c.direction) > -FACE_FACING ||
					!tree.raycast(at_height(c.finish, level), at_height(c.start, level), c.back, normal) || normal.dot(c.direction) < FACE_FACING) {
				c.geometry_reject = Profile3D::REJECT_BLOCKED;
				return;
			}
			c.thickness = (c.back - c.front).dot(c.direction);
			if (c.thickness <= 0.001) {
				c.geometry_reject = Profile3D::REJECT_BLOCKED;
				return;
			}
			c.grips = _grips(c.start, c.start + reach, c.side, c.direction, c.crest - settings.grip_probe_depth, c.crest);
		}

		// Recast edges on a ramp are stair-stepped; use the floor normal instead:
		// a floor that rises along the edge is the side of a walkable slope.
		const BoundaryEdge &edge = edges[c.edge];
		const Vector2 tangent = xz(edge.b - edge.a).normalized();
		const real_t limit = Math::sin(Math::deg_to_rad(EDGE_SLOPE_LIMIT));
		const Vector3 supports[2] = { c.start, c.finish };
		for (const Vector3 &support : supports) {
			Vector3 normal;
			if (tree.raycast(support + Vector3(0, LEVEL_TOLERANCE, 0), support - Vector3(0, LEVEL_TOLERANCE, 0), point, normal) &&
					Math::abs(xz(normal).dot(tangent)) > limit) {
				c.sloped_edge = true;
			}
		}
	}

	Vector<Vector3> _body_path(const Candidate &p_candidate, Profile3D::TraversalKind p_kind) const {
		const Candidate &c = p_candidate;
		Vector<Vector3> path;
		path.push_back(c.start);
		switch (p_kind) {
			case Profile3D::KIND_CLIMB:
				path.push_back(at_height(c.start, c.finish.y));
				break;
			case Profile3D::KIND_DROP:
				path.push_back(at_height(c.finish, c.start.y));
				break;
			case Profile3D::KIND_HANG_DROP: {
				// Step over the lip, hang along the wall, let go.
				Vector3 out = at_height(c.finish, c.start.y);
				if (c.grips.face_found) {
					const Vector3 candidate = c.grips.face + c.direction * (settings.body_radius + SURFACE_MARGIN);
					if ((candidate - c.start).dot(c.direction) < (c.finish - c.start).dot(c.direction)) {
						out = at_height(candidate, c.start.y);
					}
				}
				path.push_back(out);
				path.push_back(at_height(out, c.finish.y));
			} break;
			case Profile3D::KIND_VAULT:
			case Profile3D::KIND_CLIMB_OVER:
				path.push_back(at_height(c.start, c.crest));
				path.push_back(at_height(c.finish, c.crest));
				break;
			default:
				break;
		}
		path.push_back(c.finish);
		Vector<Vector3> result;
		for (const Vector3 &point : path) {
			if (result.is_empty() || result[result.size() - 1].distance_squared_to(point) > WAYPOINT_TOLERANCE * WAYPOINT_TOLERANCE) {
				result.push_back(point);
			}
		}
		return result;
	}

	bool _capsule_clear(const Vector3 &p_from, const Vector3 &p_to) const {
		const real_t half = settings.body_height * 0.5;
		const Vector3 axis(0, MAX(real_t(0.0), half - settings.body_radius), 0);
		const Vector3 lift(0, half + SURFACE_MARGIN, 0);
		const Vector3 a = p_from + lift;
		const Vector3 b = p_to + lift;
		if (xz(b - a).length_squared() <= CMP_EPSILON2) {
			// Vertical leg: one longer capsule covers the whole motion.
			const Vector3 bottom = (a.y < b.y ? a : b) - axis;
			const Vector3 upper = (a.y < b.y ? b : a) + axis;
			return !tree.capsule_overlaps(bottom, upper, settings.body_radius - 0.001);
		}
		const int steps = MAX(1, int(Math::ceil(a.distance_to(b) / (settings.body_radius * 0.5))));
		for (int i = 0; i <= steps; i++) {
			const Vector3 center = a.lerp(b, real_t(i) / steps);
			if (tree.capsule_overlaps(center - axis, center + axis, settings.body_radius - 0.001)) {
				return false;
			}
		}
		return true;
	}

	bool _path_clear(const Vector<Vector3> &p_path) const {
		for (int i = 0; i + 1 < p_path.size(); i++) {
			if (!_capsule_clear(p_path[i], p_path[i + 1])) {
				return false;
			}
		}
		return p_path.size() > 1 || _capsule_clear(p_path[0], p_path[0]);
	}

	Vector<Vector3> _anchors(const Candidate &p_candidate, Profile3D::TraversalKind p_kind) const {
		Vector<Vector3> anchors;
		if (p_kind == Profile3D::KIND_VAULT) {
			anchors.push_back(at_height(p_candidate.front, p_candidate.crest));
			anchors.push_back(at_height(p_candidate.back, p_candidate.crest));
		} else if (p_kind == Profile3D::KIND_DROP) {
			if (p_candidate.grips.face_found) {
				anchors.push_back(at_height(p_candidate.grips.face, p_candidate.start.y));
			}
		} else {
			for (const Vector3 &grip : p_candidate.grips.points) {
				anchors.push_back(grip);
			}
		}
		return anchors;
	}

	Reject _evaluate(const Candidate &p_candidate, const NavigationTraversalBaker3D::Profile &p_profile, Profile3D::TraversalKind p_kind, real_t &r_height) const {
		const Candidate &c = p_candidate;
		if ((p_profile.allowed_kinds & (1u << p_kind)) == 0) {
			return Profile3D::REJECT_KIND;
		}
		if (c.shape == SHAPE_UP) {
			r_height = c.finish.y - c.start.y;
		} else if (c.shape == SHAPE_DOWN) {
			r_height = c.start.y - c.finish.y;
		} else {
			r_height = c.crest - c.start.y;
		}
		const Vector2 &range = p_profile.height_ranges[p_kind];
		if (r_height < range.x - HEIGHT_EPSILON || r_height > range.y + HEIGHT_EPSILON) {
			return Profile3D::REJECT_HEIGHT;
		}
		if (p_kind == Profile3D::KIND_CLIMB_OVER && c.crest - c.finish.y > range.y + HEIGHT_EPSILON) {
			return Profile3D::REJECT_HEIGHT;
		}
		if (c.gap > p_profile.max_gap) {
			return Profile3D::REJECT_GAP;
		}
		if (c.slope > p_profile.max_slope) {
			return Profile3D::REJECT_SLOPE;
		}
		if (p_kind == Profile3D::KIND_VAULT) {
			if (c.thickness > p_profile.max_thickness) {
				return Profile3D::REJECT_THICKNESS;
			}
			if (Math::abs(c.finish.y - c.start.y) > p_profile.max_landing_delta) {
				return Profile3D::REJECT_LANDING_DELTA;
			}
		}
		if (p_kind == Profile3D::KIND_CLIMB || p_kind == Profile3D::KIND_CLIMB_OVER || p_kind == Profile3D::KIND_HANG_DROP) {
			if (c.grips.points.size() != 2) {
				return Profile3D::REJECT_NO_GRIP;
			}
			if (c.grips.angle > p_profile.max_grip_angle) {
				return Profile3D::REJECT_GRIP_ANGLE;
			}
			if (c.grips.depth > p_profile.max_grip_depth) {
				return Profile3D::REJECT_GRIP_DEPTH;
			}
		}
		return Profile3D::REJECT_NONE;
	}

	// Walking distance from approach to exit through shared polygon edges.
	real_t _walk_distance(const Candidate &p_candidate) const {
		struct Entry {
			real_t distance;
			int polygon;
		};
		auto less = [](const Entry &a, const Entry &b) {
			return a.distance > b.distance || (a.distance == b.distance && a.polygon > b.polygon);
		};
		LocalVector<Entry> heap;
		HashMap<int, real_t> best;
		HashMap<int, Vector3> position;
		const int source = p_candidate.source_polygon;
		const int target = p_candidate.target_polygon;
		heap.push_back({ real_t(0.0), source });
		best.insert(source, real_t(0.0));
		position.insert(source, p_candidate.nav_start);
		while (!heap.is_empty()) {
			const Entry entry = heap[0];
			heap[0] = heap[heap.size() - 1];
			heap.resize(heap.size() - 1);
			for (uint32_t i = 0;;) {
				const uint32_t l = i * 2 + 1;
				const uint32_t r = l + 1;
				uint32_t m = i;
				if (l < heap.size() && less(heap[m], heap[l])) {
					m = l;
				}
				if (r < heap.size() && less(heap[m], heap[r])) {
					m = r;
				}
				if (m == i) {
					break;
				}
				SWAP(heap[i], heap[m]);
				i = m;
			}
			if (entry.distance > best[entry.polygon]) {
				continue;
			}
			const Vector3 from = position[entry.polygon];
			if (entry.polygon == target) {
				return entry.distance + from.distance_to(p_candidate.nav_end);
			}
			for (const Portal &portal : adjacency[entry.polygon]) {
				// Greedy: cross each shared edge at its point closest to where we are.
				const Vector3 crossing = Geometry3D::get_closest_point_to_segment(from, portal.a, portal.b);
				const real_t distance = entry.distance + from.distance_to(crossing);
				if (distance > walk_limit) {
					continue;
				}
				HashMap<int, real_t>::Iterator known = best.find(portal.polygon);
				if (known && known->value <= distance) {
					continue;
				}
				best[portal.polygon] = distance;
				position[portal.polygon] = crossing;
				heap.push_back({ distance, portal.polygon });
				for (uint32_t i = heap.size() - 1; i > 0;) {
					const uint32_t parent = (i - 1) / 2;
					if (!less(heap[parent], heap[i])) {
						break;
					}
					SWAP(heap[parent], heap[i]);
					i = parent;
				}
			}
		}
		return real_t(Math::INF);
	}

	void _surface_quad(const Candidate &p_candidate, Vector3 *r_quad) const {
		const Candidate &c = p_candidate;
		const BoundaryEdge &target = edges[c.target_edge];
		const Vector3 entry = -c.direction * settings.edge_inset;
		const Vector3 exit = -target.outward * settings.edge_inset;
		const Vector2 segment = xz(target.b - target.a);
		const Vector2 across = xz(c.direction);
		const Vector3 span_points[2] = { c.span[1], c.span[0] };
		r_quad[0] = c.span[0] + entry;
		r_quad[1] = c.span[1] + entry;
		for (int i = 0; i < 2; i++) {
			const Vector2 offset = xz(span_points[i] - target.a);
			const real_t denominator = segment.cross(across);
			const real_t fraction = Math::abs(denominator) > CMP_EPSILON ? CLAMP(offset.cross(across) / denominator, real_t(0.0), real_t(1.0)) : real_t(0.5);
			r_quad[2 + i] = target.a.lerp(target.b, fraction) + exit;
		}
	}

public:
	Baker(const NavigationTraversalBaker3D::Settings &p_settings, const Vector<Vector3> &p_vertices, const Vector<Vector<int>> &p_polygons) :
			settings(p_settings), nav_vertices(p_vertices), nav_polygons(p_polygons) {}

	void run(const Vector<float> &p_geometry_vertices, const Vector<int> &p_geometry_indices, NavigationTraversalBaker3D::Output &r_output) {
		const uint64_t started = OS::get_singleton()->get_ticks_usec();
		r_output.vertices = nav_vertices;
		r_output.polygons = nav_polygons;
		tree.create(p_geometry_vertices, p_geometry_indices);
		_collect_edges();
		walk_limit = settings.search_distance * 5.0;

		// 1. Sample every boundary edge and measure each span against the geometry.
		int segments = 0;
		for (uint32_t edge_index = 0; edge_index < edges.size(); edge_index++) {
			const BoundaryEdge &edge = edges[edge_index];
			const Vector2 offset = xz(edge.outward) * settings.search_distance;
			const Vector2 low = edge.min + offset.min(Vector2()) - Vector2(SURFACE_MARGIN, SURFACE_MARGIN);
			const Vector2 high = edge.max + offset.max(Vector2()) + Vector2(SURFACE_MARGIN, SURFACE_MARGIN);
			LocalVector<int> facing;
			HashMap<int, bool> seen;
			_for_buckets(edge, settings.search_distance + SURFACE_MARGIN, [&](const Vector2i &p_cell) {
				auto bucket = buckets.find(p_cell);
				if (!bucket) {
					return;
				}
				for (int other_index : bucket->value) {
					const BoundaryEdge &other = edges[other_index];
					if (other.polygon == edge.polygon || seen.has(other_index)) {
						continue;
					}
					seen.insert(other_index, true);
					if (other.min.x <= high.x && other.max.x >= low.x && other.min.y <= high.y && other.max.y >= low.y && edge.outward.dot(other.outward) <= -0.25) {
						facing.push_back(other_index);
					}
				}
			});
			// Bucket order depends on the grid; sort for a deterministic result.
			facing.sort();

			const real_t length = xz(edge.b - edge.a).length();
			const int count = MAX(1, int(Math::ceil(length / settings.sample_spacing)));
			for (int i = 0; i < count; i++) {
				segments++;
				Candidate c;
				c.edge = edge_index;
				c.span[0] = edge.a.lerp(edge.b, real_t(i) / count);
				c.span[1] = edge.a.lerp(edge.b, real_t(i + 1) / count);
				c.direction = edge.outward;
				c.side = c.direction.cross(Vector3(0, 1, 0));
				c.source_polygon = edge.polygon;
				const Vector3 center = (c.span[0] + c.span[1]) * 0.5;
				c.nav_start = center - c.direction * settings.edge_inset;
				const Destination destination = facing.is_empty() ? Destination() : _find_destination(center, c.direction, facing);
				if (!destination.found) {
					continue;
				}
				c.target_polygon = destination.polygon;
				c.target_edge = destination.edge;
				c.nav_end = destination.point;
				_measure(c);
				candidates.push_back(c);
			}
		}

		// 2. Classify per profile; a span may yield several kinds for different profiles.
		LocalVector<Accepted> accepted;
		Array debug_candidates;
		int accepted_spans = 0;
		for (uint32_t candidate_index = 0; candidate_index < candidates.size(); candidate_index++) {
			const Candidate &c = candidates[candidate_index];
			LocalVector<Profile3D::TraversalKind> kinds;
			if (c.geometry_reject == Profile3D::REJECT_NONE) {
				if (c.shape == SHAPE_UP) {
					kinds.push_back(Profile3D::KIND_CLIMB);
				} else if (c.shape == SHAPE_DOWN) {
					kinds.push_back(Profile3D::KIND_DROP);
					kinds.push_back(Profile3D::KIND_HANG_DROP);
				} else if (c.shape == SHAPE_OBSTACLE) {
					kinds.push_back(Profile3D::KIND_VAULT);
					kinds.push_back(Profile3D::KIND_CLIMB_OVER);
				}
			}
			LocalVector<Reject> reasons;
			LocalVector<int> chosen;
			reasons.resize(settings.profiles.size());
			chosen.resize(settings.profiles.size());
			for (uint32_t p = 0; p < settings.profiles.size(); p++) {
				const NavigationTraversalBaker3D::Profile &profile = settings.profiles[p];
				chosen[p] = -1;
				reasons[p] = c.geometry_reject != Profile3D::REJECT_NONE ? c.geometry_reject : Profile3D::REJECT_KIND;
				Reject first_detail = Profile3D::REJECT_NONE;
				bool any_allowed = false;
				for (Profile3D::TraversalKind kind : kinds) {
					real_t height = 0.0;
					const Reject reason = _evaluate(c, profile, kind, height);
					if (reason == Profile3D::REJECT_NONE) {
						chosen[p] = kind;
						break;
					}
					if (reason != Profile3D::REJECT_KIND) {
						any_allowed = true;
						if (reason != Profile3D::REJECT_HEIGHT && first_detail == Profile3D::REJECT_NONE) {
							first_detail = reason;
						}
					}
				}
				if (chosen[p] >= 0) {
					reasons[p] = Profile3D::REJECT_NONE;
				} else if (c.geometry_reject == Profile3D::REJECT_NONE) {
					reasons[p] = !any_allowed ? Profile3D::REJECT_KIND : (first_detail != Profile3D::REJECT_NONE ? first_detail : Profile3D::REJECT_HEIGHT);
				}
			}

			// Clearance per kind (the body path differs), then the ramp-side filter.
			HashMap<int, bool> clear_by_kind;
			bool walk_checked = false;
			bool walkable = false;
			for (uint32_t p = 0; p < settings.profiles.size(); p++) {
				if (chosen[p] < 0) {
					continue;
				}
				const Profile3D::TraversalKind kind = Profile3D::TraversalKind(chosen[p]);
				if (!clear_by_kind.has(kind)) {
					clear_by_kind.insert(kind, _path_clear(_body_path(c, kind)));
				}
				if (!clear_by_kind[kind]) {
					reasons[p] = Profile3D::REJECT_BLOCKED;
					chosen[p] = -1;
					continue;
				}
				if (c.sloped_edge) {
					if (!walk_checked) {
						walk_checked = true;
						walkable = _walk_distance(c) <= walk_limit;
					}
					if (walkable) {
						reasons[p] = Profile3D::REJECT_WALKABLE;
						chosen[p] = -1;
					}
				}
			}

			for (int kind = 0; kind < Profile3D::KIND_MAX; kind++) {
				uint32_t layers = 0;
				real_t enter_cost = real_t(Math::INF);
				Vector<Vector3> path;
				for (uint32_t p = 0; p < settings.profiles.size(); p++) {
					if (chosen[p] != kind) {
						continue;
					}
					if (path.is_empty()) {
						path = _body_path(c, Profile3D::TraversalKind(kind));
					}
					const NavigationTraversalBaker3D::Profile &profile = settings.profiles[p];
					layers |= profile.layer_mask;
					const real_t duration = NavigationTraversalBaker3D::path_duration(path, Profile3D::TraversalKind(kind));
					const real_t length = c.nav_start.distance_to(c.nav_end);
					enter_cost = MIN(enter_cost, MAX(real_t(0.0), duration * profile.time_scale + profile.extra_penalty - length));
				}
				if (layers == 0) {
					continue;
				}
				Accepted entry;
				entry.candidate = candidate_index;
				entry.kind = Profile3D::TraversalKind(kind);
				entry.layers = layers;
				entry.enter_cost = enter_cost;
				entry.body_path = path;
				entry.anchors = _anchors(c, entry.kind);
				_surface_quad(c, entry.quad);
				if (entry.quad[0].distance_squared_to(entry.quad[1]) < 0.0001 || entry.quad[2].distance_squared_to(entry.quad[3]) < 0.0001) {
					continue;
				}
				accepted.push_back(entry);
				accepted_spans++;
			}

			bool rejected = false;
			for (Reject reason : reasons) {
				rejected = rejected || reason != Profile3D::REJECT_NONE;
			}
			if (settings.keep_rejected && rejected) {
				Dictionary record;
				record["approach"] = c.start;
				record["exit"] = c.finish;
				record["shape"] = int(c.shape);
				Dictionary profile_reasons;
				for (uint32_t p = 0; p < settings.profiles.size(); p++) {
					profile_reasons[settings.profiles[p].id] = int(reasons[p]);
				}
				record["reasons"] = profile_reasons;
				debug_candidates.push_back(record);
			}
		}

		// 3. Merge consecutive spans of one edge with the same kind, layers and polygon pair.
		struct Group {
			LocalVector<int> members;
		};
		LocalVector<Group> groups;
		HashMap<String, int> open;
		int open_edge = -1;
		for (uint32_t i = 0; i < accepted.size(); i++) {
			const Accepted &entry = accepted[i];
			const Candidate &c = candidates[entry.candidate];
			if (c.edge != open_edge) {
				open.clear();
				open_edge = c.edge;
			}
			const String key = vformat("%d:%d:%d:%d", int(entry.kind), int64_t(entry.layers), c.source_polygon, c.target_polygon);
			HashMap<String, int>::Iterator group = open.find(key);
			if (group) {
				const Accepted &last = accepted[groups[group->value].members[groups[group->value].members.size() - 1]];
				const Candidate &previous = candidates[last.candidate];
				if (previous.span[1].distance_to(c.span[0]) <= 0.01 && Math::abs(previous.start.y - c.start.y) <= MERGE_TOLERANCE &&
						Math::abs(previous.finish.y - c.finish.y) <= MERGE_TOLERANCE && Math::abs(previous.crest - c.crest) <= MERGE_TOLERANCE) {
					groups[group->value].members.push_back(i);
					continue;
				}
			}
			Group created;
			created.members.push_back(i);
			open[key] = groups.size();
			groups.push_back(created);
		}

		// 4. Emit one polygon per group.
		for (const Group &group : groups) {
			const Accepted &first = accepted[group.members[0]];
			const Accepted &last = accepted[group.members[group.members.size() - 1]];
			const Accepted &middle = accepted[group.members[group.members.size() / 2]];
			const Candidate &c = candidates[middle.candidate];
			const Vector3 quad[4] = { first.quad[0], last.quad[1], last.quad[2], first.quad[3] };
			if (quad[0].distance_squared_to(quad[1]) < 0.0001 || quad[2].distance_squared_to(quad[3]) < 0.0001) {
				continue;
			}
			const int base = r_output.vertices.size();
			Vector<int> polygon;
			for (int i = 0; i < 4; i++) {
				r_output.vertices.push_back(quad[i]);
				polygon.push_back(base + i);
			}
			const int index = r_output.polygons.size();
			r_output.polygons.push_back(polygon);

			// The representative span sits in the middle; the body may start anywhere on `span`.
			const Vector3 center = (c.span[0] + c.span[1]) * 0.5;
			const Candidate &first_c = candidates[first.candidate];
			const Candidate &last_c = candidates[last.candidate];
			Vector<Vector3> span;
			span.push_back(c.start + ((first_c.span[0] + first_c.span[1]) * 0.5 - center) * Vector3(1, 0, 1));
			span.push_back(c.start + ((last_c.span[0] + last_c.span[1]) * 0.5 - center) * Vector3(1, 0, 1));

			Dictionary surface;
			surface["source"] = c.source_polygon;
			surface["target"] = c.target_polygon;
			surface["layers"] = int64_t(middle.layers);
			surface["enter_cost"] = middle.enter_cost;
			surface["kind"] = int(middle.kind);
			surface["approach"] = c.start;
			surface["exit"] = c.finish;
			surface["anchors"] = middle.anchors;
			surface["crest"] = c.crest;
			surface["body_path"] = middle.body_path;
			surface["span"] = span;
			r_output.surfaces[index] = surface;
		}

		Dictionary debug;
		debug["segments"] = segments;
		debug["candidates"] = int(candidates.size());
		debug["accepted_segments"] = accepted_spans;
		debug["surfaces"] = r_output.surfaces.size();
		debug["usec"] = int64_t(OS::get_singleton()->get_ticks_usec() - started);
		if (settings.keep_rejected) {
			debug["rejected"] = debug_candidates;
		}
		r_output.debug = debug;
	}
};

} // namespace

real_t NavigationTraversalBaker3D::path_duration(const Vector<Vector3> &p_path, Kind p_kind) {
	real_t seconds = SURFACE_MARGIN / LANDING_SPEED;
	const bool vault = p_kind == NavigationTraversalProfile3D::KIND_VAULT;
	for (int i = 1; i < p_path.size(); i++) {
		const Vector3 motion = p_path[i] - p_path[i - 1];
		real_t speed = vault ? VAULT_MOVE_SPEED : CLIMB_MOVE_SPEED;
		if (motion.y > WAYPOINT_TOLERANCE) {
			speed = vault ? VAULT_SPEED : CLIMB_SPEED;
		} else if (motion.y < -WAYPOINT_TOLERANCE) {
			speed = LANDING_SPEED;
		}
		seconds += motion.length() / speed;
	}
	return seconds;
}

NavigationTraversalBaker3D::Settings NavigationTraversalBaker3D::make_settings(const Ref<NavigationTraversalRules3D> &p_rules, real_t p_cell_height) {
	Settings settings;
	ERR_FAIL_COND_V(p_rules.is_null(), settings);
	settings.search_distance = p_rules->get_search_distance();
	settings.search_height = p_rules->get_search_height();
	settings.sample_spacing = MAX(real_t(0.05), p_rules->get_sample_spacing());
	settings.edge_inset = p_rules->get_edge_inset();
	settings.support_search = p_rules->get_support_search();
	settings.body_radius = MAX(real_t(0.01), p_rules->get_body_radius());
	settings.body_height = MAX(settings.body_radius * 2.0, p_rules->get_body_height());
	settings.hand_spacing = p_rules->get_hand_spacing();
	settings.grip_probe_depth = p_rules->get_grip_probe_depth();
	settings.cell_height = p_cell_height;
	settings.keep_rejected = p_rules->is_keeping_rejected_candidates();
	const TypedArray<NavigationTraversalProfile3D> profiles = p_rules->get_profiles();
	for (int i = 0; i < profiles.size(); i++) {
		const Ref<NavigationTraversalProfile3D> profile = profiles[i];
		// Duplicate ids: the first profile wins, as in get_profile().
		if (profile.is_null() || p_rules->get_profile(profile->get_id()) != profile) {
			continue;
		}
		Profile snapshot;
		snapshot.id = profile->get_id();
		snapshot.layer_mask = profile->get_layer_mask();
		snapshot.allowed_kinds = static_cast<uint32_t>(static_cast<NavigationTraversalProfile3D::TraversalKindFlags>(profile->get_allowed_kinds()));
		for (int kind = 0; kind < NavigationTraversalProfile3D::KIND_MAX; kind++) {
			snapshot.height_ranges[kind] = profile->get_height_range(NavigationTraversalProfile3D::TraversalKind(kind));
		}
		snapshot.max_gap = profile->get_max_gap();
		snapshot.max_slope = profile->get_max_slope();
		snapshot.max_thickness = profile->get_max_thickness();
		snapshot.max_landing_delta = profile->get_max_landing_delta();
		snapshot.max_grip_angle = profile->get_max_grip_angle();
		snapshot.max_grip_depth = profile->get_max_grip_depth();
		snapshot.time_scale = profile->get_time_scale();
		snapshot.extra_penalty = profile->get_extra_penalty();
		settings.profiles.push_back(snapshot);
	}
	return settings;
}

void NavigationTraversalBaker3D::bake(const Settings &p_settings, const Vector<float> &p_geometry_vertices, const Vector<int> &p_geometry_indices,
		const Vector<Vector3> &p_vertices, const Vector<Vector<int>> &p_polygons, Output &r_output) {
	Baker baker(p_settings, p_vertices, p_polygons);
	baker.run(p_geometry_vertices, p_geometry_indices, r_output);
}
