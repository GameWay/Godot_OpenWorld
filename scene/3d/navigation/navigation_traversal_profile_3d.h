/**************************************************************************/
/*  navigation_traversal_profile_3d.h                                     */
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
#include "core/variant/type_info.h"

class NavigationTraversalProfile3D : public Resource {
	GDCLASS(NavigationTraversalProfile3D, Resource);

public:
	enum TraversalKind {
		KIND_CLIMB,
		KIND_CLIMB_OVER,
		KIND_VAULT,
		KIND_DROP,
		KIND_HANG_DROP,
		KIND_MAX,
	};

	enum TraversalKindFlags {
		KIND_FLAG_CLIMB = 1 << KIND_CLIMB,
		KIND_FLAG_CLIMB_OVER = 1 << KIND_CLIMB_OVER,
		KIND_FLAG_VAULT = 1 << KIND_VAULT,
		KIND_FLAG_DROP = 1 << KIND_DROP,
		KIND_FLAG_HANG_DROP = 1 << KIND_HANG_DROP,
		KIND_FLAG_ALL = (1 << KIND_MAX) - 1,
	};

	enum RejectReason {
		REJECT_NONE,
		REJECT_KIND,
		REJECT_NO_SUPPORT,
		REJECT_HEIGHT,
		REJECT_GAP,
		REJECT_SLOPE,
		REJECT_NO_GRIP,
		REJECT_GRIP_ANGLE,
		REJECT_GRIP_DEPTH,
		REJECT_BLOCKED,
		REJECT_THICKNESS,
		REJECT_LANDING_DELTA,
		REJECT_MAX,
	};

	static constexpr int MAX_LAYER_BIT = 31;

private:
	StringName id;
	int layer_bit = 0;
	BitField<TraversalKindFlags> allowed_kinds = KIND_FLAG_ALL;

	Vector2 height_ranges[KIND_MAX] = {
		Vector2(0.45, 1.85), // Climb.
		Vector2(1.2, 2.2), // Climb over.
		Vector2(0.35, 1.2), // Vault.
		Vector2(0.45, 2.1), // Drop.
		Vector2(2.1, 3.5), // Hang drop.
	};

	real_t max_gap = 2.35;
	real_t max_slope = 43.0;
	real_t max_thickness = 0.5;
	real_t max_landing_delta = 0.45;
	real_t max_grip_angle = 35.0;
	real_t max_grip_depth = 0.2;
	real_t time_scale = 3.4;
	real_t extra_penalty = 1.19;

protected:
	static void _bind_methods();

public:
	void set_id(const StringName &p_id);
	StringName get_id() const;

	void set_layer_bit(int p_layer_bit);
	int get_layer_bit() const;
	uint32_t get_layer_mask() const;

	void set_allowed_kinds(BitField<TraversalKindFlags> p_kinds);
	BitField<TraversalKindFlags> get_allowed_kinds() const;
	void set_kind_allowed(TraversalKind p_kind, bool p_allowed);
	bool is_kind_allowed(TraversalKind p_kind) const;

	void set_height_range(TraversalKind p_kind, const Vector2 &p_range);
	Vector2 get_height_range(TraversalKind p_kind) const;
	bool is_height_in_range(TraversalKind p_kind, real_t p_height) const;

	void set_max_gap(real_t p_value);
	real_t get_max_gap() const;
	void set_max_slope(real_t p_value);
	real_t get_max_slope() const;
	void set_max_thickness(real_t p_value);
	real_t get_max_thickness() const;
	void set_max_landing_delta(real_t p_value);
	real_t get_max_landing_delta() const;
	void set_max_grip_angle(real_t p_value);
	real_t get_max_grip_angle() const;
	void set_max_grip_depth(real_t p_value);
	real_t get_max_grip_depth() const;

	void set_time_scale(real_t p_value);
	real_t get_time_scale() const;
	void set_extra_penalty(real_t p_value);
	real_t get_extra_penalty() const;
	real_t get_enter_cost(real_t p_duration, real_t p_length) const;
};

VARIANT_ENUM_CAST(NavigationTraversalProfile3D::TraversalKind);
VARIANT_BITFIELD_CAST(NavigationTraversalProfile3D::TraversalKindFlags);
VARIANT_ENUM_CAST(NavigationTraversalProfile3D::RejectReason);
