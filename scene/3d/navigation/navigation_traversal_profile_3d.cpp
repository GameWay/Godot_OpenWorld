/**************************************************************************/
/*  navigation_traversal_profile_3d.cpp                                   */
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

#include "navigation_traversal_profile_3d.h"

#include "core/object/class_db.h"

void NavigationTraversalProfile3D::set_id(const StringName &p_id) {
	if (id == p_id) {
		return;
	}
	id = p_id;
	emit_changed();
}

StringName NavigationTraversalProfile3D::get_id() const {
	return id;
}

void NavigationTraversalProfile3D::set_layer_bit(int p_layer_bit) {
	ERR_FAIL_INDEX_MSG(p_layer_bit, MAX_LAYER_BIT + 1, "Traversal profile layer bit must be between 0 and 31.");
	if (layer_bit == p_layer_bit) {
		return;
	}
	layer_bit = p_layer_bit;
	emit_changed();
}

int NavigationTraversalProfile3D::get_layer_bit() const {
	return layer_bit;
}

uint32_t NavigationTraversalProfile3D::get_layer_mask() const {
	return 1u << layer_bit;
}

void NavigationTraversalProfile3D::set_allowed_kinds(BitField<TraversalKindFlags> p_kinds) {
	const BitField<TraversalKindFlags> kinds = int64_t(p_kinds) & KIND_FLAG_ALL;
	if (allowed_kinds == kinds) {
		return;
	}
	allowed_kinds = kinds;
	emit_changed();
}

BitField<NavigationTraversalProfile3D::TraversalKindFlags> NavigationTraversalProfile3D::get_allowed_kinds() const {
	return allowed_kinds;
}

void NavigationTraversalProfile3D::set_kind_allowed(TraversalKind p_kind, bool p_allowed) {
	ERR_FAIL_INDEX(p_kind, KIND_MAX);
	int64_t kinds = allowed_kinds;
	if (p_allowed) {
		kinds |= int64_t(1) << p_kind;
	} else {
		kinds &= ~(int64_t(1) << p_kind);
	}
	set_allowed_kinds(kinds);
}

bool NavigationTraversalProfile3D::is_kind_allowed(TraversalKind p_kind) const {
	ERR_FAIL_INDEX_V(p_kind, KIND_MAX, false);
	return (int64_t(allowed_kinds) & (int64_t(1) << p_kind)) != 0;
}

void NavigationTraversalProfile3D::set_height_range(TraversalKind p_kind, const Vector2 &p_range) {
	ERR_FAIL_INDEX(p_kind, KIND_MAX);
	if (height_ranges[p_kind] == p_range) {
		return;
	}
	height_ranges[p_kind] = p_range;
	emit_changed();
}

Vector2 NavigationTraversalProfile3D::get_height_range(TraversalKind p_kind) const {
	ERR_FAIL_INDEX_V(p_kind, KIND_MAX, Vector2());
	return height_ranges[p_kind];
}

bool NavigationTraversalProfile3D::is_height_in_range(TraversalKind p_kind, real_t p_height) const {
	ERR_FAIL_INDEX_V(p_kind, KIND_MAX, false);
	const Vector2 &range = height_ranges[p_kind];
	return p_height >= range.x && p_height <= range.y;
}

#define TRAVERSAL_PROFILE_REAL_PROPERTY(m_name)                       \
	void NavigationTraversalProfile3D::set_##m_name(real_t p_value) { \
		if (m_name == p_value) {                                       \
			return;                                                    \
		}                                                              \
		m_name = p_value;                                              \
		emit_changed();                                                \
	}                                                                  \
	real_t NavigationTraversalProfile3D::get_##m_name() const {       \
		return m_name;                                                 \
	}

TRAVERSAL_PROFILE_REAL_PROPERTY(max_gap)
TRAVERSAL_PROFILE_REAL_PROPERTY(max_slope)
TRAVERSAL_PROFILE_REAL_PROPERTY(max_thickness)
TRAVERSAL_PROFILE_REAL_PROPERTY(max_landing_delta)
TRAVERSAL_PROFILE_REAL_PROPERTY(max_grip_angle)
TRAVERSAL_PROFILE_REAL_PROPERTY(max_grip_depth)
TRAVERSAL_PROFILE_REAL_PROPERTY(time_scale)
TRAVERSAL_PROFILE_REAL_PROPERTY(extra_penalty)

#undef TRAVERSAL_PROFILE_REAL_PROPERTY

real_t NavigationTraversalProfile3D::get_enter_cost(real_t p_duration, real_t p_length) const {
	return MAX(real_t(0.0), p_duration * time_scale + extra_penalty - p_length);
}

