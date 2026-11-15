# vc_git: OEngine's git client for the editor, started by modules/vc_git (register_types.cpp).
#
# Based on Godit. MIT License, Copyright (c) 2026 Michal Rychtar.
# Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
# associated documentation files (the "Software"), to deal in the Software without restriction,
# including without limitation the rights to use, copy, modify, merge, publish, distribute,
# sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions: The above copyright notice and this
# permission notice shall be included in all copies or substantial portions of the Software.
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
# NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES
# OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
@tool
extends EditorPlugin

const VcGitDockScene := preload("dock/vc_git_dock.tscn")
const VcGitHistoryDockScene := preload("dock/vc_git_history_dock.tscn")
const DiffGutterScript := preload("dock/gutter/diff_gutter.gd")
const BlameGutterScript := preload("dock/gutter/blame_gutter.gd")
const ScriptMenuScript := preload("dock/gutter/script_menu.gd")
const Settings := preload("util/settings.gd")
const GitCli := preload("util/git_cli.gd")
const VcGitDockScript := preload("dock/vc_git_dock.gd")
const FilesystemColorsScript := preload("dock/filesystem/filesystem_colors.gd")
const FilesystemMenuScript := preload("dock/filesystem/filesystem_menu.gd")
const CombinedDockScript := preload("dock/vc_git_combined_dock.gd")
const RepoWatcherScript := preload("util/repo_watcher.gd")
const GitCliRepo := preload("util/git_cli_repo.gd")
const HeadInspectorScript := preload("dock/inspector/head_inspector.gd")
const SceneDockColorsScript := preload("dock/inspector/scene_dock_colors.gd")
const Dialogs := preload("dock/widgets/dialogs.gd")
const EditorCommands := preload("util/editor_commands.gd")
const EditorOpen := preload("util/editor_open.gd")
const BranchToolbarScript := preload("dock/widgets/branch_toolbar.gd")
const RepoOpener := preload("util/repo_opener.gd")
const QuickCommitScript := preload("dock/widgets/quick_commit.gd")
const RemindersScript := preload("util/reminders.gd")
const SetupCheckScript := preload("dock/widgets/setup_check.gd")
const RemoteActions := preload("dock/widgets/remote_actions.gd")
const LocalHistoryRecorderScript := preload("util/local_history_recorder.gd")
const LocalHistoryWindowScript := preload("dock/widgets/local_history_window.gd")
## Holds Changes + Branches until they go into the Git tab (and the "Initialize Repository" view's logic without a repo); never shown.
var dock_instance: Control
## Holds History + Console until they go into the Git tab; never shown.
var history_dock_instance: Control
## The Git tab next to 2D/3D/Script: every panel, shown in place of the viewport while the tab is open.
var combined_dock: Control
## Changed-line flags in the script editor's gutter, next to Bookmarks.
var diff_gutter: Node
## Author/age column in the script editor, toggled from its menu, Editor Settings and the palette.
var blame_gutter: Node
## "Git" submenu in the script editor's right-click menu.
var script_menu: EditorContextMenuPlugin
## Git status colors in the FileSystem dock.
var filesystem_colors: Node
## "Git" items in the FileSystem dock's right-click menu.
var filesystem_menu: EditorContextMenuPlugin
## Polls the repo for every panel; created before them, since they subscribe as they get their repo.
var watcher: Node
## Toasts about work left uncommitted or unpushed for hours.
var reminders: Node
## Local History snapshots on save, on a timer and before git operations.
var local_history: Node
## Project > Tools > Git: Settings… (Editor Settings > Version Control > Vc Git), Check Setup…, the project's Local History and the repository's faster status.
var tools_menu: PopupMenu
## The screen to go back to when the Git tab is toggled off; the open file's history follows it.
var _last_main_screen := "Script"
## "Last commit" rows under changed properties in the Inspector.
var head_inspector: EditorInspectorPlugin
## Changed nodes colored in the Scene dock.
var scene_dock_colors: Node
## Command Palette entries and their shortcuts.
var commands: RefCounted
## The current branch in the editor's main toolbar, or null outside a repository.
var branch_toolbar: BranchToolbarScript
## The quick commit window, created the first time it's opened.
var _quick_commit: QuickCommitScript
## vc_git Setup, created the first time it's opened.
var _setup_check: SetupCheckScript
## Local History, created the first time it's opened.
var _local_history_window: LocalHistoryWindowScript
var _activation_started := false

