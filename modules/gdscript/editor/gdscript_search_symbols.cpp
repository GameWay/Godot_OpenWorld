/**************************************************************************/
/*  gdscript_search_symbols.cpp                                           */
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

#include "gdscript_editor_language.h"

#include "modules/gdscript/gdscript_parser.h"

namespace {

void collect_search_symbols(const GDScriptParser::ClassNode *p_class, const String &p_container, Vector<EditorLanguage::SearchSymbol> &r_symbols) {
	using Member = GDScriptParser::ClassNode::Member;
	String container = p_container;
	if (p_class->identifier) {
		EditorLanguage::SearchSymbol symbol;
		symbol.name = p_class->identifier->name;
		symbol.container = container;
		symbol.line = MAX(0, p_class->identifier->start_line - 1);
		symbol.icon = "ClassList";
		r_symbols.push_back(symbol);
		container = container.is_empty() ? symbol.name : container + "." + symbol.name;
	}
	for (const Member &member : p_class->members) {
		if (member.type == Member::CLASS) {
			collect_search_symbols(member.m_class, container, r_symbols);
			continue;
		}
		if (member.type == Member::GROUP || member.type == Member::UNDEFINED) {
			continue;
		}
		EditorLanguage::SearchSymbol symbol;
		symbol.name = member.get_name();
		symbol.container = container;
		symbol.line = MAX(0, member.get_line() - 1);
		switch (member.type) {
			case Member::FUNCTION:
				symbol.icon = "MemberMethod";
				break;
			case Member::SIGNAL:
				symbol.icon = "MemberSignal";
				break;
			case Member::VARIABLE:
				symbol.icon = "MemberProperty";
				break;
			case Member::CONSTANT:
			case Member::ENUM_VALUE:
				symbol.icon = "MemberConstant";
				break;
			case Member::ENUM:
				symbol.icon = "Enum";
				break;
			default:
				break;
		}
		r_symbols.push_back(symbol);
		if (member.type == Member::ENUM) {
			for (const GDScriptParser::EnumNode::Value &value : member.m_enum->values) {
				if (!value.identifier) {
					continue;
				}
				EditorLanguage::SearchSymbol enum_value;
				enum_value.name = value.identifier->name;
				enum_value.container = container.is_empty() ? symbol.name : container + "." + symbol.name;
				enum_value.line = MAX(0, value.identifier->start_line - 1);
				enum_value.icon = "MemberConstant";
				r_symbols.push_back(enum_value);
			}
		}
	}
}

} // namespace

void GDScriptEditorLanguage::get_search_symbols(const String &p_code, const String &p_path, Vector<SearchSymbol> &r_symbols) const {
	GDScriptParser parser;
	// Completion mode retains declarations in incomplete scripts. No analyzer or
	// ResourceLoader: indexing must not execute @tool code or resolve dependencies.
	parser.parse(p_code, p_path, true);
	if (parser.get_tree()) {
		collect_search_symbols(parser.get_tree(), String(), r_symbols);
	}
}