void NavigationTraversalProfile3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_id", "id"), &NavigationTraversalProfile3D::set_id);
	ClassDB::bind_method(D_METHOD("get_id"), &NavigationTraversalProfile3D::get_id);
	ClassDB::bind_method(D_METHOD("set_layer_bit", "layer_bit"), &NavigationTraversalProfile3D::set_layer_bit);
	ClassDB::bind_method(D_METHOD("get_layer_bit"), &NavigationTraversalProfile3D::get_layer_bit);
	ClassDB::bind_method(D_METHOD("get_layer_mask"), &NavigationTraversalProfile3D::get_layer_mask);
	ClassDB::bind_method(D_METHOD("set_allowed_kinds", "kinds"), &NavigationTraversalProfile3D::set_allowed_kinds);
	ClassDB::bind_method(D_METHOD("get_allowed_kinds"), &NavigationTraversalProfile3D::get_allowed_kinds);
	ClassDB::bind_method(D_METHOD("set_kind_allowed", "kind", "allowed"), &NavigationTraversalProfile3D::set_kind_allowed);
	ClassDB::bind_method(D_METHOD("is_kind_allowed", "kind"), &NavigationTraversalProfile3D::is_kind_allowed);
	ClassDB::bind_method(D_METHOD("set_height_range", "kind", "range"), &NavigationTraversalProfile3D::set_height_range);
	ClassDB::bind_method(D_METHOD("get_height_range", "kind"), &NavigationTraversalProfile3D::get_height_range);
	ClassDB::bind_method(D_METHOD("is_height_in_range", "kind", "height"), &NavigationTraversalProfile3D::is_height_in_range);
	ClassDB::bind_method(D_METHOD("set_max_gap", "max_gap"), &NavigationTraversalProfile3D::set_max_gap);
	ClassDB::bind_method(D_METHOD("get_max_gap"), &NavigationTraversalProfile3D::get_max_gap);
	ClassDB::bind_method(D_METHOD("set_max_slope", "max_slope"), &NavigationTraversalProfile3D::set_max_slope);
	ClassDB::bind_method(D_METHOD("get_max_slope"), &NavigationTraversalProfile3D::get_max_slope);
	ClassDB::bind_method(D_METHOD("set_max_thickness", "max_thickness"), &NavigationTraversalProfile3D::set_max_thickness);
	ClassDB::bind_method(D_METHOD("get_max_thickness"), &NavigationTraversalProfile3D::get_max_thickness);
	ClassDB::bind_method(D_METHOD("set_max_landing_delta", "max_landing_delta"), &NavigationTraversalProfile3D::set_max_landing_delta);
	ClassDB::bind_method(D_METHOD("get_max_landing_delta"), &NavigationTraversalProfile3D::get_max_landing_delta);
	ClassDB::bind_method(D_METHOD("set_max_grip_angle", "max_grip_angle"), &NavigationTraversalProfile3D::set_max_grip_angle);
	ClassDB::bind_method(D_METHOD("get_max_grip_angle"), &NavigationTraversalProfile3D::get_max_grip_angle);
	ClassDB::bind_method(D_METHOD("set_max_grip_depth", "max_grip_depth"), &NavigationTraversalProfile3D::set_max_grip_depth);
	ClassDB::bind_method(D_METHOD("get_max_grip_depth"), &NavigationTraversalProfile3D::get_max_grip_depth);
	ClassDB::bind_method(D_METHOD("set_time_scale", "time_scale"), &NavigationTraversalProfile3D::set_time_scale);
	ClassDB::bind_method(D_METHOD("get_time_scale"), &NavigationTraversalProfile3D::get_time_scale);
	ClassDB::bind_method(D_METHOD("set_extra_penalty", "extra_penalty"), &NavigationTraversalProfile3D::set_extra_penalty);
	ClassDB::bind_method(D_METHOD("get_extra_penalty"), &NavigationTraversalProfile3D::get_extra_penalty);
	ClassDB::bind_method(D_METHOD("get_enter_cost", "duration", "length"), &NavigationTraversalProfile3D::get_enter_cost);

	static constexpr const char *HINT_HEIGHT = "0,10,0.01,or_greater,suffix:m";
	static constexpr const char *HINT_DISTANCE = "0,10,0.01,or_greater,suffix:m";
	static constexpr const char32_t *HINT_ANGLE = U"0,90,0.1,suffix:°";

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "id"), "set_id", "get_id");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "layer_bit", PROPERTY_HINT_RANGE, "0,31,1"), "set_layer_bit", "get_layer_bit");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "allowed_kinds", PROPERTY_HINT_FLAGS, "Climb,Climb Over,Vault,Drop,Hang Drop"), "set_allowed_kinds", "get_allowed_kinds");

	ADD_GROUP("Common", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_gap", PROPERTY_HINT_RANGE, HINT_DISTANCE), "set_max_gap", "get_max_gap");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_slope", PROPERTY_HINT_RANGE, HINT_ANGLE), "set_max_slope", "get_max_slope");

	ADD_GROUP("Climb", "");
	ADD_PROPERTYI(PropertyInfo(Variant::VECTOR2, "climb_height", PROPERTY_HINT_RANGE, HINT_HEIGHT), "set_height_range", "get_height_range", KIND_CLIMB);

	ADD_GROUP("Climb Over", "");
	ADD_PROPERTYI(PropertyInfo(Variant::VECTOR2, "climb_over_height", PROPERTY_HINT_RANGE, HINT_HEIGHT), "set_height_range", "get_height_range", KIND_CLIMB_OVER);

	ADD_GROUP("Vault", "");
	ADD_PROPERTYI(PropertyInfo(Variant::VECTOR2, "vault_height", PROPERTY_HINT_RANGE, HINT_HEIGHT), "set_height_range", "get_height_range", KIND_VAULT);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_thickness", PROPERTY_HINT_RANGE, HINT_DISTANCE), "set_max_thickness", "get_max_thickness");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_landing_delta", PROPERTY_HINT_RANGE, HINT_DISTANCE), "set_max_landing_delta", "get_max_landing_delta");

	ADD_GROUP("Drop", "");
	ADD_PROPERTYI(PropertyInfo(Variant::VECTOR2, "drop_height", PROPERTY_HINT_RANGE, HINT_HEIGHT), "set_height_range", "get_height_range", KIND_DROP);

	ADD_GROUP("Hang Drop", "");
	ADD_PROPERTYI(PropertyInfo(Variant::VECTOR2, "hang_drop_height", PROPERTY_HINT_RANGE, HINT_HEIGHT), "set_height_range", "get_height_range", KIND_HANG_DROP);

	ADD_GROUP("Grip", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_grip_angle", PROPERTY_HINT_RANGE, HINT_ANGLE), "set_max_grip_angle", "get_max_grip_angle");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_grip_depth", PROPERTY_HINT_RANGE, HINT_DISTANCE), "set_max_grip_depth", "get_max_grip_depth");

	ADD_GROUP("Cost", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "time_scale", PROPERTY_HINT_RANGE, "0,100,0.01,or_greater"), "set_time_scale", "get_time_scale");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "extra_penalty", PROPERTY_HINT_RANGE, "0,100,0.01,or_greater"), "set_extra_penalty", "get_extra_penalty");

	BIND_ENUM_CONSTANT(KIND_CLIMB);
	BIND_ENUM_CONSTANT(KIND_CLIMB_OVER);
	BIND_ENUM_CONSTANT(KIND_VAULT);
	BIND_ENUM_CONSTANT(KIND_DROP);
	BIND_ENUM_CONSTANT(KIND_HANG_DROP);
	BIND_ENUM_CONSTANT(KIND_MAX);

	BIND_BITFIELD_FLAG(KIND_FLAG_CLIMB);
	BIND_BITFIELD_FLAG(KIND_FLAG_CLIMB_OVER);
	BIND_BITFIELD_FLAG(KIND_FLAG_VAULT);
	BIND_BITFIELD_FLAG(KIND_FLAG_DROP);
	BIND_BITFIELD_FLAG(KIND_FLAG_HANG_DROP);
	BIND_BITFIELD_FLAG(KIND_FLAG_ALL);

	BIND_ENUM_CONSTANT(REJECT_NONE);
	BIND_ENUM_CONSTANT(REJECT_KIND);
	BIND_ENUM_CONSTANT(REJECT_NO_SUPPORT);
	BIND_ENUM_CONSTANT(REJECT_HEIGHT);
	BIND_ENUM_CONSTANT(REJECT_GAP);
	BIND_ENUM_CONSTANT(REJECT_SLOPE);
	BIND_ENUM_CONSTANT(REJECT_NO_GRIP);
	BIND_ENUM_CONSTANT(REJECT_GRIP_ANGLE);
	BIND_ENUM_CONSTANT(REJECT_GRIP_DEPTH);
	BIND_ENUM_CONSTANT(REJECT_BLOCKED);
	BIND_ENUM_CONSTANT(REJECT_THICKNESS);
	BIND_ENUM_CONSTANT(REJECT_LANDING_DELTA);
	BIND_ENUM_CONSTANT(REJECT_MAX);
}