const AUTO_RELOAD_SETTING_KEY := "auto_reload_external_changes"
const AUTO_RELOAD_EDITOR_SETTING := "text_editor/behavior/files/auto_reload_scripts_on_external_change"

const AUTO_SAVE_SETTING_KEY := "auto_save_scripts"
const AUTO_SAVE_EDITOR_SETTING := "text_editor/behavior/files/autosave_interval_secs"
const AUTO_SAVE_INTERVAL_SECS := 3
const AUTO_SAVE_SECONDS_KEY := "auto_save_seconds"
## The user's own autosave interval, kept while vc_git's auto-save is on so turning it off puts it back.
const AUTO_SAVE_ORIGINAL_KEY := "auto_save_original_interval"

const BLAME_SETTING_KEY := "show_blame"

## The Git tab's name at the top of the editor, after Script.
const GIT_SCREEN_NAME := "Git"

const INSPECTOR_SETTING_KEY := "inspector_changes"
const ID_SETTINGS := 12
const ID_SETUP := 13
const ID_LOCAL_HISTORY := 14
const ID_FAST_STATUS := 15
const FAST_STATUS_HELP := "Git's fsmonitor daemon and untracked cache: git status asks what changed instead of scanning every file. Set in this repository's git config."
const TOOLBAR_BRANCH_SETTING_KEY := "toolbar_branch"


