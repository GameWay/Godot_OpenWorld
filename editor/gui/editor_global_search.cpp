/**************************************************************************/
/*  editor_global_search.cpp                                              */
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

#include "editor_global_search.h"

#include "core/input/input.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "editor/docks/filesystem_dock.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/run/editor_run_bar.h"
#include "editor/script/script_editor_plugin.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/item_list.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/option_button.h"
#include "scene/gui/tab_bar.h"
#include "scene/gui/text_edit.h"
#include "scene/main/window.h"

void EditorDoubleShift::reset() {
	held_keys.clear();
	cancel();
}

void EditorDoubleShift::cancel() {
	clean_shift = false;
	first_tap = false;
}

bool EditorDoubleShift::key_event(const Ref<InputEventKey> &p_key, uint64_t p_now) {
	const Key key = p_key->get_keycode() == Key::NONE ? p_key->get_physical_keycode() : p_key->get_keycode();
	const uint64_t id = uint64_t(key) | (uint64_t(p_key->get_location()) << 32);
	if (p_key->is_echo()) {
		return false;
	}
	const bool was_held = held_keys.has(id);
	if (p_key->is_pressed()) {
		held_keys.insert(id);
	} else {
		held_keys.erase(id);
	}
	if (key != Key::SHIFT || p_key->is_ctrl_pressed() || p_key->is_alt_pressed() || p_key->is_meta_pressed()) {
		cancel();
		return false;
	}
	if (p_key->is_pressed()) {
		if (was_held || held_keys.size() != 1) {
			cancel();
			return false;
		}
		clean_shift = true;
		pressed_at = p_now;
		return false;
	}
	if (!was_held || !clean_shift || !held_keys.is_empty() || p_now - pressed_at > INTERVAL_MSEC) {
		cancel();
		return false;
	}
	clean_shift = false;
	// Wait for the release as well: Shift followed by Shift+A is not two taps.
	if (first_tap && p_now - first_tap_at <= INTERVAL_MSEC) {
		first_tap = false;
		return true;
	}
	first_tap = true;
	first_tap_at = p_now;
	return false;
}

bool EditorGlobalSearch::name_before(const String &p_a, const String &p_b, const String &p_query) {
	// Port of Script-IDE quick_open_popup.gd::sort_by_filter (MIT, Marius Hanl).
	for (int i = 0; i < p_query.length(); i++) {
		if (i >= p_a.length() && i >= p_b.length()) {
			break; // A deterministic tie break also makes this a strict ordering.
		}
		if (i >= p_a.length()) {
			return true;
		}
		if (i >= p_b.length()) {
			return false;
		}
		const bool a_match = p_query[i] == p_a[i];
		const bool b_match = p_query[i] == p_b[i];
		if (a_match != b_match) {
			return a_match;
		}
	}
	return p_a < p_b;
}

void EditorGlobalSearch::input(const Ref<InputEvent> &p_event) {
	Window *editor_window = get_window();
	if (dialog->is_visible() || EditorRunBar::get_singleton()->is_playing() || editor_window->get_exclusive_child()) {
		double_shift.reset();
		return;
	}
	Ref<InputEventKey> key = p_event;
	if (key.is_valid()) {
		// Also covers keys held before a dialog closed or editor focus returned,
		// whose original press was delivered to a different viewport.
		if (!key->is_pressed() && key->get_keycode() == Key::SHIFT && Input::get_singleton()->is_any_key_pressed()) {
			double_shift.reset();
			return;
		}
		if (double_shift.key_event(key, OS::get_singleton()->get_ticks_msec())) {
			popup_search();
			get_viewport()->set_input_as_handled();
		}
	} else if (p_event->is_pressed()) {
		double_shift.cancel();
	}
}

void EditorGlobalSearch::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			EditorFileSystem::get_singleton()->connect("filesystem_changed", callable_mp(this, &EditorGlobalSearch::_invalidate));
			EditorFileSystem::get_singleton()->connect("script_classes_updated", callable_mp(this, &EditorGlobalSearch::_invalidate));
			get_window()->connect("focus_exited", callable_mp(this, &EditorGlobalSearch::_reset_shortcut));
			EditorRunBar::get_singleton()->connect("play_pressed", callable_mp(this, &EditorGlobalSearch::_reset_shortcut));
			_update_theme();
		} break;
		case NOTIFICATION_THEME_CHANGED: {
			_update_theme();
		} break;
		case NOTIFICATION_APPLICATION_FOCUS_OUT: {
			double_shift.reset();
		} break;
		case NOTIFICATION_PROCESS: {
			if (!dialog->is_visible()) {
				set_process(false);
				break;
			}
			const uint64_t deadline = OS::get_singleton()->get_ticks_usec() + 4000;
			while (next_script < script_paths.size()) {
				_index_script(script_paths[next_script++]);
				if (OS::get_singleton()->get_ticks_usec() >= deadline) {
					break;
				}
			}
			if (next_script == script_paths.size()) {
				set_process(false);
			}
			_refresh();
		} break;
	}
}

