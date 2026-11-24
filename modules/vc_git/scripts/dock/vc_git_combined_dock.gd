## Everything in the Git tab next to 2D/3D/Script, SourceTree-style: a sidebar with the views and Branches, the chosen view beside it.
@tool
extends Control

const UiScale := preload("../util/ui_scale.gd")
const Settings := preload("../util/settings.gd")
const RepoInitView := preload("widgets/repo_init_view.gd")
const SplitMemory := preload("../util/split_memory.gd")

const VIEW_SETTING_KEY := "combined_view"
const SIDEBAR_WIDTH := 240
## Below this width (a side dock) the view buttons go in a row on top and Branches becomes a view of its own.
const WIDE_MIN_WIDTH := 720
const VIEWS := ["changes", "history", "branches", "console"]
const VIEW_TITLES := {"changes": "File Status", "history": "History", "branches": "Branches", "console": "Console"}
const VIEW_ICONS := {"changes": &"Edit", "history": &"History", "branches": &"GuiTreeArrowRight", "console": &"Terminal"}
const VIEW_TOOLTIPS := {
	"changes": "Changed files, staging and the commit box",
	"history": "Commit graph of the whole repository",
	"branches": "Local and remote branches, tags, stashes and remotes",
	"console": "Every git command the plugin ran, and a prompt for your own",
}

## Space between the toolbar, sidebar and view panels (unscaled px).
const PANEL_GAP := 4

var _panels: Dictionary = {}
## The editor's dark background between docks, showing in the gaps between the three panels.
var _background: Panel
var _sync_bar: Control
var _layout: VBoxContainer
## Sidebar | views; its separation is the gap between those two panels.
var _split: HSplitContainer
var _sidebar: VBoxContainer
## The sidebar's rounded panel (see _style_panels()).
var _sidebar_panel: PanelContainer
## The branch/Fetch/Pull/Push bar across the top, in a rounded panel of its own.
var _toolbar: PanelContainer
## The sync bar.
var _toolbar_row: HBoxContainer
var _buttons: BoxContainer
var _views: TabContainer
## _views over _commit_panel.
var _view_split: VSplitContainer
## File Status' commit message and buttons, as a panel of their own under the view; hidden on the other views.
var _commit_panel: PanelContainer
var _view_buttons := {}
## Number of changed files, right-aligned on the File Status button.
var _changes_badge: Label
var _headers: Array[Label] = []
var _wide := true
## The view shown last, besides Branches while it sits in the sidebar.
var _view := "changes"


