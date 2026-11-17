/**************************************************************************/
/*  texture_editor_plugin.cpp                                             */
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

#include "texture_editor_plugin.h"

#include "core/object/callable_mp.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/scene/texture/color_channel_selector.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/aspect_ratio_container.h"
#include "scene/gui/button.h"
#include "scene/gui/color_rect.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/grid_container.h"
#include "scene/gui/scroll_bar.h"
#include "scene/gui/label.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/texture_rect.h"
#include "scene/resources/animated_texture.h"
#include "scene/resources/atlas_texture.h"
#include "scene/resources/compressed_texture.h"
#include "scene/resources/dpi_texture.h"
#include "scene/resources/gradient_texture.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/material.h"
#include "scene/resources/portable_compressed_texture.h"
#include "scene/resources/streamed_texture.h"
#include "scene/resources/texture_rd.h"
#include "servers/rendering/rendering_device.h"

constexpr const char *texture_2d_shader_code = R"(
shader_type canvas_item;
render_mode blend_mix;

instance uniform vec4 u_channel_factors = vec4(1.0);
instance uniform float lod = 0.0;

vec4 filter_preview_colors(vec4 input_color, vec4 factors) {
	// Filter RGB.
	vec4 output_color = input_color * vec4(factors.rgb, input_color.a);

	// Remove transparency when alpha is not enabled.
	output_color.a = mix(1.0, output_color.a, factors.a);

	// Switch to opaque grayscale when visualizing only one channel.
	float csum = factors.r + factors.g + factors.b + factors.a;
	float single = clamp(2.0 - csum, 0.0, 1.0);
	for (int i = 0; i < 4; i++) {
		float c = input_color[i];
		output_color = mix(output_color, vec4(c, c, c, 1.0), factors[i] * single);
	}

	return output_color;
}

void fragment() {
	COLOR = filter_preview_colors(textureLod(TEXTURE, UV, lod), u_channel_factors);
}
)";

void TexturePreview::init_shaders() {
	texture_material.instantiate();

	Ref<Shader> texture_shader;
	texture_shader.instantiate();
	texture_shader->set_code(texture_2d_shader_code);

	texture_material->set_shader(texture_shader);
}

void TexturePreview::finish_shaders() {
	texture_material.unref();
}

TextureRect *TexturePreview::get_texture_display() {
	return texture_display;
}

void TexturePreview::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_THEME_CHANGED: {
			if (!is_inside_tree()) {
				// TODO: This is a workaround because `NOTIFICATION_THEME_CHANGED`
				// is getting called for some reason when the `TexturePreview` is
				// getting destroyed, which causes `get_theme_font()` to return `nullptr`.
				// See https://github.com/godotengine/godot/issues/50743.
				break;
			}

			if (metadata_label) {
				Ref<Font> metadata_label_font = get_theme_font(SNAME("expression"), EditorStringName(EditorFonts));
				metadata_label->add_theme_font_override(SceneStringName(font), metadata_label_font);
			}

			if (metadata_toggle) {
				metadata_toggle->set_button_icon(get_editor_theme_icon(SNAME("Info")));
			}

			if (open_big_preview) {
				open_big_preview->set_button_icon(get_editor_theme_icon(SNAME("DistractionFree")));
			}

			bg_rect->set_color(get_theme_color(SNAME("dark_color_2"), EditorStringName(Editor)));
			checkerboard->set_texture(get_editor_theme_icon(SNAME("Checkerboard")));
			theme_cache.outline_color = get_theme_color(SNAME("extra_border_color_1"), EditorStringName(Editor));
		} break;
	}
}

void TexturePreview::_draw_outline() {
	const float outline_width = Math::round(EDSCALE);
	const Rect2 outline_rect = Rect2(Vector2(), outline_overlay->get_size()).grow(outline_width * 0.5);
	outline_overlay->draw_rect(outline_rect, theme_cache.outline_color, false, outline_width);
}

