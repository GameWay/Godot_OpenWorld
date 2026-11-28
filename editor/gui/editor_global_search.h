/**************************************************************************/
/*  editor_global_search.h                                                */
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

#include "core/input/input_event.h"
#include "core/object/editor_language.h"
#include "core/templates/hash_set.h"
#include "scene/gui/control.h"

class ConfirmationDialog;
class EditorFileSystemDirectory;
class ItemList;
class Label;
class LineEdit;
class OptionButton;
class TabBar;

// Kept separate from the UI so chords, repeats and focus resets can be tested.
class EditorDoubleShift {
	HashSet<uint64_t> held_keys;
	uint64_t pressed_at = 0;
	uint64_t first_tap_at = 0;
	bool clean_shift = false;
	bool first_tap = false;

public:
	static constexpr uint64_t INTERVAL_MSEC = 500;
	void reset();
	void cancel();
	bool key_event(const Ref<InputEventKey> &p_key, uint64_t p_now);
};

class EditorGlobalSearch : public Control {
	GDCLASS(EditorGlobalSearch, Control);

public:
	enum Category { ALL,
		SCENES,
		SCRIPTS,
		RESOURCES,
		OTHER,
		SYMBOLS };
	struct Entry {
		String path;
		String name;
		String detail;
		StringName icon;
		Category category = OTHER;
		int order = 3;
		int line = -1;
	};
	// Script-IDE's prefix-character preference and lexical tie break.
	static bool name_before(const String &p_a, const String &p_b, const String &p_query);
	void popup_search();
	EditorGlobalSearch();

protected:
	void _notification(int p_what);
	virtual void input(const Ref<InputEvent> &p_event) override;

private:
	struct SymbolCache {
		uint64_t modified = 0;
		uint64_t size = 0;
		String source;
		Vector<EditorLanguage::SearchSymbol> symbols;
	};
	EditorDoubleShift double_shift;
	ConfirmationDialog *dialog = nullptr;
	TabBar *tabs = nullptr;
	OptionButton *scope = nullptr;
	LineEdit *query = nullptr;
	ItemList *results = nullptr;
	Label *status = nullptr;
	Vector<Entry> entries;
	Vector<Entry> matches;
	Vector<String> script_paths;
	HashMap<String, SymbolCache> symbol_cache;
	HashMap<String, String> open_sources;
	HashMap<String, EditorLanguage *> open_languages;
	bool index_dirty = true;
	bool refresh_queued = false;
	int next_script = 0;

	void _invalidate();
	void _rebuild_index();
	void _index_directory(EditorFileSystemDirectory *p_dir);
	void _index_script(const String &p_path);
	void _refresh();
	void _filter_changed();
	void _query_input(const Ref<InputEvent> &p_event);
	void _open_selected();
	void _update_theme();
	void _dialog_hidden();
	void _reset_shortcut();
};
