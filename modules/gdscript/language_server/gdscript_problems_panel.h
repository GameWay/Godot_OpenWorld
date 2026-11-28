/**************************************************************************/
/*  gdscript_problems_panel.h                                             */
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

#include "editor/docks/editor_dock.h"

class Script;
class Tree;

// A view of existing editor/LSP publications. Never parses or validates scripts.
class GDScriptProblemsPanel : public EditorDock {
	GDCLASS(GDScriptProblemsPanel, EditorDock);

	struct Snapshot {
		Array diagnostics;
		uint64_t revision = 0;
		uint64_t modified_time = 0;
	};
	HashMap<int, HashMap<String, Snapshot>> snapshots;
	uint64_t revision = 0;
	Tree *tree = nullptr;
	bool refresh_pending = false;

	void _diagnostics_updated(const String &p_path, const Array &p_diagnostics, int p_client_id);
	void _diagnostics_cleared(const String &p_path, int p_client_id);
	void _client_closed(int p_client_id);
	void _script_closed(const Ref<Script> &p_script);
	void _filesystem_changed();
	void _queue_refresh();
	void _refresh();
	void _item_activated();
	void _update_indicator(bool p_errors, bool p_warnings);

protected:
	void _notification(int p_what);

public:
	GDScriptProblemsPanel();
};