void TexturePreview::_update_texture_display_ratio() {
	if (texture_display->get_texture().is_valid()) {
		centering_container->set_ratio(texture_display->get_texture()->get_size().aspect());
	}
}

static Image::Format get_texture_2d_format(const Ref<Texture2D> &p_texture) {
#ifdef RD_ENABLED
	const Ref<Texture2DRD> rd_texture = p_texture;
	if (rd_texture.is_valid() && RD::get_singleton() && RD::get_singleton()->texture_is_valid(rd_texture->get_texture_rd_rid())) {
		return rd_texture->get_image()->get_format();
	}
#endif

	return p_texture->get_format();
}

static int get_texture_mipmaps_count(const Ref<Texture2D> &p_texture) {
	ERR_FAIL_COND_V(p_texture.is_null(), -1);

	// We are having to download the image only to get its mipmaps count. It would be nice if we didn't have to.
	Ref<Image> image;
	Ref<AtlasTexture> at = p_texture;
#ifdef RD_ENABLED
	Ref<Texture2DRD> rd_texture = p_texture;
#endif

	if (at.is_valid()) {
		// The AtlasTexture tries to obtain the region from the atlas as an image,
		// which will fail if it is a compressed format.
		Ref<Texture2D> atlas = at->get_atlas();
		if (atlas.is_valid()) {
			image = atlas->get_image();
		}
	} else {
#ifdef RD_ENABLED
		if (rd_texture.is_valid()) {
			if (RD::get_singleton() && RD::get_singleton()->texture_is_valid(rd_texture->get_texture_rd_rid())) {
				return -1;
			}
			image = p_texture->get_image();
		} else {
			image = p_texture->get_image();
		}
#else
		image = p_texture->get_image();
#endif
	}

	if (image.is_valid()) {
		return image->get_mipmap_count();
	}
	return -1;
}

void TexturePreview::_update_metadata_label_text() {
	const Ref<Texture2D> texture = texture_display->get_texture();
	ERR_FAIL_COND(texture.is_null());

	Image::Format format;
	int mipmaps;

	Ref<Image> image = texture->get_image();
	if (image.is_valid()) {
		format = image->get_format();
		mipmaps = image->get_mipmap_count();
	} else {
		format = get_texture_2d_format(texture.ptr());
		mipmaps = get_texture_mipmaps_count(texture);
	}

	const String format_name = format != Image::FORMAT_MAX ? Image::get_format_name(format) : texture->get_class();

	const Vector2i resolution = texture->get_size();

	if (format != Image::FORMAT_MAX) {
		// Avoid signed integer overflow that could occur with huge texture sizes by casting everything to uint64_t.
		uint64_t memory = uint64_t(resolution.x) * uint64_t(resolution.y) * uint64_t(Image::get_format_pixel_size(format));
		// Handle VRAM-compressed formats.
		memory = Image::get_format_pixels_shifted(format, memory);

		float mipmaps_multiplier = 1.0;
		float mipmap_increase = 0.25;
		for (int i = 0; i < mipmaps; i++) {
			// Each mip adds 25% memory usage of the previous one.
			// With a complete mipmap chain, memory usage increases by ~33%.
			mipmaps_multiplier += mipmap_increase;
			mipmap_increase *= 0.25;
		}
		memory *= mipmaps_multiplier;

		if (mipmaps >= 1) {
			metadata_label->set_text(
					vformat(String::utf8("%d×%d %s\n") + TTR("%s Mipmaps") + "\n" + TTR("Memory: %s"),
							texture->get_width(),
							texture->get_height(),
							format_name,
							mipmaps,
							String::humanize_size(memory)));
		} else {
			// "No Mipmaps" is easier to distinguish than "0 Mipmaps",
			// especially since 0, 6, and 8 look quite close with the default code font.
			metadata_label->set_text(
					vformat(String::utf8("%d×%d %s\n") + TTR("No Mipmaps") + "\n" + TTR("Memory: %s"),
							texture->get_width(),
							texture->get_height(),
							format_name,
							String::humanize_size(memory)));
		}
	} else {
		metadata_label->set_text(
				vformat(String::utf8("%d×%d %s"),
						texture->get_width(),
						texture->get_height(),
						format_name));
	}
}