## panels: {"changes", "branches", "history", "console"} taken from the other two docks, or {} when the project has no repository yet.
func _init(panels: Dictionary) -> void:
	name = "Git"
	auto_translate_mode = AUTO_TRANSLATE_MODE_DISABLED # English throughout, see vc_git_dock.gd
	set_anchors_and_offsets_preset(PRESET_FULL_RECT)
	if panels.is_empty():
		add_child(RepoInitView.new("This project isn't a git repository yet."))
		return
	_panels = panels

	_background = Panel.new()
	_background.set_anchors_and_offsets_preset(PRESET_FULL_RECT)
	_background.mouse_filter = MOUSE_FILTER_IGNORE
	add_child(_background)
	_layout = VBoxContainer.new()
	_layout.set_anchors_and_offsets_preset(PRESET_FULL_RECT)
	add_child(_layout)
	_sync_bar = panels["changes"].detach_sync_bar()
	_sync_bar.set_toolbar_mode(true)
	# Its ↻ (and a fetch/pull/push/checkout) reloads History too, not just File Status; History has no Refresh button of its own.
	_sync_bar.changed.connect(panels["history"].reload)
	_toolbar = PanelContainer.new()
	_toolbar_row = HBoxContainer.new()
	_toolbar.add_child(_toolbar_row)
	_sync_bar.size_flags_horizontal = SIZE_EXPAND_FILL
	_toolbar_row.add_child(_sync_bar)
	_layout.add_child(_toolbar)
	panels["branches"].set_sidebar_mode(true, _sync_bar.operation_bar)
	panels["branches"].ref_selected.connect(_on_ref_selected)

	var split := HSplitContainer.new()
	_split = split
	split.size_flags_vertical = SIZE_EXPAND_FILL
	_layout.add_child(split)
	_sidebar = VBoxContainer.new()
	_sidebar.custom_minimum_size.x = UiScale.px(SIDEBAR_WIDTH)
	_sidebar.add_theme_constant_override("separation", int(UiScale.px(2)))
	_sidebar_panel = PanelContainer.new()
	_sidebar_panel.add_child(_sidebar)
	split.add_child(_sidebar_panel)
	_views = TabContainer.new()
	_views.tabs_visible = false
	_views.size_flags_vertical = SIZE_EXPAND_FILL
	# The view over the commit message panel (File Status only), with a draggable gap between them.
	_view_split = VSplitContainer.new()
	_view_split.size_flags_horizontal = SIZE_EXPAND_FILL
	_view_split.add_child(_views)
	_commit_panel = PanelContainer.new()
	_commit_panel.add_child(panels["changes"].detach_commit_box())
	_view_split.add_child(_commit_panel)
	split.add_child(_view_split)
	SplitMemory.keep_offset(_view_split, "combined_commit_panel")
	SplitMemory.keep_offset(split, "combined_sidebar")

	_sidebar.add_child(_header("Workspace"))
	var gap := Control.new()
	gap.custom_minimum_size.y = UiScale.px(8)
	_sidebar.add_child(gap) # Branches below brings its own BRANCHES / TAGS / … headers

	_buttons = BoxContainer.new()
	_buttons.add_theme_constant_override("separation", int(UiScale.px(1)))
	_buttons.size_flags_vertical = SIZE_SHRINK_BEGIN # in the toolbar, level with the sync bar's buttons
	var group := ButtonGroup.new()
	for view: String in VIEWS:
		var button := Button.new()
		button.text = VIEW_TITLES[view]
		button.tooltip_text = VIEW_TOOLTIPS[view]
		button.toggle_mode = true
		button.button_group = group
		button.alignment = HORIZONTAL_ALIGNMENT_LEFT
		button.custom_minimum_size.y = UiScale.px(26)
		button.pressed.connect(show_view.bind(view))
		_buttons.add_child(button)
		_view_buttons[view] = button

	_changes_badge = Label.new()
	_changes_badge.set_anchors_and_offsets_preset(PRESET_RIGHT_WIDE)
	_changes_badge.offset_left = -UiScale.px(48)
	_changes_badge.offset_right = -UiScale.px(8)
	_changes_badge.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	_changes_badge.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	_changes_badge.mouse_filter = MOUSE_FILTER_IGNORE
	_changes_badge.modulate.a = 0.6
	_view_buttons["changes"].add_child(_changes_badge)
	_set_change_count(panels["changes"].change_count)
	panels["changes"].changes_counted.connect(_set_change_count)

	for view: String in VIEWS:
		panels[view].visible = true # the tab containers they came from hid all but the current one
		panels[view].size_flags_vertical = SIZE_EXPAND_FILL
		_views.add_child(panels[view])
	_view = Settings.get_value(VIEW_SETTING_KEY, "changes")
	_arrange()
	resized.connect(func() -> void:
		if (size.x >= UiScale.px(WIDE_MIN_WIDTH)) != _wide:
			_wide = not _wide
			_arrange()
	)


## Small dimmed section title, like SourceTree's WORKSPACE.
func _header(text: String) -> Label:
	var label := Label.new()
	label.text = text.to_upper()
	label.modulate.a = 0.55
	var margin := StyleBoxEmpty.new()
	margin.content_margin_left = UiScale.px(6)
	margin.content_margin_top = UiScale.px(4)
	label.add_theme_stylebox_override("normal", margin)
	_headers.append(label)
	return label