func _enter_tree() -> void:
	# Headless editor runs (CLI checks, imports, exports) must not start Git polling, change
	# editor preferences, or take Local History snapshots alongside the interactive editor.
	if DisplayServer.get_name() == "headless" and OS.get_environment("VC_GIT_HEADLESS_EDITOR") != "1":
		return
	_activation_started = true
	Settings.migrate_legacy() # what the git add-on vc_git was made from kept
	GitCli.prepare_environment()
	EditorOpen.remember_scene_state() # open scenes as loaded, for a git operation that changes them
	watcher = RepoWatcherScript.new()
	add_child(watcher)
	reminders = RemindersScript.new()
	add_child(reminders)
	local_history = LocalHistoryRecorderScript.new()
	add_child(local_history)

	tools_menu = PopupMenu.new()
	tools_menu.auto_translate_mode = Node.AUTO_TRANSLATE_MODE_DISABLED # it goes into the editor's Tools menu, which would translate it (see vc_git_dock.gd)
	tools_menu.add_item("Settings…", ID_SETTINGS)
	tools_menu.add_item("Check Setup…", ID_SETUP)
	tools_menu.add_item("Local History of the Project…", ID_LOCAL_HISTORY)
	tools_menu.add_separator()
	tools_menu.add_check_item("Faster Status for Large Projects", ID_FAST_STATUS)
	tools_menu.set_item_tooltip(tools_menu.get_item_index(ID_FAST_STATUS), FAST_STATUS_HELP)
	tools_menu.about_to_popup.connect(_update_tools_menu)
	tools_menu.id_pressed.connect(_on_tools_menu_id_pressed)
	Settings.notifier().changed.connect(_on_setting_changed)
	add_tool_submenu_item("Git", tools_menu)
	_apply_auto_reload_setting()
	_apply_auto_save_setting()
	_add_commands()

	dock_instance = VcGitDockScene.instantiate()
	dock_instance.plugin = self
	history_dock_instance = VcGitHistoryDockScene.instantiate()
	_build_git_screen()
	var toolbar_repo: RefCounted = RepoOpener.open_current_project_repo()["repo"] # its own, like the panels': watcher.repo is set only once the plugin is ready
	if toolbar_repo != null:
		branch_toolbar = BranchToolbarScript.new()
		branch_toolbar.visible = Settings.get_value(TOOLBAR_BRANCH_SETTING_KEY, true)
		add_control_to_container(CONTAINER_TOOLBAR, branch_toolbar)
		branch_toolbar.set_repo(toolbar_repo)
		branch_toolbar.changes_requested.connect(_show_changes.bind(""))

	diff_gutter = DiffGutterScript.new()
	add_child(diff_gutter)
	diff_gutter.enable(self)
	diff_gutter.navigate_shortcuts = [commands.shortcut("previous_change"), commands.shortcut("next_change")]

	diff_gutter.change_clicked.connect(_on_gutter_change_clicked)

	blame_gutter = BlameGutterScript.new()
	add_child(blame_gutter)
	blame_gutter.set_enabled(Settings.get_value(BLAME_SETTING_KEY, false))
	blame_gutter.commit_clicked.connect(_show_commit)
	blame_gutter.toggle_requested.connect(_set_blame_enabled)

	script_menu = ScriptMenuScript.new()
	script_menu.diff_gutter = diff_gutter
	script_menu.blame_gutter = blame_gutter
	script_menu.show_commit = _show_commit
	script_menu.show_file_history = _show_file_history
	add_context_menu_plugin(EditorContextMenuPlugin.CONTEXT_SLOT_SCRIPT_EDITOR_CODE, script_menu)
	diff_gutter.text_tab_refreshed.connect(script_menu.hook_text_editor)
	dock_instance.file_history_requested.connect(_show_file_history)

	filesystem_colors = FilesystemColorsScript.new()
	add_child(filesystem_colors)
	filesystem_menu = FilesystemMenuScript.new()
	filesystem_menu.colors = filesystem_colors
	filesystem_menu.dock = dock_instance
	filesystem_menu.show_file_history = _show_file_history
	filesystem_menu.show_change = func(path: String) -> void:
		_show_changes("")
		dock_instance.reveal_change(path, 1)
	add_context_menu_plugin(EditorContextMenuPlugin.CONTEXT_SLOT_FILESYSTEM, filesystem_menu)
	history_dock_instance.changes_requested.connect(_show_changes)
	main_screen_changed.connect(_on_main_screen_changed)
	Dialogs.show_console = _show_console
	RemoteActions.open_setup_check = open_setup_check
	LocalHistoryWindowScript.opener = open_local_history
	head_inspector = HeadInspectorScript.new()
	head_inspector.enabled = Settings.get_value(INSPECTOR_SETTING_KEY, true)
	add_inspector_plugin(head_inspector)
	scene_dock_colors = SceneDockColorsScript.new()
	scene_dock_colors.enabled = head_inspector.enabled
	add_child(scene_dock_colors)
	scene_saved.connect(func(_path: String) -> void: scene_dock_colors.refresh.call_deferred())
	# Saves show up right away; the panels' polling still catches changes made outside the editor.
	resource_saved.connect(func(_r: Resource) -> void: _queue_poll())
	scene_saved.connect(func(_p: String) -> void: _queue_poll())
	resource_saved.connect(func(_r: Resource) -> void: local_history.on_saved())
	scene_saved.connect(func(_p: String) -> void: local_history.on_saved())
	EditorInterface.get_resource_filesystem().filesystem_changed.connect(_queue_poll)


var _poll_queued := false


## One poll per frame however many saves (Save All) or rescans triggered it.
func _queue_poll() -> void:
	if _poll_queued:
		return
	_poll_queued = true
	(func() -> void:
		_poll_queued = false
		if is_instance_valid(watcher):
			watcher.poll_now()
	).call_deferred()


