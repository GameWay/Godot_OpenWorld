/**************************************************************************/
/*  gdscript_problems_panel.cpp                                           */
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

#include "gdscript_problems_panel.h"

#include "gdscript_language_protocol.h"

#include "core/io/file_access.h"
#include "core/io/image.h"
#include "core/io/resource_loader.h"
#include "core/object/callable_mp.h"
#include "editor/editor_interface.h"
#include "editor/editor_string_names.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/script/script_editor_plugin.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/tree.h"
#include "scene/resources/image_texture.h"

void GDScriptProblemsPanel::_diagnostics_updated(const String &p_path, const Array &p_diagnostics, int p_client_id) {
	if (!p_path.begins_with("res://")) {
		return;
	}
	Snapshot snapshot;
	snapshot.diagnostics = p_diagnostics;
	snapshot.revision = ++revision;
	snapshot.modified_time = FileAccess::get_modified_time(p_path.get_slice("::", 0));
	snapshots[p_client_id][p_path] = snapshot;
	_queue_refresh();
}

void GDScriptProblemsPanel::_diagnostics_cleared(const String &p_path, int p_client_id) {
	if (snapshots.has(p_client_id)) {
		snapshots[p_client_id].erase(p_path);
	}
	_queue_refresh();
}

void GDScriptProblemsPanel::_client_closed(int p_client_id) {
	snapshots.erase(p_client_id);
	_queue_refresh();
}

void GDScriptProblemsPanel::_script_closed(const Ref<Script> &p_script) {
	_diagnostics_cleared(p_script->get_path(), -1);
}

void GDScriptProblemsPanel::_filesystem_changed() {
	// Discard results for removed, moved or changed files. Only the normal
	// producer can supply a new result; filesystem events never trigger parsing.
	for (KeyValue<int, HashMap<String, Snapshot>> &source : snapshots) {
		Vector<String> stale;
		for (const KeyValue<String, Snapshot> &entry : source.value) {
			String file = entry.key.get_slice("::", 0);
			if (!FileAccess::exists(file) || (source.key != -1 && FileAccess::get_modified_time(file) != entry.value.modified_time)) {
				stale.push_back(entry.key);
			}
		}
		for (const String &path : stale) {
			source.value.erase(path);
		}
	}
	_queue_refresh();
}

void GDScriptProblemsPanel::_queue_refresh() {
	if (!refresh_pending) {
		refresh_pending = true;
		callable_mp(this, &GDScriptProblemsPanel::_refresh).call_deferred();
	}
}

void GDScriptProblemsPanel::_refresh() {
	refresh_pending = false;
	if (!is_inside_tree()) {
		return;
	}
	// Output uses RichTextLabel's normal style. Reuse the complete style so
	// background, content padding and corners follow the active editor theme.
	tree->add_theme_style_override(SceneStringName(panel), get_theme_stylebox(SNAME("normal"), SNAME("RichTextLabel")));

	// One current publication per file, never a merged history of messages.
	// A live built-in editor buffer takes precedence over external clients.
	HashMap<String, Snapshot> current;
	for (const KeyValue<int, HashMap<String, Snapshot>> &source : snapshots) {
		if (source.key == -1) {
			continue;
		}
		for (const KeyValue<String, Snapshot> &entry : source.value) {
			if (!current.has(entry.key) || current[entry.key].revision < entry.value.revision) {
				current[entry.key] = entry.value;
			}
		}
	}
	if (snapshots.has(-1)) {
		for (const KeyValue<String, Snapshot> &entry : snapshots[-1]) {
			current[entry.key] = entry.value;
		}
	}
	Vector<String> paths;
	for (const KeyValue<String, Snapshot> &entry : current) {
		paths.push_back(entry.key);
	}
	paths.sort();

	tree->clear();
	TreeItem *root = tree->create_item();
	bool errors = false;
	bool warnings = false;
	for (const String &path : paths) {
		for (const Variant &value : current[path].diagnostics) {
			Dictionary diagnostic = value;
			int severity = diagnostic.get("severity", 0);
			if (severity != LSP::DiagnosticSeverity::Error && severity != LSP::DiagnosticSeverity::Warning) {
				continue;
			}
			bool error = severity == LSP::DiagnosticSeverity::Error;
			errors |= error;
			warnings |= !error;
			Dictionary range = diagnostic["range"];
			Dictionary start = range["start"];
			int line = start["line"];
			int column = start["character"];
			TreeItem *item = tree->create_item(root);
			item->set_text(0, diagnostic["message"]);
			item->set_icon(0, get_editor_theme_icon(error ? SNAME("StatusError") : SNAME("StatusWarning")));
			item->set_text(1, path.trim_prefix("res://"));
			item->set_tooltip_text(1, path);
			item->set_text(2, itos(line + 1) + ":" + itos(column + 1));
			Dictionary location;
			location["path"] = path;
			location["line"] = line;
			location["column"] = column;
			item->set_metadata(0, location);
		}
	}
	_update_indicator(errors, warnings);
}

