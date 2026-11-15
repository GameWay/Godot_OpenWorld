@tool
## Child head_inspector.gd puts into the property's own editor: it colours the name with the warning colour while the value differs from the last commit. A click on that name opens the value at HEAD and Revert; once the value matches HEAD again the name is plain.
## A Node, not a Control: EditorProperty lays out its Control children as the value editor.
extends Node

## {"value"} or {"path"} (an ExtResource), and "text": see head_inspector.gd's head_value().
var _head: Dictionary
var _property: StringName
var _object: Object
## The Inspector's editor of _property, this node's parent.
var _target: EditorProperty
var _changed := false
var _popup: PopupPanel


## Puts the marker into editor, the editor of property of object.
static func attach(editor: EditorProperty, object: Object, property: StringName, head: Dictionary) -> void:
	var marker := new()
	marker._object = object
	marker._property = property
	marker._head = head
	marker._target = editor
	marker.auto_translate_mode = Node.AUTO_TRANSLATE_MODE_DISABLED # the Inspector would translate Revert (see vc_git_dock.gd)
	editor.add_child(marker)
	editor.gui_input.connect(marker._on_target_input)


func _ready() -> void:
	_refresh()


## The value only changes through the Inspector or undo; checking each frame is a dictionary lookup and a compare.
func _process(_delta: float) -> void:
	if is_instance_valid(_target):
		_refresh()


func _refresh() -> void:
	var changed := not same(_object.get(_property), _head)
	if changed == _changed:
		return
	_changed = changed
	if changed:
		_target.add_theme_color_override(&"property_color", EditorInterface.get_editor_theme().get_color(&"warning_color", &"Editor"))
	else:
		_target.remove_theme_color_override(&"property_color")
		if is_instance_valid(_popup):
			_popup.hide()
	_target.queue_redraw()


func _on_target_input(event: InputEvent) -> void:
	var click := event as InputEventMouseButton
	if not _changed or click == null or not click.pressed or click.button_index != MOUSE_BUTTON_LEFT:
		return
	if click.position.x > _label_width():
		return
	_open_popup(_target.get_screen_position() + click.position)
	_target.accept_event()


## The name column ends where the property's value editor starts.
func _label_width() -> float:
	for child in _target.get_children():
		var control := child as Control
		if control != null and control.visible and control.position.x > 0.0:
			return control.position.x
	return _target.size.x * 0.5


func _open_popup(at: Vector2) -> void:
	if not is_instance_valid(_popup):
		_popup = PopupPanel.new()
		_popup.auto_translate_mode = AUTO_TRANSLATE_MODE_DISABLED
		var box := VBoxContainer.new()
		var caption := Label.new()
		caption.text = "Last commit"
		caption.add_theme_color_override(&"font_color", EditorInterface.get_editor_theme().get_color(&"warning_color", &"Editor"))
		box.add_child(caption)
		var value := Label.new()
		value.text = _head["text"]
		value.autowrap_mode = TextServer.AUTOWRAP_ARBITRARY
		value.custom_minimum_size.x = 220 * EditorInterface.get_editor_scale()
		box.add_child(value)
		var revert := Button.new()
		revert.text = "Revert"
		revert.tooltip_text = "Set it back to the value in the last commit"
		revert.pressed.connect(_revert)
		box.add_child(revert)
		_popup.add_child(box)
		add_child(_popup)
	_popup.reset_size()
	_popup.popup(Rect2i(Vector2i(at), Vector2i.ZERO))


func _revert() -> void:
	_popup.hide()
	if is_instance_valid(_target):
		_target.emit_changed(_property, load(_head["path"]) if _head.has("path") else _head["value"])


## Whether current matches a HEAD value from head_value(): floats and vectors approximately, resources by file.
static func same(current: Variant, head: Dictionary) -> bool:
	if head.has("path"):
		return current is Resource and (current as Resource).resource_path == head["path"]
	var old: Variant = head["value"]
	if typeof(current) in [TYPE_INT, TYPE_FLOAT] and typeof(old) in [TYPE_INT, TYPE_FLOAT]:
		return is_equal_approx(float(current), float(old))
	if typeof(current) != typeof(old):
		return false
	if typeof(current) in [TYPE_VECTOR2, TYPE_VECTOR3, TYPE_VECTOR4, TYPE_COLOR, TYPE_RECT2, TYPE_TRANSFORM2D, TYPE_TRANSFORM3D, TYPE_QUATERNION, TYPE_BASIS, TYPE_PLANE, TYPE_AABB]:
		return current.is_equal_approx(old)
	if typeof(current) == TYPE_OBJECT:
		return current == old or (current is Resource and old is Resource and not (current as Resource).resource_path.is_empty() and (current as Resource).resource_path == (old as Resource).resource_path)
	return current == old
