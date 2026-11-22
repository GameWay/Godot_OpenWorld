## vc_git's settings. The preferences are Editor Settings under version_control/vc_git/ (registered,
## with their defaults and ranges, by modules/vc_git/register_types.cpp); the team's conventions are
## project settings (project.godot, committed with the project); everything else is UI state (open
## view, dividers, column widths, reminder clocks), kept in files: for all projects in the editor's
## config folder, or for this project in user://. No class_name: internal helper, addressed via preload.
extends RefCounted

const SCOPE_EDITOR := "editor"
const SCOPE_GLOBAL := "global"
const SCOPE_PROJECT := "project"
const SCOPE_TEAM := "team"

const EDITOR_PREFIX := "version_control/vc_git/"
## Preferences, key -> its editor setting under EDITOR_PREFIX.
const EDITOR_KEYS := {
	"auto_stage_changes": "changes/auto_stage",
	"auto_add_new_files": "changes/auto_add_new_files",
	"confirm_shortcut_commit": "changes/confirm_shortcut_commit",
	"diff_side_by_side": "diff/side_by_side",
	"diff_ignore_whitespace": "diff/ignore_whitespace",
	"diff_syntax": "diff/syntax_highlighting",
	"diff_hide_noise": "diff/dim_godot_noise",
	"diff_context": "diff/context_lines",
	"diff_scene_view": "diff/scenes_node_by_node",
	"diff_image_mode": "diff/image_mode",
	"history_absolute_dates": "history/absolute_dates",
	"local_history": "local_history/enabled",
	"local_history_minutes": "local_history/interval_minutes",
	"local_history_days": "local_history/keep_days",
	"local_history_max_mb": "local_history/max_file_size_mb",
	"changed_lines_markers": "editor/changed_lines_markers",
	"highlight_changed_characters": "editor/highlight_changed_characters",
	"show_blame": "editor/show_blame",
	"toolbar_branch": "editor/toolbar_file_status",
	"inspector_changes": "editor/inspector_changes",
	"auto_fetch": "sync/auto_fetch",
	"auto_fetch_minutes": "sync/auto_fetch_minutes",
	"reminders": "sync/reminders",
	"reminder_hours": "sync/reminder_hours",
	"auto_save_scripts": "sync/auto_save_scripts",
	"auto_save_seconds": "sync/auto_save_seconds",
	"auto_reload_external_changes": "sync/auto_reload_external_changes",
	"save_before_git": "sync/save_before_git",
}
## Team conventions, key -> its project setting.
const TEAM_KEYS := { "auto_add_ignore_masks": "editor/version_control/vc_git/auto_add_ignore_masks" }
## UI state that follows the user to every project; any other key stays per project.
const GLOBAL_KEYS := [
	"dock_layout", "diff_main_screen", "history_visible_columns",
	"history_hash_col_width", "history_author_col_width", "history_date_col_width",
	"auto_save_original_interval", "local_history_group_folders",
]

const CONFIG_PATH := "user://vc_git_settings.cfg"
const GLOBAL_FILE_NAME := "vc_git_settings.cfg"
const SECTION := "vc_git"

## Tests point these elsewhere; "" means the real places.
static var local_path_override := ""
static var global_path_override := ""
static var project_file_override := ""
static var _notifier: Notifier
## Editor setting path -> value as last seen, to tell which ones Editor Settings changed.
static var _editor_values := {}


class Notifier:
	extends RefCounted
	signal changed(key: String)


## Emits changed(key) after every set_value() and reset(), and when a preference is changed in
## Editor Settings, so open panels and menus follow a change made anywhere. Created on first use:
## the editor doesn't run static initializers of scripts that aren't @tool.
static func notifier() -> Notifier:
	if _notifier == null:
		_notifier = Notifier.new()
		var editor_settings := _editor_settings()
		if editor_settings != null:
			for key: String in EDITOR_KEYS:
				_editor_values[_editor_path(key)] = editor_settings.get_setting(_editor_path(key))
			editor_settings.settings_changed.connect(_on_editor_settings_changed)
	return _notifier


static func _on_editor_settings_changed() -> void:
	var editor_settings := _editor_settings()
	for key: String in EDITOR_KEYS:
		var path := _editor_path(key)
		var value: Variant = editor_settings.get_setting(path)
		if value != _editor_values.get(path):
			_editor_values[path] = value
			_notifier.changed.emit(key)


static func scope_of(key: String) -> String:
	if TEAM_KEYS.has(key):
		return SCOPE_TEAM
	if EDITOR_KEYS.has(key) and _editor_settings() != null:
		return SCOPE_EDITOR
	return SCOPE_GLOBAL if GLOBAL_KEYS.has(key) or EDITOR_KEYS.has(key) else SCOPE_PROJECT


