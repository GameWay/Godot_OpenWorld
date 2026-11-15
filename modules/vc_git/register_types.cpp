/**************************************************************************/
/*  register_types.cpp                                                    */
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

#include "register_types.h"

#include "vc_git_scripts.gen.h"

#include "core/config/project_settings.h"
#include "core/io/compression.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/object/script_language.h"
#include "editor/editor_node.h"
#include "editor/file_system/editor_paths.h"
#include "editor/plugins/editor_plugin.h"
#include "editor/settings/editor_settings.h"

// OEngine: vc_git, the editor's git client (the Git tab, git status in the docks, Local History…).
// It is written in GDScript (scripts/): the sources are compressed into the editor binary, extracted
// to the editor's cache when they change, and run in every project. Editor builds only.
// Its preferences are Editor Settings under version_control/vc_git/ (see scripts/util/settings.gd).

static const char *VC_GIT_SETTINGS = "version_control/vc_git/";
static const char *VC_GIT_ENABLED_SETTING = "version_control/vc_git/enabled";
// Shared by the team, so in project.godot.
static const char *VC_GIT_MASKS_SETTING = "editor/version_control/vc_git/auto_add_ignore_masks";
// The add-on vc_git was made from: a project that still enables it keeps it, since running both
// would register everything twice.
static const char *VC_GIT_PROJECT_ADDON = "res://addons/git/plugin.cfg";
static const char *VC_GIT_COMPLETE_MARKER = ".complete";

static void _vc_git_def(const String &p_name, const Variant &p_default, PropertyHint p_hint = PROPERTY_HINT_NONE, const String &p_hint_string = String(), bool p_restart = false) {
	const String path = String(VC_GIT_SETTINGS) + p_name;
	_EDITOR_DEF(path, p_default, p_restart);
	if (p_hint != PROPERTY_HINT_NONE) {
		EditorSettings::get_singleton()->add_property_hint(PropertyInfo(p_default.get_type(), path, p_hint, p_hint_string));
	}
}

static void _vc_git_register_settings() {
	_vc_git_def("enabled", true, PROPERTY_HINT_NONE, String(), true);

	_vc_git_def("changes/auto_stage", true);
	_vc_git_def("changes/auto_add_new_files", true);
	_vc_git_def("changes/confirm_shortcut_commit", true);

	_vc_git_def("diff/side_by_side", false);
	_vc_git_def("diff/ignore_whitespace", false);
	_vc_git_def("diff/syntax_highlighting", true);
	_vc_git_def("diff/dim_godot_noise", true);
	_vc_git_def("diff/context_lines", 3, PROPERTY_HINT_ENUM, "3 Lines:3,10 Lines:10,25 Lines:25,Whole File:-1");
	_vc_git_def("diff/scenes_node_by_node", true);
	_vc_git_def("diff/image_mode", "side", PROPERTY_HINT_ENUM, "side,swipe,onion");

	_vc_git_def("history/absolute_dates", false);

	_vc_git_def("local_history/enabled", true);
	_vc_git_def("local_history/interval_minutes", 5, PROPERTY_HINT_RANGE, "1,120,1,suffix:min");
	_vc_git_def("local_history/keep_days", 7, PROPERTY_HINT_RANGE, "1,365,1,suffix:days");
	_vc_git_def("local_history/max_file_size_mb", 5, PROPERTY_HINT_RANGE, "1,1024,1,suffix:MB");

	_vc_git_def("editor/show_blame", false);
	_vc_git_def("editor/toolbar_file_status", true);
	_vc_git_def("editor/inspector_changes", true);

	_vc_git_def("sync/auto_fetch", false);
	_vc_git_def("sync/auto_fetch_minutes", 10, PROPERTY_HINT_RANGE, "1,240,1,suffix:min");
	_vc_git_def("sync/reminders", true);
	_vc_git_def("sync/reminder_hours", 2, PROPERTY_HINT_RANGE, "1,48,1,suffix:h");
	_vc_git_def("sync/auto_save_scripts", false);
	_vc_git_def("sync/auto_save_seconds", 3, PROPERTY_HINT_RANGE, "1,600,1,suffix:s");
	_vc_git_def("sync/auto_reload_external_changes", true);
	_vc_git_def("sync/save_before_git", false);

	Array masks;
	for (const char *mask : { "*.tmp", "*.bak", "*.orig", "*~", ".DS_Store" }) {
		masks.push_back(mask);
	}
	GLOBAL_DEF(PropertyInfo(Variant::ARRAY, VC_GIT_MASKS_SETTING, PROPERTY_HINT_TYPE_STRING, vformat("%d:", Variant::STRING)), masks);
}

static void _vc_git_open_settings() {
	EditorNode::get_singleton()->open_editor_settings("version_control/vc_git");
}

static uint32_t _vc_git_read_u32(const uint8_t *p_data) {
	return uint32_t(p_data[0]) | (uint32_t(p_data[1]) << 8) | (uint32_t(p_data[2]) << 16) | (uint32_t(p_data[3]) << 24);
}

