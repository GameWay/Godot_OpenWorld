/**************************************************************************/
/*  navigation_traversal_rules_3d.h                                       */
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

#include "core/io/resource.h"
#include "core/variant/typed_array.h"
#include "scene/3d/navigation/navigation_traversal_profile_3d.h"

class NavigationTraversalRules3D : public Resource {
	GDCLASS(NavigationTraversalRules3D, Resource);

	real_t search_distance = 2.6;
	real_t search_height = 3.2;
	real_t sample_spacing = 0.4;
	real_t edge_inset = 0.1;
	real_t support_search = 0.4;
	real_t body_radius = 0.32;
	real_t body_height = 1.8;
	real_t hand_spacing = 0.42;
	real_t grip_probe_depth = 0.15;
	Vector<Ref<NavigationTraversalProfile3D>> profiles;

	void _profile_changed();

protected:
	static void _bind_methods();

public:
	void set_search_distance(real_t p_value);
	real_t get_search_distance() const;
	void set_search_height(real_t p_value);
	real_t get_search_height() const;
	void set_sample_spacing(real_t p_value);
	real_t get_sample_spacing() const;
	void set_edge_inset(real_t p_value);
	real_t get_edge_inset() const;
	void set_support_search(real_t p_value);
	real_t get_support_search() const;
	void set_body_radius(real_t p_value);
	real_t get_body_radius() const;
	void set_body_height(real_t p_value);
	real_t get_body_height() const;
	void set_hand_spacing(real_t p_value);
	real_t get_hand_spacing() const;
	void set_grip_probe_depth(real_t p_value);
	real_t get_grip_probe_depth() const;

	void set_profiles(const TypedArray<NavigationTraversalProfile3D> &p_profiles);
	TypedArray<NavigationTraversalProfile3D> get_profiles() const;

	Ref<NavigationTraversalProfile3D> get_profile(const StringName &p_id) const;
	bool has_profile(const StringName &p_id) const;
	uint32_t get_layers_for(const StringName &p_id) const;
	uint32_t get_all_layers() const;
	PackedStringArray get_validation_warnings() const;

	~NavigationTraversalRules3D();
};