static func get_value(key: String, default: Variant) -> Variant:
	match scope_of(key):
		SCOPE_EDITOR:
			var editor_settings := _editor_settings()
			var path := _editor_path(key)
			return editor_settings.get_setting(path) if editor_settings.has_setting(path) else default
		SCOPE_TEAM:
			var setting: String = TEAM_KEYS[key]
			return ProjectSettings.get_setting(setting) if ProjectSettings.has_setting(setting) else default
		SCOPE_GLOBAL:
			var global := _load(_global_path())
			# ConfigFile.get_value() logs an error for a missing key when default is null.
			return global.get_value(SECTION, key) if global.has_section_key(SECTION, key) else default
	var local := _load(_local_path())
	return local.get_value(SECTION, key) if local.has_section_key(SECTION, key) else default


static func set_value(key: String, value: Variant) -> void:
	_store(key, value)
	notifier().changed.emit(key)


## Back to the default.
static func reset(key: String) -> void:
	_store(key, null)
	notifier().changed.emit(key)


static func _store(key: String, value: Variant) -> void:
	match scope_of(key):
		SCOPE_EDITOR:
			var editor_settings := _editor_settings()
			var path := _editor_path(key)
			if value == null:
				value = editor_settings.property_get_revert(path)
			_editor_values[path] = value # notifier() emits for this change already
			editor_settings.set_setting(path, value)
		SCOPE_TEAM:
			var setting: String = TEAM_KEYS[key]
			if value == null:
				value = ProjectSettings.property_get_revert(setting)
			ProjectSettings.set_setting(setting, value)
			if project_file_override.is_empty():
				ProjectSettings.save()
			else:
				ProjectSettings.save_custom(project_file_override)
		var scope:
			var path := _global_path() if scope == SCOPE_GLOBAL else _local_path()
			var cfg := _load(path)
			if value == null:
				if not cfg.has_section_key(SECTION, key):
					return
				cfg.erase_section_key(SECTION, key)
			else:
				cfg.set_value(SECTION, key, value)
			_save(cfg, path)


static func _editor_settings() -> EditorSettings:
	return EditorInterface.get_editor_settings() if Engine.is_editor_hint() else null


static func _editor_path(key: String) -> String:
	return EDITOR_PREFIX + EDITOR_KEYS[key]


static func _local_path() -> String:
	return local_path_override if not local_path_override.is_empty() else CONFIG_PATH


## The editor's config folder (next to editor_settings-4.x.tres); user:// where there's no editor (tests run with --script).
static func _global_path() -> String:
	if not global_path_override.is_empty():
		return global_path_override
	if Engine.is_editor_hint():
		return EditorInterface.get_editor_paths().get_config_dir().path_join(GLOBAL_FILE_NAME)
	return "user://vc_git_global_settings.cfg"


static func _load(path: String) -> ConfigFile:
	var cfg := ConfigFile.new()
	cfg.load(path) # a missing file is fine: no keys
	return cfg


static func _save(cfg: ConfigFile, path: String) -> void:
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	cfg.save(path)


## Once per project and per user: moves what the git add-on this came from ("godit") kept, so its
## preferences, team masks, UI state and changelists carry over. Its Local History moves in local_history.gd.
static func migrate_legacy() -> void:
	if not Engine.is_editor_hint():
		return
	const LEGACY_SECTION := "godit"
	var editor_settings := _editor_settings()
	var config_dir := EditorInterface.get_editor_paths().get_config_dir()
	# The per-project file first: a value kept for all projects wins over an older per-project one.
	for pair: Array in [["user://godit_settings.cfg", _local_path()], [config_dir.path_join("godit_settings.cfg"), _global_path()]]:
		var old_path: String = pair[0]
		if not FileAccess.file_exists(old_path):
			continue
		var old := _load(old_path)
		var target := _load(pair[1])
		for key: String in (old.get_section_keys(LEGACY_SECTION) if old.has_section(LEGACY_SECTION) else PackedStringArray()):
			var value: Variant = old.get_value(LEGACY_SECTION, key)
			if EDITOR_KEYS.has(key):
				editor_settings.set_setting(_editor_path(key), value)
			elif GLOBAL_KEYS.has(key) and pair[1] == _local_path():
				var global := _load(_global_path())
				global.set_value(SECTION, key, value)
				_save(global, _global_path())
			else:
				target.set_value(SECTION, key, value)
		_save(target, pair[1])
		DirAccess.remove_absolute(old_path)
	if FileAccess.file_exists("user://godit_changelists.cfg") and not FileAccess.file_exists("user://vc_git_changelists.cfg"):
		var changelists := _load("user://godit_changelists.cfg")
		var moved := ConfigFile.new()
		for section in changelists.get_sections():
			for key in changelists.get_section_keys(section):
				moved.set_value(SECTION if section == LEGACY_SECTION else section, key, changelists.get_value(section, key))
		_save(moved, "user://vc_git_changelists.cfg")
		DirAccess.remove_absolute("user://godit_changelists.cfg")
	const LEGACY_MASKS := "godit/changes/auto_add_ignore_masks"
	if ProjectSettings.has_setting(LEGACY_MASKS):
		ProjectSettings.set_setting(TEAM_KEYS["auto_add_ignore_masks"], ProjectSettings.get_setting(LEGACY_MASKS))
		ProjectSettings.set_setting(LEGACY_MASKS, null)
		ProjectSettings.save()