void TexturePreview::_toggle_metadata_label() {
	metadata_label->set_visible(!metadata_label->is_visible());
	if (toolbar) {
		metadata_toggle->set_modulate(Color(1, 1, 1, metadata_label->is_visible() ? 1.0 : 0.5));
	} else {
		metadata_toggle->set_modulate(Color(1, 1, 1, metadata_label->is_visible() ? 0.8 : 0.4));
	}
}

void TexturePreview::_open_big_preview() {
	popup_texture_dialog(texture_display->get_texture());
}

void TexturePreview::popup_texture_dialog(const Ref<Texture2D> &p_texture) {
	ERR_FAIL_COND(p_texture.is_null());

	String title = p_texture->get_path().get_file();
	if (title.is_empty()) {
		title = p_texture->get_name();
	}

	Ref<Texture2D> texture = p_texture;
	// Load the full-resolution image for streamed textures.
	const Ref<StreamedTexture2D> streamed_texture(texture);
	if (streamed_texture.is_valid()) {
		const Ref<Image> image = streamed_texture->get_image();
		if (image.is_valid()) {
			texture = ImageTexture::create_from_image(image);
		}
	}

	AcceptDialog *dialog = memnew(AcceptDialog);
	dialog->set_title(title.is_empty() ? TTR("Texture Preview") : vformat(TTR("Texture Preview: %s"), title));

	// Toolbar on top, then the preview with scroll bars along the right and bottom edges.
	VBoxContainer *vb = memnew(VBoxContainer);
	dialog->add_child(vb);

	TexturePreview *big_preview = memnew(TexturePreview(texture, true, true));
	vb->add_child(big_preview->get_zoom_toolbar());

	// The view area holds the grid plus overlays that must stay fixed to its corners
	// (the preview itself shrinks when scroll bars appear).
	Control *view_area = memnew(Control);
	view_area->set_v_size_flags(SIZE_EXPAND_FILL);
	view_area->set_mouse_filter(MOUSE_FILTER_IGNORE);
	view_area->set_custom_minimum_size(Size2(256, 256) * EDSCALE);
	vb->add_child(view_area);

	GridContainer *grid = memnew(GridContainer);
	grid->set_columns(2);
	grid->set_anchors_and_offsets_preset(PRESET_FULL_RECT);
	grid->add_theme_constant_override("h_separation", 0);
	grid->add_theme_constant_override("v_separation", 0);
	view_area->add_child(grid);

	big_preview->set_h_size_flags(SIZE_EXPAND_FILL);
	big_preview->set_v_size_flags(SIZE_EXPAND_FILL);
	grid->add_child(big_preview);

	VScrollBar *vbar = memnew(VScrollBar);
	vbar->hide();
	grid->add_child(vbar);

	HScrollBar *hbar = memnew(HScrollBar);
	hbar->hide();
	grid->add_child(hbar);

	grid->add_child(memnew(Control)); // Corner.

	big_preview->set_zoom_scroll_bars(hbar, vbar);

	// The dialog frees itself once closed.
	dialog->connect(SceneStringName(confirmed), callable_mp((Node *)dialog, &Node::queue_free), CONNECT_DEFERRED);
	dialog->connect("canceled", callable_mp((Node *)dialog, &Node::queue_free), CONNECT_DEFERRED);
	EditorNode::get_singleton()->get_gui_base()->add_child(dialog);

	// Pin the metadata overlay to the bottom-right corner of the view area, clear of the scroll bars,
	// so it doesn't jump when they appear or disappear while zooming.
	Label *metadata = big_preview->metadata_label;
	if (metadata) {
		metadata->get_parent()->remove_child(metadata);
		view_area->add_child(metadata);
		const real_t margin = 4 * EDSCALE;
		metadata->set_anchors_and_offsets_preset(PRESET_BOTTOM_RIGHT, PRESET_MODE_MINSIZE);
		metadata->set_h_grow_direction(GROW_DIRECTION_BEGIN);
		metadata->set_v_grow_direction(GROW_DIRECTION_BEGIN);
		metadata->set_offset(SIDE_RIGHT, -(vbar->get_combined_minimum_size().x + margin));
		metadata->set_offset(SIDE_BOTTOM, -(hbar->get_combined_minimum_size().y + margin));
		metadata->set_offset(SIDE_LEFT, metadata->get_offset(SIDE_RIGHT) - metadata->get_combined_minimum_size().x);
		metadata->set_offset(SIDE_TOP, metadata->get_offset(SIDE_BOTTOM) - metadata->get_combined_minimum_size().y);
	}
	dialog->popup_centered_clamped(Size2(1000, 900) * EDSCALE, 0.8);
}

