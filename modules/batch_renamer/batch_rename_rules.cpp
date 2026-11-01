/**************************************************************************/
/*  batch_rename_rules.cpp                                                */
/*  OEngine: batch rename of FileSystem items.                            */
/**************************************************************************/

#include "batch_rename_rules.h"

String BatchRenameRules::prepare() {
	regex.unref();
	regex_valid = false;
	if (!use_regex || search.is_empty()) {
		return String();
	}
	regex.instantiate();
	if (regex->compile((ignore_case ? "(?i)" : "") + search, false) != OK) {
		return "Invalid regular expression.";
	}
	regex_valid = true;
	return String();
}

String BatchRenameRules::apply(const String &p_name, int p_index) const {
	String name = p_name;

	if (!search.is_empty()) {
		if (use_regex) {
			if (regex_valid) {
				name = regex->sub(name, replacement, true);
			}
		} else {
			name = ignore_case ? name.replacen(search, replacement) : name.replace(search, replacement);
		}
	} else if (!replacement.is_empty()) {
		name = replacement;
	}

	if (remove_prefix) {
		if (prefix_mode == TrimMode::SEPARATOR) {
			const int at = prefix_separator.is_empty() ? -1 : name.findn(prefix_separator);
			if (at >= 0) {
				name = name.substr(at + prefix_separator.length());
			}
		} else {
			name = name.substr(MIN(prefix_chars, name.length()));
		}
	}

	if (remove_suffix) {
		if (suffix_mode == TrimMode::SEPARATOR) {
			const int at = suffix_separator.is_empty() ? -1 : name.rfindn(suffix_separator);
			if (at >= 0) {
				name = name.left(at);
			}
		} else {
			name = name.left(MAX(0, name.length() - suffix_chars));
		}
	}

	name = add_prefix + name + add_suffix;

	if (numbering) {
		if (numbering_type == NumberingType::NUMBER) {
			name += String::num_int64(number_start + p_index * number_step).pad_zeros(number_digits);
		} else {
			name += letters_for(letter_start, p_index);
		}
	}

	switch (case_mode) {
		case CaseMode::KEEP:
			break;
		case CaseMode::SNAKE:
			name = to_snake_case(name);
			break;
		case CaseMode::PASCAL:
			name = name.to_pascal_case();
			break;
		case CaseMode::LOWER:
			name = name.to_lower();
			break;
		case CaseMode::UPPER:
			name = name.to_upper();
			break;
		case CaseMode::SWAP_FIRST:
		case CaseMode::SWAP_ALL: {
			const int count = case_mode == CaseMode::SWAP_FIRST ? MIN(1, name.length()) : name.length();
			for (int i = 0; i < count; i++) {
				const String letter = name.substr(i, 1);
				const String swapped = letter == letter.to_upper() ? letter.to_lower() : letter.to_upper();
				name = name.left(i) + swapped + name.substr(i + 1);
			}
		} break;
	}
	return name;
}

String BatchRenameRules::to_snake_case(const String &p_name) {
	// Engine helper splits compound words; collapse the gaps left by spaces and repeated separators.
	String result = p_name.to_snake_case();
	while (result.contains("__")) {
		result = result.replace("__", "_");
	}
	return result.trim_prefix("_").trim_suffix("_");
}

String BatchRenameRules::letters_for(const String &p_start, int p_offset) {
	// Spreadsheet-style sequence: A..Z, AA..AZ, BA...; case follows the start letter.
	const String start = p_start.is_empty() ? String("A") : p_start;
	const bool lower = start[0] >= 'a' && start[0] <= 'z';
	int64_t value = 0;
	for (int i = 0; i < start.length(); i++) {
		const char32_t c = start.to_upper()[i];
		if (c < 'A' || c > 'Z') {
			return String();
		}
		value = value * 26 + (c - 'A' + 1);
	}
	value += p_offset;
	String letters;
	while (value > 0) {
		value--;
		letters = String::chr('A' + char32_t(value % 26)) + letters;
		value /= 26;
	}
	return lower ? letters.to_lower() : letters;
}
