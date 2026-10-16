// =============================================================================
// cli/commands_fmt.cpp — `cp3c fmt`, a conservative non-destructive formatter.
//
// OWNERSHIP: cli module (src/cli/**). Implements cp3::cli::cmdFmt.
//
// LIMITATIONS (deliberate, documented): this is NOT the canonical cp3
// formatter — that is a later job. It never reflows code or rewrites tokens; it
// only normalizes line endings, strips trailing whitespace, ensures a single
// trailing newline, and re-indents code lines by brace depth (2 spaces per
// level). Interiors of multi-line comments, template literals, and lines that
// begin inside one of those are left byte-for-byte untouched, and no
// non-whitespace character is ever changed — so it can never corrupt a file.
// Known imprecision: braces inside regular-expression literals or inside a
// template interpolation that spans lines can throw off the indentation depth
// (indentation only).
// =============================================================================
#include <cstdio>
#include <string>
#include <vector>

#include "cp3/diagnostic.hpp"
#include "cp3/lexer.hpp"
#include "cp3/source.hpp"

#include "private/commands.hpp"
#include "private/util.hpp"

namespace cp3::cli {

namespace {

// Returns the index just past a quoted literal that starts at `i` (where s[i]
// is the opening quote), honouring backslash escapes.
std::size_t skipQuoted(const std::string& s, std::size_t i) {
    const char quote = s[i];
    ++i;
    while (i < s.size() && s[i] != quote) {
        if (s[i] == '\\') ++i;
        ++i;
    }
    if (i < s.size()) ++i;
    return i;
}

// Given `i` just past a template `${`, returns the index just past the matching
// `}` of the interpolation, honouring nested braces and string literals.
std::size_t scanInterpolation(const std::string& s, std::size_t i) {
    int depth = 1;
    while (i < s.size() && depth > 0) {
        const char c = s[i];
        if (c == '\\') {
            i += 2;
            continue;
        }
        if (c == '"' || c == '\'') {
            i = skipQuoted(s, i);
            continue;
        }
        if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
        }
        ++i;
    }
    return i;
}

// Advances past one character inside a block comment, closing it on `*/`.
std::size_t scanBlockComment(const std::string& s, std::size_t i,
                             bool& inBlockComment) {
    if (s[i] == '*' && i + 1 < s.size() && s[i + 1] == '/') {
        inBlockComment = false;
        return i + 2;
    }
    return i + 1;
}

// Advances past one character inside a template literal body.
std::size_t scanTemplateText(const std::string& s, std::size_t i,
                             bool& inTemplate) {
    const char c = s[i];
    if (c == '\\') return i + 2;
    if (c == '`') {
        inTemplate = false;
        return i + 1;
    }
    if (c == '$' && i + 1 < s.size() && s[i + 1] == '{') {
        return scanInterpolation(s, i + 2);
    }
    return i + 1;
}

// Advances past one character of ordinary code, tracking brace depth. A line
// comment consumes the rest of the line (returns s.size()).
std::size_t scanCodeText(const std::string& s, std::size_t i, int& depth,
                         bool& inBlockComment, bool& inTemplate) {
    const char c = s[i];
    if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') return s.size();
    if (c == '/' && i + 1 < s.size() && s[i + 1] == '*') {
        inBlockComment = true;
        return i + 2;
    }
    if (c == '"' || c == '\'') return skipQuoted(s, i);
    if (c == '`') {
        inTemplate = true;
        return i + 1;
    }
    if (c == '{') {
        ++depth;
    } else if (c == '}') {
        if (depth > 0) --depth;
    }
    return i + 1;
}

// Updates the brace depth and multi-line-construct state for one code line.
void formatScanLine(const std::string& s, int& depth, bool& inBlockComment,
                    bool& inTemplate) {
    std::size_t i = 0;
    while (i < s.size()) {
        if (inBlockComment) {
            i = scanBlockComment(s, i, inBlockComment);
        } else if (inTemplate) {
            i = scanTemplateText(s, i, inTemplate);
        } else {
            i = scanCodeText(s, i, depth, inBlockComment, inTemplate);
        }
    }
}

// Normalizes CRLF / CR line endings to '\n'.
std::string normalizeLineEndings(const std::string& input) {
    std::string normalized;
    normalized.reserve(input.size() + 1);
    for (std::size_t i = 0; i < input.size(); ++i) {
        const char c = input[i];
        if (c == '\r') {
            if (i + 1 < input.size() && input[i + 1] == '\n') ++i;
            normalized.push_back('\n');
        } else {
            normalized.push_back(c);
        }
    }
    return normalized;
}

std::vector<std::string> splitLines(const std::string& normalized) {
    std::vector<std::string> lines;
    std::string cur;
    for (char c : normalized) {
        if (c == '\n') {
            lines.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

// Re-indents safe (single-line-context) code lines by brace depth.
std::string reindentLines(const std::vector<std::string>& lines) {
    std::string out;
    int depth = 0;
    bool inBlockComment = false;
    bool inTemplate = false;
    for (const std::string& raw : lines) {
        std::string line = raw;
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }

        std::size_t lead = 0;
        while (lead < line.size() && (line[lead] == ' ' || line[lead] == '\t')) {
            ++lead;
        }
        const std::string body = line.substr(lead);

        const bool safe = !inBlockComment && !inTemplate;
        if (body.empty()) {
            out.push_back('\n');
        } else if (safe) {
            int lineDepth = depth;
            if (body[0] == '}' || body[0] == ']' || body[0] == ')') {
                lineDepth = depth > 0 ? depth - 1 : 0;
            }
            out.append(static_cast<std::size_t>(lineDepth) * 2, ' ');
            out += body;
            out.push_back('\n');
        } else {
            out += line;  // inside a multi-line construct: leave untouched
            out.push_back('\n');
        }

        formatScanLine(body, depth, inBlockComment, inTemplate);
    }
    return out;
}

std::string formatSource(const std::string& input) {
    const std::string normalized = normalizeLineEndings(input);
    std::vector<std::string> lines = splitLines(normalized);
    // Drop trailing blank lines.
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
    return reindentLines(lines);
}

}  // namespace

int cmdFmt(const CliOptions& opt) {
    if (opt.file.empty()) {
        std::fprintf(stderr, "cp3c: fmt needs a .cp3 file\n");
        return kExitUsageError;
    }
    const fs::path path(opt.file);
    std::string text;
    if (!readTextFile(path, text)) {
        std::fprintf(stderr, "cp3c: cannot read '%s'\n", opt.file.c_str());
        return kExitUsageError;
    }

    // Report lexical problems, but never rewrite a file we cannot lex cleanly.
    SourceManager sm;
    SourceFile* file = sm.addFile(opt.file, text);
    CollectingDiagnosticSink sink;
    if (file != nullptr) {
        Lexer lexer(*file, sink);
        (void)lexer.lex();
    }
    renderDiagnostics(sink.diagnostics(), makeRenderOptions(opt, opt.file),
                      stderr);
    if (sink.hasErrors()) {
        std::fprintf(stderr,
                     "cp3c: fmt: refusing to rewrite '%s' (lexical errors)\n",
                     opt.file.c_str());
        return kExitCompileError;
    }

    const std::string formatted = formatSource(text);
    if (formatted == text) {
        if (opt.verbose) {
            std::fprintf(stderr, "cp3c: %s already formatted\n",
                         opt.file.c_str());
        }
        return kExitSuccess;
    }
    if (!writeTextFile(path, formatted, nullptr)) {
        std::fprintf(stderr, "cp3c: cannot write '%s'\n", opt.file.c_str());
        return kExitCompileError;
    }
    std::fprintf(stderr, "cp3c: formatted %s\n", opt.file.c_str());
    return kExitSuccess;
}

}  // namespace cp3::cli