void TexturePreview::_update_zoom_layout() {
	if (!zoom_view) {
		return;
	}
	const Ref<Texture2D> texture = texture_display->get_texture();
	const Size2 view_size = zoom_view->get_size();
	if (texture.is_null() || texture->get_width() <= 0 || texture->get_height() <= 0 || view_size.x <= 0 || view_size.y <= 0) {
		return;
	}

	const Size2 tex_size = texture->get_size();
	if (zoom_fit) {
		zoom = MIN(view_size.x / tex_size.x, view_size.y / tex_size.y);
	}

	const Size2 display_size = tex_size * zoom;
	for (int axis = 0; axis < 2; axis++) {
		if (display_size[axis] <= view_size[axis]) {
			// Center when the texture fits along this axis.
			zoom_offset[axis] = (view_size[axis] - display_size[axis]) * 0.5;
		} else {
			zoom_offset[axis] = CLAMP(zoom_offset[axis], view_size[axis] - display_size[axis], 0.0);
		}
	}

	centering_container->set_position(zoom_offset.round());
	centering_container->set_size(display_size.round());
	zoom_button->set_text(itos(Math::round(zoom * 100.0)) + "%");

	// Scroll bars appear only when the texture extends past the view.
	// Showing/hiding them resizes the view, which re-runs this layout.
	updating_scroll = true;
	if (h_scroll) {
		h_scroll->set_visible(display_size.x > view_size.x + 0.5);
		h_scroll->set_max(display_size.x);
		h_scroll->set_page(view_size.x);
		h_scroll->set_value(-zoom_offset.x);
	}
	if (v_scroll) {
		v_scroll->set_visible(display_size.y > view_size.y + 0.5);
		v_scroll->set_max(display_size.y);
		v_scroll->set_page(view_size.y);
		v_scroll->set_value(-zoom_offset.y);
	}
	updating_scroll = false;
}

void TexturePreview::_scroll_changed(double p_value) {
	if (updating_scroll) {
		return;
	}
	if (h_scroll && h_scroll->is_visible()) {
		zoom_offset.x = -h_scroll->get_value();
	}
	if (v_scroll && v_scroll->is_visible()) {
		zoom_offset.y = -v_scroll->get_value();
	}
	_update_zoom_layout();
}

void TexturePreview::set_zoom_scroll_bars(HScrollBar *p_h_scroll, VScrollBar *p_v_scroll) {
	h_scroll = p_h_scroll;
	v_scroll = p_v_scroll;
	for (ScrollBar *bar : { (ScrollBar *)h_scroll, (ScrollBar *)v_scroll }) {
		if (bar) {
			bar->connect(SceneStringName(value_changed), callable_mp(this, &TexturePreview::_scroll_changed));
		}
	}
	_update_zoom_layout();
}

void TexturePreview::_set_zoom(real_t p_zoom, const Vector2 &p_pivot) {
	const real_t new_zoom = CLAMP(p_zoom, 0.01, 64.0);
	// Keep the texture point under the pivot in place.
	zoom_offset = p_pivot - (p_pivot - zoom_offset) * (new_zoom / zoom);
	zoom = new_zoom;
	zoom_fit = false;
	_update_zoom_layout();
}

