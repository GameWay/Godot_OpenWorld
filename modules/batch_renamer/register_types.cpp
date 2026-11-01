/**************************************************************************/
/*  register_types.cpp                                                    */
/*  OEngine: batch rename of FileSystem items.                            */
/**************************************************************************/

#include "register_types.h"

#ifdef TOOLS_ENABLED
#include "batch_renamer_editor_plugin.h"

#include "core/object/class_db.h"
#endif

void initialize_batch_renamer_module(ModuleInitializationLevel p_level) {
#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		GDREGISTER_INTERNAL_CLASS(BatchRenameDialog);
		GDREGISTER_INTERNAL_CLASS(BatchRenamerContextMenuPlugin);
		GDREGISTER_INTERNAL_CLASS(BatchRenamerEditorPlugin);
		EditorPlugins::add_by_type<BatchRenamerEditorPlugin>();
	}
#endif
}

void uninitialize_batch_renamer_module(ModuleInitializationLevel p_level) {
}
