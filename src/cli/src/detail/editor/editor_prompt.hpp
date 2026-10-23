// =============================================================================
// src/cli/src/detail/editor_prompt.hpp - prompted input for `editor-plugin`.
//
// OWNERSHIP: cli module (src/cli/**). PRIVATE header.
//
// The wizard and the non-wizard `install`/`uninstall` paths share these. Every
// reader returns false on EOF so no prompt path can loop forever.
// =============================================================================
#pragma once

#include <string>
#include <vector>

#include "detail/editor/editor.hpp"

namespace cp3::cli {

// Prints `question` verbatim, then reads one trimmed line. False on EOF.
bool readAnswer(const std::string& question, std::string& answer);

// Empty input returns `fallback`; EOF counts as "no".
bool promptYesNo(const std::string& question, bool fallback);

// Numbered editor selection; `selected` receives the choice. Empty input
// selects the first editor. One invalid answer is reprompted, then false.
bool promptEditorChoice(const std::vector<EditorInfo>& editors,
                        EditorInfo& selected);

// Reads a menu number in [minimum, maximum]. One invalid answer is reprompted,
// then -1 (also on EOF).
int promptMenuChoice(int minimum, int maximum, const std::string& question);

}  // namespace cp3::cli