void TexturePreview::_zoom_reset() {
	zoom_fit = true;
	_update_zoom_layout();
}

void TexturePreview::_zoom_view_gui_input(const Ref<InputEvent> &p_event) {
	const Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid()) {
		if (mb->is_pressed() && mb->is_command_or_control_pressed() && (mb->get_button_index() == MouseButton::WHEEL_UP || mb->get_button_index() == MouseButton::WHEEL_DOWN)) {
			const real_t step = mb->get_factor() > 0 ? 1.0 + 0.15 * mb->get_factor() : 1.15;
			_set_zoom(mb->get_button_index() == MouseButton::WHEEL_UP ? zoom * step : zoom / step, mb->get_position());
			zoom_view->accept_event();
			return;
		}

		if (mb->is_pressed() && !mb->is_command_or_control_pressed()) {
			// Plain wheel scrolls the zoomed texture (Shift scrolls horizontally).
			Vector2 scroll;
			const real_t amount = 40 * EDSCALE * (mb->get_factor() > 0 ? mb->get_factor() : 1.0);
			switch (mb->get_button_index()) {
				case MouseButton::WHEEL_UP:
					scroll = mb->is_shift_pressed() ? Vector2(amount, 0) : Vector2(0, amount);
					break;
				case MouseButton::WHEEL_DOWN:
					scroll = mb->is_shift_pressed() ? Vector2(-amount, 0) : Vector2(0, -amount);
					break;
				case MouseButton::WHEEL_LEFT:
					scroll = Vector2(amount, 0);
					break;
				case MouseButton::WHEEL_RIGHT:
					scroll = Vector2(-amount, 0);
					break;
				default:
					break;
			}
			if (scroll != Vector2()) {
				zoom_offset += scroll;
				_update_zoom_layout();
				zoom_view->accept_event();
				return;
			}
		}

		if (mb->get_button_index() == MouseButton::LEFT || mb->get_button_index() == MouseButton::MIDDLE) {
			panning = mb->is_pressed();
			zoom_view->set_default_cursor_shape(panning ? CURSOR_DRAG : CURSOR_ARROW);
			if (mb->is_pressed() && mb->is_double_click()) {
				_zoom_reset();
			}
			zoom_view->accept_event();
		}
		return;
	}

	const Ref<InputEventMouseMotion> mm = p_event;
	if (mm.is_valid() && panning) {
		zoom_offset += mm->get_relative();
		_update_zoom_layout();
		zoom_view->accept_event();
		return;
	}

	const Ref<InputEventMagnifyGesture> mg = p_event;
	if (mg.is_valid()) {
		_set_zoom(zoom * mg->get_factor(), mg->get_position());
		zoom_view->accept_event();
		return;
	}

	const Ref<InputEventPanGesture> pg = p_event;
	if (pg.is_valid()) {
		zoom_offset -= pg->get_delta() * 20 * EDSCALE;
		_update_zoom_layout();
		zoom_view->accept_event();
	}
}

void TexturePreview::on_selected_channels_changed() {
	texture_display->set_instance_shader_parameter("u_channel_factors", channel_selector->get_selected_channel_factors());
}

void TexturePreview::on_selected_mipmap_changed(double p_value) {
	texture_display->set_instance_shader_parameter("lod", mipmap_spinbox->get_value());
}

