/**************************************************************************/
/*  batch_renamer_editor_plugin.h                                         */
/*  OEngine: batch rename of FileSystem items.                            */
/**************************************************************************/

#pragma once

#include "batch_rename_rules.h"

#include "editor/inspector/editor_context_menu_plugin.h"
#include "editor/plugins/editor_plugin.h"
#include "scene/gui/dialogs.h"

class CheckBox;
class Label;
class LineEdit;
class OptionButton;
class SpinBox;
class Tree;
class EditorFileSystemDirectory;

// Window with rename rules on the left and an Old/New preview of the selection on the right.
class BatchRenameDialog : public ConfirmationDialog {
	GDCLASS(BatchRenameDialog, ConfirmationDialog);

	struct Item {
		String path;
		String base_name;
		String extension;
		bool is_folder = false;
		String new_name;
		String error;
	};

	Vector<String> selection;
	Vector<Item> items;
	BatchRenameRules rules;

	LineEdit *search = nullptr;
	LineEdit *replacement = nullptr;
	OptionButton *search_mode = nullptr;
	CheckBox *ignore_case = nullptr;

	CheckBox *remove_prefix = nullptr;
	OptionButton *prefix_mode = nullptr;
	LineEdit *prefix_separator = nullptr;
	SpinBox *prefix_chars = nullptr;

	CheckBox *remove_suffix = nullptr;
	OptionButton *suffix_mode = nullptr;
	LineEdit *suffix_separator = nullptr;
	SpinBox *suffix_chars = nullptr;

	LineEdit *add_prefix = nullptr;
	LineEdit *add_suffix = nullptr;

	CheckBox *numbering = nullptr;
	OptionButton *numbering_type = nullptr;
	OptionButton *number_format = nullptr;
	SpinBox *number_start = nullptr;
	SpinBox *number_step = nullptr;
	LineEdit *letter_start = nullptr;

	OptionButton *case_mode = nullptr;
	CheckBox *include_contents = nullptr;

	Tree *preview = nullptr;
	Label *status = nullptr;

	void _add_section_title(Control *p_parent, const String &p_title);
	void _collect_items();
	void _collect_folder(EditorFileSystemDirectory *p_folder, HashSet<String> &r_seen);
	void _add_item(const String &p_path, bool p_is_folder, HashSet<String> &r_seen);
	void _read_rules();
	void _update_controls();
	void _update_preview();
	void _on_selection_rule_changed();
	void _reset_rules();
	void _apply();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void popup_for_paths(const Vector<String> &p_paths);

	BatchRenameDialog();
};

// "Batch Rename..." entry in the FileSystem dock context menu.
class BatchRenamerContextMenuPlugin : public EditorContextMenuPlugin {
	GDCLASS(BatchRenamerContextMenuPlugin, EditorContextMenuPlugin);

	BatchRenameDialog *dialog = nullptr;

	void _open(const Variant &p_data);

public:
	virtual void get_options(const OptionsData &p_data) override;

	void set_dialog(BatchRenameDialog *p_dialog) { dialog = p_dialog; }
};

class BatchRenamerEditorPlugin : public EditorPlugin {
	GDCLASS(BatchRenamerEditorPlugin, EditorPlugin);

	BatchRenameDialog *dialog = nullptr;
	Ref<BatchRenamerContextMenuPlugin> context_menu;

protected:
	void _notification(int p_what);

public:
	virtual String get_plugin_name() const override { return "BatchRenamer"; }

	BatchRenamerEditorPlugin();
};