func _exit_tree() -> void:
	if not _activation_started:
		return
	_activation_started = false
	# Before freeing the UI: a fetch/push still running would otherwise outlive the plugin and resume coroutines on freed panels.
	GitCli.shutdown()
	# A failed first import can abort _enter_tree() before any UI is registered.
	if commands == null:
		for service in [watcher, reminders, local_history]:
			if is_instance_valid(service): service.free()
		GitCli.restore_environment()
		return
	if Settings.notifier().changed.is_connected(_on_setting_changed):
		Settings.notifier().changed.disconnect(_on_setting_changed)
	if branch_toolbar != null:
		remove_control_from_container(CONTAINER_TOOLBAR, branch_toolbar)
		branch_toolbar.free()
	if _quick_commit != null:
		_quick_commit.free()
	if _setup_check != null:
		_setup_check.free()
	if _local_history_window != null:
		_local_history_window.free()
	RemoteActions.open_setup_check = Callable()
	LocalHistoryWindowScript.opener = Callable()
	remove_tool_menu_item("Git")
	commands.clear()
	if EditorInterface.get_resource_filesystem().filesystem_changed.is_connected(_queue_poll):
		EditorInterface.get_resource_filesystem().filesystem_changed.disconnect(_queue_poll)
	Dialogs.show_console = Callable()
	if is_instance_valid(head_inspector): remove_inspector_plugin(head_inspector)
	if is_instance_valid(scene_dock_colors): scene_dock_colors.free() # restores the dock's own colors
	if is_instance_valid(combined_dock):
		if combined_dock.get_parent() != null:
			combined_dock.get_parent().remove_child(combined_dock)
		combined_dock.free()
	if is_instance_valid(dock_instance): dock_instance.free()
	if is_instance_valid(history_dock_instance): history_dock_instance.free()

	if is_instance_valid(diff_gutter):
		if diff_gutter.has_method("disable"): diff_gutter.disable()
		diff_gutter.free()
	if is_instance_valid(script_menu):
		script_menu.unhook_text_editors()
		remove_context_menu_plugin(script_menu)
	script_menu = null
	if is_instance_valid(filesystem_menu): remove_context_menu_plugin(filesystem_menu)
	filesystem_menu = null
	if is_instance_valid(filesystem_colors): filesystem_colors.free()
	if is_instance_valid(blame_gutter):
		if blame_gutter.has_method("disable"): blame_gutter.disable()
		blame_gutter.free()
	if is_instance_valid(reminders): reminders.free()
	if is_instance_valid(local_history): local_history.free()
	if is_instance_valid(watcher): watcher.free()

	GitCli.restore_environment()


func _shortcut_input(event: InputEvent) -> void:
	if commands != null and commands.handle(event):
		get_viewport().set_input_as_handled()


