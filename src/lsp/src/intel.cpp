// =============================================================================
// src/lsp/src/intel.cpp - query providers (wire shape) from lsp_intel.hpp.
//
// OWNERSHIP: LSP workstream (src/lsp/**). Implements detail/lsp_intel.hpp.
//
// One function per LSP request method. Each converts the request's UTF-16
// position into the compiler's byte coordinates, calls the matching IntelHook
// and packages the answer. Every failure mode (unknown document, missing
// hook, throwing hook) degrades to the empty result for that method: the
// client sees a well-formed no-answer, never a broken frame or a dead server.
// =============================================================================
#include "detail/lsp_intel.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "detail/lsp_text.hpp"

namespace cp3::lsp {
namespace {

// Caps keep answers bounded even for a pathological document; well above any
// real completion list (the semantic engine offers at most a few hundred).
constexpr std::size_t kMaxCompletionItems = 512;
constexpr std::size_t kMaxWorkspaceSymbols = 512;

Json positionJson(const LspPosition& pos) {
    Json object = jsonObject();
    object.set("line", jsonInt(pos.line));
    object.set("character", jsonInt(pos.character));
    return object;
}

bool endsAfterStart(const LspPosition& end, const LspPosition& start) {
    if (end.line != start.line) return end.line > start.line;
    return end.character > start.character;
}

// A range from compiler coordinates in `text`. A missing/degenerate end
// advances one character: clients render a zero-width range poorly.
Json rangeJson(const std::string& text, std::uint32_t line,
               std::uint32_t column, std::uint32_t endLine,
               std::uint32_t endColumn) {
    const LspPosition start = toLspPosition(text, line, column);
    LspPosition end = advanceOne(text, start);
    if (endLine != 0 && endColumn != 0) {
        const LspPosition reported = toLspPosition(text, endLine, endColumn);
        if (endsAfterStart(reported, start)) end = reported;
    }
    Json range = jsonObject();
    range.set("start", positionJson(start));
    range.set("end", positionJson(end));
    return range;
}

// A Markdown documentation block (the LSP `MarkupContent` shape the editor
// renders as formatted text).
Json markdownDoc(const std::string& value) {
    Json doc = jsonObject();
    doc.set("kind", jsonString("markdown"));
    doc.set("value", jsonString(value));
    return doc;
}

Json additionalEditJson(const std::string& text,
                        const AdditionalEdit& edit) {
    Json item = jsonObject();
    item.set("range",
             rangeJson(text, edit.line, edit.column,
                       edit.endLine, edit.endColumn));
    item.set("newText", jsonString(edit.newText));
    return item;
}

Json completionItemJson(const CompletionCandidate& candidate,
                        std::size_t priority,
                        const std::string& documentText) {
    Json item = jsonObject();
    item.set("label", jsonString(candidate.label));
    item.set("kind", jsonInt(candidate.kind));
    // Preserve the compiler's ranking: locals first, then imports and module
    // names, then keywords. Without sortText VS Code alphabetizes the list.
    std::string sortText = std::to_string(priority);
    if (sortText.size() < 8) sortText.insert(0, 8 - sortText.size(), '0');
    item.set("sortText", jsonString(sortText));
    if (!candidate.detail.empty()) {
        item.set("detail", jsonString(candidate.detail));
    }
    if (!candidate.documentation.empty()) {
        item.set("documentation", markdownDoc(candidate.documentation));
    }
    if (candidate.insertTextFormat == 2 && !candidate.insertText.empty()) {
        item.set("insertText", jsonString(candidate.insertText));
        item.set("insertTextFormat", jsonInt(2));
    }
    if (!candidate.additionalEdits.empty()) {
        Json edits = jsonArray();
        for (const AdditionalEdit& edit : candidate.additionalEdits) {
            edits.push(additionalEditJson(documentText, edit));
        }
        item.set("additionalTextEdits", std::move(edits));
    }
    return item;
}

Json symbolJson(const std::string& text, const SymbolEntry& entry) {
    const LspPosition start = toLspPosition(text, entry.line, entry.column);
    Json item = jsonObject();
    item.set("name", jsonString(entry.name));
    item.set("kind", jsonInt(entry.kind));
    item.set("range",
             rangeJson(text, entry.line, entry.column, entry.endLine, entry.endColumn));
    Json selection = jsonObject();
    selection.set("start", positionJson(start));
    selection.set("end", positionJson(advanceOne(text, start)));
    item.set("selectionRange", std::move(selection));
    return item;
}

Json symbolInformationJson(const std::string& text, const std::string& uri,
                           const SymbolEntry& entry) {
    Json location = jsonObject();
    location.set("uri", jsonString(uri));
    location.set("range",
                 rangeJson(text, entry.line, entry.column, entry.endLine, entry.endColumn));
    Json item = jsonObject();
    item.set("name", jsonString(entry.name));
    item.set("kind", jsonInt(entry.kind));
    item.set("location", std::move(location));
    return item;
}

// A document the providers can answer for: mapped to a file with content.
bool hasTarget(const DocumentView& document) {
    return document.path != nullptr && document.text != nullptr &&
           !document.path->empty();
}

std::string lowerAscii(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return text;
}

// Fuzzy match in the style of the platform's symbol search: every query
// character must appear, in order, in the candidate (case-insensitive). A
// literal substring match is a subsequence, so this only loosens the search;
// it lets "ZqxZetaClass" find "ZqxZetaBoxedClassTen".
bool fuzzySubsequence(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    std::size_t cursor = 0;
    for (char ch : needle) {
        const std::size_t found = haystack.find(ch, cursor);
        if (found == std::string::npos) return false;
        cursor = found + 1;
    }
    return true;
}

// The declaration's own text when the target file is not the queried one.
std::optional<std::string> fetchTarget(const std::string& path,
                                       const IntelHooks& intel) {
    if (!intel.readFile) return std::nullopt;
    try {
        return intel.readFile(path);
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace

Json completionResult(const DocumentView& document, std::uint32_t line,
                      std::uint32_t character, const IntelHooks& intel) {
    Json items = jsonArray();
    if (hasTarget(document) && intel.completion) {
        std::vector<CompletionCandidate> candidates;
        try {
            const std::uint32_t column =
                toByteColumn(*document.text, line, character);
            candidates =
                intel.completion(*document.path, *document.text, line + 1, column);
        } catch (...) {
            candidates.clear();
        }
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            const CompletionCandidate& candidate = candidates[i];
            if (items.items.size() >= kMaxCompletionItems) break;
            items.push(completionItemJson(candidate, i, *document.text));
        }
    }
    Json result = jsonObject();
    result.set("isIncomplete", jsonBool(false));
    result.set("items", std::move(items));
    return result;
}

Json hoverResult(const DocumentView& document, std::uint32_t line,
                 std::uint32_t character, const IntelHooks& intel) {
    if (!hasTarget(document) || !intel.hover) return jsonNull();
    std::optional<HoverAnswer> answer;
    try {
        const std::uint32_t column = toByteColumn(*document.text, line, character);
        answer = intel.hover(*document.path, *document.text, line + 1, column);
    } catch (...) {
        answer.reset();
    }
    if (!answer || (answer->contents.empty() && answer->documentation.empty())) {
        return jsonNull();
    }
    // The signature (or the inferred type) goes into a code block; the
    // documentation is appended as markdown, so the editor formats the
    // description and the parameter list.
    std::string value;
    if (!answer->contents.empty()) {
        value = "```cp3\n" + answer->contents + "\n```";
    }
    if (!answer->documentation.empty()) {
        if (!value.empty()) value += "\n\n";
        value += answer->documentation;
    }
    Json contents = jsonObject();
    contents.set("kind", jsonString("markdown"));
    contents.set("value", jsonString(value));
    Json result = jsonObject();
    result.set("contents", std::move(contents));
    return result;
}

Json definitionResult(const DocumentView& document, std::uint32_t line,
                      std::uint32_t character, const IntelHooks& intel) {
    Json locations = jsonArray();
    if (!hasTarget(document) || !intel.definition) return locations;
    std::vector<SourceTarget> targets;
    try {
        const std::uint32_t column = toByteColumn(*document.text, line, character);
        targets =
            intel.definition(*document.path, *document.text, line + 1, column);
    } catch (...) {
        targets.clear();
    }
    for (const SourceTarget& target : targets) {
        if (target.path.empty()) continue;
        std::optional<std::string> fetched;
        const std::string* targetText = document.text;
        if (target.path != *document.path) {
            fetched = fetchTarget(target.path, intel);
            if (!fetched) continue;
            targetText = &*fetched;
        }
        Json location = jsonObject();
        location.set("uri", jsonString(pathToUri(target.path)));
        location.set("range", rangeJson(*targetText, target.line, target.column,
                                        target.endLine, target.endColumn));
        locations.push(std::move(location));
    }
    return locations;
}

Json documentSymbolResult(const DocumentView& document, const IntelHooks& intel) {
    Json symbols = jsonArray();
    if (!hasTarget(document) || !intel.documentSymbols) return symbols;
    std::vector<SymbolEntry> entries;
    try {
        entries = intel.documentSymbols(*document.path, *document.text);
    } catch (...) {
        entries.clear();
    }
    for (const SymbolEntry& entry : entries) {
        symbols.push(symbolJson(*document.text, entry));
    }
    return symbols;
}

Json signatureHelpResult(const DocumentView& document, std::uint32_t line,
                         std::uint32_t character, const IntelHooks& intel) {
    if (!hasTarget(document) || !intel.signatureHelp) return jsonNull();
    std::optional<SignatureAnswer> answer;
    try {
        const std::uint32_t column = toByteColumn(*document.text, line, character);
        answer =
            intel.signatureHelp(*document.path, *document.text, line + 1, column);
    } catch (...) {
        answer.reset();
    }
    if (!answer || answer->label.empty()) return jsonNull();
    Json parameters = jsonArray();
    for (std::size_t i = 0; i < answer->parameters.size(); ++i) {
        Json item = jsonObject();
        item.set("label", jsonString(answer->parameters[i]));
        if (i < answer->parameterDocs.size() &&
            !answer->parameterDocs[i].empty()) {
            item.set("documentation", markdownDoc(answer->parameterDocs[i]));
        }
        parameters.push(std::move(item));
    }
    Json signature = jsonObject();
    signature.set("label", jsonString(answer->label));
    if (!answer->documentation.empty()) {
        signature.set("documentation", markdownDoc(answer->documentation));
    }
    signature.set("parameters", std::move(parameters));
    Json signatures = jsonArray();
    signatures.push(std::move(signature));
    Json result = jsonObject();
    result.set("signatures", std::move(signatures));
    result.set("activeSignature", jsonInt(0));
    const bool inRange = answer->activeParameter >= 0 &&
                         static_cast<std::size_t>(answer->activeParameter) <
                             answer->parameters.size();
    result.set("activeParameter", jsonInt(inRange ? answer->activeParameter : 0));
    return result;
}

Json formattingResult(const DocumentView& document, const IntelHooks& intel) {
    Json edits = jsonArray();
    if (!hasTarget(document) || !intel.format) return edits;
    std::optional<std::string> formatted;
    try {
        formatted = intel.format(*document.path, *document.text);
    } catch (...) {
        formatted.reset();
    }
    if (!formatted || *formatted == *document.text) return edits;
    Json range = jsonObject();
    range.set("start", positionJson(LspPosition{0, 0}));
    range.set("end", positionJson(endPosition(*document.text)));
    Json edit = jsonObject();
    edit.set("range", std::move(range));
    edit.set("newText", jsonString(*formatted));
    edits.push(std::move(edit));
    return edits;
}

Json workspaceSymbolResult(const std::string& query,
                           const std::vector<DocumentView>& documents,
                           const IntelHooks& intel) {
    Json symbols = jsonArray();
    if (!intel.documentSymbols) return symbols;
    const std::string needle = lowerAscii(query);
    for (const DocumentView& document : documents) {
        if (!hasTarget(document) || document.uri == nullptr) continue;
        std::vector<SymbolEntry> entries;
        try {
            entries = intel.documentSymbols(*document.path, *document.text);
        } catch (...) {
            entries.clear();
        }
        for (const SymbolEntry& entry : entries) {
            if (symbols.items.size() >= kMaxWorkspaceSymbols) return symbols;
            if (!fuzzySubsequence(lowerAscii(entry.name), needle)) continue;
            symbols.push(
                symbolInformationJson(*document.text, *document.uri, entry));
        }
    }
    return symbols;
}

// ---------------------------------------------------------------------------
// Workspace-scan helpers (workspace/symbol over files that are not open)
// ---------------------------------------------------------------------------

namespace {

bool isWordCh(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') || ch == '_' || ch == '$';
}

bool isSpaceCh(char ch) { return ch == ' ' || ch == '\t'; }

void skipSpaces(std::string_view& s) {
    while (!s.empty() && isSpaceCh(s.front())) s.remove_prefix(1);
}

// Consumes `word` from the front of `s` when it appears there as a whole
// word (identifier boundaries only), plus any trailing whitespace.
bool takeWord(std::string_view& s, std::string_view word) {
    if (s.size() < word.size()) return false;
    if (s.substr(0, word.size()) != word) return false;
    if (s.size() > word.size() && isWordCh(s[word.size()])) return false;
    s.remove_prefix(word.size());
    skipSpaces(s);
    return true;
}

int kindForDeclaration(std::string_view keyword) {
    if (keyword == "function") return 12;   // SymbolKind.Function
    if (keyword == "class") return 5;       // SymbolKind.Class
    if (keyword == "interface") return 11;  // SymbolKind.Interface
    if (keyword == "enum") return 10;       // SymbolKind.Enum
    if (keyword == "type") return 26;       // SymbolKind.TypeParameter
    return 13;                              // SymbolKind.Variable
}

void pushEntry(std::vector<SymbolEntry>& entries, std::string name, int kind,
               std::uint32_t line, std::size_t column) {
    if (entries.size() >= 512) return;
    SymbolEntry entry;
    entry.name = std::move(name);
    entry.kind = kind;
    entry.line = line;
    entry.column = static_cast<std::uint32_t>(column + 1);
    entry.endLine = line;
    entry.endColumn = static_cast<std::uint32_t>(column + entry.name.size());
    entries.push_back(std::move(entry));
}

}  // namespace

// A lightweight declaration scan for workspace-symbol queries across the
// files the semantic engine has not opened. Deliberately not the frontend:
// the scan is regex-free, linear, and only finds declaration shapes and their
// kinds - enough for name search, never for edits.
std::vector<SymbolEntry> scrapeSymbols(const std::string& text) {
    std::vector<SymbolEntry> entries;
    std::size_t start = 0;
    std::uint32_t line = 1;
    const std::size_t size = text.size();
    while (start <= size && entries.size() < 512) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = size;
        std::string_view view(text.data() + start, end - start);
        if (!view.empty() && view.back() == '\r') view.remove_suffix(1);
        std::string_view s = view;
        skipSpaces(s);
        bool exported = takeWord(s, "export");
        takeWord(s, "default");
        takeWord(s, "declare");
        takeWord(s, "async");
        takeWord(s, "abstract");
        int kind = -1;
        std::string_view keyword;
        for (const char* candidate : {"function", "class", "interface", "enum",
                                      "type", "const", "let", "var"}) {
            if (takeWord(s, candidate)) {
                keyword = candidate;
                kind = kindForDeclaration(candidate);
                break;
            }
        }
        if (kind >= 0) {
            std::size_t i = 0;
            while (i < s.size() && isWordCh(s[i])) i += 1;
            if (i > 0) {
                // The identifier starts where the remaining text begins.
                const std::size_t offset = view.size() - s.size();
                pushEntry(entries, std::string(s.substr(0, i)), kind, line,
                          offset);
            }
        } else if (exported) {
            const std::size_t open = s.find('{');
            const std::size_t close = open == std::string_view::npos
                                          ? std::string_view::npos
                                          : s.find('}', open);
            if (open != std::string_view::npos && close != std::string_view::npos) {
                const std::size_t base = view.size() - s.size();
                const std::string_view list = s.substr(open + 1, close - open - 1);
                std::size_t cursor = 0;
                while (cursor < list.size() && entries.size() < 512) {
                    const std::size_t comma = list.find(',', cursor);
                    const std::string_view part = list.substr(
                        cursor, comma == std::string_view::npos ? list.size() - cursor
                                                                : comma - cursor);
                    // One specifier: `a` or `a as b` (plus possible `type `).
                    std::string_view trimmed = part;
                    skipSpaces(trimmed);
                    takeWord(trimmed, "type");
                    std::size_t i = 0;
                    while (i < trimmed.size() && isWordCh(trimmed[i])) i += 1;
                    if (i > 0) {
                        std::string name(trimmed.substr(0, i));
                        std::size_t nameOffset =
                            base + open + 1 + cursor + (part.size() - trimmed.size());
                        std::string_view rest = trimmed;
                        rest.remove_prefix(i);
                        skipSpaces(rest);
                        std::string_view tail = rest;
                        if (takeWord(tail, "as")) {
                            const std::size_t secondBase =
                                base + open + 1 + cursor + (part.size() - tail.size());
                            std::size_t j = 0;
                            while (j < tail.size() && isWordCh(tail[j])) j += 1;
                            if (j > 0) {
                                name.assign(tail.substr(0, j));
                                nameOffset = secondBase;
                            }
                        }
                        pushEntry(entries, std::move(name), 13, line, nameOffset);
                    }
                    cursor = comma == std::string_view::npos ? list.size() : comma + 1;
                }
            }
        }
        start = end + 1;
        line += 1;
    }
    return entries;
}

Json symbolInformationFor(const std::string& text, const std::string& uri,
                          const SymbolEntry& entry) {
    return symbolInformationJson(text, uri, entry);
}

// Folding ranges from pure text: multi-line block comments and template
// literals, matched bracket pairs that open and close on different lines
// (regions), and runs of >= 3 consecutive `import` lines (imports). Unlike
// the syntax providers this needs no language knowledge, so it stays total on
// any input.
Json foldingRangeResult(const DocumentView& document) {
    Json ranges = jsonArray();
    if (document.text == nullptr || document.text->empty()) return ranges;
    const std::string& text = *document.text;
    const std::size_t size = text.size();
    std::size_t index = 0;
    std::uint32_t line = 0;
    enum State { Normal = 0, BlockComment, DoubleString, SingleString, TemplateString };
    State state = Normal;
    std::uint32_t blockCommentStart = 0;
    std::uint32_t templateStart = 0;
    struct Frame { std::uint32_t line; char open; };
    std::vector<Frame> stack;
    while (index < size) {
        const char ch = text[index];
        if (state == Normal) {
            if (ch == '\n') { line += 1; index += 1; continue; }
            if (ch == '/' && index + 1 < size) {
                if (text[index + 1] == '/') {
                    while (index < size && text[index] != '\n') index += 1;
                    continue;
                }
                if (text[index + 1] == '*') {
                    state = BlockComment;
                    blockCommentStart = line;
                    index += 2;
                    continue;
                }
            }
            if (ch == '"') { state = DoubleString; index += 1; continue; }
            if (ch == '\'') { state = SingleString; index += 1; continue; }
            if (ch == '`') { state = TemplateString; templateStart = line; index += 1; continue; }
            if (ch == '(' || ch == '[' || ch == '{') {
                stack.push_back({line, ch});
                index += 1;
                continue;
            }
            if (ch == ')' || ch == ']' || ch == '}') {
                const char wanted = ch == ')' ? '(' : ch == ']' ? '[' : '{';
                if (!stack.empty() && stack.back().open == wanted) {
                    const std::uint32_t openLine = stack.back().line;
                    stack.pop_back();
                    if (line > openLine) {
                        Json range = jsonObject();
                        range.set("startLine", jsonInt(openLine));
                        range.set("endLine", jsonInt(line));
                        range.set("kind", jsonString("region"));
                        ranges.push(std::move(range));
                    }
                }
                index += 1;
                continue;
            }
            index += 1;
            continue;
        }
        if (state == BlockComment) {
            if (ch == '*' && index + 1 < size && text[index + 1] == '/') {
                if (line > blockCommentStart) {
                    Json range = jsonObject();
                    range.set("startLine", jsonInt(blockCommentStart));
                    range.set("endLine", jsonInt(line));
                    range.set("kind", jsonString("comment"));
                    ranges.push(std::move(range));
                }
                state = Normal;
                index += 2;
                continue;
            }
            if (ch == '\n') { line += 1; index += 1; continue; }
            index += 1;
            continue;
        }
        if (state == DoubleString || state == SingleString) {
            const char quote = state == DoubleString ? '"' : '\'';
            if (ch == '\\') { index += 2; continue; }
            if (ch == quote) { state = Normal; index += 1; continue; }
            if (ch == '\n') { line += 1; index += 1; state = Normal; continue; }
            index += 1;
            continue;
        }
        // Template literal: scanned verbatim; interpolation braces are not
        // tracked (a backtick inside ${...} would end the literal early).
        if (ch == '\\') { index += 2; continue; }
        if (ch == '`') {
            if (line > templateStart) {
                Json range = jsonObject();
                range.set("startLine", jsonInt(templateStart));
                range.set("endLine", jsonInt(line));
                range.set("kind", jsonString("region"));
                ranges.push(std::move(range));
            }
            state = Normal;
            index += 1;
            continue;
        }
        if (ch == '\n') { line += 1; index += 1; continue; }
        index += 1;
    }
    // Consecutive `import` lines fold together once the run reaches three.
    std::size_t lineStart = 0;
    std::uint32_t currentLine = 0;
    std::uint32_t runStart = 0;
    std::uint32_t runLength = 0;
    while (lineStart <= size) {
        std::size_t lineEnd = text.find('\n', lineStart);
        if (lineEnd == std::string::npos) lineEnd = size;
        std::string_view view(text.data() + lineStart, lineEnd - lineStart);
        if (!view.empty() && view.back() == '\r') view.remove_suffix(1);
        std::string_view trimmed = view;
        skipSpaces(trimmed);
        if (trimmed.substr(0, 6) == "import") {
            if (runLength == 0) runStart = currentLine;
            runLength += 1;
        } else {
            if (runLength >= 3) {
                Json range = jsonObject();
                range.set("startLine", jsonInt(runStart));
                range.set("endLine", jsonInt(currentLine - 1));
                range.set("kind", jsonString("imports"));
                ranges.push(std::move(range));
            }
            runLength = 0;
        }
        lineStart = lineEnd + 1;
        currentLine += 1;
    }
    if (runLength >= 3) {
        Json range = jsonObject();
        range.set("startLine", jsonInt(runStart));
        range.set("endLine", jsonInt(currentLine - 1));
        range.set("kind", jsonString("imports"));
        ranges.push(std::move(range));
    }
    // Keep the answer bounded.
    while (ranges.items.size() > 512) ranges.items.pop_back();
    return ranges;
}

}  // namespace cp3::lsp
