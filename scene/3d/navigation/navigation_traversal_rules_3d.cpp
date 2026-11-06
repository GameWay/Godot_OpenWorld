/**************************************************************************/
/*  navigation_traversal_rules_3d.cpp                                     */
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

#include "navigation_traversal_rules_3d.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/core_string_names.h"
#include "core/templates/hash_map.h"

#define TRAVERSAL_RULES_REAL_PROPERTY(m_name)                       \
	void NavigationTraversalRules3D::set_##m_name(real_t p_value) { \
		if (m_name == p_value) {                                     \
			return;                                                  \
		}                                                            \
		m_name = p_value;                                            \
		emit_changed();                                              \
	}                                                                \
	real_t NavigationTraversalRules3D::get_##m_name() const {       \
		return m_name;                                               \
	}

TRAVERSAL_RULES_REAL_PROPERTY(search_distance)
TRAVERSAL_RULES_REAL_PROPERTY(search_height)
TRAVERSAL_RULES_REAL_PROPERTY(sample_spacing)
TRAVERSAL_RULES_REAL_PROPERTY(edge_inset)
TRAVERSAL_RULES_REAL_PROPERTY(support_search)
TRAVERSAL_RULES_REAL_PROPERTY(body_radius)
TRAVERSAL_RULES_REAL_PROPERTY(body_height)
TRAVERSAL_RULES_REAL_PROPERTY(hand_spacing)
TRAVERSAL_RULES_REAL_PROPERTY(grip_probe_depth)

#undef TRAVERSAL_RULES_REAL_PROPERTY

void NavigationTraversalRules3D::_profile_changed() {
	emit_changed();
}

void NavigationTraversalRules3D::set_profiles(const TypedArray<NavigationTraversalProfile3D> &p_profiles) {
	const Callable on_changed = callable_mp(this, &NavigationTraversalRules3D::_profile_changed);
	for (const Ref<NavigationTraversalProfile3D> &profile : profiles) {
		if (profile.is_valid() && profile->is_connected(CoreStringName(changed), on_changed)) {
			profile->disconnect_changed(on_changed);
		}
	}
	profiles.clear();
	for (int i = 0; i < p_profiles.size(); i++) {
		// Keep empty entries so the inspector can append new elements.
		const Ref<NavigationTraversalProfile3D> profile = p_profiles[i];
		profiles.push_back(profile);
		if (profile.is_valid()) {
			profile->connect_changed(on_changed);
		}
	}
	emit_changed();
}

TypedArray<NavigationTraversalProfile3D> NavigationTraversalRules3D::get_profiles() const {
	TypedArray<NavigationTraversalProfile3D> result;
	for (const Ref<NavigationTraversalProfile3D> &profile : profiles) {
		result.push_back(profile);
	}
	return result;
}

Ref<NavigationTraversalProfile3D> NavigationTraversalRules3D::get_profile(const StringName &p_id) const {
	if (p_id == StringName()) {
		return Ref<NavigationTraversalProfile3D>();
	}
	for (const Ref<NavigationTraversalProfile3D> &profile : profiles) {
		if (profile.is_valid() && profile->get_id() == p_id) {
			return profile;
		}
	}
	return Ref<NavigationTraversalProfile3D>();
}

bool NavigationTraversalRules3D::has_profile(const StringName &p_id) const {
	return get_profile(p_id).is_valid();
}

uint32_t NavigationTraversalRules3D::get_layers_for(const StringName &p_id) const {
	const Ref<NavigationTraversalProfile3D> profile = get_profile(p_id);
	return profile.is_valid() ? profile->get_layer_mask() : 0;
}

uint32_t NavigationTraversalRules3D::get_all_layers() const {
	uint32_t layers = 0;
	for (const Ref<NavigationTraversalProfile3D> &profile : profiles) {
		if (profile.is_valid()) {
			layers |= profile->get_layer_mask();
		}
	}
	return layers;
}

PackedStringArray NavigationTraversalRules3D::get_validation_warnings() const {
	PackedStringArray warnings;
	HashMap<StringName, int> ids;
	HashMap<int, StringName> bits;
	for (int i = 0; i < profiles.size(); i++) {
		const Ref<NavigationTraversalProfile3D> &profile = profiles[i];
		if (profile.is_null()) {
			warnings.push_back(vformat(RTR("Traversal profile %d is empty."), i));
			continue;
		}
		const StringName id = profile->get_id();
		if (id == StringName()) {
			warnings.push_back(vformat(RTR("Traversal profile %d has no id."), i));
		} else if (ids.has(id)) {
			warnings.push_back(vformat(RTR("Traversal profiles %d and %d share the id \"%s\"; only the first one is used."), ids[id], i, id));
		} else {
			ids.insert(id, i);
		}
		const int bit = profile->get_layer_bit();
		if (bits.has(bit)) {
			warnings.push_back(vformat(RTR("Traversal profiles \"%s\" and \"%s\" share layer bit %d; their traversal surfaces cannot be told apart."), bits[bit], id, bit));
		} else {
			bits.insert(bit, id);
		}
	}
	return warnings;
}