void GDScriptProblemsPanel::_update_indicator(bool p_errors, bool p_warnings) {
	if (!p_errors && !p_warnings) {
		set_dock_icon(Ref<Texture2D>());
		return;
	}
	// Draw literal circles, independent of emoji fonts and platform glyphs.
	int cell = MAX(8, int(8 * EDSCALE));
	Ref<Image> image = Image::create_empty(cell * (int(p_errors) + int(p_warnings)), cell, false, Image::FORMAT_RGBA8);
	image->fill(Color(0, 0, 0, 0));
	for (int i = 0; i < int(p_errors) + int(p_warnings); i++) {
		Color color = get_theme_color(p_errors && i == 0 ? SNAME("error_color") : SNAME("warning_color"), EditorStringName(Editor));
		for (int y = 0; y < cell; y++) {
			for (int x = 0; x < cell; x++) {
				float distance = Vector2(x + 0.5f - cell * 0.5f, y + 0.5f - cell * 0.5f).length();
				Color pixel = color;
				pixel.a *= CLAMP(cell * 0.375f + 0.5f - distance, 0.0f, 1.0f);
				image->set_pixel(i * cell + x, y, pixel);
			}
		}
	}
	set_dock_icon(ImageTexture::create_from_image(image));
}

void GDScriptProblemsPanel::_item_activated() {
	TreeItem *item = tree->get_selected();
	if (!item) {
		return;
	}
	Dictionary location = item->get_metadata(0);
	Ref<Script> script = ResourceLoader::load(location["path"]);
	if (script.is_valid()) {
		EditorInterface::get_singleton()->edit_script(script, int(location["line"]) + 1, int(location["column"]) + 1);
	}
}

void GDScriptProblemsPanel::_notification(int p_what) {
	if (p_what == NOTIFICATION_THEME_CHANGED) {
		_queue_refresh();
	}
}

GDScriptProblemsPanel::GDScriptProblemsPanel() {
	set_name("Problems");
	set_title(TTRC("Problems"));
	set_layout_key("Problems");
	set_default_slot(DOCK_SLOT_BOTTOM);
	set_available_layouts(DOCK_LAYOUT_HORIZONTAL | DOCK_LAYOUT_FLOATING);
	set_force_show_icon(true);
	set_custom_minimum_size(Size2(0, 120 * EDSCALE));
	set_tooltip_text(TTR("Current GDScript diagnostics published by the script editor and connected language server clients. Unopened scripts and dependent scripts are not automatically checked."));

	tree = memnew(Tree);
	tree->set_name("Diagnostics");
	tree->set_columns(3);
	tree->set_column_titles_visible(true);
	tree->set_column_title(0, TTR("Message"));
	tree->set_column_title(1, TTR("File"));
	tree->set_column_title(2, TTR("Line:Column"));
	tree->set_column_expand_ratio(0, 4);
	tree->set_column_expand_ratio(1, 2);
	tree->set_column_expand(2, false);
	tree->set_column_custom_minimum_width(2, 100 * EDSCALE);
	tree->set_hide_root(true);
	tree->set_select_mode(Tree::SELECT_ROW);
	tree->connect("item_activated", callable_mp(this, &GDScriptProblemsPanel::_item_activated));
	add_child(tree);

	Ref<GDScriptWorkspace> workspace = GDScriptLanguageProtocol::get_singleton()->get_workspace();
	// LSP can publish on its worker thread. All state and UI mutations occur
	// on the main thread, preserving publication/lifecycle event order.
	workspace->connect("diagnostics_updated", callable_mp(this, &GDScriptProblemsPanel::_diagnostics_updated), CONNECT_DEFERRED);
	workspace->connect("diagnostics_cleared", callable_mp(this, &GDScriptProblemsPanel::_diagnostics_cleared), CONNECT_DEFERRED);
	workspace->connect("diagnostics_client_closed", callable_mp(this, &GDScriptProblemsPanel::_client_closed), CONNECT_DEFERRED);
	ScriptEditor::get_singleton()->connect("script_close", callable_mp(this, &GDScriptProblemsPanel::_script_closed), CONNECT_DEFERRED);
	EditorFileSystem::get_singleton()->connect("filesystem_changed", callable_mp(this, &GDScriptProblemsPanel::_filesystem_changed));
}