func _notification(what: int) -> void:
	if what != NOTIFICATION_THEME_CHANGED or _panels.is_empty():
		return
	for view: String in _view_buttons:
		if has_theme_icon(VIEW_ICONS[view], &"EditorIcons"):
			_view_buttons[view].icon = get_theme_icon(VIEW_ICONS[view], &"EditorIcons")
		_style_view_button(_view_buttons[view])
	_style_panels()
	var small := int(get_theme_font_size(&"font_size", &"Label") * 0.85)
	for header in _headers:
		header.add_theme_font_size_override("font_size", small)


## Toolbar, sidebar and view as three rounded panels like the editor's docks: the docks' lighter panel color on the editor's darker background in the gaps.
func _style_panels() -> void:
	var background: StyleBox = get_theme_stylebox(&"Background", &"EditorStyles") if has_theme_stylebox(&"Background", &"EditorStyles") else null
	if background != null:
		_background.add_theme_stylebox_override("panel", background)
	var gap := int(UiScale.px(PANEL_GAP))
	_layout.add_theme_constant_override("separation", gap)
	_split.add_theme_constant_override("separation", gap)
	_view_split.add_theme_constant_override("separation", gap)
	_toolbar.add_theme_stylebox_override("panel", _panel_style(4))
	_sidebar_panel.add_theme_stylebox_override("panel", _panel_style(4))
	_views.add_theme_stylebox_override("panel", _panel_style(4))
	_commit_panel.add_theme_stylebox_override("panel", _panel_style(6))
	_panels["changes"].set_split_panel_style(_panel_style(4), gap)
	_style_view_surface()


## File Status supplies its own list and diff surfaces, exposing the background between them.
func _style_view_surface() -> void:
	if _view == "changes":
		_views.add_theme_stylebox_override("panel", StyleBoxEmpty.new())
	else:
		_views.add_theme_stylebox_override("panel", _panel_style(4))


## The editor docks' panel box (TabContainer's panel: their color and corner radius), with padding of its own; rounded even when the theme's corner radius is 0.
func _panel_style(padding: float) -> StyleBox:
	var base: StyleBox = get_theme_stylebox(&"panel", &"TabContainer") if has_theme_stylebox(&"panel", &"TabContainer") else null
	var style: StyleBoxFlat
	if base is StyleBoxFlat:
		style = base.duplicate()
		style.set_border_width_all(0)
	else:
		style = StyleBoxFlat.new()
		style.bg_color = get_theme_color(&"base_color", &"Editor") if has_theme_color(&"base_color", &"Editor") else Color(0.2, 0.2, 0.2)
	# Docks square their top corners under the tab bar; these panels have none, so all four take the bottom radius.
	var radius := style.corner_radius_bottom_left
	style.set_corner_radius_all(radius if radius > 0 else int(UiScale.px(4)))
	style.set_content_margin_all(UiScale.px(padding))
	return style


## Flat rows with a hover tint and the current view marked by an accent bar and background, instead of the stock button look.
func _style_view_button(button: Button) -> void:
	var accent := get_theme_color(&"accent_color", &"Editor")
	var font_color := get_theme_color(&"font_color", &"Label")
	var normal := StyleBoxFlat.new()
	normal.bg_color = Color(0, 0, 0, 0)
	normal.content_margin_left = UiScale.px(10)
	normal.content_margin_right = UiScale.px(8)
	normal.set_corner_radius_all(int(UiScale.px(3)))
	var hover := normal.duplicate() as StyleBoxFlat
	hover.bg_color = Color(font_color, 0.07)
	var pressed := normal.duplicate() as StyleBoxFlat
	pressed.bg_color = Color(accent, 0.22)
	pressed.border_color = accent
	pressed.border_width_left = int(UiScale.px(3))
	pressed.content_margin_left = UiScale.px(7) # keeps the text in place next to the bar
	button.add_theme_stylebox_override("normal", normal)
	button.add_theme_stylebox_override("hover", hover)
	button.add_theme_stylebox_override("pressed", pressed)
	button.add_theme_stylebox_override("hover_pressed", pressed)
	button.add_theme_stylebox_override("focus", StyleBoxEmpty.new())
	button.add_theme_color_override("font_color", Color(font_color, 0.75))
	button.add_theme_color_override("font_hover_color", font_color)
	button.add_theme_color_override("font_pressed_color", font_color)
	button.add_theme_color_override("font_hover_pressed_color", font_color)


