/**************************************************************************/
/*  batch_renamer_editor_plugin.cpp                                       */
/*  OEngine: batch rename of FileSystem items.                            */
/**************************************************************************/

#include "batch_renamer_editor_plugin.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "editor/docks/filesystem_dock.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/check_box.h"
#include "scene/gui/grid_container.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/option_button.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/separator.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/split_container.h"
#include "scene/gui/tree.h"

namespace {

LineEdit *make_line_edit(Control *p_parent, const String &p_placeholder) {
	LineEdit *edit = memnew(LineEdit);
	edit->set_placeholder(p_placeholder);
	edit->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	p_parent->add_child(edit);
	return edit;
}

Label *make_label(Control *p_parent, const String &p_text) {
	Label *label = memnew(Label);
	label->set_text(p_text);
	p_parent->add_child(label);
	return label;
}

SpinBox *make_spin_box(Control *p_parent, double p_min, double p_max, double p_value) {
	SpinBox *spin = memnew(SpinBox);
	spin->set_min(p_min);
	spin->set_max(p_max);
	spin->set_value(p_value);
	spin->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	p_parent->add_child(spin);
	return spin;
}

} // namespace

void BatchRenameDialog::_add_section_title(Control *p_parent, const String &p_title) {
	p_parent->add_child(memnew(HSeparator));
	Label *header = make_label(p_parent, p_title);
	header->set_theme_type_variation("HeaderSmall");
}

