// =============================================================================
// src/sema/src/support/docs.cpp - the doc-comment reader and parser.
//
// OWNERSHIP: sema module (src/sema/**). Implements detail/docs.h.
//
// A doc comment is documentation, not behaviour: this unit reads the comment
// that precedes a declaration, parses its tags and hands the result to the
// analyzer, which publishes it into `SemaResult::docs` (cp3/analysis.hpp).
// Nothing here reports a diagnostic - a comment attached to a declaration the
// compiler does not know is simply dropped, and a comment never changes what
// the compiled program does.
// =============================================================================
#include "detail/docs.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace cp3 {
namespace {

bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' ||
           c == '\f';
}

std::string trim(const std::string& text) {
    std::size_t begin = 0;
    while (begin < text.size() && isSpace(text[begin])) ++begin;
    std::size_t end = text.size();
    while (end > begin && isSpace(text[end - 1])) --end;
    return text.substr(begin, end - begin);
}

// Splits on CR, LF and CRLF.
std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::string current;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\r' || c == '\n') {
            if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ++i;
            lines.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(c);
    }
    lines.push_back(current);
    return lines;
}

// The offset of the first non-space byte at or before `at`.
std::size_t skipSpaceBack(const std::string& text, std::size_t at) {
    while (at > 0 && isSpace(text[at - 1])) --at;
    return at;
}

// True when only whitespace precedes `at` on its line.
bool atLineStart(const std::string& text, std::size_t at) {
    while (at > 0) {
        const char c = text[at - 1];
        if (c == '\n') return true;
        if (!isSpace(c)) return false;
        --at;
    }
    return true;
}

// The block doc comment that ends just before `after` (the offset just past
// its closing `*/`).
struct BlockDoc {
    std::size_t open = 0;          // the offset of the opening "/**"
    std::size_t contentBegin = 0;  // the first content byte
    std::size_t contentEnd = 0;    // the offset of the closing "*/"
    bool found = false;
};

BlockDoc blockDocEndingAt(const std::string& text, std::size_t after) {
    BlockDoc doc;
    if (after < 2) return doc;
    // The comment ends with "*/", so the last two bytes are '*' then '/'.
    if (text[after - 1] != '/' || text[after - 2] != '*') return doc;
    doc.contentEnd = after - 2;
    for (std::size_t s = doc.contentEnd; s >= 3; --s) {
        if (text[s - 3] == '/' && text[s - 2] == '*' && text[s - 1] == '*') {
            doc.open = s - 3;
            doc.contentBegin = s;
            doc.found = true;
            break;
        }
    }
    // `/**/` is an ordinary (empty) block comment, not a doc comment.
    if (doc.found && doc.contentBegin >= doc.contentEnd) doc.found = false;
    return doc;
}

