@tool
## Child head_inspector.gd puts into the property's own editor: while the value differs from the last commit, the name turns the warning colour and so does the property's revert icon, which then offers Reset (the default) and Revert (the value at HEAD); once the value matches HEAD again both are plain.
## A Node, not a Control: EditorProperty lays out its Control children as the value editor.
extends Node

## {"value"} or {"path"} (an ExtResource), and "text": see head_inspector.gd's head_value().
var _head: Dictionary
var _property: StringName
var _object: Object
## The Inspector's editor of _property, this node's parent.
var _target: EditorProperty
var _changed := false


## Puts the marker into editor, the editor of property of object.
static func attach(editor: EditorProperty, object: Object, property: StringName, head: Dictionary) -> void:
	var marker := new()
	marker._object = object
	marker._property = property
	marker._head = head
	marker._target = editor
	editor.add_child(marker)


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
		var color := EditorInterface.get_editor_theme().get_color(&"warning_color", &"Editor")
		_target.add_theme_color_override(&"property_color", color)
		# The revert icon (OEngine EditorProperty) offers the value at HEAD next to the default.
		var head_value: Variant = load(_head["path"]) if _head.has("path") else _head["value"]
		_target.set_alt_revert(head_value, _head["text"], color)
	else:
		_target.remove_theme_color_override(&"property_color")
		_target.clear_alt_revert()
	_target.queue_redraw()


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