BatchRenameDialog::BatchRenameDialog() {
	set_title(TTR("Batch Rename"));
	set_ok_button_text(TTR("Apply"));
	add_button(TTR("Reset"), false, "reset");
	connect("custom_action", callable_mp(this, &BatchRenameDialog::_reset_rules).unbind(1));
	connect(SceneStringName(confirmed), callable_mp(this, &BatchRenameDialog::_apply));

	HSplitContainer *split = memnew(HSplitContainer);
	add_child(split);

	ScrollContainer *scroll = memnew(ScrollContainer);
	scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
	scroll->set_custom_minimum_size(Size2(360, 0) * EDSCALE);
	split->add_child(scroll);

	VBoxContainer *rules_box = memnew(VBoxContainer);
	rules_box->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	scroll->add_child(rules_box);

	const Callable changed = callable_mp(this, &BatchRenameDialog::_update_preview);

	// Selection.
	include_contents = memnew(CheckBox);
	include_contents->set_text(TTR("Include folder contents"));
	include_contents->set_tooltip_text(TTR("Also rename every file and subfolder inside the selected folders."));
	include_contents->connect(SceneStringName(toggled), callable_mp(this, &BatchRenameDialog::_on_selection_rule_changed).unbind(1));
	rules_box->add_child(include_contents);

	// Rename / search and replace.
	_add_section_title(rules_box, TTR("Rename"));
	HBoxContainer *search_options = memnew(HBoxContainer);
	rules_box->add_child(search_options);
	search_mode = memnew(OptionButton);
	search_mode->add_item(TTR("Plain Text"));
	search_mode->add_item(TTR("Regex"));
	search_mode->connect(SceneStringName(item_selected), changed.unbind(1));
	search_options->add_child(search_mode);
	ignore_case = memnew(CheckBox);
	ignore_case->set_text(TTR("Ignore Case"));
	ignore_case->set_pressed(true);
	ignore_case->connect(SceneStringName(toggled), changed.unbind(1));
	search_options->add_child(ignore_case);

	GridContainer *search_grid = memnew(GridContainer);
	search_grid->set_columns(2);
	rules_box->add_child(search_grid);
	make_label(search_grid, TTR("Search"));
	search = make_line_edit(search_grid, TTR("Search (optional)"));
	search->connect(SceneStringName(text_changed), changed.unbind(1));
	make_label(search_grid, TTR("Rename To"));
	replacement = make_line_edit(search_grid, TTR("Replace, or new name if Search is empty"));
	replacement->connect(SceneStringName(text_changed), changed.unbind(1));

	// Prefix and suffix removal.
	_add_section_title(rules_box, TTR("Remove Old Prefix / Suffix"));
	GridContainer *trim_grid = memnew(GridContainer);
	trim_grid->set_columns(4);
	rules_box->add_child(trim_grid);

	remove_prefix = memnew(CheckBox);
	remove_prefix->set_text(TTR("Prefix"));
	remove_prefix->connect(SceneStringName(toggled), changed.unbind(1));
	trim_grid->add_child(remove_prefix);
	prefix_mode = memnew(OptionButton);
	prefix_mode->add_item(TTR("Separator"));
	prefix_mode->add_item(TTR("First Chars"));
	prefix_mode->connect(SceneStringName(item_selected), changed.unbind(1));
	trim_grid->add_child(prefix_mode);
	prefix_separator = make_line_edit(trim_grid, "_");
	prefix_separator->set_text("_");
	prefix_separator->connect(SceneStringName(text_changed), changed.unbind(1));
	prefix_chars = make_spin_box(trim_grid, 1, 64, 1);
	prefix_chars->connect(SceneStringName(value_changed), changed.unbind(1));

	remove_suffix = memnew(CheckBox);
	remove_suffix->set_text(TTR("Suffix"));
	remove_suffix->connect(SceneStringName(toggled), changed.unbind(1));
	trim_grid->add_child(remove_suffix);
	suffix_mode = memnew(OptionButton);
	suffix_mode->add_item(TTR("Separator"));
	suffix_mode->add_item(TTR("Last Chars"));
	suffix_mode->connect(SceneStringName(item_selected), changed.unbind(1));
	trim_grid->add_child(suffix_mode);
	suffix_separator = make_line_edit(trim_grid, "_");
	suffix_separator->set_text("_");
	suffix_separator->connect(SceneStringName(text_changed), changed.unbind(1));
	suffix_chars = make_spin_box(trim_grid, 1, 64, 1);
	suffix_chars->connect(SceneStringName(value_changed), changed.unbind(1));

	// Prefix and suffix addition.
	_add_section_title(rules_box, TTR("Add Prefix / Suffix"));
	GridContainer *add_grid = memnew(GridContainer);
	add_grid->set_columns(2);
	rules_box->add_child(add_grid);
	make_label(add_grid, TTR("Add Prefix"));
	add_prefix = make_line_edit(add_grid, TTR("New Prefix"));
	add_prefix->connect(SceneStringName(text_changed), changed.unbind(1));
	make_label(add_grid, TTR("Add Suffix"));
	add_suffix = make_line_edit(add_grid, TTR("New Suffix"));
	add_suffix->connect(SceneStringName(text_changed), changed.unbind(1));

	// Auto increment.
	_add_section_title(rules_box, TTR("Auto Increment"));
	HBoxContainer *numbering_options = memnew(HBoxContainer);
	rules_box->add_child(numbering_options);
	numbering = memnew(CheckBox);
	numbering->set_text(TTR("Enabled"));
	numbering->connect(SceneStringName(toggled), changed.unbind(1));
	numbering_options->add_child(numbering);
	numbering_type = memnew(OptionButton);
	numbering_type->add_item("#");
	numbering_type->add_item("Aa");
	numbering_type->connect(SceneStringName(item_selected), changed.unbind(1));
	numbering_options->add_child(numbering_type);

	GridContainer *numbering_grid = memnew(GridContainer);
	numbering_grid->set_columns(2);
	rules_box->add_child(numbering_grid);
	make_label(numbering_grid, TTR("Format"));
	number_format = memnew(OptionButton);
	number_format->add_item("0");
	number_format->add_item("00");
	number_format->add_item("000");
	number_format->add_item("0000");
	number_format->select(1);
	number_format->connect(SceneStringName(item_selected), changed.unbind(1));
	numbering_grid->add_child(number_format);
	make_label(numbering_grid, TTR("Start"));
	number_start = make_spin_box(numbering_grid, 0, 99999, 1);
	number_start->connect(SceneStringName(value_changed), changed.unbind(1));
	letter_start = make_line_edit(numbering_grid, "A");
	letter_start->set_text("A");
	letter_start->connect(SceneStringName(text_changed), changed.unbind(1));
	make_label(numbering_grid, TTR("Step"));
	number_step = make_spin_box(numbering_grid, 1, 99, 1);
	number_step->connect(SceneStringName(value_changed), changed.unbind(1));

	// Case.
	_add_section_title(rules_box, TTR("Change Case"));
	case_mode = memnew(OptionButton);
	case_mode->add_item(TTR("Keep"));
	case_mode->add_item("snake_case");
	case_mode->add_item("PascalCase");
	case_mode->add_item(TTR("All Lower"));
	case_mode->add_item(TTR("All Upper"));
	case_mode->add_item(TTR("Swap First"));
	case_mode->add_item(TTR("Swap All"));
	case_mode->connect(SceneStringName(item_selected), changed.unbind(1));
	rules_box->add_child(case_mode);

	// Preview.
	VBoxContainer *preview_box = memnew(VBoxContainer);
	preview_box->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	split->add_child(preview_box);
	preview = memnew(Tree);
	preview->set_columns(2);
	preview->set_column_titles_visible(true);
	preview->set_column_title(0, TTR("Old"));
	preview->set_column_title(1, TTR("New"));
	preview->set_hide_root(true);
	preview->set_select_mode(Tree::SELECT_ROW);
	preview->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	preview->set_custom_minimum_size(Size2(420, 360) * EDSCALE);
	preview_box->add_child(preview);
	status = memnew(Label);
	preview_box->add_child(status);
}