// The text of a `///` line doc comment, or "" when the line is not one.
std::string lineDocText(std::string_view line) {
    std::size_t at = 0;
    while (at < line.size() && isSpace(line[at])) ++at;
    if (line.compare(at, 3, "///") != 0) return "";
    std::string rest(line.substr(at + 3));
    if (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
    return rest;
}

// The doc comment's own lines, with the block form's leading "* " removed.
std::vector<std::string> blockDocLines(const std::string& content) {
    std::vector<std::string> lines;
    for (const std::string& raw : splitLines(content)) {
        std::string line = trim(raw);
        if (!line.empty() && line.front() == '*') line.erase(line.begin());
        if (!line.empty() && line.front() == ' ') line.erase(line.begin());
        lines.push_back(line);
    }
    return lines;
}

// Appends a paragraph to the description.
void appendSummary(DocComment& doc, const std::string& text) {
    if (text.empty()) return;
    if (doc.summary.empty()) {
        doc.summary = text;
        return;
    }
    doc.summary += "\n\n";
    doc.summary += text;
}

// One tag of a doc comment: its name, the `@param` name and its text.
struct DocTag {
    std::string name;
    std::string argument;
    std::string text;
};

// Reads the tag a line starts: "@param <name> <text>" and friends.
DocTag tagOf(const std::string& line) {
    DocTag tag;
    const std::string rest = line.substr(1);
    const std::size_t space = rest.find_first_of(" \t");
    tag.name = space == std::string::npos ? rest : rest.substr(0, space);
    const std::string body =
        space == std::string::npos ? "" : trim(rest.substr(space + 1));
    if (tag.name != "param") {
        tag.text = body;
        return tag;
    }
    const std::size_t split = body.find_first_of(" \t");
    if (split == std::string::npos) {
        tag.text = body;
        return tag;
    }
    tag.argument = body.substr(0, split);
    tag.text = trim(body.substr(split + 1));
    return tag;
}

// Reads one block (a description or a tag) into the documentation.
void applyBlock(DocComment& doc, const std::string& tag,
                const std::string& argument, const std::string& text) {
    const std::string body = trim(text);
    if (tag == "param") {
        if (!argument.empty()) {
            doc.params.push_back(DocParam{argument, body});
            return;
        }
        // `@param` without a name: kept as plain text, never an error.
        appendSummary(doc, "@param" + (body.empty() ? "" : " " + body));
        return;
    }
    if (tag == "returns" || tag == "return") {
        if (!body.empty()) {
            doc.returns = doc.returns.empty() ? body : doc.returns + "\n" + body;
        }
        return;
    }
    if (tag.empty()) {
        appendSummary(doc, body);
        return;
    }
    // An unknown tag stays plain text instead of failing.
    appendSummary(doc, "@" + tag + (body.empty() ? "" : " " + body));
}

}  // namespace

// =============================================================================
// The reader
// =============================================================================
DocComment readDocComment(const SourceFile& file, std::uint32_t offset) {
    const std::string& text = file.text;
    if (offset > text.size()) return DocComment();
    const std::size_t start = offset;
    const std::uint32_t declLine = file.positionAt(offset).line;

    // --- the block form: `/** ... */` ---------------------------------------
    const std::size_t after = skipSpaceBack(text, start);
    const BlockDoc block = blockDocEndingAt(text, after);
    if (block.found) {
        const bool opensItsLine = atLineStart(text, block.open);
        const std::uint32_t endLine = file.positionAt(
            static_cast<std::uint32_t>(after - 1)).line;
        // The comment documents the declaration only when it sits above it,
        // with no blank line in between (the JSDoc rule).
        bool blankBetween = false;
        for (std::uint32_t line = endLine + 1; line < declLine; ++line) {
            if (trim(std::string(file.lineText(line))).empty()) blankBetween = true;
        }
        if (opensItsLine && endLine < declLine && !blankBetween) {
            const std::string content =
                text.substr(block.contentBegin, block.contentEnd - block.contentBegin);
            return parseDocComment(blockDocLines(content));
        }
    }

    // --- the line form: a run of `///` lines --------------------------------
    if (!atLineStart(text, start)) return DocComment();
    std::vector<std::string> lines;
    for (std::uint32_t line = declLine; line > 1; --line) {
        const std::string doc = lineDocText(file.lineText(line - 1));
        if (doc.empty()) break;
        lines.insert(lines.begin(), doc);
    }
    if (lines.empty()) return DocComment();
    return parseDocComment(lines);
}

// =============================================================================
// The parser
// =============================================================================
DocComment parseDocComment(const std::vector<std::string>& lines) {
    DocComment doc;
    std::string text;      // the block being read
    std::string tag;       // its tag name ("" for the description)
    std::string argument;  // the `@param` name
    bool reading = false;

    for (const std::string& raw : lines) {
        const std::string line = trim(raw);
        if (line.empty()) continue;
        if (line.front() == '@') {
            if (reading) applyBlock(doc, tag, argument, text);
            const DocTag parsed = tagOf(line);
            tag = parsed.name;
            argument = parsed.argument;
            text = parsed.text;
            reading = true;
            continue;
        }
        if (!reading) {
            reading = true;
            tag.clear();
            argument.clear();
        }
        text = text.empty() ? line : text + " " + line;
    }
    if (reading) applyBlock(doc, tag, argument, text);
    return doc;
}

}  // namespace cp3
