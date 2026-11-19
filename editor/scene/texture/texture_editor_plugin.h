/**************************************************************************/
/*  texture_editor_plugin.h                                               */
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

#include "editor/inspector/editor_inspector.h"
#include "editor/plugins/editor_plugin.h"
#include "scene/gui/margin_container.h"
#include "scene/resources/texture.h"

class HScrollBar;
class VScrollBar;
class AspectRatioContainer;
class ColorRect;
class TextureRect;
class ShaderMaterial;
class Button;
class ColorChannelSelector;
class SpinBox;

class TexturePreview : public MarginContainer {
	GDCLASS(TexturePreview, MarginContainer);

private:
	struct ThemeCache {
		Color outline_color;
	} theme_cache;

	TextureRect *texture_display = nullptr;

	MarginContainer *margin_container = nullptr;
	Control *outline_overlay = nullptr;
	AspectRatioContainer *centering_container = nullptr;
	ColorRect *bg_rect = nullptr;
	TextureRect *checkerboard = nullptr;
	Label *metadata_label = nullptr;
	Button *metadata_toggle = nullptr;

	static inline Ref<ShaderMaterial> texture_material;

	ColorChannelSelector *channel_selector = nullptr;
	SpinBox *mipmap_spinbox = nullptr;
	VBoxContainer *top_right_box = nullptr;

	// Opens the texture in a large dialog (inspector preview only).
	Button *open_big_preview = nullptr;

	// Zoomable mode (used by the large dialog).
	Control *zoom_view = nullptr;
	Button *zoom_button = nullptr;
	bool zoom_fit = true;
	real_t zoom = 1.0;
	Vector2 zoom_offset;
	bool panning = false;
	HScrollBar *h_scroll = nullptr;
	VScrollBar *v_scroll = nullptr;
	bool updating_scroll = false;
	HBoxContainer *toolbar = nullptr;

	void _draw_outline();
	void _update_metadata_label_text();
	void _toggle_metadata_label();
	void _open_big_preview();
	void _texture_display_gui_input(const Ref<InputEvent> &p_event);
	void _update_zoom_layout();
	void _set_zoom(real_t p_zoom, const Vector2 &p_pivot);
	void _zoom_reset();
	void _zoom_view_gui_input(const Ref<InputEvent> &p_event);
	void _scroll_changed(double p_value);

protected:
	void _notification(int p_what);
	void _update_texture_display_ratio();
	void on_selected_channels_changed();
	void on_selected_mipmap_changed(double p_value);

public:
	static void init_shaders();
	static void finish_shaders();
	// Opens the texture in a large zoomable dialog that frees itself when closed.
	static void popup_texture_dialog(const Ref<Texture2D> &p_texture);

	TextureRect *get_texture_display();
	// Zoomable mode: scroll bars placed by the owner next to the preview.
	void set_zoom_scroll_bars(HScrollBar *p_h_scroll, VScrollBar *p_v_scroll);
	// Zoomable mode: toolbar with the preview controls, to be added by the owner above the preview.
	HBoxContainer *get_zoom_toolbar() const;
	TexturePreview(Ref<Texture2D> p_texture, bool p_show_metadata, bool p_zoomable = false);
};

class EditorInspectorPluginTexture : public EditorInspectorPlugin {
	GDCLASS(EditorInspectorPluginTexture, EditorInspectorPlugin);

	Ref<Image> this_image;

public:
	virtual bool can_handle(Object *p_object) override;
	virtual void parse_begin(Object *p_object) override;
};

class TextureEditorPlugin : public EditorPlugin {
	GDCLASS(TextureEditorPlugin, EditorPlugin);

public:
	virtual String get_plugin_name() const override { return "Texture2D"; }

	TextureEditorPlugin();
};