## Command Palette entries (Ctrl/Cmd+Shift+P, "Git: …"), with shortcuts in Editor Settings > Shortcuts > vc_git.
func _add_commands() -> void:
	commands = EditorCommands.new()
	commands.add("toggle_dock", "Toggle Git Tab", _toggle_git_screen, [EditorCommands.alt_key(KEY_G)])
	commands.add("show_changes", "Show File Status", _show_changes.bind(""), [EditorCommands.command_digit(KEY_1)])
	commands.add("show_history", "Show History", _reveal_history, [EditorCommands.command_digit(KEY_2)])
	commands.add("show_branches", "Show Branches", _reveal_branches, [EditorCommands.command_digit(KEY_3)])
	commands.add("show_console", "Show Console", _show_console, [EditorCommands.command_digit(KEY_4)])
	commands.add("commit", "Commit…", open_quick_commit, [EditorCommands.alt_key(KEY_C)])
	# Not just "Fetch", "Pull", "Push": the editor's catalog has those, and Editor Settings > Shortcuts would show them translated.
	commands.add("sync", "Sync with Remote", func() -> void: dock_instance.press_sync("sync"))
	commands.add("fetch", "Fetch All Remotes", func() -> void: dock_instance.press_sync("fetch"))
	commands.add("pull", "Pull Current Branch", func() -> void: dock_instance.press_sync("pull"))
	commands.add("push", "Push Current Branch", func() -> void: dock_instance.press_sync("push"))
	commands.add("file_history", "History of the Open Scene or Script", _show_open_file_history)
	commands.add("local_history", "Local History of the Open Scene or Script", func() -> void:
		var path := _open_file_path()
		if not path.is_empty():
			open_local_history(path)
	)
	commands.add("local_history_project", "Local History of the Project", open_local_history.bind("", true))
	commands.add("toggle_blame", "Toggle Blame", func() -> void: _set_blame_enabled(not Settings.get_value(BLAME_SETTING_KEY, false)))
	# Alt+F5 like VS Code; macOS uses F5 with modifiers for VoiceOver, so there Ctrl+Cmd+Down/Up, which neither the system nor the editor binds.
	var mac := OS.get_name() == "macOS"
	commands.add("next_change", "Next Change", func() -> void: diff_gutter.goto_change(1), [EditorCommands.ctrl_cmd_key(KEY_DOWN) if mac else EditorCommands.alt_key(KEY_F5)])
	commands.add("previous_change", "Previous Change", func() -> void: diff_gutter.goto_change(-1), [EditorCommands.ctrl_cmd_key(KEY_UP) if mac else EditorCommands.alt_key(KEY_F5, true)])
	commands.add("settings", "Settings…", open_settings)
	commands.add("check_setup", "Check Setup…", open_setup_check)
	commands.add("toggle_fast_status", "Toggle Faster Status for Large Projects", func() -> void: _set_fast_status(not _fast_status_state()["on"]))
	commands.add("switch_branch", "Switch Branch…", func() -> void:
		if branch_toolbar != null:
			branch_toolbar.open_switcher()
	)



## The current scene's history, or the current script's while the Script screen is open.
func _show_open_file_history() -> void:
	var path := _open_file_path()
	if not path.is_empty():
		_show_file_history(path)


## Repo-relative path of the current script while the Script screen is open, else of the current scene; "" when there's none.
func _open_file_path() -> String:
	var path := ""
	if _last_main_screen == "Script":
		path = EditorOpen.current_tab_path()
	elif EditorInterface.get_edited_scene_root() != null:
		path = EditorInterface.get_edited_scene_root().scene_file_path
	var repo: RefCounted = watcher.repo
	if path.is_empty() or repo == null:
		return ""
	var abs_path := ProjectSettings.globalize_path(path)
	var root: String = repo.get_repo_root() + "/"
	return abs_path.trim_prefix(root) if abs_path.begins_with(root) else ""


func _has_main_screen() -> bool:
	return true


func _get_plugin_name() -> String:
	return GIT_SCREEN_NAME


func _get_plugin_icon() -> Texture2D:
	var theme := EditorInterface.get_editor_theme()
	return theme.get_icon(&"VcsBranches", &"EditorIcons") if theme.has_icon(&"VcsBranches", &"EditorIcons") else null


func _make_visible(visible: bool) -> void:
	if is_instance_valid(combined_dock):
		combined_dock.visible = visible


func _on_main_screen_changed(screen_name: String) -> void:
	if screen_name != GIT_SCREEN_NAME:
		_last_main_screen = screen_name


## Alt+G: to the Git tab, or back to the screen open before it.
func _toggle_git_screen() -> void:
	if is_instance_valid(combined_dock) and combined_dock.visible:
		EditorInterface.set_main_screen_editor(_last_main_screen)
	else:
		EditorInterface.set_main_screen_editor(GIT_SCREEN_NAME)


func _show_commit(oid: String) -> void:
	_reveal_history()
	history_dock_instance.show_commit(oid)


func _show_file_history(path: String) -> void:
	_reveal_history()
	history_dock_instance.show_file_history(path)


## Opens the Git tab on view ("changes", "history", "branches", "console").
func _reveal_view(view: String) -> void:
	EditorInterface.set_main_screen_editor(GIT_SCREEN_NAME)
	combined_dock.show_view(view)