void EditorGlobalSearch::_invalidate() {
	index_dirty = true;
	symbol_cache.clear();
	if (dialog->is_visible() && !refresh_queued) {
		refresh_queued = true;
		callable_mp(this, &EditorGlobalSearch::_filter_changed).call_deferred();
	}
}

void EditorGlobalSearch::_index_directory(EditorFileSystemDirectory *p_dir) {
	for (int i = 0; i < p_dir->get_subdir_count(); i++) {
		_index_directory(p_dir->get_subdir(i));
	}
	for (int i = 0; i < p_dir->get_file_count(); i++) {
		Entry entry;
		entry.path = p_dir->get_file_path(i);
		entry.name = entry.path.get_file();
		entry.detail = entry.path.get_base_dir().trim_prefix("res://");
		entry.icon = p_dir->get_file_type(i);
		if (entry.icon == StringName("Resource") || entry.icon == StringName()) {
			entry.icon = "Object";
		}
		if (ClassDB::is_parent_class(p_dir->get_file_type(i), "PackedScene")) {
			entry.category = SCENES;
			entry.order = 0;
		} else if (ClassDB::is_parent_class(p_dir->get_file_type(i), "Script")) {
			entry.category = SCRIPTS;
			entry.order = 1;
			script_paths.push_back(entry.path);
		} else if (entry.path.get_extension() == "tres" || entry.path.get_extension() == "res" || entry.path.get_extension() == "gdshader") {
			entry.category = RESOURCES;
			entry.order = entry.icon == StringName("Object") ? 2 : 3;
		}
		entries.push_back(entry);
	}
}

void EditorGlobalSearch::_rebuild_index() {
	index_dirty = false;
	entries.clear();
	script_paths.clear();
	open_sources.clear();
	open_languages.clear();
	next_script = 0;
	_index_directory(EditorFileSystem::get_singleton()->get_filesystem());
	// Use editor buffers without apply_code(), reloads or saving user changes.
	DocumentEditorContainer *container = ScriptEditor::get_singleton()->get_script_container();
	for (Control *control : container->get_all_editors()) {
		ScriptEditorBase *editor = Object::cast_to<ScriptEditorBase>(control);
		if (!editor) {
			continue;
		}
		Ref<Script> script = editor->get_edited_resource();
		if (script.is_null()) {
			continue;
		}
		TextEdit *text = Object::cast_to<TextEdit>(editor->get_base_editor());
		const String path = script->get_path();
		open_sources[path] = text ? text->get_text() : script->get_source_code();
		open_languages[path] = script->get_language()->get_editor_language();
		if (!script_paths.has(path)) {
			script_paths.push_back(path);
		}
	}
	HashSet<String> existing;
	for (const String &path : script_paths) {
		existing.insert(path);
	}
	Vector<String> removed;
	for (const KeyValue<String, SymbolCache> &kv : symbol_cache) {
		if (!existing.has(kv.key)) {
			removed.push_back(kv.key);
		}
	}
	for (const String &path : removed) {
		symbol_cache.erase(path);
	}
	set_process(true);
}

void EditorGlobalSearch::_index_script(const String &p_path) {
	EditorLanguage *language = open_languages.has(p_path) ? open_languages[p_path] : nullptr;
	for (int i = 0; !language && i < ScriptServer::get_language_count(); i++) {
		ScriptLanguage *candidate = ScriptServer::get_language(i);
		// Builtin scripts carry a subresource suffix after the extension.
		if (candidate->get_extension() == p_path.get_slice("::", 0).get_extension()) {
			language = candidate->get_editor_language();
			break;
		}
	}
	if (!language) {
		return;
	}
	const String *open_source = open_sources.getptr(p_path);
	uint64_t modified = 0;
	uint64_t size = 0;
	Ref<FileAccess> file;
	if (!open_source) {
		modified = FileAccess::get_modified_time(p_path);
		file = FileAccess::open(p_path, FileAccess::READ);
		if (file.is_null()) {
			return;
		}
		size = file->get_length();
	}
	SymbolCache *cached = symbol_cache.getptr(p_path);
	if (!cached || (open_source ? cached->source != *open_source : cached->modified != modified || cached->size != size)) {
		SymbolCache updated;
		updated.modified = modified;
		updated.size = size;
		const String source = open_source ? *open_source : file->get_as_text();
		// Store source only for open buffers, to keep the project index small.
		updated.source = open_source ? source : String();
		language->get_search_symbols(source, p_path, updated.symbols);
		symbol_cache[p_path] = updated;
		cached = symbol_cache.getptr(p_path);
	}
	for (const EditorLanguage::SearchSymbol &symbol : cached->symbols) {
		Entry entry;
		entry.path = p_path;
		entry.name = symbol.name;
		entry.detail = (symbol.container.is_empty() ? String() : symbol.container + " - ") + p_path.trim_prefix("res://") + ":" + itos(symbol.line + 1);
		entry.icon = symbol.icon;
		entry.line = symbol.line;
		entry.category = SYMBOLS;
		entry.order = 4;
		entries.push_back(entry);
	}
}