NavigationTraversalRules3D::~NavigationTraversalRules3D() {
	const Callable on_changed = callable_mp(this, &NavigationTraversalRules3D::_profile_changed);
	for (const Ref<NavigationTraversalProfile3D> &profile : profiles) {
		if (profile.is_valid() && profile->is_connected(CoreStringName(changed), on_changed)) {
			profile->disconnect_changed(on_changed);
		}
	}
}

void NavigationTraversalRules3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_search_distance", "search_distance"), &NavigationTraversalRules3D::set_search_distance);
	ClassDB::bind_method(D_METHOD("get_search_distance"), &NavigationTraversalRules3D::get_search_distance);
	ClassDB::bind_method(D_METHOD("set_search_height", "search_height"), &NavigationTraversalRules3D::set_search_height);
	ClassDB::bind_method(D_METHOD("get_search_height"), &NavigationTraversalRules3D::get_search_height);
	ClassDB::bind_method(D_METHOD("set_sample_spacing", "sample_spacing"), &NavigationTraversalRules3D::set_sample_spacing);
	ClassDB::bind_method(D_METHOD("get_sample_spacing"), &NavigationTraversalRules3D::get_sample_spacing);
	ClassDB::bind_method(D_METHOD("set_edge_inset", "edge_inset"), &NavigationTraversalRules3D::set_edge_inset);
	ClassDB::bind_method(D_METHOD("get_edge_inset"), &NavigationTraversalRules3D::get_edge_inset);
	ClassDB::bind_method(D_METHOD("set_support_search", "support_search"), &NavigationTraversalRules3D::set_support_search);
	ClassDB::bind_method(D_METHOD("get_support_search"), &NavigationTraversalRules3D::get_support_search);
	ClassDB::bind_method(D_METHOD("set_body_radius", "body_radius"), &NavigationTraversalRules3D::set_body_radius);
	ClassDB::bind_method(D_METHOD("get_body_radius"), &NavigationTraversalRules3D::get_body_radius);
	ClassDB::bind_method(D_METHOD("set_body_height", "body_height"), &NavigationTraversalRules3D::set_body_height);
	ClassDB::bind_method(D_METHOD("get_body_height"), &NavigationTraversalRules3D::get_body_height);
	ClassDB::bind_method(D_METHOD("set_hand_spacing", "hand_spacing"), &NavigationTraversalRules3D::set_hand_spacing);
	ClassDB::bind_method(D_METHOD("get_hand_spacing"), &NavigationTraversalRules3D::get_hand_spacing);
	ClassDB::bind_method(D_METHOD("set_grip_probe_depth", "grip_probe_depth"), &NavigationTraversalRules3D::set_grip_probe_depth);
	ClassDB::bind_method(D_METHOD("get_grip_probe_depth"), &NavigationTraversalRules3D::get_grip_probe_depth);
	ClassDB::bind_method(D_METHOD("set_profiles", "profiles"), &NavigationTraversalRules3D::set_profiles);
	ClassDB::bind_method(D_METHOD("get_profiles"), &NavigationTraversalRules3D::get_profiles);

	ClassDB::bind_method(D_METHOD("get_profile", "id"), &NavigationTraversalRules3D::get_profile);
	ClassDB::bind_method(D_METHOD("has_profile", "id"), &NavigationTraversalRules3D::has_profile);
	ClassDB::bind_method(D_METHOD("get_layers_for", "id"), &NavigationTraversalRules3D::get_layers_for);
	ClassDB::bind_method(D_METHOD("get_all_layers"), &NavigationTraversalRules3D::get_all_layers);
	ClassDB::bind_method(D_METHOD("get_validation_warnings"), &NavigationTraversalRules3D::get_validation_warnings);

	static constexpr const char *HINT_DISTANCE = "0,10,0.01,or_greater,suffix:m";
	static constexpr const char *HINT_SPACING = "0.05,5,0.01,or_greater,suffix:m";

	ADD_GROUP("Search", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "search_distance", PROPERTY_HINT_RANGE, HINT_DISTANCE), "set_search_distance", "get_search_distance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "search_height", PROPERTY_HINT_RANGE, HINT_DISTANCE), "set_search_height", "get_search_height");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sample_spacing", PROPERTY_HINT_RANGE, HINT_SPACING), "set_sample_spacing", "get_sample_spacing");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "edge_inset", PROPERTY_HINT_RANGE, HINT_DISTANCE), "set_edge_inset", "get_edge_inset");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "support_search", PROPERTY_HINT_RANGE, HINT_DISTANCE), "set_support_search", "get_support_search");

	ADD_GROUP("Body", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "body_radius", PROPERTY_HINT_RANGE, HINT_DISTANCE), "set_body_radius", "get_body_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "body_height", PROPERTY_HINT_RANGE, HINT_DISTANCE), "set_body_height", "get_body_height");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hand_spacing", PROPERTY_HINT_RANGE, HINT_DISTANCE), "set_hand_spacing", "get_hand_spacing");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "grip_probe_depth", PROPERTY_HINT_RANGE, HINT_DISTANCE), "set_grip_probe_depth", "get_grip_probe_depth");

	ADD_GROUP("", "");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "profiles", PROPERTY_HINT_ARRAY_TYPE, MAKE_RESOURCE_TYPE_HINT("NavigationTraversalProfile3D")), "set_profiles", "get_profiles");
}