func _reveal_history() -> void:
	_reveal_view("history")


func _show_console() -> void:
	_reveal_view("console")


func _reveal_branches() -> void:
	_reveal_view("branches")


func _reveal_changes() -> void:
	_reveal_view("changes")


## The script editor's context menu and the palette; _on_setting_changed() turns the gutter on or off.
func _set_blame_enabled(enabled: bool) -> void:
	Settings.set_value(BLAME_SETTING_KEY, enabled)


## Brings Changes to front and runs action there (see changes_panel.gd's run_action()).
func _show_changes(action: String) -> void:
	_reveal_changes()
	dock_instance.show_changes(action)


func _on_gutter_change_clicked(rel_path: String, line: int) -> void:
	_reveal_changes()
	dock_instance.reveal_change(rel_path, line)


func _on_tools_menu_id_pressed(id: int) -> void:
	match id:
		ID_SETTINGS:
			open_settings()
		ID_SETUP:
			open_setup_check()
		ID_LOCAL_HISTORY:
			open_local_history("", true)
		ID_FAST_STATUS:
			_set_fast_status(not _fast_status_state()["on"])


## Applies a setting changed anywhere: Editor Settings, the script editor's menu, the palette, a dialog's "Don't ask again".
func _on_setting_changed(key: String) -> void:
	match key:
		AUTO_RELOAD_SETTING_KEY:
			_apply_auto_reload_setting()
		AUTO_SAVE_SETTING_KEY, AUTO_SAVE_SECONDS_KEY:
			_apply_auto_save_setting()
		BLAME_SETTING_KEY:
			blame_gutter.set_enabled(Settings.get_value(BLAME_SETTING_KEY, false))
		VcGitDockScript.AUTO_FETCH_SETTING_KEY, VcGitDockScript.AUTO_FETCH_MINUTES_KEY:
			dock_instance.apply_auto_fetch_setting()
		INSPECTOR_SETTING_KEY:
			head_inspector.enabled = Settings.get_value(INSPECTOR_SETTING_KEY, true)
			scene_dock_colors.enabled = head_inspector.enabled
			var inspected := EditorInterface.get_inspector().get_edited_object()
			if inspected != null:
				EditorInterface.inspect_object(inspected) # rebuilt with or without the rows
		TOOLBAR_BRANCH_SETTING_KEY:
			if branch_toolbar != null:
				branch_toolbar.visible = Settings.get_value(TOOLBAR_BRANCH_SETTING_KEY, true)


## Commit from anywhere without opening the dock ("Git: Commit…", Alt+C).
func open_quick_commit() -> void:
	if not dock_instance.has_repo():
		return
	if _quick_commit == null:
		_quick_commit = QuickCommitScript.new(dock_instance.changes_panel())
		EditorInterface.get_base_control().add_child(_quick_commit) # not in a dock: switching the layout frees those
	_quick_commit.open()


## Is git set up for this project: name, remote, login, ignores, line endings, LFS ("Git: Check Setup…", Tools > Git, "Help Me Log In" after a refused login).
func open_setup_check() -> void:
	if watcher.repo == null:
		return
	if _setup_check == null:
		_setup_check = SetupCheckScript.new(watcher.repo)
		EditorInterface.get_base_control().add_child(_setup_check) # not in a dock: switching the layout frees those
	_setup_check.open()


## Earlier versions of a file, or of a folder's files ("" = the whole project), from Local History.
func open_local_history(path: String, folder := false) -> void:
	if watcher.repo == null:
		return
	if _local_history_window == null:
		_local_history_window = LocalHistoryWindowScript.new(watcher.repo)
		EditorInterface.get_base_control().add_child(_local_history_window) # not in a dock: switching the layout frees those
	_local_history_window.open(path, folder)


## Editor Settings at Version Control > Vc Git, from the Tools menu, the docks' ⋮ menus and the palette.
## The editor opens it: modules/vc_git sets the callable before this script runs.
func open_settings() -> void:
	if has_meta(&"vc_git_open_settings"):
		(get_meta(&"vc_git_open_settings") as Callable).call()