TexturePreview::TexturePreview(Ref<Texture2D> p_texture, bool p_show_metadata, bool p_zoomable) {
	const float outline_width = Math::round(EDSCALE);

	set_custom_minimum_size(Size2(0.0, 256 * EDSCALE) + Size2(outline_width, outline_width) * 2);

	bg_rect = memnew(ColorRect);
	add_child(bg_rect);

	margin_container = memnew(MarginContainer);
	margin_container->add_theme_constant_override("margin_right", outline_width);
	margin_container->add_theme_constant_override("margin_top", outline_width);
	margin_container->add_theme_constant_override("margin_left", outline_width);
	margin_container->add_theme_constant_override("margin_bottom", outline_width);
	add_child(margin_container);

	centering_container = memnew(AspectRatioContainer);
	if (p_zoomable) {
		// The texture is positioned and scaled manually inside a clipping view.
		zoom_view = memnew(Control);
		zoom_view->set_clip_contents(true);
		zoom_view->set_mouse_filter(MOUSE_FILTER_STOP);
		zoom_view->connect(SceneStringName(gui_input), callable_mp(this, &TexturePreview::_zoom_view_gui_input));
		zoom_view->connect(SceneStringName(resized), callable_mp(this, &TexturePreview::_update_zoom_layout));
		margin_container->add_child(zoom_view);
		centering_container->set_mouse_filter(MOUSE_FILTER_IGNORE);
		zoom_view->add_child(centering_container);
	} else {
		margin_container->add_child(centering_container);
	}

	checkerboard = memnew(TextureRect);
	checkerboard->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
	checkerboard->set_stretch_mode(TextureRect::STRETCH_TILE);
	checkerboard->set_texture_repeat(CanvasItem::TEXTURE_REPEAT_ENABLED);
	centering_container->add_child(checkerboard);

	texture_display = memnew(TextureRect);
	texture_display->set_texture_filter(TEXTURE_FILTER_NEAREST_WITH_MIPMAPS);
	texture_display->set_texture(p_texture);
	texture_display->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
	texture_display->set_material(texture_material);
	texture_display->set_instance_shader_parameter("u_channel_factors", Vector4(1, 1, 1, 1));
	centering_container->add_child(texture_display);

	// Creating a separate control so it is not affected by the filtering shader.
	outline_overlay = memnew(Control);
	centering_container->add_child(outline_overlay);

	outline_overlay->connect(SceneStringName(draw), callable_mp(this, &TexturePreview::_draw_outline));

	if (p_zoomable) {
		// Let the zoom view receive all mouse input (drag to pan, wheel to scroll/zoom).
		checkerboard->set_mouse_filter(MOUSE_FILTER_IGNORE);
		texture_display->set_mouse_filter(MOUSE_FILTER_IGNORE);
		outline_overlay->set_mouse_filter(MOUSE_FILTER_IGNORE);
	}

	if (p_texture.is_valid()) {
		_update_texture_display_ratio();
		p_texture->connect_changed(callable_mp(this, &TexturePreview::_update_texture_display_ratio));
		if (p_zoomable) {
			p_texture->connect_changed(callable_mp(this, &TexturePreview::_update_zoom_layout));
		}
	}

	// Null can be passed by `Camera3DPreview` (which immediately after sets a texture anyways).
	const Image::Format format = p_texture.is_valid() ? get_texture_2d_format(p_texture.ptr()) : Image::FORMAT_MAX;
	const uint32_t components_mask = format != Image::FORMAT_MAX ? Image::get_format_component_mask(format) : 0xf;

	// Top-right column: mipmap selector, then the big preview / zoom buttons.
	top_right_box = memnew(VBoxContainer);
	top_right_box->set_h_size_flags(Control::SIZE_SHRINK_END);
	top_right_box->set_v_size_flags(Control::SIZE_SHRINK_BEGIN);
	top_right_box->set_mouse_filter(MOUSE_FILTER_IGNORE);
	add_child(top_right_box);

	// Setup Mipmap selector.
	const int mipmaps = get_texture_mipmaps_count(p_texture);
	if (mipmaps > 0) {
		mipmap_spinbox = memnew(SpinBox);
		mipmap_spinbox->set_tooltip_text(TTRC("Mipmap level index selector."));
		mipmap_spinbox->set_max(mipmaps);
		mipmap_spinbox->set_modulate(Color(1, 1, 1, 0.8));
		mipmap_spinbox->set_h_size_flags(Control::SIZE_SHRINK_END);
		mipmap_spinbox->connect(SceneStringName(value_changed), callable_mp(this, &TexturePreview::on_selected_mipmap_changed));
		top_right_box->add_child(mipmap_spinbox);
	}

	if (p_show_metadata && !p_zoomable && p_texture.is_valid()) {
		open_big_preview = memnew(Button);
		open_big_preview->set_flat(true);
		open_big_preview->set_tooltip_text(TTRC("Open the texture in a large preview window."));
		open_big_preview->set_accessibility_name(TTRC("Open Texture Preview Dialog"));
		open_big_preview->set_theme_type_variation("PreviewLightButton");
		open_big_preview->set_modulate(Color(1, 1, 1, 0.8));
		open_big_preview->set_h_size_flags(Control::SIZE_SHRINK_END);
		open_big_preview->connect(SceneStringName(pressed), callable_mp(this, &TexturePreview::_open_big_preview));
		top_right_box->add_child(open_big_preview);
	}

	if (p_zoomable) {
		zoom_button = memnew(Button);
		zoom_button->set_flat(true);
		zoom_button->set_text("100%");
		zoom_button->set_tooltip_text(TTRC("Zoom level. Ctrl+Mouse Wheel to zoom, drag or Mouse Wheel to pan.\nClick (or double-click the texture) to fit."));
		zoom_button->set_theme_type_variation("PreviewLightButton");
		zoom_button->set_modulate(Color(1, 1, 1, 0.8));
		zoom_button->set_h_size_flags(Control::SIZE_SHRINK_END);
		zoom_button->connect(SceneStringName(pressed), callable_mp(this, &TexturePreview::_zoom_reset));
		top_right_box->add_child(zoom_button);
	}

	// Add color channel selector at the bottom left if more than 1 channel is available.
	if (p_show_metadata && !Math::is_power_of_2(components_mask)) {
		channel_selector = memnew(ColorChannelSelector);
		channel_selector->connect("selected_channels_changed", callable_mp(this, &TexturePreview::on_selected_channels_changed));
		channel_selector->set_h_size_flags(Control::SIZE_SHRINK_BEGIN);
		channel_selector->set_v_size_flags(Control::SIZE_SHRINK_BEGIN);
		channel_selector->set_available_channels_mask(components_mask);
		add_child(channel_selector);
	}

	if (p_show_metadata) {
		metadata_label = memnew(Label);
		metadata_label->set_focus_mode(FOCUS_ACCESSIBILITY);

		if (p_texture.is_valid()) {
			_update_metadata_label_text();
			p_texture->connect_changed(callable_mp(this, &TexturePreview::_update_metadata_label_text));
		}

		// It's okay that these colors are static since the grid color is static too.
		metadata_label->add_theme_color_override(SceneStringName(font_color), Color(1, 1, 1));
		metadata_label->add_theme_color_override("font_shadow_color", Color(0, 0, 0));

		metadata_label->add_theme_font_size_override(SceneStringName(font_size), 14 * EDSCALE);
		metadata_label->add_theme_color_override("font_outline_color", Color(0, 0, 0));
		metadata_label->add_theme_constant_override("outline_size", 8 * EDSCALE);

		metadata_label->set_h_size_flags(Control::SIZE_SHRINK_END);
		metadata_label->set_v_size_flags(Control::SIZE_SHRINK_END);

		add_child(metadata_label);

		metadata_toggle = memnew(Button);
		metadata_toggle->set_flat(true);
		metadata_toggle->set_tooltip_text(TTRC("Toggle metadata overlay."));
		metadata_toggle->set_theme_type_variation("PreviewLightButton");
		metadata_toggle->set_modulate(Color(1, 1, 1, 0.8));
		metadata_toggle->set_h_size_flags(Control::SIZE_SHRINK_BEGIN);
		metadata_toggle->set_v_size_flags(Control::SIZE_SHRINK_END);
		metadata_toggle->connect(SceneStringName(pressed), callable_mp(this, &TexturePreview::_toggle_metadata_label));
		add_child(metadata_toggle);
	}

	if (p_zoomable) {
		// In the large dialog the controls live in a toolbar above the texture instead of overlaying it.
		// The owner places the toolbar (see `get_zoom_toolbar()`).
		toolbar = memnew(HBoxContainer);

		auto move_to_toolbar = [this](Control *p_control) {
			if (!p_control) {
				return;
			}
			if (p_control->get_parent()) {
				p_control->get_parent()->remove_child(p_control);
			}
			p_control->set_theme_type_variation(StringName());
			p_control->set_modulate(Color(1, 1, 1));
			p_control->set_h_size_flags(Control::SIZE_SHRINK_BEGIN);
			p_control->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
			toolbar->add_child(p_control);
		};

		move_to_toolbar(channel_selector);

		Control *spacer = memnew(Control);
		spacer->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		toolbar->add_child(spacer);

		move_to_toolbar(zoom_button);

		if (mipmap_spinbox) {
			Label *mip_label = memnew(Label(TTR("Mipmap:")));
			toolbar->add_child(mip_label);
			move_to_toolbar(mipmap_spinbox);
		}

		move_to_toolbar(metadata_toggle);
		if (metadata_toggle) {
			metadata_toggle->set_modulate(Color(1, 1, 1, metadata_label->is_visible() ? 1.0 : 0.5));
		}

		remove_child(top_right_box);
		memdelete(top_right_box);
		top_right_box = nullptr;
	}
}