void EditorGlobalSearch::_refresh() {
	const String text = query->get_text();
	const int category = tabs->get_current_tab();
	const bool include_addons = scope->get_selected() == 1;
	Entry selected;
	bool had_selection = false;
	const Vector<int> selection = results->get_selected_items();
	if (!selection.is_empty() && selection[0] < matches.size()) {
		selected = matches[selection[0]];
		had_selection = true;
	}
	matches.clear();
	for (const Entry &entry : entries) {
		if ((!include_addons && entry.path.begins_with("res://addons/")) || (category != ALL && entry.category != category)) {
			continue;
		}
		// Empty All starts with files, as in Script-IDE; Symbols can list every declaration.
		if (category == ALL && text.is_empty() && entry.category == SYMBOLS) {
			continue;
		}
		const String target = entry.category == SYMBOLS ? entry.name + " " + entry.detail : entry.path;
		if (text.is_empty() || text.is_subsequence_ofn(target)) {
			matches.push_back(entry);
		}
	}
	const auto compare = [&](const Entry &a, const Entry &b) {
		if (category == ALL && a.order != b.order) {
			return a.order < b.order;
		}
		if (a.name != b.name) {
			return name_before(a.name, b.name, text);
		}
		if (a.path != b.path) {
			return a.path < b.path;
		}
		return a.line < b.line;
	};
	matches.sort_custom<decltype(compare)>(compare);
	results->clear();
	int select = 0;
	// Sorting covers the entire index; only create UI rows for the first 300 hits.
	const int visible_count = MIN(300, matches.size());
	for (int i = 0; i < visible_count; i++) {
		const Entry &entry = matches[i];
		StringName icon = has_theme_icon(entry.icon, EditorStringName(EditorIcons)) ? entry.icon : StringName("Object");
		results->add_item(entry.name + (entry.detail.is_empty() ? String() : "  -  " + entry.detail), get_theme_icon(icon, EditorStringName(EditorIcons)));
		results->set_item_tooltip(i, entry.path + (entry.line >= 0 ? ":" + itos(entry.line + 1) : String()));
		if (had_selection && selected.path == entry.path && selected.line == entry.line && selected.name == entry.name) {
			select = i;
		}
	}
	if (visible_count > 0) {
		results->select(select);
		results->ensure_current_is_visible();
	}
	dialog->get_ok_button()->set_disabled(visible_count == 0);
	String message = vformat(TTR("%d results"), matches.size());
	if (matches.size() > visible_count) {
		message += " - " + TTR("Showing first 300; refine your search.");
	}
	if (next_script < script_paths.size()) {
		message += " - " + vformat(TTR("Indexing symbols: %d/%d"), next_script, script_paths.size());
	}
	status->set_text(message);
}

void EditorGlobalSearch::_filter_changed() {
	refresh_queued = false;
	if (!dialog->is_visible()) {
		return;
	}
	if (index_dirty) {
		_rebuild_index();
	}
	results->deselect_all();
	_refresh();
	query->grab_focus();
}

void EditorGlobalSearch::_query_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventKey> key = p_event;
	if (key.is_null() || !key->is_pressed()) {
		return;
	}
	if (key->get_keycode() == Key::TAB && key->is_ctrl_pressed()) {
		tabs->set_current_tab((tabs->get_current_tab() + (key->is_shift_pressed() ? 5 : 1)) % 6);
		query->accept_event();
		return;
	}
	if (key->get_keycode() == Key::UP || key->get_keycode() == Key::DOWN || key->get_keycode() == Key::PAGEUP || key->get_keycode() == Key::PAGEDOWN) {
		const Vector<int> selection = results->get_selected_items();
		int index = selection.is_empty() ? 0 : selection[0];
		const int delta = key->get_keycode() == Key::UP ? -1 : key->get_keycode() == Key::DOWN ? 1
				: key->get_keycode() == Key::PAGEUP											   ? -10
																							   : 10;
		if (results->get_item_count() > 0) {
			results->select(CLAMP(index + delta, 0, results->get_item_count() - 1));
			results->ensure_current_is_visible();
		}
		query->accept_event();
	}
}