void BatchRenameDialog::_notification(int p_what) {
	if (p_what == NOTIFICATION_THEME_CHANGED && is_visible()) {
		_update_preview();
	}
}

void BatchRenameDialog::_bind_methods() {
	ClassDB::bind_method(D_METHOD("popup_for_paths", "paths"), &BatchRenameDialog::popup_for_paths);
}

void BatchRenameDialog::popup_for_paths(const Vector<String> &p_paths) {
	selection = p_paths;
	bool has_folder = false;
	for (const String &path : selection) {
		has_folder = has_folder || DirAccess::dir_exists_absolute(path);
	}
	include_contents->set_visible(has_folder);
	_collect_items();
	_update_preview();
	popup_centered_clamped(Size2(900, 560) * EDSCALE, 0.8);
	search->grab_focus();
}

void BatchRenameDialog::_add_item(const String &p_path, bool p_is_folder, HashSet<String> &r_seen) {
	const String path = p_path.trim_suffix("/");
	if (path.is_empty() || path == "res:" || r_seen.has(path)) {
		return;
	}
	r_seen.insert(path);
	Item item;
	item.path = path;
	item.is_folder = p_is_folder;
	const String file_name = path.get_file();
	item.base_name = p_is_folder ? file_name : file_name.get_basename();
	item.extension = p_is_folder || file_name.get_extension().is_empty() ? String() : "." + file_name.get_extension();
	items.push_back(item);
}

void BatchRenameDialog::_collect_folder(EditorFileSystemDirectory *p_folder, HashSet<String> &r_seen) {
	if (p_folder == nullptr) {
		return;
	}
	for (int i = 0; i < p_folder->get_subdir_count(); i++) {
		_add_item(p_folder->get_subdir(i)->get_path(), true, r_seen);
		_collect_folder(p_folder->get_subdir(i), r_seen);
	}
	for (int i = 0; i < p_folder->get_file_count(); i++) {
		_add_item(p_folder->get_file_path(i), false, r_seen);
	}
}

void BatchRenameDialog::_collect_items() {
	items.clear();
	HashSet<String> seen;
	for (const String &path : selection) {
		const bool is_folder = DirAccess::dir_exists_absolute(path);
		_add_item(path, is_folder, seen);
		if (is_folder && include_contents->is_pressed()) {
			_collect_folder(EditorFileSystem::get_singleton()->get_filesystem_path(path.trim_suffix("/") + "/"), seen);
		}
	}
}

void BatchRenameDialog::_on_selection_rule_changed() {
	_collect_items();
	_update_preview();
}

