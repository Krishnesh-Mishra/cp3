// =============================================================================
// src/cli/src/commands/editor_prompt.cpp - prompted input for editor-plugin.
//
// OWNERSHIP: cli module (src/cli/**). Implements detail/editor_prompt.hpp.
// EOF always terminates the prompt path; invalid menu answers get exactly one
// repeat before the caller reports a usage error.
// =============================================================================
#include "detail/editor/editor_prompt.hpp"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

namespace cp3::cli {

namespace {

std::string trim(const std::string& text) {
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string lowerAscii(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return text;
}

bool parseNumber(const std::string& text, long& value) {
    if (text.empty()) return false;
    char* end = nullptr;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0') return false;
    value = parsed;
    return true;
}

bool editorMatches(const EditorInfo& editor, const std::string& answer) {
    if (lowerAscii(editor.id) == lowerAscii(answer)) return true;
    return lowerAscii(editor.name) == lowerAscii(answer);
}

}  // namespace

bool readAnswer(const std::string& question, std::string& answer) {
    std::fputs(question.c_str(), stdout);
    std::fflush(stdout);
    std::string line;
    if (!std::getline(std::cin, line)) return false;
    answer = trim(line);
    return true;
}

bool promptYesNo(const std::string& question, bool fallback) {
    std::string answer;
    const std::string suffix = fallback ? " [Y/n]: " : " [y/N]: ";
    if (!readAnswer(question + suffix, answer)) return false;
    if (answer.empty()) return fallback;
    const std::string lower = lowerAscii(answer);
    return lower == "y" || lower == "yes";
}

bool promptEditorChoice(const std::vector<EditorInfo>& editors,
                        EditorInfo& selected) {
    if (editors.empty()) return false;
    std::fputs("\n  Detected editors:\n", stdout);
    for (std::size_t i = 0; i < editors.size(); ++i) {
        std::fprintf(stdout, "    [%zu] %s (%s)\n", i + 1,
                     editors[i].name.c_str(), editors[i].id.c_str());
    }
    for (int attempt = 0; attempt < 2; ++attempt) {
        std::string answer;
        if (!readAnswer("  Editor [number or id]: ", answer)) return false;
        if (answer.empty()) {
            selected = editors.front();
            return true;
        }
        long number = 0;
        if (parseNumber(answer, number) && number >= 1 &&
            static_cast<std::size_t>(number) <= editors.size()) {
            selected = editors[static_cast<std::size_t>(number - 1)];
            return true;
        }
        for (const EditorInfo& editor : editors) {
            if (editorMatches(editor, answer)) {
                selected = editor;
                return true;
            }
        }
        std::fputs("cp3c: invalid editor choice\n", stderr);
    }
    return false;
}

int promptMenuChoice(int minimum, int maximum, const std::string& question) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        std::string answer;
        if (!readAnswer(question + ": ", answer)) return -1;
        long number = 0;
        if (parseNumber(answer, number) && number >= minimum && number <= maximum) {
            return static_cast<int>(number);
        }
        std::fputs("cp3c: invalid choice\n", stderr);
    }
    return -1;
}

}  // namespace cp3::cli
