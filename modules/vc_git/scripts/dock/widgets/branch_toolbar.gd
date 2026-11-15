## "File status: N" in the editor's main toolbar: how many files have uncommitted changes, seen whichever screen is open; a click opens Changes on the Git tab. "Git: Switch Branch…" still opens the filterable branch list from here.
@tool
extends Button

const SyncBar := preload("sync_bar.gd")
const GitCliRepo := preload("../../util/git_cli_repo.gd")
const RepoWatcher := preload("../../util/repo_watcher.gd")
const UiScale := preload("../../util/ui_scale.gd")

## After every refresh, for the dock title's color.
signal synced(ahead: int, behind: int)
## The button was clicked: show the changed files.
signal changes_requested

var _repo: RefCounted
## Refs and config the button was last drawn for; a poll that changes neither leaves it alone.
var _drawn_for := ""
var _popup: PopupPanel
var _filter: LineEdit
var _list: ItemList
## Per list item: {"kind": "local" | "remote" | "new", "name", "current"}.
var _items: Array = []
## Recently checked out branches, newest first, read when the list opens.
var _recent := PackedStringArray()
## Files with uncommitted changes in the last poll, -1 before the first one.
var _changed_count := -1
## Branch lines of the tooltip, from the last refresh().
var _branch_lines: Array = []


func _init() -> void:
	name = "VcGitBranch"
	flat = true
	focus_mode = FOCUS_NONE
	auto_translate_mode = AUTO_TRANSLATE_MODE_DISABLED # English throughout, see vc_git_dock.gd
	pressed.connect(func() -> void: changes_requested.emit())
	_build_popup()


## Follows the watcher's polls from now on.
func set_repo(repo: RefCounted) -> void:
	_repo = repo
	refresh()
	RepoWatcher.watch(self, func(snapshot: Dictionary) -> void:
		var count: int = _repo.parse_status(snapshot["status"]).size()
		if count != _changed_count:
			_changed_count = count
			_show_status()
		var key: String = snapshot["refs"] + snapshot["config"]
		if key != _drawn_for:
			_drawn_for = key
			refresh()
	)


## Branch, ahead and behind: a few rev-parse calls.
func refresh() -> void:
	if _repo == null:
		return
	var s: Dictionary = _repo.get_sync_status()
	var branch: String = s["branch"]
	var lines: Array = []
	if branch.is_empty():
		lines.append("HEAD is detached: not on a branch.")
	elif s["upstream"].is_empty():
		lines.append("%s, not published yet." % branch)
	else:
		lines.append("%s → %s" % [branch, s["upstream"]])
		lines.append("%d to push, %d to pull." % [s["ahead"], s["behind"]] if s["ahead"] > 0 or s["behind"] > 0 else "Up to date with the last fetch.")
	_branch_lines = lines
	_show_status()
	synced.emit(s["ahead"], s["behind"])


## The count on the button; the branch and sync state in the tooltip.
func _show_status() -> void:
	text = "File status: %s" % (str(_changed_count) if _changed_count >= 0 else "…")
	var lines: Array = []
	if _changed_count == 0:
		lines.append("No uncommitted changes.")
	elif _changed_count > 0:
		lines.append("%d file%s with uncommitted changes (staged, unstaged or untracked)." % [_changed_count, "" if _changed_count == 1 else "s"])
	lines.append_array(_branch_lines)
	lines.append("Click to open Changes on the Git tab.")
	tooltip_text = "\n".join(lines)


## The branch list under the button, or centered while the button is hidden.
func open_switcher() -> void:
	if _repo == null:
		return
	_filter.text = ""
	_recent = _repo.recent_checkouts()
	_fill()
	var popup_size := UiScale.size_i(340, 360)
	if is_visible_in_tree():
		_popup.popup(Rect2i(Vector2i(get_screen_position() + Vector2(0, size.y)), popup_size))
	else:
		_popup.popup_centered(popup_size)
	_filter.grab_focus()