void BatchRenameDialog::_read_rules() {
	rules.search = search->get_text();
	rules.replacement = replacement->get_text();
	rules.use_regex = search_mode->get_selected() == 1;
	rules.ignore_case = ignore_case->is_pressed();

	rules.remove_prefix = remove_prefix->is_pressed();
	rules.prefix_mode = prefix_mode->get_selected() == 0 ? BatchRenameRules::TrimMode::SEPARATOR : BatchRenameRules::TrimMode::CHARS;
	rules.prefix_separator = prefix_separator->get_text();
	rules.prefix_chars = int(prefix_chars->get_value());

	rules.remove_suffix = remove_suffix->is_pressed();
	rules.suffix_mode = suffix_mode->get_selected() == 0 ? BatchRenameRules::TrimMode::SEPARATOR : BatchRenameRules::TrimMode::CHARS;
	rules.suffix_separator = suffix_separator->get_text();
	rules.suffix_chars = int(suffix_chars->get_value());

	rules.add_prefix = add_prefix->get_text();
	rules.add_suffix = add_suffix->get_text();

	rules.numbering = numbering->is_pressed();
	rules.numbering_type = numbering_type->get_selected() == 0 ? BatchRenameRules::NumberingType::NUMBER : BatchRenameRules::NumberingType::LETTER;
	rules.number_digits = number_format->get_selected() + 1;
	rules.number_start = int(number_start->get_value());
	rules.number_step = int(number_step->get_value());
	rules.letter_start = letter_start->get_text();

	rules.case_mode = BatchRenameRules::CaseMode(case_mode->get_selected());
}

void BatchRenameDialog::_update_controls() {
	prefix_mode->set_disabled(!rules.remove_prefix);
	prefix_separator->set_editable(rules.remove_prefix && rules.prefix_mode == BatchRenameRules::TrimMode::SEPARATOR);
	prefix_chars->set_editable(rules.remove_prefix && rules.prefix_mode == BatchRenameRules::TrimMode::CHARS);
	suffix_mode->set_disabled(!rules.remove_suffix);
	suffix_separator->set_editable(rules.remove_suffix && rules.suffix_mode == BatchRenameRules::TrimMode::SEPARATOR);
	suffix_chars->set_editable(rules.remove_suffix && rules.suffix_mode == BatchRenameRules::TrimMode::CHARS);

	const bool numbers = rules.numbering_type == BatchRenameRules::NumberingType::NUMBER;
	numbering_type->set_disabled(!rules.numbering);
	number_format->set_disabled(!rules.numbering || !numbers);
	number_start->set_visible(numbers);
	number_start->set_editable(rules.numbering);
	letter_start->set_visible(!numbers);
	letter_start->set_editable(rules.numbering);
	number_step->set_editable(rules.numbering && numbers);
}

void BatchRenameDialog::_update_preview() {
	_read_rules();
	_update_controls();
	const String rule_error = rules.prepare();

	// New names and conflicts inside each parent folder, compared case-insensitively.
	HashMap<String, int> targets;
	for (int i = 0; i < items.size(); i++) {
		Item &item = items.write[i];
		item.error = rule_error;
		const String base_name = rules.apply(item.base_name, i);
		item.new_name = base_name + item.extension;
		if (!item.error.is_empty()) {
			continue;
		}
		const String old_name = item.path.get_file();
		if (base_name.strip_edges().is_empty()) {
			item.error = TTR("Name is empty.");
		} else if (base_name != base_name.strip_edges()) {
			item.error = TTR("Name begins or ends with a space.");
		} else if (!item.new_name.is_valid_filename()) {
			item.error = TTR("Name contains invalid characters.");
		} else if (item.new_name.begins_with(".")) {
			item.error = TTR("Name begins with a dot.");
		} else if (item.new_name.to_lower() != old_name.to_lower()) {
			const String new_path = item.path.get_base_dir().path_join(item.new_name);
			if (FileAccess::exists(new_path) || DirAccess::dir_exists_absolute(new_path)) {
				item.error = TTR("A file or folder with this name already exists.");
			}
		}
		const String key = item.path.get_base_dir().path_join(item.new_name).to_lower();
		targets[key] = targets.has(key) ? targets[key] + 1 : 1;
	}

	preview->clear();
	TreeItem *root = preview->create_item();
	const Color disabled_color = get_theme_color(SNAME("font_disabled_color"), EditorStringName(Editor));
	const Color error_color = get_theme_color(SNAME("error_color"), EditorStringName(Editor));
	int renamed = 0;
	int errors = 0;
	for (Item &item : items) {
		const String key = item.path.get_base_dir().path_join(item.new_name).to_lower();
		if (item.error.is_empty() && targets.has(key) && targets[key] > 1) {
			item.error = TTR("Several items get the same name.");
		}
		const bool changed = item.new_name != item.path.get_file();
		renamed += changed && item.error.is_empty() ? 1 : 0;
		errors += item.error.is_empty() ? 0 : 1;

		TreeItem *row = preview->create_item(root);
		const String relative = item.path.trim_prefix("res://");
		row->set_text(0, relative);
		row->set_tooltip_text(0, item.path);
		row->set_icon(0, get_editor_theme_icon(item.is_folder ? SNAME("Folder") : SNAME("File")));
		row->set_text(1, item.new_name);
		if (!item.error.is_empty()) {
			row->set_custom_color(1, error_color);
			row->set_tooltip_text(1, item.error);
		} else if (!changed) {
			row->set_custom_color(0, disabled_color);
			row->set_custom_color(1, disabled_color);
		}
	}

	if (!rule_error.is_empty()) {
		status->set_text(rule_error);
	} else if (errors > 0) {
		status->set_text(vformat(TTR("%d of %d items have problems (hover the new name for details)."), errors, items.size()));
	} else {
		status->set_text(vformat(TTR("%d of %d items will be renamed."), renamed, items.size()));
	}
	status->add_theme_color_override(SceneStringName(font_color), errors > 0 || !rule_error.is_empty() ? error_color : get_theme_color(SceneStringName(font_color), SNAME("Label")));
	get_ok_button()->set_disabled(errors > 0 || renamed == 0);
}