void EditorGlobalSearch::_open_selected() {
	const Vector<int> selection = results->get_selected_items();
	if (selection.is_empty() || selection[0] >= matches.size()) {
		return;
	}
	const Entry entry = matches[selection[0]];
	dialog->hide();
	if (entry.line >= 0 || entry.category == SCRIPTS) {
		Ref<Script> script = ResourceLoader::load(entry.path);
		if (script.is_valid()) {
			ScriptEditor::get_singleton()->edit(script, entry.line, 0);
		}
	} else if (ResourceLoader::exists(entry.path) || ScriptEditor::get_singleton()->get_script_container()->can_open_file(entry.path)) {
		// Unlike the plugin, the engine can open supported plain text files too.
		EditorNode::get_singleton()->load_scene_or_resource(entry.path);
	} else {
		FileSystemDock::get_singleton()->select_file(entry.path);
	}
}

void EditorGlobalSearch::_dialog_hidden() {
	if (dialog->is_visible()) {
		return;
	}
	double_shift.reset();
	set_process(false);
}

void EditorGlobalSearch::_reset_shortcut() {
	double_shift.reset();
}

void EditorGlobalSearch::popup_search() {
	if (EditorRunBar::get_singleton()->is_playing() || dialog->is_visible() || get_window()->get_exclusive_child()) {
		return;
	}
	double_shift.reset();
	// Re-read open buffers and reconcile filesystem metadata on every invocation.
	_rebuild_index();
	dialog->popup_centered_clamped(Size2(850, 570) * EDSCALE, 0.85);
	_refresh();
	query->grab_focus();
	query->select_all();
}

void EditorGlobalSearch::_update_theme() {
	if (!is_inside_tree()) {
		return;
	}
	query->set_right_icon(get_theme_icon("Search", EditorStringName(EditorIcons)));
	const StringName icons[] = { "Search", "PackedScene", "Script", "Object", "File", "MemberMethod" };
	for (int i = 0; i < 6; i++) {
		tabs->set_tab_icon(i, get_theme_icon(icons[i], EditorStringName(EditorIcons)));
	}
	results->set_fixed_icon_size(Size2(16, 16) * EDSCALE);
}

EditorGlobalSearch::EditorGlobalSearch() {
	set_name("GlobalSearch");
	set_mouse_filter(MOUSE_FILTER_IGNORE);
	set_process_input(true);
	set_process(false);
	dialog = memnew(ConfirmationDialog);
	dialog->set_name("GlobalSearchDialog");
	dialog->set_title(TTR("Search Everywhere"));
	dialog->set_ok_button_text(TTR("Open"));
	dialog->set_hide_on_ok(false);
	add_child(dialog);
	VBoxContainer *vbox = memnew(VBoxContainer);
	dialog->add_child(vbox);
	HBoxContainer *header = memnew(HBoxContainer);
	vbox->add_child(header);
	tabs = memnew(TabBar);
	tabs->set_name("Categories");
	tabs->set_h_size_flags(SIZE_EXPAND_FILL);
	tabs->set_clip_tabs(false);
	tabs->add_tab(TTR("All"));
	tabs->add_tab(TTR("Scenes"));
	tabs->add_tab(TTR("Scripts"));
	tabs->add_tab(TTR("Resources"));
	tabs->add_tab(TTR("Other"));
	tabs->add_tab(TTR("Symbols"));
	header->add_child(tabs);
	scope = memnew(OptionButton);
	scope->set_name("Scope");
	scope->add_item(TTR("Project"));
	scope->add_item(TTR("Project + Addons"));
	header->add_child(scope);
	query = memnew(LineEdit);
	query->set_name("Query");
	query->set_placeholder(TTR("Search files and symbols..."));
	query->set_clear_button_enabled(true);
	vbox->add_child(query);
	results = memnew(ItemList);
	results->set_name("Results");
	results->set_v_size_flags(SIZE_EXPAND_FILL);
	results->set_custom_minimum_size(Size2(0, 350) * EDSCALE);
	results->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	vbox->add_child(results);
	status = memnew(Label);
	status->set_name("Status");
	vbox->add_child(status);
	tabs->connect("tab_changed", callable_mp(this, &EditorGlobalSearch::_filter_changed).unbind(1));
	scope->connect("item_selected", callable_mp(this, &EditorGlobalSearch::_filter_changed).unbind(1));
	query->connect("text_changed", callable_mp(this, &EditorGlobalSearch::_filter_changed).unbind(1));
	query->connect("gui_input", callable_mp(this, &EditorGlobalSearch::_query_input));
	query->connect("text_submitted", callable_mp(this, &EditorGlobalSearch::_open_selected).unbind(1));
	results->connect("item_activated", callable_mp(this, &EditorGlobalSearch::_open_selected).unbind(1));
	dialog->connect("confirmed", callable_mp(this, &EditorGlobalSearch::_open_selected));
	dialog->connect("visibility_changed", callable_mp(this, &EditorGlobalSearch::_dialog_hidden));
}