// Returns the folder with the extracted scripts, or an empty string on failure.
static String _vc_git_extract() {
	const String base_dir = EditorPaths::get_singleton()->get_cache_dir().path_join("oengine_vc_git");
	const String dir = base_dir.path_join(_vc_git_scripts_hash);
	if (FileAccess::exists(dir.path_join(VC_GIT_COMPLETE_MARKER))) {
		return dir;
	}

	Vector<uint8_t> archive;
	archive.resize(_vc_git_scripts_uncompressed_size);
	const int64_t size = Compression::decompress(archive.ptrw(), _vc_git_scripts_uncompressed_size, _vc_git_scripts_compressed, _vc_git_scripts_compressed_size, Compression::MODE_ZSTD);
	ERR_FAIL_COND_V_MSG(size != _vc_git_scripts_uncompressed_size, String(), "vc_git: can't decompress the built-in scripts.");

	// Scripts of an earlier OEngine build are no longer used.
	Ref<DirAccess> base = DirAccess::open(base_dir);
	if (base.is_valid()) {
		for (const String &old : base->get_directories()) {
			if (old != _vc_git_scripts_hash) {
				Ref<DirAccess> old_dir = DirAccess::open(base_dir.path_join(old));
				if (old_dir.is_valid()) {
					old_dir->erase_contents_recursive();
				}
				DirAccess::remove_absolute(base_dir.path_join(old));
			}
		}
	}

	const uint8_t *r = archive.ptr();
	int64_t offset = 0;
	while (offset < size) {
		ERR_FAIL_COND_V(offset + 4 > size, String());
		const uint32_t path_length = _vc_git_read_u32(r + offset);
		offset += 4;
		ERR_FAIL_COND_V(offset + path_length + 4 > size, String());
		const String rel_path = String::utf8((const char *)r + offset, path_length);
		offset += path_length;
		const uint32_t data_length = _vc_git_read_u32(r + offset);
		offset += 4;
		ERR_FAIL_COND_V(offset + data_length > size, String());
		ERR_FAIL_COND_V_MSG(rel_path.is_absolute_path() || rel_path.contains(".."), String(), "vc_git: invalid path in the built-in scripts.");

		const String path = dir.path_join(rel_path);
		ERR_FAIL_COND_V(DirAccess::make_dir_recursive_absolute(path.get_base_dir()) != OK, String());
		Ref<FileAccess> f = FileAccess::open(path, FileAccess::WRITE);
		ERR_FAIL_COND_V_MSG(f.is_null(), String(), vformat("vc_git: can't write \"%s\".", path));
		f->store_buffer(r + offset, data_length);
		offset += data_length;
	}

	// Written last: a folder without it is an interrupted extraction and is written again.
	Ref<FileAccess> marker = FileAccess::open(dir.path_join(VC_GIT_COMPLETE_MARKER), FileAccess::WRITE);
	ERR_FAIL_COND_V(marker.is_null(), String());
	return dir;
}

static void _vc_git_start() {
	_vc_git_register_settings();
	if (!bool(EDITOR_GET(VC_GIT_ENABLED_SETTING))) {
		return;
	}

	if (ProjectSettings::get_singleton()->has_setting("editor_plugins/enabled")) {
		const PackedStringArray enabled = GLOBAL_GET("editor_plugins/enabled");
		if (enabled.has(VC_GIT_PROJECT_ADDON)) {
			print_line(vformat("vc_git: the project enables the git add-on (%s), so the built-in one stays off. Remove the add-on from the project to use the built-in one.", VC_GIT_PROJECT_ADDON));
			return;
		}
	}

	const String dir = _vc_git_extract();
	if (dir.is_empty()) {
		return;
	}

	const String script_path = dir.path_join("plugin.gd");
	Ref<Script> scr = ResourceLoader::load(script_path, "Script");
	ERR_FAIL_COND_MSG(scr.is_null(), vformat("vc_git: can't load \"%s\".", script_path));
	ERR_FAIL_COND_MSG(scr->get_instance_base_type() == StringName(), vformat("vc_git: \"%s\" has errors.", script_path));
	ERR_FAIL_COND(!ClassDB::is_parent_class(scr->get_instance_base_type(), "EditorPlugin") || !scr->is_tool());

	EditorPlugin *ep = Object::cast_to<EditorPlugin>(ClassDB::instantiate(scr->get_instance_base_type()));
	ERR_FAIL_NULL(ep);
	// What the scripts can't reach from GDScript: Editor Settings opened at vc_git's section.
	ep->set_meta("vc_git_open_settings", callable_mp_static(&_vc_git_open_settings));
	ep->set_script(scr);
	EditorNode::add_editor_plugin(ep);
}

void initialize_vc_git_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		EditorNode::add_addons_init_callback(&_vc_git_start);
	}
}

void uninitialize_vc_git_module(ModuleInitializationLevel p_level) {
}