void BatchRenameDialog::_reset_rules() {
	search->clear();
	replacement->clear();
	search_mode->select(0);
	ignore_case->set_pressed(true);
	remove_prefix->set_pressed(false);
	prefix_mode->select(0);
	prefix_separator->set_text("_");
	prefix_chars->set_value(1);
	remove_suffix->set_pressed(false);
	suffix_mode->select(0);
	suffix_separator->set_text("_");
	suffix_chars->set_value(1);
	add_prefix->clear();
	add_suffix->clear();
	numbering->set_pressed(false);
	numbering_type->select(0);
	number_format->select(1);
	number_start->set_value(1);
	number_step->set_value(1);
	letter_start->set_text("A");
	case_mode->select(0);
	_update_preview();
}

void BatchRenameDialog::_apply() {
	_update_preview();
	Vector<String> paths;
	Vector<String> new_names;
	for (const Item &item : items) {
		if (!item.error.is_empty()) {
			return;
		}
		if (item.new_name != item.path.get_file()) {
			paths.push_back(item.path);
			new_names.push_back(item.new_name);
		}
	}
	if (!paths.is_empty()) {
		FileSystemDock::get_singleton()->batch_rename(paths, new_names);
	}
}

void BatchRenamerContextMenuPlugin::get_options(const OptionsData &p_data) {
	EditorContextMenuPlugin::get_options(p_data);
	const PackedStringArray paths = p_data.get("selected_files", PackedStringArray());
	if (paths.is_empty() || (paths.size() == 1 && paths[0] == "res://")) {
		return;
	}
	const Ref<Texture2D> icon = EditorNode::get_singleton()->get_editor_theme()->get_icon(SNAME("Rename"), EditorStringName(EditorIcons));
	add_context_menu_item(TTR("Batch Rename..."), callable_mp(this, &BatchRenamerContextMenuPlugin::_open), icon);
}

void BatchRenamerContextMenuPlugin::_open(const Variant &p_data) {
	ERR_FAIL_NULL(dialog);
	const Dictionary data = p_data;
	const PackedStringArray paths = data.get("selected_files", PackedStringArray());
	Vector<String> selection;
	for (const String &path : paths) {
		if (path != "res://") {
			selection.push_back(path);
		}
	}
	if (!selection.is_empty()) {
		dialog->popup_for_paths(selection);
	}
}

BatchRenamerEditorPlugin::BatchRenamerEditorPlugin() {
	dialog = memnew(BatchRenameDialog);
	EditorNode::get_singleton()->get_gui_base()->add_child(dialog);
	context_menu.instantiate();
	context_menu->set_dialog(dialog);
}

void BatchRenamerEditorPlugin::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			add_context_menu_plugin(EditorContextMenuPlugin::CONTEXT_SLOT_FILESYSTEM, context_menu);
		} break;
		case NOTIFICATION_EXIT_TREE: {
			remove_context_menu_plugin(context_menu);
		} break;
	}
}