func _build_popup() -> void:
	_popup = PopupPanel.new()
	var box := VBoxContainer.new()
	_popup.add_child(box)
	_filter = LineEdit.new()
	_filter.placeholder_text = "Switch to branch…"
	_filter.clear_button_enabled = true
	_filter.text_changed.connect(func(_t: String) -> void: _fill())
	_filter.text_submitted.connect(func(_t: String) -> void:
		var selected := _list.get_selected_items()
		if not selected.is_empty():
			_activate(selected[0])
	)
	_filter.gui_input.connect(_on_filter_gui_input)
	box.add_child(_filter)
	_list = ItemList.new()
	_list.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_list.item_clicked.connect(func(index: int, _at: Vector2, button: int) -> void:
		if button == MOUSE_BUTTON_LEFT:
			_activate(index)
	)
	_list.item_activated.connect(_activate) # Enter while the list has focus
	box.add_child(_list)
	add_child(_popup)


## Up and down move through the list while typing in the filter.
func _on_filter_gui_input(event: InputEvent) -> void:
	if _list.item_count == 0 or not event.is_pressed():
		return
	var step := 1 if event.is_action("ui_down", true) else (-1 if event.is_action("ui_up", true) else 0)
	if step == 0:
		return
	var selected := _list.get_selected_items()
	_list.select(clampi((selected[0] if not selected.is_empty() else -1) + step, 0, _list.item_count - 1))
	_list.ensure_current_is_visible()
	_filter.accept_event()


## Local branches (the current one, then the recently used ones), then remote ones without a local branch of that name, then New Branch…; only those containing the filter.
func _fill() -> void:
	_list.clear()
	_items.clear()
	var query := _filter.text.strip_edges().to_lower()
	var branches: Array = GitCliRepo.recent_first(_repo.list_branches(false), _recent)
	var local_names := {}
	for b: Dictionary in branches:
		if not b["is_remote"]:
			local_names[b["name"]] = true
	var branch_icon: Texture2D = get_theme_icon(&"VcsBranches", &"EditorIcons") if has_theme_icon(&"VcsBranches", &"EditorIcons") else null
	for remote: bool in [false, true]:
		for b: Dictionary in branches:
			var branch: String = b["name"]
			if b["is_remote"] != remote or not (query.is_empty() or branch.to_lower().contains(query)): # contains("") is false
				continue
			if remote and local_names.has(branch.substr(branch.find("/") + 1)):
				continue # switching to it would switch to the local branch
			var label := branch
			if b["ahead"] > 0 or b["behind"] > 0:
				label += "  ↑%d ↓%d" % [b["ahead"], b["behind"]]
			if b["is_head"]:
				label += "  (current)"
			var index := _list.add_item(label, branch_icon)
			_list.set_item_tooltip(index, "%s\n%s · %s%s" % [branch, b["summary"], b["date"], "\nSwitching creates a local branch that tracks it." if remote else ""])
			if b["is_head"]:
				_list.set_item_custom_fg_color(index, Color(1, 1, 1, 0.45))
			_items.append({"kind": "remote" if remote else "local", "name": branch, "current": b["is_head"]})
	var typed := _filter.text.strip_edges()
	_list.add_item("New Branch \"%s\"…" % typed if not typed.is_empty() else "New Branch…", get_theme_icon(&"Add", &"EditorIcons") if has_theme_icon(&"Add", &"EditorIcons") else null)
	_items.append({"kind": "new", "name": typed, "current": false})
	# The first branch to switch to, so Enter after a few letters switches.
	for i in _items.size():
		if not _items[i]["current"]:
			_list.select(i)
			break


func _activate(index: int) -> void:
	if index < 0 or index >= _items.size():
		return
	var item: Dictionary = _items[index]
	_popup.hide()
	if item["current"]:
		return
	var changed: bool
	if item["kind"] == "new":
		changed = await SyncBar.create_branch_dialog(self, _repo, "HEAD", item["name"])
	else:
		changed = await SyncBar.switch_branch(self, _repo, item["name"], item["kind"] == "remote")
	if changed:
		refresh()
		if is_instance_valid(RepoWatcher.instance):
			RepoWatcher.instance.poll_now() # the panels follow on the poll