HBoxContainer *TexturePreview::get_zoom_toolbar() const {
	return toolbar;
}

bool EditorInspectorPluginTexture::can_handle(Object *p_object) {
	if (Object::cast_to<GradientTexture1D>(p_object) || Object::cast_to<GradientTexture2D>(p_object)) {
		return false;
	}

	if (Object::cast_to<Image>(p_object) != nullptr ||
			Object::cast_to<ImageTexture>(p_object) != nullptr ||
			Object::cast_to<AtlasTexture>(p_object) != nullptr ||
			Object::cast_to<CompressedTexture2D>(p_object) != nullptr ||
			Object::cast_to<PortableCompressedTexture2D>(p_object) != nullptr ||
			Object::cast_to<AnimatedTexture>(p_object) != nullptr ||
			Object::cast_to<DPITexture>(p_object) != nullptr ||
			Object::cast_to<StreamedTexture2D>(p_object) != nullptr) {
		return true;
	}

#ifdef RD_ENABLED
	if (Object::cast_to<Texture2DRD>(p_object) != nullptr) {
		return true;
	}
#endif

	Ref<Texture2D> texture_2d(Object::cast_to<Texture2D>(p_object));
	if (texture_2d.is_valid()) {
		this_image = texture_2d->get_image();
		return this_image.is_valid();
	}
	return false;
}

void EditorInspectorPluginTexture::parse_begin(Object *p_object) {
	Ref<Texture> texture(Object::cast_to<Texture>(p_object));
	if (texture.is_valid()) {
		// Load the full-resolution image for streamed textures.
		const Ref<StreamedTexture2D> streamed_texture(texture);
		if (streamed_texture.is_valid()) {
			const Ref<Image> image = streamed_texture->get_image();
			if (image.is_valid()) {
				texture = ImageTexture::create_from_image(image);
			}
		}
	} else {
		// Not a texture, try to load as an image.
		Ref<Image> image(Object::cast_to<Image>(p_object));
		texture = ImageTexture::create_from_image(image);

		ERR_FAIL_COND_MSG(texture.is_null(), "Failed to create the texture from an invalid image.");
	}

	add_custom_control(memnew(TexturePreview(texture, true)));
}

TextureEditorPlugin::TextureEditorPlugin() {
	Ref<EditorInspectorPluginTexture> plugin;
	plugin.instantiate();
	add_inspector_plugin(plugin);
}