## The Tools menu's check shows the repository's faster status (it can change outside vc_git too).
func _update_tools_menu() -> void:
	var state := _fast_status_state()
	var index := tools_menu.get_item_index(ID_FAST_STATUS)
	tools_menu.set_item_checked(index, state["on"])
	tools_menu.set_item_disabled(index, not state["available"])


## {"on", "available"} for this repository's git config (it can change outside vc_git too).
func _fast_status_state() -> Dictionary:
	var repo: RefCounted = watcher.repo
	var on: bool = repo != null and repo.is_fast_status_on()
	return {"on": on, "available": repo != null and (on or GitCliRepo.fast_status_supported())}


func _set_fast_status(on: bool) -> void:
	if watcher.repo == null:
		return
	var result: Dictionary = watcher.repo.set_fast_status(on)
	if not result["ok"]:
		EditorInterface.get_editor_toaster().push_toast("Git: " + result["error"], EditorToaster.SEVERITY_WARNING)


## Mirrors our own per-user vc_git preference onto Godot's own (editor-wide) auto-reload setting — unchecking it restores the normal "reload externally modified file?" confirmation.
func _apply_auto_reload_setting() -> void:
	EditorInterface.get_editor_settings().set_setting(AUTO_RELOAD_EDITOR_SETTING, Settings.get_value(AUTO_RELOAD_SETTING_KEY, true))


## Mirrors our own per-user vc_git preference onto Godot's own (editor-wide) autosave interval.
func _apply_auto_save_setting() -> void:
	var editor_settings := EditorInterface.get_editor_settings()
	var original: Variant = Settings.get_value(AUTO_SAVE_ORIGINAL_KEY, null)
	if Settings.get_value(AUTO_SAVE_SETTING_KEY, false):
		if original == null:
			var current: Variant = editor_settings.get_setting(AUTO_SAVE_EDITOR_SETTING)
			Settings.set_value(AUTO_SAVE_ORIGINAL_KEY, 0 if current == AUTO_SAVE_INTERVAL_SECS else current) # 3 here means an older version set it
		editor_settings.set_setting(AUTO_SAVE_EDITOR_SETTING, maxi(1, int(Settings.get_value(AUTO_SAVE_SECONDS_KEY, AUTO_SAVE_INTERVAL_SECS))))
	elif original != null:
		editor_settings.set_setting(AUTO_SAVE_EDITOR_SETTING, original)
		Settings.set_value(AUTO_SAVE_ORIGINAL_KEY, null)
	elif editor_settings.get_setting(AUTO_SAVE_EDITOR_SETTING) == AUTO_SAVE_INTERVAL_SECS:
		editor_settings.set_setting(AUTO_SAVE_EDITOR_SETTING, 0) # set by an older version that didn't remember the user's own interval


## Every panel goes into one combined view in the editor's main screen (the Git tab), hidden until the tab is picked (_make_visible()). The holder docks enter the tree just long enough to set themselves up (open the repo in _ready()).
func _build_git_screen() -> void:
	var holders: Array[Control] = [dock_instance, history_dock_instance]
	# Under the editor's base control, not this plugin: _enter_tree() runs before the plugin is ready, and children added to a node that isn't ready yet get their _ready() only later.
	var host := EditorInterface.get_base_control()
	for holder in holders:
		holder.visible = false
		host.add_child(holder)
	var panels := {}
	if dock_instance.has_repo():
		panels = dock_instance.detach_panels()
		panels.merge(history_dock_instance.detach_panels())
	for holder in holders:
		host.remove_child(holder)
	combined_dock = CombinedDockScript.new(panels)
	combined_dock.use_shortcuts(commands.shortcuts())
	combined_dock.visible = false
	combined_dock.size_flags_vertical = Control.SIZE_EXPAND_FILL
	combined_dock.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	EditorInterface.get_editor_main_screen().add_child(combined_dock)