func _set_change_count(count: int) -> void:
	_changes_badge.text = str(count) if count > 0 else ""


## The sidebar is always there, except in a pane too narrow for it.
func _has_sidebar() -> bool:
	return _wide


## Wide: buttons and Branches in a sidebar left of the views. Narrow: buttons in a row above, and Branches a view like the rest.
func _arrange() -> void:
	var sidebar := _has_sidebar()
	var branches: Control = _panels["branches"]
	if _buttons.get_parent() != null:
		_buttons.get_parent().remove_child(_buttons)
	branches.get_parent().remove_child(branches)
	_buttons.vertical = sidebar
	_view_buttons["branches"].visible = not sidebar
	_changes_badge.visible = sidebar # would sit on top of the title in a row
	for button: Button in _view_buttons.values():
		button.size_flags_horizontal = SIZE_FILL if _wide else SIZE_EXPAND_FILL
		button.clip_text = not _wide # four titles in a row would otherwise hold the side dock wider than it is
	if sidebar:
		_sidebar.add_child(_buttons)
		_sidebar.move_child(_buttons, 1)
		_sidebar.add_child(branches)
		branches.visible = true # a TabContainer hid it as a background tab
	else:
		_views.add_child(branches)
		_layout.add_child(_buttons)
		_layout.move_child(_buttons, _toolbar.get_index() + 1)
	_sidebar_panel.visible = sidebar
	show_view("changes" if sidebar and _view == "branches" else _view)


## Shortcut resources by command key (see plugin.gd), shown in the buttons' tooltips; a button also runs its shortcut itself while visible.
func use_shortcuts(shortcuts: Dictionary) -> void:
	if _panels.is_empty():
		return
	for view: String in _view_buttons:
		_view_buttons[view].shortcut = shortcuts.get("show_" + view)


## Brings "changes", "history", "branches" or "console" to front.
func show_view(view: String) -> void:
	if _views == null or not view in VIEWS:
		return
	if view == "branches" and _has_sidebar():
		return # always visible in the sidebar
	_view = view
	_style_view_surface()
	_views.current_tab = _panels[view].get_index()
	_commit_panel.visible = view == "changes"
	_view_buttons[view].button_pressed = true
	Settings.set_value(VIEW_SETTING_KEY, view)


## Hands every panel back (see _init), restoring the parts it rearranged.
func detach_panels() -> Dictionary:
	if _panels.is_empty():
		return {}
	_panels["branches"].ref_selected.disconnect(_on_ref_selected)
	_panels["branches"].set_sidebar_mode(false)
	_panels["changes"].changes_counted.disconnect(_set_change_count)
	_sync_bar.set_toolbar_mode(false)
	_panels["changes"].reattach_sync_bar()
	for panel: Control in _panels.values():
		if panel.get_parent() != null:
			panel.get_parent().remove_child(panel)
	var panels := _panels
	_panels = {}
	return panels


## Selecting a branch, tag or stash in the sidebar shows its commit in History, like SourceTree.
func _on_ref_selected(ref: String) -> void:
	if not _has_sidebar() or _view != "history":
		return
	var oid: String = _panels["history"]._repo.resolve_commit(ref)
	if not oid.is_empty():
		_panels["history"].show_commit(oid)
