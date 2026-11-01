/**************************************************************************/
/*  batch_rename_rules.h                                                  */
/*  OEngine: batch rename of FileSystem items.                            */
/**************************************************************************/

#pragma once

#include "core/string/regex.h"
#include "core/string/ustring.h"

// Name transformation applied to every item in order: search/replace, prefix and
// suffix removal, prefix and suffix addition, auto increment, case.
class BatchRenameRules {
public:
	enum class TrimMode {
		SEPARATOR,
		CHARS,
	};

	enum class NumberingType {
		NUMBER,
		LETTER,
	};

	enum class CaseMode {
		KEEP,
		SNAKE,
		PASCAL,
		LOWER,
		UPPER,
		SWAP_FIRST,
		SWAP_ALL,
	};

	// Empty search with a non-empty replacement replaces the whole name.
	String search;
	String replacement;
	bool use_regex = false;
	bool ignore_case = true;

	bool remove_prefix = false;
	TrimMode prefix_mode = TrimMode::SEPARATOR;
	String prefix_separator = "_";
	int prefix_chars = 1;

	bool remove_suffix = false;
	TrimMode suffix_mode = TrimMode::SEPARATOR;
	String suffix_separator = "_";
	int suffix_chars = 1;

	String add_prefix;
	String add_suffix;

	bool numbering = false;
	NumberingType numbering_type = NumberingType::NUMBER;
	int number_start = 1;
	int number_step = 1;
	int number_digits = 2;
	String letter_start = "A";

	CaseMode case_mode = CaseMode::KEEP;

	// Compiles the regex once per preview; returns the error text or an empty string.
	String prepare();
	String apply(const String &p_name, int p_index) const;

	static String to_snake_case(const String &p_name);
	static String letters_for(const String &p_start, int p_offset);

private:
	Ref<RegEx> regex;
	bool regex_valid = false;
};
